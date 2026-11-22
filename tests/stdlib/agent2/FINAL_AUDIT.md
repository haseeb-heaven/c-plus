# Agent 2 — FINAL AUDIT: C+ standard library safety hardening

Date: 2026-11-23. Branch `agent/modern-stdlib`, HEAD `11831a2` at the start of
this final phase. PR #17 remains open; nothing pushed or merged. All commands
below were run from the repository root with
`PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin` and `CPLUS_CLANG=/usr/bin/clang`.

## 1. Modules inspected

All **21** `stdlib/*.hp` headers were inspected: async, audio, collections,
error, file, fs, graphics, gui, http, image, input, io, json, log, net, string,
thread, time, vector, video, window. One audit document per module is in
`tests/stdlib/agent2_audit/module_audits/`. Platform modules (async/audio/
graphics/gui/http/image/input/video/window) are **STATIC source-inspection
only** — no deep test includes them and no runtime safety claim is made.

## 2. Public APIs inventoried

**381** public callable entries across 21 headers (232 distinct bare names;
149 overload/cross-header repeats). **255** entries have a static deep-test
call-site reference (234 literal calls + 21 compiler-inserted `drop()` credits);
**126** have none. Method and denominator: `coverage.py::entries()` parser over
`stdlib/*.hp`, scan restricted to the 28 `deep_*.cp` + 5 `.stdin` files. A
strict code-only variant yields 250/131. See
`tests/stdlib/agent2_audit/public_api_inventory.md` and
`tests/stdlib/agent2/API_COVERAGE_REPORT.md`.

## 3–9. Tests executed, passed, failed, build failures, timeouts, skipped, blocked

The authoritative runner is `tests/stdlib/agent2/run_all.sh`:

```sh
sh tests/stdlib/agent2/run_all.sh --clean
```

Actual final result (**exit 0**):

```
SUMMARY PASS=40 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=40
```

Breakdown of the 40: **28** `deep_*.cp`, **1** Agent 2 cross-module test,
**11** curated existing non-deep stdlib tests. Every test was built with
`--strict --sanitize=none`, executed under a timeout with its stdin fixture,
and checked against byte-exact stdout/stderr fixtures where present. Full
evidence: `tests/stdlib/agent2/results/summary.json`, `summary.txt`, and
per-test logs.

- Tests failed: **0**
- Build failures in the final run: **0**
- Timeouts: **0**
- Skipped: **0** (runner level). Inside tests, documented SKIP reasons exist:
  `net_invalid_address` (net.hp has no address-string API), `json_alloc_failure_hook`
  (yyjson allocator unreachable), `err_code_name_mapping` (no code-name API),
  and a conditional `FS5_PERMISSIONS` skip when running as root (not triggered;
  euid 501 here).
- Blocked: **0** in the runner. Two existing tests are deliberately excluded
  from the runtime corpus with recorded reasons: `adversarial.cp` is a strict
  compile-fail case, and `safety_regressions.cp` needs `<uv.h>` (libuv dev
  files absent). They are not silently skipped.

Separate suites (reported separately, not folded into the 40):
compile-time suite **8 manifests / 52 cases** — 6/9 REJECT as expected, 19/19
ACCEPT, 24/24 RUNTIME; 27 capability/tagging gaps reported, not counted as
passes. Aggregate evidence `tests/run_all_evidence.sh`: **23 suites passed,
0 failed**.

## 10. Confirmed defects reproduced

Representative reproduced defects with before/after evidence (full list in
`docs/libc/AUDIT_FINDINGS_TRACKER.md`: 17 FIXED, 46 OPEN, 7 GAP-COMPILER,
4 BLOCKED, 53 documented contract decisions):

| ID | Defect | Reproducer | Evidence |
|---|---|---|---|
| D-IO-1/2 | `io::read_line` exact-fit false BOUNDS, CRLF LF leak | `deep_io_1byte.cp`, `deep_io_task2.cp` | `DEEP_IO_CONTRACT.md`, `DEEP_IO_TASK2_CONTRACT.md` |
| D-FILE-1..8 | `file::read_line` overlong/stream misalignment/1-byte hang/size category | `deep_file_lines.cp`, `deep_file_lifecycle.cp` | `DEEP_FILE_CONTRACT.md` |
| D-FILE-9 | `file::read_line` rejected exact-fit at EOF/newline | `"abcd"` in 5-byte buffer | `DEEP_FILE_TASK2_CONTRACT.md`, commit `1438bf2` |
| D-THREAD-1 | detached-thread context on caller stack (pre-fix SIGBUS 3/3) | `test_threads_c11.c` | `AUDIT_lifecycle.md` F1 |
| D-UCHAR-1 | 4-byte `unsigned` used as 128-byte `mbstate_t*` | canary + split UTF-8 sequence | `test_uchar.c`, `AUDIT_lifecycle.md` F2 |
| D-STDIO-1..3 | close-table deletion broke probe chains; wrapped closed ring | colliding `FILE*` repro | `test_fclose_s.c`, `AUDIT_stdio_format.md` |
| D-STR-1..5 | huge-`n` OOB in memchr/memrchr/strncpy/stpncpy | `SIZE_MAX` + canary | `test_string_extra*.c` |
| D-LOG-1/2 | logger clobbered `errno`, dropped short writes | sentinel errno, nonblocking pipe | `test_logging_telemetry.c` |
| D-SIGNAL/FENV | NULL pointer parameters passed to host | table-driven NULL tests | `test_signal.c`, `test_fenv.c` |
| D-WIDE-1 | wide `%n` unchecked | sentinel int | `test_wchar_format.c`, `WIDE_FORMAT_CONTRACT.md` |

## 11. Fixes implemented

