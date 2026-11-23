# Method-coverage audit: per-method suites vs. the authoritative coverage tool

Scope: reconcile the nine sibling per-method suites under
`tests/stdlib/method_tests/<group>/` against the repository's authoritative
API inventory, `sh tests/stdlib/run_coverage.sh`.

Everything below was measured on branch `agent/modern-stdlib`, macOS/arm64,
`CPLUS_CLANG=/usr/bin/clang`, using `./build/c+` (Apple clang 16). No source
file, header, or sibling-owned subdirectory was modified by this audit.

## 1. Authoritative inventory (exact command and result)

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/stdlib/run_coverage.sh 2>&1; echo "rc=$?"
```

The tool builds `tests/stdlib/coverage.cp` and runs it from the repo root.
Exact final output:

```
library             covered    total percent
------------------ -------- -------- -------
allocator.hp              4        4  100.0%
async.hp                 14       14  100.0%
audio.hp                 29       29  100.0%
collections.hp           63       63  100.0%
error.hp                  9        9  100.0%
file.hp                  13       13  100.0%
fs.hp                     8        8  100.0%
graphics.hp              16       16  100.0%
gsl_adapt.hp             20       23   87.0%
gui.hp                   18       18  100.0%
http.hp                  19       19  100.0%
image.hp                 10       10  100.0%
input.hp                 10       10  100.0%
io.hp                     6        6  100.0%
json.hp                  32       32  100.0%
lifetime_adapt.hp         2        2  100.0%
log.hp                   14       14  100.0%
net.hp                   15       15  100.0%
string.hp                24       24  100.0%
thread.hp                21       21  100.0%
time.hp                   5        5  100.0%
vector.hp                23       23  100.0%
video.hp                 25       25  100.0%
window.hp                11       11  100.0%

