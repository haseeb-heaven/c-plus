# Honest limits of the two stdlib coverage tools

Scope: `tests/stdlib/coverage.py` and `tests/stdlib/coverage_exec.sh`. This page
records what each tool actually measures, what it cannot show, and the exact
claim language that is safe to use in reports. It is a documentation-only
change: no tool was run and no other file was edited as part of this review.

---

## 1. `tests/stdlib/coverage.py` — static API-call-site reachability

### What it measures

It is a **text search**, not a runtime measurement. It parses each
`stdlib/*.hp` header with regular expressions to build a list of public names,
then asks whether a matching name pattern appears anywhere in a fixed set of
source files. Its own docstring says so:

- `coverage.py:2` — `"""Static API-coverage report for the C+ standard library.`
- `coverage.py:5-8` — `check whether the tests and examples under tests/stdlib call them.  A .hp definition is static inline, so call sites are found by name:` / `* a function is covered when \`name(\` appears in a test or example`
- `coverage.py:9-10` — `* a class method is covered when \`method(\` appears (calls are written` / `  \`object.method(...)\`)`
- `coverage.py:11-12` — `* a destructor (\`drop\` / \`*_drop\`) is covered when its type name is used,` / `  because the compiler inserts the call and tests never write it`

The definition side (`entries()`, `coverage.py:31-54`) only recognises a narrow
single-line signature style (`SIG`, `CTOR`, `CLASS`, `coverage.py:24-27`), skips
all preprocessor lines (`coverage.py:35-36`), and drops names in
`{"if","for","while","switch","do","else","return"}`, `main`, and anything
starting with `stdlib_` (`coverage.py:28`, `coverage.py:51`). A header with no
regex-matched definitions is silently omitted from the report:

- `coverage.py:73-74` — `if not api:` / `        continue`

The evidence side (`sources()`, `coverage.py:57-61`) reads the text of
`tests/stdlib/*.cp`, `tests/stdlib/multi_tu/*.cp`, and
`tests/stdlib/examples/*.cp`. The hit test is purely lexical:

- `coverage.py:78-81` — `if name == "drop" and cls:` ... `hit = any(re.search(r"\b%s\b" % re.escape(cls), text)`
- `coverage.py:82-85` — `elif name.endswith("_drop"):` ... `hit = any(re.search(r"\b%s\b" % re.escape(owner), text)`
- `coverage.py:86-88` — `else:` / `    hit = any(re.search(r"\b%s\s*\(" % re.escape(name), text)`

The number printed is `covered / len(api)` (`coverage.py:93`), compared to a
hard-coded gate (`coverage.py:22`, `THRESHOLD = 80.0`; `coverage.py:96-97`,
`if pct < THRESHOLD:`). The success line is explicit about what passed:

- `coverage.py:104` — `print("all libraries >= %.0f%% API coverage (worst %.1f%%)" % (THRESHOLD, worst))`

### What it does NOT measure

It does not measure, and cannot establish, any of the following:

- **whether the statement executed** — a call site can sit in a test that the
  runner never runs, in an example that is only compiled, in a dead branch, or
  under `#if 0`; the text still counts.
- **branch / path / condition coverage** — one happy-path mention makes a name
  "covered"; every `if`, error return, and failure branch is invisible.
- **the effect of the call** — no assertion, return value, or postcondition is
  examined. `foo();` counts exactly as much as a checked `assert(foo() == 0);`.
- **failure paths** — allocation failure, bounds rejection, null handling, and
  the like are not distinguished from the success path.
- **sanitizer findings / UB / crashes / correctness** — the tool never compiles
  or runs anything.

### Why 80% is an API-surface heuristic only

The gate answers exactly one question: *does at least one text pattern naming
each recognised public API exist in the scanned test/example sources?* It is a
floor to catch obviously unmentioned API surface. The numerator is
name-presence in text; the denominator is regex-matched definitions. Neither
has any relationship to execution, behaviour, branch coverage, or safety.
`"all libraries >= 80% API coverage"` (`coverage.py:104`) therefore means
"most recognised public names are mentioned in test text", nothing more.

### Exact false-positive / false-negative classes

**False positives** (reported covered, never exercised):

- the name appears in a comment, a string literal, or a disabled/unreachable
  region (`#if 0`, an early `return`) rather than in live code;
