# Sanitizer matrix — final assignment (Agent 2)

Evidence document for the deep-stdlib sanitizer matrix. Every cell below is backed
by an actual compiler invocation and, where the build succeeded, an actual
process run. No sanitizer is marked as passing unless the test binary was built
with that sanitizer, executed, exited 0, and matched the checked-in byte-exact
stdout/stderr fixtures. No percentages are estimated or inferred.

## 1. Environment and provenance

| Item | Value |
|---|---|
| Document path | `tests/stdlib/agent2/SANITIZER_MATRIX.md` |
| Date | 2026-11-23 |
| Working tree | branch `agent/modern-stdlib`, HEAD `11831a2` |
| Repository root (`$ROOT`) | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` |
| Driver (`$CPLUS`) | `$ROOT/build/c+` (prebuilt 79 MB driver binary) |
| Host | macOS arm64, `Darwin ... RELEASE_ARM64_T81` / `arm64`, `uname -m = arm64` |
| Backend clang | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin...` |
| Backend selection | `CPLUS_CLANG=/usr/bin/clang` exported for every invocation; the driver itself reports `/usr/bin/clang` as "the selected backend" in its rejection messages |
| Time bounds | `timeout 30` on **every** build and **every** run (no timeout occurred) |
| Driver capability | `build/c+ build --help` advertises `--sanitize=LIST` with `none\|address\|undefined\|memory\|thread\|leak\|cfi\|integer`; support on this host is decided by the driver's own compile-and-run probe (see §3), not by the help text |

### Driver and fixture conventions used for every command

```
timeout 30 $CPLUS build $ROOT/tests/stdlib/<TEST>.cp --strict --sanitize=<NAME> [JSON_FLAGS] -o $OUT/<TEST>__<CFG>
timeout 30 $OUT/<TEST>__<CFG> < $STDIN
```

* `JSON_FLAGS = -I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson`, supplied as
  **separate argv operands** and used only for `deep_json_task5` (the only selected
  test whose source includes `stdlib/json.hp` / `yyjson.h`). The driver emits the
  usual `clang: warning: -lyyjson: 'linker' input unused ...` warnings during the
  lowering/compile step; the link succeeds and the binary runs correctly.
* `STDIN = $ROOT/tests/stdlib/deep_io_task2.stdin` (128 bytes) for `deep_io_task2`;
  `/dev/null` for the other eight tests (they have no `.stdin` fixture).
* `$OUT = /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin`
  (session temp, deliberately outside the repository).

## 2. Method

For each of the nine selected deep tests, eight configurations were attempted:

| Config key | Exact driver flag | Intent |
|---|---|---|
| `strict_none` | `--strict --sanitize=none` | baseline |
| `ubsan` | `--strict --sanitize=undefined` | undefined behavior |
| `asan` | `--strict --sanitize=address` | address |
| `leak` | `--strict --sanitize=leak` | leak |
| `integer` | `--strict --sanitize=integer` | integer |
| `thread` | `--strict --sanitize=thread` | thread |
| `memory` | `--strict --sanitize=memory` | memory |
| `cfi` | `--strict --sanitize=cfi` | control-flow integrity |

PASS requires all of: build exit 0, run exit 0, stdout byte-identical to
`tests/stdlib/<TEST>.expected`, stderr byte-identical to
`tests/stdlib/<TEST>.stderr.expected` (all nine `.stderr.expected` fixtures are
empty). UNSUPPORTED means either the driver probe rejected the sanitizer at build
time, or the sanitizer was accepted but the binary aborted with a **host
toolchain** error. BLOCKED means the configuration could not be attempted. FAIL is
used for the one remaining honest outcome: the sanitizer was accepted, the binary
actually ran under it, and it reported a genuine defect (non-zero exit plus a
diagnostic) — see footnote 1 for why this is not relabelled as UNSUPPORTED.

## 3. Sanitizer acceptance on this compiler/host — verbatim driver output

Five of the eight sanitizers were rejected for **every** test by the driver's
compile-and-run probe. The rejection text is byte-for-byte identical across all
nine tests for a given sanitizer; it is reproduced verbatim here:

```
error: sanitizer 'address' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
```
```
error: sanitizer 'leak' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
```
```
error: sanitizer 'thread' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
```
```
error: sanitizer 'memory' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
```
```
error: sanitizer 'cfi' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
```

