# No-Python testing — final evidence

Branch: `agent/modern-stdlib` (HEAD `487ab89`, "Convert Python test tooling to native C+ and add 100 fuzz configs").
Snapshot: 2026-11-23. All commands below were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` unless stated otherwise.

This document records **observed** results only. It does not claim a green
build or a passing suite where the observed result was a failure. No source,
header, wrapper or test file was edited by this verification; no `git add` or
`git commit` was performed. The only file authored is this document.

---

## 1. Verification commands and exact results

### V1 — No `.py` files remain under `tests/` or `tools/`

```sh
find tests tools -name '*.py' -not -path '*/__pycache__/*'
```

Result: **empty output** (exit 0). Repeated without the exclusion,
`find tests tools -name '*.py'` is also empty. Count: `0`.

Two `__pycache__` directories remain under `tests/` with stale bytecode only —
no Python source. These are excluded per the task scope:

```
tests/fuzz100/__pycache__/run.cpython-312.pyc
tests/libc/__pycache__/test_contract_integrity.cpython-312.pyc
tests/stdlib/__pycache__/coverage.cpython-312.pyc
tests/stdlib/__pycache__/run_adversarial.cpython-312.pyc
```

Working-tree deletion state for the Python sources that were tracked in HEAD:

```
 D tests/libc/test_compiler_contract_alignment.py
 D tests/libc/test_contract_consumption.py
 D tests/libc/test_contract_integrity.py
 D tests/libc/validate_contracts.py
 D tests/stdlib/agent2/run_all.py
 D tests/stdlib/coverage.py
```

(`benchmarks/timeit.py` is still present in HEAD and on disk; it is a benchmark,
not a test runner, and is outside `tests/`/`tools/`.)

### V2 — All 8 native C+ tools build via the build script — **FAILED (4/8 built)**

```sh
sh tests/native/build_native_tools.sh 2>&1; echo "===BUILD_EXIT:$?==="
```

The script was run twice; both runs produced identical output. Exact output:

```text
root: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
driver: ./build/c+
output: build/native-tools

== validate_contracts ==
+ ./build/c+ build tests/libc/validate_contracts.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/validate_contracts
  ok   validate_contracts
== test_compiler_contract_alignment ==
+ ./build/c+ build tests/libc/test_compiler_contract_alignment.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/test_compiler_contract_alignment
  ok   test_compiler_contract_alignment
== test_contract_consumption ==
+ ./build/c+ build tests/libc/test_contract_consumption.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/test_contract_consumption -- build/libc/libcplus_hardened.a
  ok   test_contract_consumption
== test_contract_integrity ==
+ ./build/c+ build tests/libc/test_contract_integrity.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/test_contract_integrity -- build/libc/libcplus_hardened.a
  ok   test_contract_integrity
== coverage ==
+ ./build/c+ build tests/stdlib/coverage.cp --strict --sanitize=none -o build/native-tools/coverage
  FAIL coverage (compiler rc=1; see build/native-tools/coverage.build.log)
== run_adversarial ==
+ ./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/run_adversarial -- build/libc/libcplus_hardened.a
  FAIL run_adversarial (compiler rc=1; see build/native-tools/run_adversarial.build.log)
== run_all ==
+ ./build/c+ build tests/stdlib/agent2/run_all.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/run_all -- build/libc/libcplus_hardened.a
  FAIL run_all (compiler rc=1; see build/native-tools/run_all.build.log)
== fuzz100_run ==
+ ./build/c+ build tests/fuzz100/run.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/fuzz100_run -- build/libc/libcplus_hardened.a
  FAIL fuzz100_run (compiler rc=1; see build/native-tools/fuzz100_run.build.log)

