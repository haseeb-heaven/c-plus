# Deep JSON contract — `stdlib/json.hp`

This is the per-family contract for the two deep JSON suites on the
`agent/modern-stdlib` branch. The shared harness rules live in
[DEEP_TESTS_HARNESS.md](DEEP_TESTS_HARNESS.md).

| File | Purpose | stdin fixture | Expected process result |
| --- | --- | --- | --- |
| `tests/stdlib/deep_json_types.cp` | typed conversions, wrong types, NULL args, missing/invalid members, builder failures, repeated writes, borrowed-view rules | none (`/dev/null`) | exit 0, `RESULT 65 0` |
| `tests/stdlib/deep_json_stress.cp` | bounded large/deep input from stdin, generated 900-element array and 64-level object, canonical round-trip, allocation-fault interaction | `tests/stdlib/deep_json_stress.stdin` (5 lines) | exit 0, `RESULT 20 0` |

Both are self-checking: every `check()` prints `<label> 1|0`, the stress suite
additionally prints `line <n> <parsed> <values> <maxdepth> <roundtrip>`, and the
last line is `RESULT <checks> <failed>`. The harness compares no `.expected`
file; it requires exit 0 and clean stderr. A `0` in any check, a `RESULT` line
with a non-zero second field, a runtime fatal, or a sanitizer report is a
contract violation.

## 1. Classification

- **success** — documented happy path. Returns `STDLIB_OK` (`0`), a valid view
  or document, or a written buffer.
- **recoverable-failure** — documented refusal. Returns `STDLIB_FAIL` (`-1`),
  an invalid view/document, or `NULL`, with a specific `error.category()` and
  `error.code()`. The process must survive and the `error` must be well formed.
- **fatal-safety** — deliberate violation whose only correct outcome is a
  runtime fatal or compile-time rejection. **None is executed** by either JSON
  suite; the two observations below are recorded as one-shot evidence instead.

## 2. Observed typed-getter contract (reproduced, unchanged)

All values below were reproduced before any fix and are now pinned by
`deep_json_types.cp`. `stdlib/json.hp` was **not modified**: every case matched
the intended contract, so there was no confirmed defect to fix.

### 2.1 Integer range / fractional conversion

| Input for member `n` | `get_int` | `out` | `category` / `code` |
| --- | --- | --- | --- |
| `0`, `-1`, `2147483647`, `-2147483648` | `STDLIB_OK` | exact value | OK / OK |
| `2147483648` (`INT_MAX+1`) | `STDLIB_FAIL` | untouched sentinel | `RANGE`(2) / `BOUNDS`(101) |
| `-2147483649` (`INT_MIN-1`) | `STDLIB_FAIL` | untouched sentinel | `RANGE` / `BOUNDS` |
| `9223372036854775807`, `-9223372036854775808`, `18446744073709551615` | `STDLIB_FAIL` | untouched sentinel | `RANGE` |
| `99999999999999999999999999999999` (stored as real) | `STDLIB_FAIL` | untouched sentinel | `INVALID`(1) |
| `1.0`, `1e2`, `-0.0` | `STDLIB_FAIL` | untouched sentinel | `INVALID` / `UNSUPPORTED`(108) |

**No silent truncation and no integral-real acceptance** were observed.
`get_double` accepts any JSON number (`1.5`, `2147483648`, `1e-9`); integers
above 2^53 necessarily lose precision when converted to `double`, which is
inherent to the typed API and not a truncation of the stored value. A number
that overflows `double` (`1e400`) is a recoverable *parse* failure with
`CATEGORY_INVALID`, not infinity.

### 2.2 Wrong types, NULL arguments, missing members, bad indices

- Each getter refuses a wrong `yyjson` type with `STDLIB_FAIL` and
  `CATEGORY_INVALID` / `UNSUPPORTED`; `get_int` on a string, `get_double` on a
  bool, `get_string` on a number, `get_bool` on an array or object are pinned.