Each of those five configurations exited the build with code **1**, produced no
output binary, and therefore had **no run** (`RUN_EXIT N/A`). No pass is claimed
for any of them. The ASan result is recorded verbatim as a driver-probe rejection;
it is **not** claimed as an ASan pass. Independently, the repository's own
`tests/crt/asan/run_asan.sh` documents the same host limitation: Apple clang 16 on
macOS arm64 fails at process start with `sanitizer_malloc_mac.inc:189`, which is
why the driver's compile-and-run probe refuses `address` here.

Three configurations were accepted by the probe and actually executed:
`strict_none`, `ubsan`, and `integer`.

## 4. Matrix

Cells use the vocabulary PASS / UNSUPPORTED / BLOCKED, plus FAIL³ for the genuine
integer-sanitizer finding described in footnotes 1 and 3.

| Test | Strict/no-san | ASan | UBSan | Leak | Integer | Thread |
|---|---|---|---|---|---|---|
| `deep_collections_alloc` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | FAIL³ | UNSUPPORTED⁴ |
| `deep_collections_boundary` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | FAIL³ | UNSUPPORTED⁴ |
| `deep_file_task2` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | PASS | UNSUPPORTED⁴ |
| `deep_io_task2` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | PASS | UNSUPPORTED⁴ |
| `deep_string_task4` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | FAIL³ | UNSUPPORTED⁴ |
| `deep_vector_task4` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | FAIL³ | UNSUPPORTED⁴ |
| `deep_fs_task5` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | PASS | UNSUPPORTED⁴ |
| `deep_net_task5` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | PASS | UNSUPPORTED⁴ |
| `deep_json_task5` | PASS | UNSUPPORTED¹ | PASS | UNSUPPORTED² | PASS | UNSUPPORTED⁴ |

`memory` and `cfi` also had a column requested for the attempt log; both were
rejected for all nine tests with the verbatim probe errors in §3 and are recorded
in the per-configuration accounting in §7 (they are omitted as columns only
because the requested table schema lists six configuration columns).

### Footnotes

1. **ASan — UNSUPPORTED (driver probe rejection).** Build exit 1, no binary, no
   run. Exact error: `error: sanitizer 'address' is not supported by the selected
   backend (/usr/bin/clang); detected by a compile-and-run probe`. This is the
   known Apple-clang-16/arm64 sanitizer runtime limitation documented in
   `tests/crt/asan/run_asan.sh` (`sanitizer_malloc_mac.inc:189`); it is a
   host/toolchain limitation, not a test defect, and not a pass.
2. **Leak — UNSUPPORTED (driver probe rejection).** Same shape as footnote 1 with
   `'leak'`; build exit 1, no run.
3. **Integer — genuine finding, not a host abort.** The probe accepted
   `--sanitize=integer` and the binary was built and executed under it. On four
   allocation-heavy tests the process aborted with exit **134** and a real UBSan
   report (quoted in §6):
   `.../cplus_runtime.c:174:5: runtime error: unsigned integer overflow: <x> * 13787848793156543929 cannot be represented in type 'uintptr_t' (aka 'unsigned long')`
   followed by `SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior .../cplus_runtime.c:174:5 in `.
   The source line is the splitmix64-style `cplus_hash()` multiplier
   `x *= (uintptr_t)0xbf58476d1ce4e5b9ULL;`, which relies on defined modulo-2^64
   unsigned wraparound but is still reported by `-fsanitize=integer` and is fatal
   because the driver compiles with `-fno-sanitize-recover=all`. This is neither a
   PASS (the run did not exit 0 and stdout/stderr fixtures do not match) nor a
   host toolchain abort (there is no `sanitizer_malloc_mac.inc` / interceptor
   error), so it is reported as FAIL rather than mislabelled UNSUPPORTED. The
   other five tests passed under `integer` with byte-exact fixture matches.
4. **Thread — UNSUPPORTED (driver probe rejection).** Build exit 1, no run.
   `memory` and `cfi` are UNSUPPORTED for the identical reason (probe rejection);
   none of these three produced an executable.

No cell is BLOCKED: all 72 attempted build configurations were issued against a
present, executable driver and all returned a definite result.

## 5. Exact commands

### 5.1 Representative exact expanded commands (all operands as actually passed)

