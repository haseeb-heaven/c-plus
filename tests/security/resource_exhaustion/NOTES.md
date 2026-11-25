# Task 6 Phase 3 — verification notes (exact commands, results, SKIPs)

Date: 2026-11-23. Root: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.
Host: arm64 macOS, `/usr/bin/clang` Apple clang 16.0.0, C+ driver `build/c+`
(`C+ 0.1.0`, backend host Clang/LLVM). All paths below are relative to root.

Files created (all inside `tests/security/resource_exhaustion/`):
`README.md`, `NOTES.md`, `run.sh`, `.gitignore`, `support/re_harness.hp`,
`support/re_watchdog.hp`, `support/re_limits.hp`, and 8 harnesses:
`re_mem_pressure.cp`, `re_fd_exhaust.cp`, `re_huge_collections.cp`,
`re_json_nesting.cp`, `re_string_growth.cp`, `re_thread_failure.cp`,
`re_socket_exhaust.cp`, `re_malformed_guard.cp`. No source files outside this
directory were edited; no `git add`/`git commit` was run by this agent.

## 1. Verification order — strict/no-sanitizer FIRST

Direct per-harness commands used (from root; `extra` is empty except for the
two yyjson harnesses; in zsh use `${=extra}` because zsh does not word-split an
unquoted parameter):

```sh
mkdir -p tests/security/resource_exhaustion/.reout
ROOT=$PWD
for f in tests/security/resource_exhaustion/re_*.cp; do
  n=$(basename "$f" .cp)
  case "$n" in *json*|*malformed*) extra="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson";; *) extra="";; esac
  timeout -k 5 30 ./build/c+ build "$f" --strict --sanitize=none $extra \
      -o "tests/security/resource_exhaustion/.reout/${n}_strict_none"
  timeout -k 5 30 "tests/security/resource_exhaustion/.reout/${n}_strict_none" \
      </dev/null >"tests/security/resource_exhaustion/.reout/${n}.strict.out" \
      2>"tests/security/resource_exhaustion/.reout/${n}.strict.err"
done
```

Result: **8/8 builds rc=0, 8/8 runs rc=0, all stderr files empty.** Final
self-report line of each run (`<checks> <failures>`):

| Harness | build rc | run rc | final line |
| --- | --- | --- | --- |
| `re_mem_pressure` | 0 | 0 | `RESOURCE_EXHAUSTION 17 0` |
| `re_fd_exhaust` | 0 | 0 | `RESOURCE_EXHAUSTION 5 0` |
| `re_huge_collections` | 0 | 0 | `RESOURCE_EXHAUSTION 21 0` |
| `re_json_nesting` | 0 | 0 | `RESOURCE_EXHAUSTION 7 0` |
| `re_string_growth` | 0 | 0 | `RESOURCE_EXHAUSTION 8 0` |
| `re_thread_failure` | 0 | 0 | `RESOURCE_EXHAUSTION 7 0` |
| `re_socket_exhaust` | 0 | 0 | `RESOURCE_EXHAUSTION 7 0` |
| `re_malformed_guard` | 0 | 0 | `RESOURCE_EXHAUSTION 6 0` |

Total strict/no-sanitizer: **78 checks, 0 failures**.

## 2. UBSan — attempted second, after strict passed

### 2.1 Plain UBSan build is BLOCKED by an external interface mismatch

```sh
timeout -k 5 30 ./build/c+ build tests/security/resource_exhaustion/re_mem_pressure.cp \
    --strict --sanitize=undefined -o tests/security/resource_exhaustion/.reout/re_mem_pressure_ubsan
```

rc=1 for all 8 harnesses, with the identical first diagnostic:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                                ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

This is the known cross-repository blocker documented in
`docs/security/verification-status.md` §3.1 and
`docs/security/stdlib-threat-model.md` §7.4 (external header defines
`CPLUS_EXIT_ARITHMETIC = 110`; this branch's `runtime/include/cplus/cplus.h`
ends at `CPLUS_EXIT_IO = 109`). It fails before any test code is compiled, so
it is **not** a finding against these fixtures.

### 2.2 UBSan with a build-only shim (no source edit)

```sh
ROOT=$PWD
for f in tests/security/resource_exhaustion/re_*.cp; do
  n=$(basename "$f" .cp)
  case "$n" in *json*|*malformed*) extra="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson";; *) extra="";; esac
  timeout -k 5 30 ./build/c+ build "$f" --strict --sanitize=undefined \
      -DCPLUS_EXIT_ARITHMETIC=110 $extra \
      -o "tests/security/resource_exhaustion/.reout/${n}_ubsan"
  timeout -k 5 30 "tests/security/resource_exhaustion/.reout/${n}_ubsan" \
      </dev/null >".../${n}.ubsan.out" 2>".../${n}.ubsan.err"
done
```

`-DCPLUS_EXIT_ARITHMETIC=110` supplies the missing constant **to the external
runtime compile only**. It does not alter any checked-in header, does not touch
test logic, and leaves UBSan instrumentation and its fatal traps active; it is
not a suppression of sanitizer findings.

Result: **8/8 builds rc=0, 8/8 runs rc=0, all stderr files empty** (no UBSan
diagnostics). Final self-report lines are byte-identical to the strict pass:
17/0, 5/0, 21/0, 7/0, 8/0, 7/0, 7/0, 6/0. Total UBSan: **78 checks, 0 failures**.
Instrumentation was confirmed with
`nm tests/security/resource_exhaustion/.reout/re_mem_pressure_ubsan | grep -c __ubsan`
→ `10`, versus `0` for the corresponding `_strict_none` binary.

