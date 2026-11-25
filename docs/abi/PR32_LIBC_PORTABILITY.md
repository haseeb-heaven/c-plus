# PR32 — libc+ / SafeCRT portability evidence

Scope: issue #32 (P1 Portability), libc+ portion only. Compiler/runtime portability
is handled by PR AGENT 1 and is out of scope here.

All results below were produced by `sh tests/pr32_portability/run.sh` (POSIX sh,
bounded by `timeout 60`, no Python, no `|| true`). Raw evidence is written under
`tests/pr32_portability/out/`:

| File | Contents |
| --- | --- |
| `out/toolchain.txt` | Exact host OS/arch, compiler/linker/SDK/C+ driver versions, sanitizer summary |
| `out/install_smoke.txt` / `out/sdk_consumer.txt` | Exact commands, exit codes and full output of the two existing clean-prefix scripts |
| `out/link.txt` | Static + shared link commands, exit codes, compiler/linker errors |
| `out/sanitizers.txt` | ASan/UBSan/TSan compile+run probes with exact error text |
| `out/run.log` | Runner trace and per-check status lines |
| `out/*.status` | One machine-readable `PASS` / `FAIL` / `NOT_TESTED` line per check |

Every untested target below is explicitly labelled **EXPERIMENTAL** or
**UNSUPPORTED**. No target is declared supported from a build on another OS.

## 1. Local host (macOS arm64) — toolchain actually measured

From `out/toolchain.txt` (probe UTC 2026-11-24T07:11:31Z):

- OS: `Darwin Haseebs-MacBook-Pro.local 27.2.0 ... RELEASE_ARM64_T8112 arm64`;
  `sw_vers`: macOS 27.2.1 (26B131). Arch: `arm64`.
- Requested compiler: `/opt/homebrew/opt/llvm/bin/clang` — **Homebrew clang 23.1.6**,
  target `arm64-apple-darwin27.2.0`, resource dir
  `/opt/homebrew/Cellar/llvm/23.1.6/lib/clang/23`.
- System compiler (CMake default): `/usr/bin/cc` — **Apple clang 16.0.0
  (clang-1600.0.26.6)**.
- `nm`: `/usr/bin/nm` — `llvm-nm, compatible with GNU nm` (Apple LLVM 16.0.0).
- `ar`: `/usr/bin/ar` (Apple, BSD ar).
- `cmake`: `/opt/homebrew/bin/cmake` — 4.4.5. `ninja`: 1.11.1.git.kitware.jobserver-1.
- SDK (`xcrun`): `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`, version **15.2**.
- C+ driver: `build/c+` → `C+ 0.1.0`, backend host Clang/LLVM.
- Bounds: `/usr/bin/timeout` (GNU). Windows cross compiler:
  `/opt/homebrew/bin/x86_64-w64-mingw32-gcc` (**GCC 16.2.0**).

## 2. Local check results (this host, not CI)

Final runner line from the evidence run in this worktree:

```
PR32_LIBC 10 7 0 3
```

| Check | Result | Evidence / exact failure text |
| --- | --- | --- |
| `toolchain` | PASS | `out/toolchain.txt`: host OS/arch, compiler/linker/SDK/C+ driver versions recorded. |
| `install_smoke` | NOT_TESTED | `tests/libc/install_smoke.sh` exits 1 at the compiler-side contract validator (`[CPLUS-FFI]` / `[CPLUS-LIFETIME]` diagnostics, system `math.h` reads). Agent 1 (compiler) scope, not a libc install failure; exact output in `out/install_smoke.txt`. |
| `sdk_consumer` | PASS | `tests/sdk/run_sdk_consumer.sh` exited 0 with the pinned system compiler (`CPLUS_CLANG=/usr/bin/clang`). |
| `link_static` | PASS | Tiny consumer builds and runs against `build/libc/libcplus_hardened.a` (`build_exit: 0`, `run_exit: 0`). |
| `link_shared_sources` | PASS | Shared library built directly from all 78 libc sources with the pinned system compiler. |
| `link_shared_consumer` | PASS | Consumer links and runs against the installed shared library. |
| `asan` | NOT_TESTED | Probe compiles, but the host ASan runtime aborts (exit 134, `CHECK failed: sanitizer_malloc_mac.inc`). Environmental, not a libc+ defect; exact error in `out/san_asan.run.log`. |
| `ubsan` | PASS | `-fsanitize=undefined` probe compiles and runs. |
| `tsan` | NOT_TESTED | Probe compiles, but the host TSan runtime aborts (exit 134, `Interceptors are not working`). Environmental; exact error in `out/san_tsan.run.log`. |
| `mingw_compile` | PASS (compile-only) | MinGW produces a PE `.exe`. **Runtime NOT TESTED** (Highball/Wine stalls here). The MinGW consumer header check compiles (link errors expected without a Windows libc+ build).

## 3. Platform matrix