Baseline / UBSan / Integer — non-JSON, no-stdin example (`deep_collections_alloc`,
`strict_none`):

```sh
timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_collections_alloc.cp --strict --sanitize=none  -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__strict_none
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__strict_none < /dev/null
```

Same test under UBSan and Integer (only `--sanitize` and the output name differ):

```sh
timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_collections_alloc.cp --strict --sanitize=undefined -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__ubsan
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__ubsan < /dev/null
timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_collections_alloc.cp --strict --sanitize=integer -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__integer
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__integer < /dev/null
```

ASan rejection command and its exact build stderr (`deep_collections_alloc`;
the other eight tests are the same command with the test name and `__asan`
output changed):

```sh
timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_collections_alloc.cp --strict --sanitize=address  -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_collections_alloc__asan
# build exit 1; no run; stderr:
# error: sanitizer 'address' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe
```

JSON test with yyjson flags as separate argv operands (`deep_json_task5`,
`integer` shown; identical flag list was used for its `strict_none`, `ubsan`,
`asan`, `leak`, `thread`, `memory`, `cfi` attempts):

```sh
timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_json_task5.cp --strict --sanitize=integer -I/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/third_party/yyjson -L/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/ext -lyyjson -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_json_task5__integer
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_json_task5__integer < /dev/null
```

stdin-fixtured test (`deep_io_task2`, `ubsan` shown):

```sh
timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_io_task2.cp --strict --sanitize=undefined -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_io_task2__ubsan
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/bin/deep_io_task2__ubsan < /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_io_task2.stdin
```

### 5.2 Command uniformity