native tools: 4 built, 4 failed, 0 missing
===BUILD_EXIT:1===
```

**All 8 `.cp` sources exist; none is `MISSING`.** Four fail to compile under
`--strict` with C+ FFI/lifetime diagnostics. The first diagnostic of each,
verbatim from its `.build.log`:

```text
tests/stdlib/coverage.cp:90:27: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe
tests/native/cplus_test_native.hp:46:5: error: [CPLUS-FFI] pointer parameter 'buf' has unverifiable extent; subscript cannot be proven safe   (run_all build log)
./stdlib/error.hp:401:9: error: [CPLUS-LIFETIME] variable 'total' is uninitialized (read before it is initialized)
tests/fuzz100/run.cp:170:32: error: [CPLUS-FFI] pointer parameter 'a' has unverifiable extent; subscript cannot be proven safe
```

Full verbatim compiler logs are at
`build/native-tools/{coverage,run_adversarial,run_all,fuzz100_run}.build.log`.

Timing evidence that these 4 failures are in-flight sibling work, not a
tooling regression in this document's scope: the binaries currently in
`build/native-tools/` are dated `07:59`, while `stdlib/error.hp` (08:46) and
`stdlib/collections.hp` (08:47) were modified afterwards. The build script
does not remove an old binary when recompilation fails, so those 4 executables
are **stale** and were deliberately not run or reported as results.

Prerequisites were present and are not the cause: `build/c+`,
`build/ext/libyyjson.a` and `build/libc/libcplus_hardened.a` all exist.

### V3 — Named wrappers and the evidence aggregator contain no `python3`

```sh
grep -nE '(^|[^[:alnum:]_])(python3|python)([[:space:]]|$)' \
  tests/fuzz100/run.sh tests/stdlib/agent2/run_all.sh \
  tests/stdlib/run_adversarial.sh tests/libc/run_validate_contracts.sh \
  tools/run_final_evidence.sh