- the call site is in a file the runner does not execute (an example that is
  compiled but not run, or a test excluded from the run list);
- a *different* function/method with the same unqualified name satisfies the
  regex — the search uses only `name`, not `cls.name` (`coverage.py:87`), so it
  is not name-resolution-aware;
- an RAII destructor or `*_drop` is counted because the type/owner **word**
  appears anywhere in the text (`coverage.py:78-85`) — a mention in a comment,
  a `sizeof(Type)`, or a type name in an unrelated source file is enough;
- the test containing the call aborts/crashes before reaching it — static text
  cannot know this.

**False negatives** (reported uncovered or invisible, actually exercised):

- the API is reached indirectly — compiler-inserted calls, a function pointer
  (`&name`), an operator, a macro/template expansion, or a callback — with no
  literal `name(` in the scanned sources;
- the call is generated in a source outside the three glob patterns at
  `coverage.py:59` (e.g. only from `demos/` or a generated file);
- a destructor is reached only through `auto`/typedef so the class name never
  appears as a word;
- a definition written in a style the `SIG`/`CTOR` regexes at
  `coverage.py:24-26` do not match (multi-line signature, attributes,
  trailing qualifiers, out-of-class `Class::method`) is dropped from `api`
  entirely, inflating the percentage rather than being counted as missing;
- deliberate exclusions at `coverage.py:51` (`stdlib_*`, `main`, control-flow
  key words) mean those names can never be credited even if genuinely public;
- a whole header with no regex-matched definitions is skipped at
  `coverage.py:73-74`, so it is neither 0% nor 100% — it is simply absent.

---

## 2. `tests/stdlib/coverage_exec.sh` — lowered-unit line execution

### What it measures

The script preprocesses a test into a single `.i` translation unit, compiles
that lowered unit with `-fprofile-instr-generate -fcoverage-mapping`, links and
runs it, then uses `llvm-profdata`/`llvm-cov` to count lines whose regions have
an execution count:

- `coverage_exec.sh:2` — `# EXECUTION coverage for the library headers (not just API call sites).`
- `coverage_exec.sh:4-8` — `The C+ driver preprocesses a test into one .i file; compiling that with` / `-fprofile-instr-generate -fcoverage-mapping and running it records which` / `lines of the *lowered* headers actually executed.`
- `coverage_exec.sh:84-88` — the `build_one` compile/link commands using those flags.
- `coverage_exec.sh:72-73` — `"  %-22s %d/%d stdlib lines executed (%.1f%%)"`

On this host the `#line` mapping back to individual `.hp` files is lost, and
the script says so itself:

- `coverage_exec.sh:75-78` — `lowered unit: %d/%d lines executed (%.1f%%); per-header ` / `attribution unavailable on this host`

So even the reported number is "lines executed in the lowered unit", not
reliably per-header line coverage of the original sources on this host.

### The correctness gap: `|| true`, no timeout, no gate

This tool measures execution of **whatever ran**. It is not a correctness
signal, for three independent reasons visible in the code:

1. **Non-zero exit is swallowed.** `coverage_exec.sh:49-50`:

   ```sh
     LLVM_PROFILE_FILE="$OUT/$name.profraw" "$OUT/$name" <"$input" \
       >/dev/null 2>&1 || true
   ```

   A test that exits 1, asserts, or otherwise fails still leaves a `.profraw`
   (the profiler flushes on normal exit) and still contributes its coverage
   lines. The `|| true` also discards the exit status entirely, and stdout and
   stderr are sent to `/dev/null`, so the failure is invisible in the report.

2. **There is no timeout at all.** The script contains no `timeout` invocation
   and no watchdog. A hanging test blocks the script indefinitely; `|| true`
   only masks exit status, it cannot terminate a hang. Nothing in
   `coverage_exec.sh` bounds the run. (By contrast, the deep-test harness uses
   explicit external `timeout` guards — this script does not.)

3. **The script's exit status is unrelated to run success or coverage.**
   `set -u` is set (`coverage_exec.sh:9`) but not `set -e`; `fail=0`
   (`coverage_exec.sh:42`) is changed to 1 **only** when a build fails
   (`coverage_exec.sh:96-99`). Missing `.profraw`, failed `llvm-profdata merge`,
   and failed `llvm-cov export` are all silently skipped with `|| return 0`
   (`coverage_exec.sh:51`, `52`, `54`). There is no coverage threshold in this
   script at all. It can exit 0 with every test having failed at runtime.

