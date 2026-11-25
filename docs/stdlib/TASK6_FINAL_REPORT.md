# Agent 2 — Task 6: stdlib regression-suite integration and final report

Date: 2026-11-23. Branch `agent/modern-stdlib`. Compiler used: `build/c+` with
`/usr/bin/clang` backend; PATH restricted to `/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`.

## 1. Runner implementation

`tests/stdlib/run_deep_tests.sh` is the dedicated regression runner. Its contract:

| Requirement | Implementation |
|---|---|
| Discover all `deep_*.cp` | `for src in tests/stdlib/deep_*.cp`; optional substring pattern arguments |
| Build `--strict --sanitize=none` | One of three modes: `default`, `strict-none`, `ubsan` (`--strict --sanitize=undefined`) |
| Run with required input | `tests/stdlib/<name>.stdin` when present, else `/dev/null` |
| Per-test timeouts | `timeout -k 5 60` on build, `timeout -k 5 30` on run; `-k` escalates hangs to SIGKILL |
| Exit codes | Captured exactly (`124`/`>=128` = TIMEOUT/SKIPPED, never a pass) |
| stdout/stderr comparison | `<name>.expected` and `<name>.stderr.expected`, byte-exact `diff -u`, saved per-mode `.diff` |
| Build vs runtime failures | Separate `BUILD-FAIL`, `RUN-FAIL`, `OUT-FAIL`, `TIMEOUT` classes and columns |
| Logs preserved | `build/stdlib-deep/<name>_<mode>.{build.log,out,err,diff}` |
| Non-zero on failure | rc 1 for any failure; rc 2 for timeouts only; rc 0 only when clean |
| Clear summary | Per-mode PASSED/FAILED/SKIPPED/BUILD-FAIL/RUN-FAIL/OUT-FAIL table plus a NO-EXPECTATION list |

Expectation fixtures were captured from a green `--strict --sanitize=none` run, required
byte-identical output across two consecutive runs, and cross-checked against the final
lines pinned in the contract docs (e.g. `RESULT 32 0`, `ALL_OK 1`, `SUMMARY 8 6 0 0`,
`deep_fs_time: 54 checks, 0 failures`). They are regression baselines from a verified run,
**not independent proof that the output is correct**.

`tests/run_tests.sh` (compiler tests) and `tests/stdlib/coverage_exec.sh` (an execution-
coverage tool that ignores exit status via `|| true` and has no timeout) are **not**
correctness runners; see `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md`.

## 2. Actual clean-run output

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
rm -rf build/stdlib-deep
CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/run_deep_tests.sh
```

Real result (exit 0):

```
MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default            28        0        0           0         0         0
strict-none        28        0        0           0         0         0
ubsan              28        0        0           0         0         0
deep stdlib tests: 84 passed, 0 failed, 0 skipped (28 file(s) x 3 modes)
```

Full per-test output was preserved at
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/task6_clean_run.log`.

## 3. Inventory (static call-site method, not execution coverage)

Method: `sorted(Path('stdlib').glob('*.hp'))` for the modulus; `coverage.py`'s own
`entries()` parser for the callable count; scan restricted to the 28 `deep_*.cp` plus
5 sibling `.stdin` files; literal `\bname\s*\(` for calls, with 21 compiler-inserted
`drop()` credits when the owning class token appears. Full details and caveats in
`docs/stdlib/REGRESSION_INVENTORY.md`.

| Metric | Count | Denominator / method |
|---|---:|---|
| stdlib modules inspected | 21 | all `stdlib/*.hp` files |
| Public callable entries | 381 | `coverage.py` parser entries (232 distinct bare names; 149 overload/cross-header repeats) |
| APIs exercised by deep tests | 255 | 234 literal call sites + 21 implicit `drop()` credits |
| APIs exercised, strict code-only variant | 250 | excludes 5 comment/string-only credits |
| APIs not exercised | 126 | 381 − 255 |

The exercised/not-exercised split is a **static textual call-site inventory**; it does
not measure execution, branch paths, call effects, or safety.

## 4. Compile-time results (reported separately from runtime)

Command: `CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/compile_time/run_compile_time.sh`.
8 manifests / 52 cases:

| Expected class | As expected | Total | Denominator |
|---|---:|---:|---|
| REJECT | 6 | 9 | declared REJECT cases |
| ACCEPT | 19 | 19 | declared ACCEPT cases |
| RUNTIME | 24 | 24 | declared RUNTIME (compiler proof unavailable) |

Observed normalization of the 52 cases: **5** tagged `[CPLUS-` REJECT, **7** untagged
(PARTIAL) rejections, **40** compiles-clean. **27** cases are explicit compiler
capability / diagnostic-tagging gaps and are **not counted as passes**. Backend warnings
without `[CPLUS-` are never counted as C+ ownership/lifetime enforcement.

## 5. Runtime results

`sh tests/run_all_evidence.sh` (all 23 suites, exit 0):

- compiler tests 150/150; libc 85/85; CRT unit 80/80; CRT conformance all pass;
  attack suites 23/23; corpora 20/20; scenarios 52/52; methods 14/14;
  hundred-scenario 30/30; family suites 20/20; integration 8/8;
  SDK consumer and compat matrix ok; contract validator 203 C17 APIs; contract alignment ok.
- Deep stdlib: 28 tests × 3 modes = **84 invocations, 0 failed, 0 skipped** with
  byte-exact stdout/stderr fixtures.
- Rust-vs-C+ comparison: 600 cases — 473 matched, 119 documented differences,
  8 both-skip, 0 failures. Outcome equality is not a safety-equivalence proof.

## 6. Sanitizer results (separate from functional runtime)

- Default Safe mode enables UBSan + bounds/null/alignment checks; all 28 deep tests pass.
- Explicit `--strict --sanitize=undefined`: 28/28 deep tests pass, no sanitizer diagnostics.
- `--strict --sanitize=none`: 28/28 deep tests pass with byte-identical stdout.
- libc/CRT suites were additionally built and run under `-fsanitize=undefined -fno-sanitize-recover=all`
  during the fix work (audit regressions: 113/113, allocator fault matrix 294/294,
  logger 20/20 repeated runs).
- ASan could not be used for most suites on this host: the Apple clang ASan runtime aborts
  with `sanitizer_malloc_mac.inc:189 CHECK failed` even for a trivial program. A subset
  was verified with Homebrew clang plus an explicit CommandLineTools SDK; absence of a
  report here is **not** a memory-safety proof.

## 7. Allocation-failure results

| Suite | Result | Method / denominator |
|---|---|---|
| `deep_collections_alloc.cp` | `ALLOC 59 0` | every allocation point enumerated with `stdlib_alloc_seq()` for intset (2 buffers), deque/queue/list/stack (1 each); for each N=1..count: rc −1, state/order/capacity unchanged, telemetry balanced, reset retry succeeds |
| `tests/libc/test_alloc_fault_matrix.c` | `294/294 checks ok` | `cplus_alloc_fail_at/after/reset`, plain + UBSan |
| `deep_collections_growth.cp` | `RESULT 32 0` | intmap/strmap rebuild 1/2/3 and key-copy failures, rollback + retry |
| `deep_string_task4.cp` | `STRING_TASK4 86 0` | constructor failure → `valid()==0`, copy_from single-point atomicity, retry, alloc/free balance |
| `deep_vector_task4.cp` | `VECTOR_TASK4 16 0` | insert/copy_from/shrink_to_fit failure atomicity |

No memory exhaustion was used; all injections are deterministic counters. Successful
normal allocation alone is never reported as memory-safety evidence.

## 8. Reproduced defects fixed in this work