```

Result: **no matches** in any of the five files. Each wrapper's launch line is
a native executable:

| File | Launch line (verbatim) |
|---|---|
| `tests/fuzz100/run.sh` | `13: exec "$ROOT/build/native-tools/fuzz100_run" --root "$ROOT" "$@"` |
| `tests/stdlib/agent2/run_all.sh` | `47: exec "$ROOT/build/native-tools/run_all" "$ROOT" "$RESULTS" "$CPLUS" "$BUILD_TIMEOUT" "$TIMEOUT" \` |
| `tests/stdlib/run_adversarial.sh` | `16: exec "$ROOT/build/native-tools/run_adversarial" "$ROOT" "$@"` |
| `tests/libc/run_validate_contracts.sh` | `28: exec "$OUT"` (after a `c+ build ... -o build/native-tools/validate_contracts`) |
| `tools/run_final_evidence.sh` | `78: build/native-tools/emit_result ...`; `run_suite ...` blocks invoke only shell/`sh` runners |

`tools/run_final_evidence.sh` still contains prose comments containing the word
"Python" (e.g. "the final line ... captured ... by Python", "Python contract
tests", "(native shell report generation above replaces this Python block)"),
but it contains **no `python3` invocation**; suite 217–229 (`contract-validator`
through `contract-integrity`) invoke the native `.cp` binaries and
`tests/libc/run_validate_contracts.sh`.

### V4 — Residual `python3` calls elsewhere under `tests/` (honest scope note)

The five files in V3 are Python-free, and no `.py` source remains, but the
broader `tests/` tree still contains `python3` invocations. These were found,
not overlooked:

```text
tests/libc/install_smoke.sh:17:python3 "$ROOT/tests/libc/validate_contracts.py" >/tmp/libc_contracts_check.log 2>&1
tests/libc/coverage_libc.sh:127:python3 - "$REPORT" <<'PY'
tests/stdlib/compile_time/run_compile_time.sh:13:python3 - "$ROOT" "$CPLUS" "$OUT" <<'PY'
tests/stdlib/fuzz/f100_json_run.sh:16:python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'
tests/stdlib/fuzz/f100_thread_run.sh:14:python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'
tests/stdlib/coverage_exec.sh:55:  python3 - "$OUT/$name.json" "$name" <<'PY'
tests/stdlib/run_stdlib_tests.sh:241:  if python3 "$ROOT/tests/stdlib/coverage.py" >"$OUT/coverage.txt" 2>&1; then
tests/crt/coverage_safecrt.sh:72:python3 - "$REPORT" <<'PY'
tests/conformance/run_conformance.sh:38:if ! python3 - "$MANIFEST" >"$OUT/plan.tsv" <<'PY'
tests/conformance/run_conformance.sh:238:if ! python3 - "$OUT/results.tsv" "$OUT/results.json" <<'PY'
```

Two of these reference `.py` files that no longer exist on disk
(`install_smoke.sh:17` → `tests/libc/validate_contracts.py`;
`run_stdlib_tests.sh:241` → `tests/stdlib/coverage.py`), so those code paths are
now dangling. They are outside the four wrappers named in the task and were
**not** modified by this verification. This document therefore does **not**
claim "the whole test tree is Python-free"; it claims only V1–V3.

### V5 — Python outside `tests/` is not a test runner

There are no `tools/*.py` files in this repository; the example
`tools/format_cpp.py` named in the task does not exist here (it belongs to the
parent-project `AGENTS.md`, a different repository). Python files that do exist
outside `tests/`/`tools/` are:

```
./benchmarks/timeit.py
./scratch/generate_bugs.py
```

A benchmark timing helper and a scratch bug-input generator are **not test
runners**; neither is part of the test evidence path. No test verdict in this
document comes from a Python interpreter.

---

## 2. Conversion table — Python file → C+ replacement → exact output

"Exact output" below is what the named command printed in this snapshot,
including exit status. `rc=134` is SIGABRT (a bounds-check abort in the native
runtime), not a shell timeout. Where compilation failed, no run output exists;
the build diagnostic is the result.

| # | Python file | C+ replacement / native binary | Command run | Exact output (and rc) |
|---|---|---|---|---|
| 1 | `tests/libc/validate_contracts.py` | `tests/libc/validate_contracts.cp` → `build/native-tools/validate_contracts` | `sh tests/libc/run_validate_contracts.sh` | `C+ error: index 1053 is outside the bounds of 'buf' (length 1024)` — `rc=134` |
| 2 | `tests/libc/test_compiler_contract_alignment.py` | `tests/libc/test_compiler_contract_alignment.cp` → `build/native-tools/test_compiler_contract_alignment` | `build/native-tools/test_compiler_contract_alignment` | `compiler contract alignment: ok` — `rc=0` |
| 3 | `tests/libc/test_contract_consumption.py` | `tests/libc/test_contract_consumption.cp` → `build/native-tools/test_contract_consumption` | `build/native-tools/test_contract_consumption` | `{"status":"ok","contract_apis":38,"apis_with_diagnostics":"16/38","diagnostic_categories":5,"categories_with_compiler_consumption":"2/5","categories_gap":"3/5","probes_run":"5/5","probes_with_expected_tag":"2/5","gaps":["[CPLUS-SAFE-CRT]","[CPLUS-SAFE-FREE]","[CPLUS-SAFE-REALLOC]"]}` — `rc=0` |
| 4 | `tests/libc/test_contract_integrity.py` | `tests/libc/test_contract_integrity.cp` → `build/native-tools/test_contract_integrity` | `build/native-tools/test_contract_integrity` | `cp: /Users/libc: No such file or directory` / `contract_integrity: cannot create isolated overlay` — `rc=1` |
| 4b | (same tool, explicit repo-root argument) | (same binary) | `build/native-tools/test_contract_integrity "$PWD"` | `C+ error: index 28913 is outside the bounds of 'buf' (length 4096)` — `rc=134` |
| 5 | `tests/stdlib/coverage.py` | `tests/stdlib/coverage.cp` → `build/native-tools/coverage` | `sh tests/native/build_native_tools.sh` | `FAIL coverage (compiler rc=1; see build/native-tools/coverage.build.log)`; first error `tests/stdlib/coverage.cp:90:27: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe`. No run performed. |
| 6 | `tests/stdlib/run_adversarial.py` | `tests/stdlib/run_adversarial.cp` → `build/native-tools/run_adversarial` | `sh tests/native/build_native_tools.sh` | `FAIL run_adversarial (compiler rc=1; see build/native-tools/run_adversarial.build.log)`; first error `./stdlib/error.hp:684:16: error: [CPLUS-FFI] pointer parameter 'message' has unverifiable extent; subscript cannot be proven safe`. No run performed. |
| 7 | `tests/stdlib/agent2/run_all.py` | `tests/stdlib/agent2/run_all.cp` → `build/native-tools/run_all` | `sh tests/native/build_native_tools.sh` | `FAIL run_all (compiler rc=1; see build/native-tools/run_all.build.log)`; first error `./stdlib/error.hp:684:16: error: [CPLUS-FFI] pointer parameter 'message' has unverifiable extent; subscript cannot be proven safe`. No run performed. |
| 8 | `tests/fuzz100/run.py` | `tests/fuzz100/run.cp` → `build/native-tools/fuzz100_run` | `sh tests/native/build_native_tools.sh` | `FAIL fuzz100_run (compiler rc=1; see build/native-tools/fuzz100_run.build.log)`; first error `./stdlib/error.hp:684:16: error: [CPLUS-FFI] pointer parameter 'message' has unverifiable extent; subscript cannot be proven safe`. No run performed. |

Predecessor names in rows 1–7 match `docs/stdlib/NATIVE_TEST_TOOLS.md`; rows 6
and 8 are named there as predecessors but were never tracked in HEAD (the
whole `tests/fuzz100/` runner is untracked work on this branch). Six of the
eight predecessor `.py` files were tracked in HEAD and are deleted in the
working tree (see V1).

### Reproduce the four runs that did build

```sh
sh tests/libc/run_validate_contracts.sh                              # rc=134, bounds abort
build/native-tools/test_compiler_contract_alignment                  # rc=0, "ok"
build/native-tools/test_contract_consumption                         # rc=0, JSON line above
build/native-tools/test_contract_integrity                           # rc=1, overlay bootstrap failure
build/native-tools/test_contract_integrity "$PWD"                    # rc=134, bounds abort
```

### Why `test_contract_integrity` prints `/Users/libc`

Not a shell/env problem: the native tool's own root detector
(`tests/libc/test_contract_integrity.cp:257`, `find_repo()`) probes for
`<cwd>/tests/libc/validate_contracts.py`. With that `.py` file deleted, the
walk-up stops at `/Users`, so the overlay command becomes
`cp -R "<root>/libc" ...` → `cp -R /Users/libc ...`. Passing the root as
`argv[1]` bypasses the probe and reaches a separate 4096-byte buffer abort.
Both outputs are recorded above verbatim; neither was fixed here.

---

## 3. What is and is not claimed

**Verified here**

- No `.py` source file exists under `tests/` or `tools/` (V1).
- The four named wrappers and `tools/run_final_evidence.sh` contain no
  `python3` invocation; their launch lines are native binaries (V3).
- All 8 `.cp` sources exist; 4 compile (`validate_contracts`,
  `test_compiler_contract_alignment`, `test_contract_consumption`,
  `test_contract_integrity`) and 4 currently fail to compile under `--strict`
  (V2). Script exit is 1.
- Exact stdout/stderr and exit codes for every tool that could be run (§2).
- Python outside `tests/` is not a test runner; there are no `tools/*.py` files
  in this repository (V5).

**Not claimed**

- That `sh tests/native/build_native_tools.sh` currently exits 0 or builds all
  8 tools — it exits 1 with 4 failures (V2).
- That any of the 4 non-compiling tools passed, produced output, or was run.
  Pre-existing binaries in `build/native-tools/` are stale relative to the
  latest header edits and were not used as evidence.
- That `validate_contracts` or `test_contract_integrity` passes — both aborted
  on bounds checks in this snapshot (§2).
- That `python3` is absent from the entire `tests/` tree — it is not (V4),
  including two now-dangling references to deleted `.py` files.
- Any live-game, emulated, or full-suite success. None was run.

**Files touched by this verification:** only `docs/stdlib/NO_PYTHON_TESTING.md`.
No source, header, wrapper, fixture or script was modified; nothing was staged
or committed.