The run set is also fixed at four tests (`coverage_exec.sh:93`):
`foundation`, `io_edge_cases`, `log_test`, `collections_test`.

### Consequence

`tests/stdlib/coverage_exec.sh` **cannot be used as a correctness gate**. It
reports which lowered lines were touched during runs whose outcome it
deliberately ignores and whose duration it does not bound. It says nothing
about assertions, expected output, branch paths, failure handling, or
sanitizer findings.

---

## 3. Corrected claim language for reports

**May be used:**

- for `coverage.py`: **"static API-call-site reachability"** — e.g. "static
  API-call-site reachability of the public `.hp` surface was >= 80% per
  library";
- for `coverage_exec.sh`: **"line execution of lowered headers for tests that
  completed"** — with the qualification that "completed" includes a non-zero
  exit, because of `|| true`, and that on this host the figure is for the
  lowered unit rather than per-header;
- forms such as "names present in test/example text", "lines touched during a
  run", "recorded figure", "evidence of observed behaviour".

**Must never be used:**

- "execution coverage proving safety";
- "coverage proves correctness", "verified/safe by coverage", "X% of the
  library is proven safe";
- "execution coverage" as a description of `coverage.py`;
- "100% API coverage" as if it implied the API was fully executed or tested;
- "all tests passed" inferred from `coverage_exec.sh`'s exit status.

---

## 4. Documentation audit (report only; no other files changed)

A repo-wide search for references to `coverage.py` / `coverage_exec.sh` and
their numbers found only two Markdown documents. Findings:

### Overstating doc

- **`docs/libc/COVERAGE.md:28`** — quoted phrase:

  > `| Modern stdlib (.hp) | \`sh tests/stdlib/run_stdlib_tests.sh\` | 38 passed, 0 failed, 0 skipped (**100.0%** API coverage across all 21 \`.hp\` headers) (recorded) |`

  Why this overstates the tool:
  - `coverage.py` never prints a single library-wide "100.0% across all 21
    headers" number. It prints a per-library table and skips headers with no
    regex-matched definitions (`coverage.py:73-74`); its own success line is
    per-library and reports the *worst* value (`coverage.py:104`).
  - "API coverage" here is presented as a recorded completeness result, while
    the only thing measured is static name presence in test text (§1).
  - It directly contradicts `docs/MODERN_STDLIB.md:204,209-212`, which records
    `Gate: **>= 80% per library** — current worst is 88.9%` and a table with
    `gui | 88.9%`, `json | 93.3%`, `string | 95.0%`, `video | 92.0%`. A
    "100.0%" figure cannot coexist with a recorded worst of 88.9%.

### Checked, no overstatement found

- **`docs/MODERN_STDLIB.md:188-204`** — this is the honest reference. It
  correctly says at line 190-192 that `coverage.py` "measures **API surface
  reachability**" and "It is *not* execution coverage — a function can be
  named in a test that never runs that path", and at line 200 that "neither is
  claimed to be the other". It does **not** claim these tools prove
  correctness or safety. Two omissions are worth noting for completeness (not
  overstatements): it does not mention the `|| true` / no-timeout caveats in
  §2, and line 199 ("CI/Linux is where per-header execution coverage is
  verified") is an unverified statement about CI, not a claim about these two
  scripts.
- No file under `docs/stdlib/` cites either tool's numbers; that directory's
  uses of the word "coverage" refer to deep-test case coverage, which is a
  different subject.
- Related script-level ambiguity (not a doc, so not edited):
  `tests/stdlib/run_stdlib_tests.sh:242` labels a passing coverage check as
  `pass "coverage (API surface reachability; execution coverage: coverage_exec.sh)"`
  although that runner only invokes `coverage.py` (line 241), not
  `coverage_exec.sh`. `tests/stdlib/run_all.sh:25` is the place that does call
  `coverage_exec.sh`. Readers should not take the single "coverage gate green"
  line at `docs/MODERN_STDLIB.md:217` to mean execution coverage was checked.