all libraries >= 80% API coverage (worst 87.0%)
```

Exit code 0. Summing the columns with
`awk 'NR>2 && $1 ~ /\.hp$/ {t+=$3;c+=$2;n++} END{...}'` gives:

```
headers=24 covered=411 total=414
```

**Reconciling the "411" figure.** The task brief quotes *411 API entries across
24 headers*. The tool actually parses **414** entries across those 24 headers;
**411** is the number of those entries reached by the sources the tool scans
(the 3 unreached entries are listed in §3). This audit uses 414 as the correct
denominator and reports 411 as the tool's covered count.

## 2. How the tool counts (and what it cannot see)

`tests/stdlib/coverage.cp` is a *static call-site scan*, not execution coverage.
Relevant implementation facts (read from that file, not inferred):

- `main()` enumerates `find stdlib -maxdepth 1 -type f -name '*.hp'` → **26**
  headers exist, but `parse_header()`/`main()` skip any header with zero parsed
  entries. 24 headers survive (§1).
- `is_skipped()` drops control keywords, `main`, and **every name beginning with
  `stdlib_`**. This is why the two additive headers `stdlib/alloc_policy.hp`
  and `stdlib/memory.hp` contribute **zero** entries: all of their public
  helpers are named `stdlib_*` (4 in `alloc_policy.hp`, 17 in `memory.hp`).
  Those 21 public, callable, documented helpers are real API surface but are
  *outside* the 411/414 inventory.
- `gather_sources()` concatenates `.cp` files from exactly these locations:
  `tests/stdlib/` (maxdepth 1), `tests/stdlib/multi_tu/`,
  `tests/stdlib/examples/`, `tests/stdlib/upstream_reuse/`,
  `tests/stdlib/allocator/`, `tests/stdlib/system_safety/support/`.
  It does **not** descend into `tests/stdlib/method_tests/`. Consequently the
  authoritative tool cannot observe any call made by the new per-method suites;
  its 411/414 numbers are invariant to this work.
- A destructor `drop` is credited when the owning class name occurs as a word;
  `*_drop` is credited when the owner name occurs as a word. All other entries
  need a `name(` token. Class constructors are reported as `<Class>.<Class>`
  (e.g. `condvar.condvar`), not `<Class>.ctor`.

## 3. The three entries the authoritative tool does not reach

Compiling a temporary copy of `coverage.cp` with `THRESHOLD 100.0` (temporary
file under `/tmp`, repository untouched) prints the below-100% list:

```
BELOW 100%: gsl_adapt.hp (87.0%)
  not called by any test/example: gsl_adapt_refuse_null, gsl_adapt_refuse_range, gsl_adapt_refuse_bounds
```

These three are the entire 411-vs-414 difference. The sibling `net_http` suite
adds dedicated cases for them in `methods_gsl_adapt.cp`; see §5 for whether
those invocations were actually executed and verified.

## 4. Full 414-entry inventory (per header)

Obtained by compiling a temporary copy of `coverage.cp` with `THRESHOLD 200.0`
and `if (hit)` changed to `if (0)`, which forces every parsed entry into the
"not called" report. Counts match the `total` column of §1 exactly:

| header | entries | header | entries |
|---|---:|---|---:|
| allocator.hp | 4 | json.hp | 32 |
| async.hp | 14 | lifetime_adapt.hp | 2 |
| audio.hp | 29 | log.hp | 14 |
| collections.hp | 63 | net.hp | 15 |
| error.hp | 9 | string.hp | 24 |
| file.hp | 13 | thread.hp | 21 |
| fs.hp | 8 | time.hp | 5 |
| graphics.hp | 16 | vector.hp | 23 |
| gsl_adapt.hp | 23 | video.hp | 25 |
| gui.hp | 18 | window.hp | 11 |
| http.hp | 19 | | |
| image.hp | 10 | **TOTAL** | **414** |
| input.hp | 10 | | |
| io.hp | 6 | | |

## 5. Manifest reconciliation (final)

The eight sibling manifests were counted directly with
`awk '/^[[:space:]]*(#|$)/{next}{n++}END{print n+0}' <group>/methods.txt`:

| group | manifest entries | `METHOD_TEST` total | passed | failed | skipped |
|---|---:|---:|---:|---:|---:|
| alloc_mem | 87 | 87 | 87 | 0 | 0 |
| str_vec | 47 | 47 | 47 | 0 | 0 |
| collections | 63 | 63 | 63 | 0 | 0 |
| file_io | 28 | 28 | 28 | 0 | 0 |
| thread_async | 37 | 37 | 37 | 0 | 0 |
| json_log_time | 56 | 56 | 56 | 0 | 0 |
| multimedia | 127 | 127 | 127 | 0 | 0 |
| net_http | 73 | 73 | 73 | 0 | 0 |
| **SUM** | **518** | **518** | **518** | **0** | **0** |

Every manifest line count equals the corresponding `METHOD_TEST` total, and
the aggregate runner re-derived all eight totals from rebuilt-and-executed test
binaries (see `RESULTS.md`): **518 invoked, 518 passed, 0 failed, 0 skipped**.

### Difference against the authoritative inventory

- Authoritative tool: **411 covered / 414 total** entries across 24 headers
  (§1). Sibling manifests: **518** entries. Difference: **+104 vs 414**
  (**+107 vs 411**).
- The difference is **not** 116 untested APIs. It is a counting-rule mismatch,
  documented below, and no bare "100% of 411" claim is made.

Causes, in descending order of size:

1. **`stdlib_*` names are invisible to the tool.** `is_skipped()` in
   `coverage.cp` discards every name beginning with `stdlib_`. This removes all
   **21** public helpers in the two headers the tool omits entirely
   (`alloc_policy.hp` 4: `stdlib_count_size_overflow`,
   `stdlib_size_class_round_up`, `stdlib_size_class_words`,
   `stdlib_size_normalize_zero`; `memory.hp` 17: `stdlib_mem_capacity`,
   `stdlib_mem_count`, `stdlib_mem_token`, `stdlib_mem_check_token`,
   `stdlib_mem_check`, `stdlib_mem_span_ok`, `stdlib_mem_span_contains`,
   `stdlib_mem_bounds`, `stdlib_mem_key_current`, `stdlib_mem_query`,
   `stdlib_mem_check_range`, `stdlib_mem_copy`, `stdlib_mem_move`,
   `stdlib_mem_set`, `stdlib_mem_borrow`, `stdlib_mem_transfer`,
   `stdlib_mem_release`). All 21 are listed and verified in
   `alloc_mem/methods.txt`. The same rule hides public free helpers elsewhere
   (e.g. `stdlib_alloc*`, `stdlib_calloc`, `stdlib_realloc`,
   `stdlib_transfer_ownership`, `stdlib_release`, `stdlib_note`,
   `stdlib_io_token`, `stdlib_telemetry_*`), which the siblings also enumerate.
2. **Headers the tool cannot parse.** `find stdlib -maxdepth 1 -name '*.hp'`
   yields 26 headers, but `main()` skips headers with zero parsed entries, so
   `alloc_policy.hp` and `memory.hp` never appear in its table (§2).
3. **Regex-based enumeration differs from "public callables".** The tool's
   `SIG`/`CTOR` regexes count only callables whose definition matches a
   single-line pattern; constructors are reported as `<Class>.<Class>`, and
   `void` members / multi-line declarations / additive free functions are
   frequently missed. Sibling manifests count each constructor and each `void`
   member as a method (e.g. `window::ctor`, `audio_engine::drop`,
   `gsl_span_i32_at`, `lifetime_owner_init`), producing a larger, explicitly
   enumerated list.
4. **The tool cannot see this work.** Its `gather_sources()` never descends into
   `tests/stdlib/method_tests/` (§2), so its covered count remains 411 no
   matter what the new suites execute.

### Gaps

- Authoritative tool gaps: `gsl_adapt_refuse_null`, `gsl_adapt_refuse_range`,
  `gsl_adapt_refuse_bounds` (§3). The sibling `net_http` suite invokes all
  three in `methods_gsl_adapt.cp` and verified them (`net_http 73/73`), but the
  authoritative tool still reports them as unreached because of cause 4. There
  is therefore **no unverified behavioral gap** in the 414-entry inventory,
  but the tool's static number is unchanged.
- Manifest gaps: none among the 518 entries — 0 failures, 0 skips. Every
  manifest entry has a dedicated test case that calls the method and checks an
  observable result, as recorded in the group `RUN_NOTES.md` files.
- Honest coverage statement: **518/518 per-method cases passed with 0 skipped**;
  the authoritative static scan remains **411/414 across 24 headers**, with two
  additional public headers and their 21 `stdlib_*` helpers outside that
  inventory. Claiming "100% of 411" would conflate two different enumerations
  and is deliberately not done here.