| Defect | Evidence | Fix |
|---|---|---|
| `file::read_line` rejected exact-fit lines at EOF or before `\n` while `io.hp` accepted them | repro: `"abcd"` in 5-byte buffer → `FAIL/101` in `file.hp`, `OK` in `io.hp` | `file.hp` now peeks the next byte like `io.hp`; genuine overlong still drains + `BOUNDS` |
| `io.hp` exact-fit after `fgets` width and CRLF boundary leaked a stray LF | overlong/fit repro in `DEEP_IO_CONTRACT.md` | peek + CRLF consume |
| Detached-thread start context on caller stack | pre-fix build crashed 3/3 (SIGBUS) | heap-allocated context freed in trampoline |
| `static unsigned` used as `mbstate_t*` (4 vs 128 bytes) | canary + split-sequence repro | real `mbstate_t` + separate surrogate word |
| stdio close-table deletion broke probe chains | colliding `FILE*` repro | tombstones, compaction, fail-closed `EMFILE` |
| Huge-`n` memory/string wrappers scanned/wrote OOB | `SIZE_MAX` + canary repro | `RSIZE_MAX_MEM` guards and NULL checks |
| Logger clobbered `errno`, dropped short writes | sentinel `errno` + nonblocking-pipe repro | save/restore + bounded write loop |
| Signal/fenv NULL pointer parameters | table-driven NULL repro | uniform `EINVAL` guards (ISO-optional NULLs preserved) |
| Wide `%n` unchecked | sentinel-int repro | opt-in `cplus_swprintf_checked` Tier 2 |
| `cplus_isblank` uncast `int` | ctype regression | EOF guard + `isblank((unsigned char)c)` |

No defect was fixed without a reproduction. Other findings remain documented rather
than "fixed to pass": `fs_mkdtemp` consumes its `XXXXXX` template on failure (inherited
from libc; retry requires reseeding), `%n` Tier-1 wide formatting remains caller
responsibility, and unknown-capacity raw pointers cannot be bounds-checked by the library.

## 9. Remaining untested modules and APIs

Exact list from `REGRESSION_INVENTORY.md` (126 of 381 entries not exercised by any deep test):

- `input.hp` (10): poll_event, event_is_quit, event_is_key_down/up, event_scancode, event_text, keyboard_state, key_pressed, mouse_state, mouse_button_left.
- `video.hp` (22): video_error_text; frame (ctor/width/height/format/pts/drop); video_codec_id; decoder (ctor/width/height/next_frame/drop); media (ctor/stream_count/video_index/video_width/video_height/video_codec/duration_ms/open_decoder/drop).
- `audio.hp` (21): audio_context, audio_sound, audio_engine, audio_device, audio_decoder constructors and methods, audio_sleep_ms.
- `gui.hp` (16), `http.hp` (14), `image.hp` (9), `graphics.hp` (9), `async.hp` (9), `window.hp` (6).
- Small gaps on otherwise covered modules: `io.hp::println`, `json_view` constructor,
  `log_stderr`, `log_stdout`, `net::connection` constructor (spelling only), `vector::slice`
  constructor (implicit factory used), `thread::condvar` and its 3 methods.
- `collections.hp`, `error.hp`, `file.hp`, `fs.hp`, `string.hp`, `time.hp`: 0 unexercised entries
  by the static call-site measure — this does **not** mean every branch or failure path ran.

## 10. Known defects and unresolved risks

- 127 wrapper-safety findings (`docs/libc/AUDIT_FINDINGS_TRACKER.md`): 17 FIXED,
  53 documented contract decisions, 7 compiler gaps, 4 blocked, **46 still OPEN**.
- 27 compile-time capability/tagging gaps across 8 manifests; needed compiler work is
  specified in `docs/libc/AGENT1_COMPILER_GAPS.md`. Untagged rejections are PARTIAL, not
  enforcement.
- ISO C inventory: 948 APIs, 27 MISSING (23 C23 math + `timespec_getres`, `FE_TIESAWAY`,
  `mbrtoc8`, `c8rtomb`), 49 UNSUPPORTED. SafeCRT: 387 of 437 APIs not integrated.
- ABI evidence remains 2 of 714 matrix rows; a 70-layout host/C+ agreement test was added,
  but it measures layout only — no calling convention or Windows x86 evidence.
- ASan is unavailable on this host for most builds; UBSan passing is not a memory-safety proof.
- Remote Linux CI and SonarCloud post-change gates are **UNVERIFIED**; local green runs
  are not remote results. `docs/STATUS.md` and `docs/libc/REMOTE_GATE_EVIDENCE.md` keep them
  explicitly unverified.
- No Rust-equivalence or full-libc-coverage claim is made.