| Target | Status | Evidence source | Notes |
| --- | --- | --- | --- |
| **macOS arm64** | **TESTED HERE** (with failures F1–F3) | `tests/pr32_portability/out/*` from this host | Only locally executed target. Static link and shared-consumer link pass; clean-prefix scripts fail as detailed above. Not a blanket "supported" claim. |
| **Linux x86-64** | **CI-ONLY — not locally verified** | `.github/workflows/libc-crt.yml` jobs `linux-crt`, `asan-libc-crt`, `stdlib-deep-family-integration`; new `.github/workflows/libc-portability.yml` job `linux-x86-64` | Existing jobs run on `ubuntu-latest` / `ubuntu-24.04` (x86-64) and include `sh tests/libc/install_smoke.sh` and `sh tests/sdk/run_sdk_consumer.sh`. Those CI results were **not** run or observed on this macOS host; execution evidence must come from CI artifacts. |
| **Linux arm64** | **EXPERIMENTAL — NO CI RUNNER, not locally verified** | No arm64 job exists in `.github/workflows/libc-crt.yml`; new workflow also has no arm64 job | Explicitly **not** covered. No build, install, link or runtime evidence exists. Must not be claimed supported. |
| **Windows (PE, x86-64)** | **COMPILE-ONLY / EXPERIMENTAL — RUNTIME UNSUPPORTED** | Local `out/link.txt` (mingw 16.2.0 PE compile); new workflow job `windows-compile-only` | Runtime is **BLOCKED**: Highball/Wine stalls in the game/Wine workflow. The libc+ headers do not compile under mingw (`sigset_t` errors). No Windows runtime claim is made by any job. |

## 4. CI workflow added

`.github/workflows/libc-portability.yml` (new file; existing workflows untouched):

- `linux-x86-64` (`ubuntu-latest`): records `uname -a`, clang/nm/ar/cmake versions,
  builds the static archive with `sh tests/libc/build_libc.sh`, then runs the
  bounded `sh tests/pr32_portability/run.sh`. Uploads `tests/pr32_portability/out/`.
- `macos-arm64` (`macos-latest`): same, plus `sw_vers`, `clang -print-target-triple`
  and `xcrun` SDK path/version. Uploads the same evidence directory.
- `windows-compile-only` (`windows-latest`, `shell: bash`): records available
  mingw/clang versions, compiles a trivial C source to a PE `.exe` with
  `x86_64-w64-mingw32-gcc` (or `C:/mingw64/bin/gcc.exe`) and states explicitly
  that runtime is NOT TESTED. It never invokes the game or Wine.

All three jobs have `timeout-minutes` caps; the runner enforces a further
260-second global bound with bounded probes (150s each); the host
was observed to need ~100s per SDK/install smoke probe under CI-like load.

## 5. Reproduce locally

```sh
sh tests/pr32_portability/run.sh
# evidence: tests/pr32_portability/out/{toolchain,install_smoke,sdk_consumer,link,sanitizers}.txt
# final line: PR32_LIBC <checks> <pass> <fail> <not_tested>
```

The runner never uses `|| true` and never converts an unbuildable sanitizer into
a pass: a sanitizer is `PASS` only when its probe both compiles and runs on the
host. Missing scripts are recorded `NOT FOUND` / `NOT_TESTED`, never fabricated.

## UPDATE — Linux `test_corpus_uninit` exit 70 root cause (2026-11-25)

The long-standing `linux-crt` exit 70 in `test_corpus_uninit` was **not** a
watchdog hang. A local glibc reproduction (Ubuntu glibc 2.39, aarch64, GCC 13,
`build_libc.sh` + the corpus binary) gives:

```
corpus_uninit case 1
Fatal glibc error: ../iconv/loop.c:309 (internal_ascii_loop_single): assertion failed: (state->__count & 7) <= sizeof (state->__value)
FINDING: corpus_uninit crash at case 1 cat=1 variant=1 idx=1 signal=6
```

`signal=6` is SIGABRT from a glibc fatal assertion, not SIGALRM (14). The test
had filled an `mbstate_t` with `0xFF` (and another with random bytes). That is
not a valid representation of the type: glibc's iconv asserts that its recorded
byte count fits `__value` and aborts. macOS/libiconv tolerates the bytes, hence
the 0.34s local run versus the immediate Linux exit 70. The test never reached
the 120s alarm; both handlers use exit code 70, which made the symptom look like
the watchdog.

Two fixes, both test-side (no library change):

1. `tests/libc/test_corpus_uninit.c` now builds its four hostile `mbstate_t`
   states from the initial state and three genuine partial states captured from
   real 2/3/4-byte UTF-8 lead conversions. All 760 cases and every assertion are
   unchanged. Both signal handlers now report the exact case descriptor to
   stderr with `write(2)` (async-signal-safe, immediately flushed) instead of
   `snprintf`.
2. `tests/libc/run_libc_tests.sh` captured child status with `rc=$?` after a
   failing command in an `if` **body**. Under `set -e` that aborted the runner
   before the `FAIL` block, leaking the child's rc (70) as the step exit code and
   suppressing all diagnostics. Status is now captured in `if` conditions, and
   the `FINDING:` lines are echoed beside `FAIL` before the full trace.

Verification: the patched binary runs all 760 cases and prints
`corpus_uninit: 760 cases ok` under glibc in both `C` and `C.utf8` locales;
`CORPUS_WATCHDOG 3 3 0`; local runner final line `libc tests: 109 passed, 0 failed`.
The x86-64 CI runner itself was not available locally, so a CI rerun is still
required to confirm the remote job.

## UPDATE — Windows runtime now verified on this host

Earlier notes in this document recorded Windows runtime as BLOCKED. That is
superseded: Highball/Wine engine `x64-crossover26.3-r27` now runs genuine PE32+
apps here when the two MinGW runtime DLLs (`libwinpthread-1.dll`,
`libgcc_s_seh-1.dll`) are beside the exe and a clean bottle state is used
(`highball run -v`). Eight real C+ apps were executed on both macOS arm64 and
Windows PE32+ with identical marker lines. Evidence, binaries, transcripts and
screenshots: `proof/real_apps_cross_platform/` (`CROSS_PROOF 58 58 0`). This is
host/engine-specific evidence, not a claim for all Windows configurations.