Changes are in small reviewable commits on this branch: stdlib headers
`io.hp`, `file.hp`, `thread.hp`; SafeCRT `safe_io.c`; libc sources
`log/log.c`, `src/thread/threads_c11.c`, `src/uchar/uchar.c`, `src/stdio/stdio.c`,
`src/string/*.c`, `src/signal/signal2.c`, `src/fenv/*.c`, `src/wchar/wchar_format.c`,
`install-include/cplus_libc.h`; plus contracts, matrices and runners. No API
signature or documented return convention was broken. Exact per-fix diffs are
in the commit history (`5fe387d`, `d45681b`, `1438bf2`, `a8dd030`, `584f290`,
`54280c8`, and intervening commits).

## 12. Regression tests added

28 `deep_*.cp` tests plus `agent2/cross_module.cp`, with 5 `.stdin` fixtures,
28 stdout and 28 stderr expectation fixtures. New tests since the baseline
cover Task 2 I/O and file boundaries, Task 3 allocation-point enumeration and
collections boundaries, Task 4 string/vector ownership, Task 5 thread/log/fs/
net/error/json gaps, and cross-module failure composition. Every fix listed in
§10 has a permanent regression test.

## 13. Sanitizer results

`tests/stdlib/agent2/SANITIZER_MATRIX.md` records **72 builds attempted** across
9 tests × 8 configurations; **27** builds were accepted by the driver probe and
actually executed under `timeout 30`; **45** were rejected at build time.

| Configuration | Result |
|---|---|
| strict, no sanitizer | 9/9 PASS |
| UBSan (`--sanitize=undefined`) | 9/9 PASS |
| Integer (`--sanitize=integer`) | 5 PASS, **4 FAIL** (genuine `-fsanitize=integer` report in `runtime/cplus_runtime.c:123` `cplus_hash` — intentional wrapping multiply, outside Agent 2 scope; reported to runtime/compiler owner) |
| ASan, Leak, Thread, Memory, CFI | UNSUPPORTED by the selected backend: exact probe error recorded (`error: sanitizer '<name>' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe`) |

No sanitizer is claimed to have passed unless the binary was built and executed
with it. UBSan passing is not a memory-safety proof.

## 14. API coverage

Static call-site reachability: 255/381 entries referenced by deep tests; 126
not referenced (`input.hp` 10/10, `video.hp` 22/25, `audio.hp` 21/29, `gui.hp`
16/18, `http.hp` 14/19, `image.hp` 9/10, `graphics.hp` 9/16, `async.hp` 9/14,
`window.hp` 6/11, plus small gaps elsewhere). Function-level runtime
attribution for `.hp` headers is unavailable on this host, so execution is
measured at test granularity (40 runner invocations in strict mode; 84 deep
invocations in the three-mode runner). Name presence is not execution and is
not proof of safety. No percentage is invented.

## 15. Allocation-failure coverage

- `deep_collections_alloc.cp`: `ALLOC 59 0` — every allocation point of
  intset/deque/list/stack/queue enumerated with `stdlib_alloc_seq()`, each
  failure asserted atomic with balanced telemetry.
- `test_alloc_fault_matrix.c`: `294/294` — libc allocator wrappers, plain + UBSan.
- `deep_string_task4.cp` (`86` checks) and `deep_vector_task4.cp` (`16`) cover
  constructor, `copy_from`, `reserve`, `insert` and `shrink_to_fit` failure paths.
- `deep_collections_growth.cp` (`32`) covers multi-buffer rebuild rollback.
- JSON allocations are **not** injectable: yyjson is called with a NULL
  allocator; recorded as a SKIP, not a pass.

No memory exhaustion was used; all injections are deterministic counters.

## 16. Resource-lifecycle coverage

Double/repeated close and drop, borrowed vs owned `FILE*`, move transfer,
fd/socket leak checks, temp-file cleanup, guard ownership transfer, early-return
RAII, stale slice/view refusal without dereference, and telemetry
`alloc_count == free_count` are covered by `deep_file_*`, `deep_thread_*`,
`deep_net*`, `deep_vector_task4`, `deep_string_task4`, `deep_cross_family` and
`agent2/cross_module.cp`.

## 17–19. Remaining risks, compiler dependencies, reproduction

**Verified** (by actual build+run+exit+output check): the 40 runner tests,
compile-time classifications, allocation fault matrix, 70-layout ABI agreement.
**Tested with limited coverage**: UBSan matrix; individual failure paths of
each deep test. **Untested**: all platform modules' runtime behavior; JSON OOM
paths; 126 unreferenced API entries; most documented branches of `error.hp`
metadata helpers. **Known defects**: 46 OPEN wrapper-safety findings and the
4 integer-sanitizer findings in runtime hashing. **Compiler-dependent**:
27 compile-time capability/tagging gaps and the RAII name-dedup defect
(`RaiiLowering::registerOwned`), documented in
`docs/libc/AGENT1_COMPILER_GAPS.md` and `docs/libc/COMPILE_TIME_GAPS.md`.
**Cannot be guaranteed**: memory safety of raw-pointer callers with unknown
capacity, format-string correctness in Tier‑1 wide APIs, Rust-equivalent
guarantees, Windows x86 ABI, ASan cleanliness on this host, remote CI/Sonar
gates (all UNVERIFIED).

Final reproduction:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/stdlib/agent2/run_all.sh --clean
CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/compile_time/run_compile_time.sh
sh tests/run_all_evidence.sh
```

## Verdict

**PARTIALLY COMPLETE.** All runnable library tests pass and all fixes have
regressions, but compiler-dependent gaps, 46 open audit findings, untested
platform modules, unsupported sanitizer configurations, and unverified remote
gates remain. No required test was silently skipped; blockers are documented
with evidence and responsible component.