- NULL `out` pointers for all four getters are refused with `CATEGORY_INVALID`
  and never dereferenced. `error_reset(NULL)` / `error_fail_cat(NULL, ...)` are
  themselves NULL-safe in `error.hp`, so a NULL `error *` cannot crash a getter.
- `member(NULL)` and `member("absent")` both return an **invalid** view; `at(3)`
  and `at(SIZE_MAX)` on a 3-element array return an invalid view. `at()` on a
  string, object, number or JSON `null` also returns an invalid view —
  `yyjson_arr_get()` checks the type, so there is no wild pointer.
- A JSON `null` is a **valid** view (`valid() == 1`) that belongs to no typed
  accessor: all `is_*` predicates are false, `size() == 0`, and every typed
  getter refuses it with `CATEGORY_INVALID`.
- `json_parse_file(NULL)` returns an invalid document with `code() == IO`(109)
  but `category() == INVALID`(1). This split is **deliberate in `json.hp`**
  (a bad argument gets the INVALID category); genuine file errors use
  `CATEGORY_IO`. Pin it as a contract, not a defect.

### 2.3 Builder failures (recoverable)

- `add_int`, `add_double`, `add_bool`, `add_string` on an invalid (dropped)
  builder all return `STDLIB_FAIL` / `CATEGORY_INVALID` / `UNSUPPORTED`;
  `write()` returns `NULL`. `drop()` is idempotent.
- `add_double(NAN)` and `add_double(INFINITY)` return `STDLIB_FAIL` /
  `CATEGORY_RANGE`(2) / `BOUNDS`(101); non-finite values never reach the
  serializer.
- `add_string(NULL, ...)` and `add_string(key, NULL)` return `STDLIB_FAIL` /
  `CATEGORY_INVALID`.
- Repeated `write()` on the same builder or parsed document produces byte-
  identical buffers with identical lengths. `json_free()` is paired with every
  buffer; escaping (`\"`, `\\`) is preserved across two consecutive writes.

### 2.4 Borrowed-view lifetime / fatal-safety observations (not executed)

- A `json_view` borrows from its `json`. It has no owner reference: after
  `doc.drop()` the view's pointer is dangling and **must not be used**. The
  suites only use views before the owner is dropped, and document the rule here.
- C+ RAII rejects copying an owning `json` at compile time:
  `json b = a;` produces `error: resource 'b' cannot be copied from 'a'` with
  the hint `transfer ownership with cplus_move(a)`. This was verified once with
  an out-of-tree one-shot compile; it is not part of either suite because the
  failure is a compiler diagnostic, not a runtime event.
- No deliberate use-after-free or double-free is executed; there is no C+
  fatal exit code to pin for this family.

## 3. Stress limits and fault injection

- **Bounded depth.** The in-test `walk()` budget is `JSON_DEEP_BUDGET = 96`
  levels; the fixture's deepest document is 10 levels and the generated object
  is 64 levels. `alarm(20)` is a backstop; reaching `RESULT` first is the real
  termination proof. No unbounded recursion is exercised.
- **Bounded size.** stdin lines are capped at `JSON_LINE_CAP = 8192` bytes via
  `fgets`; the generated array is 900 integers. A 5-line fixture is required
  (4 valid documents, 1 malformed) and pinned by `s_stdin_lines`.
- **No generic object iteration.** `json.hp` exposes `size()` and `at()` for
  arrays and `member(key)` for objects, but no key enumeration. The stress
  walker therefore recurses through arrays only; object depth is exercised by
  repeated `member("k")` on a generated 64-level object. This is a documented
  API limitation, not a defect.
- **String length is not exposed.** `get_string()` returns a borrowed C string;
  JSON strings containing `\u0000` cannot be measured through `json.hp`.