## 3. Bounded runner (single entry point)

```sh
sh tests/security/resource_exhaustion/run.sh            # default: strict-none
sh tests/security/resource_exhaustion/run.sh --mode all # strict-none THEN ubsan
/usr/bin/time -p sh tests/security/resource_exhaustion/run.sh --mode all
```

`--mode all` result (16 cases, each build and run under `timeout -k 5 30`):

```
MODE           NAME                       STATUS       DETAIL
strict-none    re_fd_exhaust              PASS         RESOURCE_EXHAUSTION 5 0
ubsan          re_fd_exhaust              PASS         RESOURCE_EXHAUSTION 5 0
strict-none    re_huge_collections        PASS         RESOURCE_EXHAUSTION 21 0
ubsan          re_huge_collections        PASS         RESOURCE_EXHAUSTION 21 0
strict-none    re_json_nesting            PASS         RESOURCE_EXHAUSTION 7 0
ubsan          re_json_nesting            PASS         RESOURCE_EXHAUSTION 7 0
strict-none    re_malformed_guard         PASS         RESOURCE_EXHAUSTION 6 0
ubsan          re_malformed_guard         PASS         RESOURCE_EXHAUSTION 6 0
strict-none    re_mem_pressure            PASS         RESOURCE_EXHAUSTION 17 0
ubsan          re_mem_pressure            PASS         RESOURCE_EXHAUSTION 17 0
strict-none    re_socket_exhaust          PASS         RESOURCE_EXHAUSTION 7 0
ubsan          re_socket_exhaust          PASS         RESOURCE_EXHAUSTION 7 0
strict-none    re_string_growth           PASS         RESOURCE_EXHAUSTION 8 0
ubsan          re_string_growth           PASS         RESOURCE_EXHAUSTION 8 0
strict-none    re_thread_failure          PASS         RESOURCE_EXHAUSTION 7 0
ubsan          re_thread_failure          PASS         RESOURCE_EXHAUSTION 7 0

MODE            PASS   FAIL   BUILD  TIMEOUT    SKIP  BLOCKED
strict-none        8      0       0        0       0        0
ubsan              8      0       0        0       0        0
resource_exhaustion: 16 passed, 0 failed, 0 build-failures, 0 timeouts, 0 skipped, 0 blocked
resource_exhaustion suite: PASS
```

Timing: `real 25.63 / user 10.54 / sys 2.34` seconds for all 16 build+run
pairs. No individual build or run approached the 30 s bound. JSON summary:
`.reout/summary.json` (validated with `python3 -m json.tool`), counts
`PASS 16 / FAIL 0 / BUILD_FAILURE 0 / TIMEOUT 0 / SKIPPED 0 / BLOCKED 0`.

## 4. SKIPs (explicit, never counted as passes)

Exactly one in-test sub-check SKIP, emitted identically in both modes by
`re_thread_failure.cp`:

```
SKIP_REASON re_tf_injection pthread_create failure not injectable via RLIMIT_NPROC on this host
```

Reason: lowering soft `RLIMIT_NPROC` to 1 in a child did not make any of the 16
bounded `thread::start()` attempts fail on this macOS host, so an OS-level
`pthread_create` resource failure could not be injected deterministically. The
harness returns the dedicated SKIP exit code and the parent records the reason;
the deterministic `thread::start()` error paths (NULL function, double start,
drop-joins) are still verified in the parent. No case-level `.skip` markers and
no other `SKIP_REASON` lines exist in any output.

## 5. Documented limitation — no explicit yyjson depth limit

`third_party/yyjson/yyjson.h:113-117` defines `YYJSON_READER_DEPTH_LIMIT 0`
("0 disables the policy limit"), and `build/ext/libyyjson.a` was built with that
default. `stdlib/json.hp` passes `YYJSON_READ_NOFLAG` and no `yyjson_alc`, so
there is no explicit reader depth limit to assert. `re_json_nesting.cp` prints
`RE_JN_READER_DEPTH_LIMIT 0`, asserts that pinned value, and imposes its own
bounded 1024-level balanced / 4096-level unterminated inputs, each parsed in a
watched child. It verifies bounded termination and clean INVALID diagnostics,
**not** a parser depth bound.

## 6. Findings / defects

**No library defects found.** All 156 check executions (78 per mode × 2 modes)
passed; no sanitizer diagnostic, timeout, crash or stderr output occurred.

Observations recorded for the parent (not defects in the tested library):

1. External `CPLUS_EXIT_ARITHMETIC` header/runtime mismatch blocks plain
   instrumented builds (§2.1); build-only shim used for UBSan evidence (§2.2).
2. `pthread_create` resource failure is not injectable via `RLIMIT_NPROC` here
   (§4) — declared SKIP.
3. No explicit yyjson reader depth limit in this build (§5) — documented.
4. Telemetry fixtures must arm `stdlib_telemetry_reset()` **before**
   constructing owning containers; otherwise a constructor's allocation is
   uncounted and the alloc/free balance assertion fails. This was a bug in an
   early draft of two fixtures, fixed in the checked-in versions; it is a test
   harness invariant, not a library behaviour change.
5. A parallel agent's repository-wide commit picked up regenerable `.reout`
   binaries/logs that this runner produced (they are tracked in HEAD). A local
   `tests/security/resource_exhaustion/.gitignore` containing `.reout/` now
   prevents future output from being re-added; no git index operation was
   performed by this agent.