For every one of the 72 build attempts the command was exactly the pattern in
§5.1 with `<TEST>` and `<NAME>` substituted; `JSON_FLAGS` was present only for
`deep_json_task5`. For each of the 27 successful builds the run command was
exactly `timeout 30 <OUT>/<TEST>__<CFG> < <STDIN>` with `STDIN` as defined in §1.
The full per-invocation logs (build command, build exit, run command, run exit,
stdout, stderr) were captured to
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/sanmatrix/logs/<TEST>__<CFG>.meta`
plus sibling `.build.log`, `.build.stderr`, `.stdout`, `.stderr` files.

## 6. Per-configuration evidence (exact recorded result fields)

### 6.1 Configurations that ran

For `strict_none`, `ubsan`, and the five passing `integer` rows: build exit **0**,
run exit **0**, stderr empty (matches the empty `.stderr.expected`), and stdout
byte-identical to the `.expected` fixture. Recorded final stdout line:

| Test | Strict/no-san — build/run exit, stdout last line | UBSan — build/run exit, stdout last line | Integer — build/run exit, stdout last line |
|---|---|---|---|
| `deep_collections_alloc` | 0 / 0 — `ALLOC 59 0` | 0 / 0 — `ALLOC 59 0` | 0 / **134** — *(empty)* — see 6.2 |
| `deep_collections_boundary` | 0 / 0 — `DIGEST 141365270 2760897655` | 0 / 0 — `DIGEST 141365270 2760897655` | 0 / **134** — `bd_empty_queue 1` — see 6.2 |
| `deep_file_task2` | 0 / 0 — `FILE_TASK2 38 0` | 0 / 0 — `FILE_TASK2 38 0` | 0 / 0 — `FILE_TASK2 38 0` |
| `deep_io_task2` | 0 / 0 — `IO_TASK2 22 0` | 0 / 0 — `IO_TASK2 22 0` | 0 / 0 — `IO_TASK2 22 0` |
| `deep_string_task4` | 0 / 0 — `STRING_TASK4 86 0` | 0 / 0 — `STRING_TASK4 86 0` | 0 / **134** — `t4_ctor_allocfail_at0 1` — see 6.2 |
| `deep_vector_task4` | 0 / 0 — `VECTOR_TASK4 16 0` | 0 / 0 — `VECTOR_TASK4 16 0` | 0 / **134** — *(empty)* — see 6.2 |
| `deep_fs_task5` | 0 / 0 — `FS_TASK5 35 0` | 0 / 0 — `FS_TASK5 35 0` | 0 / 0 — `FS_TASK5 35 0` |
| `deep_net_task5` | 0 / 0 — `NET_TASK5 9 0` | 0 / 0 — `NET_TASK5 9 0` | 0 / 0 — `NET_TASK5 9 0` |
| `deep_json_task5` | 0 / 0 — `JSON_TASK5 23 0` | 0 / 0 — `JSON_TASK5 23 0` | 0 / 0 — `JSON_TASK5 23 0` |

All 18 strict_none/ubsan rows and the 5 passing integer rows matched both
fixtures byte-for-byte; no stdout or stderr diff was recorded for them.

### 6.2 Integer sanitizer runtime aborts (first 5 stderr lines)

Each of the following had build exit 0 and run exit 134 (SIGABRT). Build stderr
was empty in all four cases. The first (and only) two stderr lines are quoted
verbatim; the varied multiplier is the runtime hash input.

`deep_collections_alloc` — stdout last line *(empty)*:
```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5: runtime error: unsigned integer overflow: 4350972196 * 13787848793156543929 cannot be represented in type 'uintptr_t' (aka 'unsigned long')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5 in 
```
`deep_collections_boundary` — stdout last line `bd_empty_queue 1`:
```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5: runtime error: unsigned integer overflow: 4319236020 * 13787848793156543929 cannot be represented in type 'uintptr_t' (aka 'unsigned long')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5 in 
```
`deep_string_task4` — stdout last line `t4_ctor_allocfail_at0 1`:
```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5: runtime error: unsigned integer overflow: 4386066724 * 13787848793156543929 cannot be represented in type 'uintptr_t' (aka 'unsigned long')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5 in 
```
`deep_vector_task4` — stdout last line *(empty)*:
```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5: runtime error: unsigned integer overflow: 4362293540 * 13787848793156543929 cannot be represented in type 'uintptr_t' (aka 'unsigned long')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:174:5 in 
```

### 6.3 Configurations rejected at build by the driver probe

For `asan`, `leak`, `thread`, `memory`, `cfi` and every test: build exit **1**,
no executable, run not attempted (`RUN_EXIT N/A`), stdout last line `<none>`,
stderr first line is the verbatim probe error from §3 for that sanitizer. Build
stdout was empty for all 45 rejections.

## 7. Accounting

### Configuration counts (exact, no estimates)

| Quantity | Count |
|---|---|
| Deep tests selected | 9 |
| Configurations per test | 8 |
| **Total build configurations attempted** | **72** |
| Builds accepted by probe | 27 (9 × strict_none, 9 × ubsan, 9 × integer) |
| Builds rejected by probe | 45 (9 × each of asan, leak, thread, memory, cfi) |
| Builds timed out / BLOCKED | 0 |
| **Runs actually executed** | **27** (all accepted builds; none timed out) |
| Runs exit 0 with byte-exact fixture match (PASS) | 23 (9 strict_none + 9 ubsan + 5 integer) |
| Runs aborted under sanitizer with a diagnostic (FAIL³) | 4 (integer: collections_alloc, collections_boundary, string_task4, vector_task4) |
| Host-toolchain-abort UNSUPPORTED cases observed | 0 (the five UNSUPPORTED configs were rejected before any run) |
| Sanitizer configurations not executed at all | 45 |

### Unsupported reasons (as recorded)

| Sanitizer | Status | Reason |
|---|---|---|
| `address` | UNSUPPORTED | Driver probe rejection, verbatim in §3; corroborated by Apple clang 16 / arm64 `sanitizer_malloc_mac.inc:189` host limitation in `tests/crt/asan/run_asan.sh`. |
| `leak` | UNSUPPORTED | Driver probe rejection, verbatim in §3. |
| `thread` | UNSUPPORTED | Driver probe rejection, verbatim in §3. |
| `memory` | UNSUPPORTED | Driver probe rejection, verbatim in §3. |
| `cfi` | UNSUPPORTED | Driver probe rejection, verbatim in §3. |
| `integer` | supported but 4 genuine findings | Accepted and executed; see footnote 3 and §6.2. |

### Honesty statement

`address` was **not** executed on any test — its cells are UNSUPPORTED, not PASS.
The same holds for `leak`, `thread`, `memory` and `cfi`. The `integer` rows marked
FAIL did execute under the sanitizer but did not pass; they are not counted as
passes. The only configurations counted as PASS are the 23 runs that actually
completed with exit 0 and byte-identical fixtures.