- **Allocation fault injection gap.** `stdlib_alloc_fail_after(0)` (every
  checked `stdlib_alloc` fails) has **no effect** on `json_builder` or `json`
  in the current implementation: they allocate through `yyjson`'s own
  allocator (`yyjson_mut_doc_new(NULL)`, `yyjson_read_opts(..., NULL, ...)`),
  not `stdlib_alloc`. This matches `stdlib/image.hp`'s use of external
  allocators and is treated as a design characteristic, not a change-worthy
  defect. `deep_json_stress.cp` asserts the neutral contract — under injection,
  each operation either succeeds or returns a well-formed recoverable error,
  and no fatal/UBSan report occurs — so a future change to route yyjson through
  the checked allocator will not break the test. Builder argument/value-level
  failures (§2.3) provide the deterministic failure coverage that *is* available.

## 4. Findings and fixes

| # | Finding | Disposition |
| --- | --- | --- |
| 1 | 65 typed/getter/API contract assertions match the header's intended behaviour; no truncation, no range acceptance, no wrong-type acceptance | no fix |
| 2 | `json_parse_file(NULL)` pairs `CATEGORY_INVALID` with `CPLUS_EXIT_IO` | documented deliberate contract; no fix |
| 3 | `json.hp` bypasses the checked allocator; `stdlib_alloc_fail_at/after` cannot inject builder OOM | documented limitation; no behaviour change |
| 4 | No object key iteration, no string length accessor; JSON null is a valid untyped view | documented API limitation; no fix |
| 5 | Views dangle after owner drop; owning copies rejected at compile time | documented lifetime rule; no fix |

**`stdlib/json.hp` was not modified.** No reproduced behaviour contradicted the
documented API contract, and changing the allocator or adding API merely to
make a test green is explicitly out of scope.

## 5. Verification commands and results

Run from the repository root with the built `build/c+` compiler.

```sh
# harness, default Safe mode (UBSan + bounds/null/alignment on)
tests/stdlib/run_stdlib_tests.sh deep_json
# => ok deep_json_stress, ok deep_json_types, 2 passed 0 failed

tests/stdlib/run_stdlib_tests.sh json_test   # regression: 1 passed 0 failed

# direct builds, strict mode with sanitizers disabled
build/c+ build tests/stdlib/deep_json_types.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/stdlib-tests/deep_json_types.strict-nosan
build/c+ build tests/stdlib/deep_json_stress.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/stdlib-tests/deep_json_stress.strict-nosan
timeout 30 build/stdlib-tests/deep_json_stress.strict-nosan \
  < tests/stdlib/deep_json_stress.stdin
# => RESULT 65 0 / RESULT 20 0, rc 0, empty stderr

# explicit UBSan
build/c+ build tests/stdlib/deep_json_types.cp --sanitize=undefined \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/stdlib-tests/deep_json_types.ubsan
build/c+ build tests/stdlib/deep_json_stress.cp --sanitize=undefined \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/stdlib-tests/deep_json_stress.ubsan
# => both rc 0, RESULT 65 0 / RESULT 20 0, empty stderr

# ASan + UBSan (host-supported toolchain only; SDK path from the harness doc)
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang \
build/c+ build tests/stdlib/deep_json_stress.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/stdlib-tests/deep_json_stress.asan \
  -- -fsanitize=address,undefined -fno-omit-frame-pointer -isysroot "$SDK"
ASAN_OPTIONS=detect_leaks=1 timeout 60 build/stdlib-tests/deep_json_stress.asan \
  < tests/stdlib/deep_json_stress.stdin
# => rc 0, RESULT 20 0, empty stderr
```

Observed on this host (macOS/arm64, Apple clang 16 backend, Homebrew clang 23
for ASan): default harness **2/2 pass**, strict+no-sanitize **2/2 pass**,
explicit UBSan **2/2 pass**, ASan+UBSan **2/2 pass**, original `json_test`
**unchanged and passing**. No sanitizer report, no fatal exit, no timeout.
The unrelated `deep_net` suite failed in a full `deep_` run (environmental;
outside this task's file ownership).

## 6. Counts

- `deep_json_types.cp`: **65 checks**, 0 failed.
- `deep_json_stress.cp`: **20 checks** (including 5 stdin line groups), 0 failed.
- Changes to `stdlib/json.hp`: **0 lines** (no confirmed defect).
- New files: 2 `.cp` tests, 1 `.stdin` fixture, this contract document.
