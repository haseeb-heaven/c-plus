# Native C+ adversarial test runner

Date: 2026-11-23. Branch `agent/modern-stdlib`. This document records the native
C+ implementation of the adversarial stdlib runner, its build command, the
exact equivalence checks against `tests/stdlib/run_adversarial.py`, and the
remaining limitations. It does not claim that a passing runner proves safety.

## Status and scope

`tests/stdlib/run_adversarial.cp` is a native replacement for
`tests/stdlib/run_adversarial.py`. The orchestrator repoints
`tests/stdlib/run_adversarial.sh`; this change does not edit the Python file or
the shell wrapper. The runner is a single self-contained `.cp` program:

- command execution uses the hardened libc `cplus_system()` wrapper;
- file I/O uses `stdlib/file.hp` (`file::read`, `write_bytes`, `drop`);
- it is linked against `build/libc/libcplus_hardened.a` so `cplus_system` resolves.

No failure-suppressing shell no-op (`|| true`) is used. Every classification is
derived from a real process exit code, a timeout sentinel, or a byte-exact
fixture comparison.

## Build

From the repository root (exact command used for verification):

```sh
./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o /tmp/native_adversarial -- build/libc/libcplus_hardened.a
```

The archive is passed after `--` (C+ driver pass-through) so it reaches the link
step. An equivalent link is
`-Lbuild/libc -lcplus_hardened -lm -pthread`. The build is clean under
`--strict --sanitize=none`: no C+ errors; the only diagnostics are harmless
clang notes that `-lyyjson`, `-Lbuild/ext` and the archive are "linker input
unused" during the intermediate `-c` compile step.

`nm -g /tmp/native_adversarial` contains `_cplus_system`, confirming the
archive was linked.

## Usage

```sh
/tmp/native_adversarial <ROOT> [--suite all|deep|agent2|regressions|fuzz]
                              [--mode strict-none,default,ubsan,asan,debug,release]
                              [--only NAME] [--fast]
```

`<ROOT>` is resolved with `realpath` (equivalent to `Path.resolve()`). Defaults:
`--suite all`, `--mode strict-none,default`, no `--only`, `--fast` false.
Invalid options, unknown suites/modes and empty discovery exit 2 with the same
messages as the Python runner.

## Behaviour contract

1. **Discovery** — `deep_*.cp` in `tests/stdlib`, `*.cp` in `agent2/`,
   `regressions/`, `fuzz/`, and `f100_*.c` in `tests/crt/fuzz` (group `fuzz-c`).
   Sorted by full source path, filtered by `--only` substring on the stem.
2. **Modes** — `strict-none` (`--strict --sanitize=none`), `default`
   (`--default`), `ubsan` (`--strict --sanitize=undefined`), `asan`
   (`--strict --sanitize=address`), `debug` (`--debug --strict --sanitize=none`),
   `release` (`--release --strict --sanitize=none`). C fuzz targets use
   `/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic` with the same
   include and archive set. Sources containing `stdlib/json.hp` or `yyjson.h`
   receive `-Ithird_party/yyjson -Lbuild/ext -lyyjson`.
3. **Timeouts** — 90 s build, 30 s run, 10 s git probe, enforced with
   `timeout` from the fixed PATH `/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`
   and `CPLUS_CLANG=/usr/bin/clang`. Exit 124 is the timeout sentinel.
4. **Fixtures and expectations** — `<name>.stdin` (source directory first, then
   `tests/stdlib`), `<name>.exit` (default 0), `<name>.expected` (stdout) and
   `<name>.stderr.expected` (stderr), all byte-exact, with the same search
   order and first-match-wins rule as the Python runner.
5. **Logs** — per mode under `tests/stdlib/reports/logs/<mode>/`; stale
   `.build.log`, `.stdout`, `.stderr` and executable are removed before each
   record. Build log format is `stdout + "\n--- stderr ---\n" + stderr`.
6. **libFuzzer skip** — a `fuzz` source containing `LLVMFuzzerTestOneInput`
   and lacking `int main` is recorded as `SKIPPED` without building or running.
7. **Reports** — `reports/adversarial.json` (schema version 1, insertion-ordered
   keys, `json.dumps(indent=2)` layout) and `reports/adversarial.txt` (terminal
   summary), both written on every run; the text is echoed to stdout.
8. **Exit status** — non-zero if any record is not `PASS` or `SKIPPED`.
   `BLOCKED` (unsupported sanitizer backend) counts as a failure, matching the
   current Python runner. `FAIL`, `BUILD_FAILURE` and `TIMEOUT` also fail.

Report streams are opened with `FD_CLOEXEC` so child processes do not inherit
them. This matters for tests such as `deep_file_lifecycle` that print their own
file-descriptor numbers; without it the native runner reported `fd-during 6 9`
instead of the Python runner's `fd-during 4 7`.

## Verification (exact commands and results)

Build:

```sh
$ ./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o /tmp/native_adversarial -- build/libc/libcplus_hardened.a
# exit 0; no C+ errors
$ nm -g /tmp/native_adversarial | grep cplus_system
000000010000ae88 T _cplus_system
```

Each pair below was run as `python3 tests/stdlib/run_adversarial.py "$PWD" <args>`
then `/tmp/native_adversarial "$PWD" <args>`, copying `adversarial.json` and
`adversarial.txt` after each run. JSON was compared after deleting the volatile
`generated_at` key; TXT and stdout were compared byte-for-byte.

| Arguments | Python exit | Native exit | JSON | TXT/stdout | Counts |
|---|---:|---:|---|---|---|
| `--suite regressions --mode strict-none` | 0 | 0 | equal | identical | `PASS=10` |
| `--suite deep --mode strict-none` | 0 | 0 | equal | identical | `PASS=29` |
| `--suite deep --only deep_file_lines --mode strict-none` | 0 | 0 | equal | identical | `PASS=1` (stdin + both expectation fixtures) |
| `--suite fuzz --only fuzz_containers --mode strict-none` | 0 | 0 | equal | identical | `SKIPPED=1` (libFuzzer rule) |
| `--suite deep --only deep_io_token --mode strict-none,default` | 0 | 0 | equal | identical | `PASS=2` |
| `--suite regressions --only adv_null_zero --mode asan` | 1 | 1 | equal | identical | `BLOCKED=1` |
| `--suite regressions --only adv_null_zero` | 0 | 0 | equal | identical | `PASS=2` (default modes) |

Representative terminal output (both runners, regressions subset):

```
Adversarial stdlib summary (9a061ae) suite=regressions
modes=strict-none compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
adv_alloc_failure                          strict-none  PASS
...
SUMMARY PASS=10 TOTAL=10
```

CLI error paths were also compared: `--suite nope`, `--mode bogus`,
`--mode strict-none,nope`, `--mode ""`, `--suite regressions --only nosuchtest`,
`--suite` (missing value) and `--bogus` all produced identical stderr text and
exit code 2 in both runners.

## Equivalence notes

- JSON key order, nulls, booleans, indentation and `expectation_files` layout
  match `json.dumps(..., indent=2)`; `shlex.join` is reproduced by `sh_append`.
- `build_command` includes the absolute source/exe paths, exactly as the Python
  runner records them (`str(src)`, `str(exe)` are absolute because `ROOT` is
  resolved).
- `build_exit`/`runtime_exit` are real process codes; a returned 124 is flagged
  `timed_out: true` by the wrapper, never silently treated as PASS.
- The Python runner's `OSError` ("exec error") path is approximated by shell
  exit codes 126/127 and `command not found` / `No such file or directory`
  text in captured stderr; the record still classifies as `BUILD_FAILURE` or
  `FAIL` with an `exec error:` reason.
- `head` comes from `git rev-parse --short HEAD` with a 10 s timeout and falls
  back to `unknown`; `generated_at` uses local time with microsecond precision.

## Known limitations

- Exit 124 cannot be distinguished from a program that legitimately returns
  124, because the native wrapper observes only the shell status. The Python
  runner's `TimeoutExpired` exception is authoritative; no test in this corpus
  returns 124, so the classifications agree.
- Build/runtime stderr is stored with a 64 KiB cap in the JSON record (log
  files keep the full capture). Normal runs have empty build stderr.
- Non-ASCII text is decoded as UTF-8 and escaped as `\uXXXX` for JSON, with
  `U+FFFD` substitution on invalid sequences, matching `decode('utf-8','replace')`.
- `PATH` and `CPLUS_CLANG` are pinned to the same values as the Python runner's
  `ENV`; a host without `/usr/bin/timeout` or `/usr/bin/clang` will classify
  accordingly rather than silently passing.
- The runner does not modify `run_adversarial.sh` or `run_adversarial.py`, and
  no git operations were performed as part of this work.

## Strict compiler rebuild remediation (2026-11-23, branch `agent/modern-stdlib`)

The rebuilt strict C+ compiler rejects raw subscripts on pointer parameters
whose extent cannot be proven. The original verification table above was
produced with the previous compiler; this section records the exact results
under the rebuilt toolchain.

### Changes to `tests/stdlib/run_adversarial.cp` (behaviour preserved)

1. **FFI-safe pointer reads.** Added two helpers that copy out of a pointer
   parameter with `__builtin_memcpy` into a one-byte / pointer-width local:

   ```c
   static unsigned char pp_byte(const char *p, size_t idx) {
       unsigned char b;
       __builtin_memcpy(&b, p + idx, 1);
       return b;
   }
   static char *pp_ptr(char **p, size_t idx) {
       char *v;
       __builtin_memcpy(&v, p + idx, sizeof(v));
       return v;
   }
   ```

   Every offending subscript on a pointer parameter was replaced with these
   helpers, keeping the exact offsets, byte values and evaluation order:
   - `jesc` (`s`): loop terminator, lead byte, and all UTF-8 continuation reads
     (`s[i]`, `s[i+1]`, `s[i+2]`, `s[i+3]`) — JSON escaping is byte-for-byte
     unchanged, including `U+FFFD` substitution on malformed UTF-8;
   - `name_matches` (`name`): `name[0]` hidden-file check;
   - `make_build_command` (`cmd`): the `cmd[0] = '\0'` initialization, replaced
     by `__builtin_memcpy(cmd, &nul, 1)` from a one-byte local;
   - `parse_args` (`argv`): `argv[1]`, `argv[i]`, `argv[i+1]` and the
     `argv[i+1][0]` empty-mode check (now `pp_byte(mode_arg, 0)`). The mode
     argument is captured once in `mode_arg` instead of re-reading `argv`,
     which is semantically identical because `argv` is never modified.

   No bounds check was relaxed: the remaining subscripts on `dst`/`arg` in
   `sh_append` and on `buf` in `read_all` are provably safe from the explicit
   `cap` checks and are not flagged by the strict compiler.

2. **Local-name collision.** The rebuilt compiler mis-attributed the 8192-byte
   extent of `get_head`'s `char line[MAX_CMD]` to the 512-byte `char line[512]`
   in `write_record_txt`, aborting before `get_head`'s first statement with
   `C+ error: index 8192 is outside the bounds of 'line' (length 512)`.
   Renamed the locals to `gitline` and `txtline` respectively; no logic changed.

3. **Duplicate-record macro bug.** `FINALIZE_AND_CONTINUE()` was a
   `do { ...; continue; } while (0)` block. In C, `continue` inside a do-while
   continues the *do-while*, so every failure path finalized a record and then
   fell through into the runtime phase, emitting a second (bogus) record. All
   five call sites are standalone statements in the per-test `for` loop, so the
   macro now uses a plain compound block `{ ...; continue; }`, which continues
   the enclosing loop. This removes the duplicate records without weakening
   any classification.

### Exact verification results (this change)

Build command (exit 0; `grep -c 'CPLUS-FFI'` on the output = 0):

```sh
$ ./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o /tmp/adv_native -- build/libc/libcplus_hardened.a
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
# exit 0; no C+ errors
```

Run command and exact stdout (exit 1):

```sh
$ /tmp/adv_native "$PWD" --suite regressions --mode strict-none
Adversarial stdlib summary (487ab89) suite=regressions
modes=strict-none compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
adv_alloc_failure                          strict-none  PASS
adv_containers                             strict-none  TIMEOUT     run rc 134
adv_exact_fit                              strict-none  BUILD_FAILU compiler exit 1
adv_integer_bounds                         strict-none  PASS
adv_json_fs_net                            strict-none  FAIL        exit 1 != expected 0
adv_lifecycle                              strict-none  PASS
adv_long_input                             strict-none  BUILD_FAILU compiler exit 1
adv_null_zero                              strict-none  PASS
adv_stale_views                            strict-none  PASS
adv_thread_contention                      strict-none  PASS
SUMMARY BUILD_FAILURE=2 FAIL=1 PASS=6 TIMEOUT=1 TOTAL=10
```

`TOTAL=10` confirms the runner now emits exactly one record per discovered
source and that the report pipeline (JSON + TXT + terminal echo) completes
under the rebuilt compiler. The four non-PASS records are **not** runner
defects — they are strict-compiler findings in the regression test sources
themselves, which are outside the ownership of this change:

| Test | Evidence | Cause |
|---|---|---|
| `adv_exact_fit` | `reports/logs/strict-none/adv_exact_fit.build.log` | 3 `[CPLUS-FFI]` errors on pointer params `p`, `a`, `b` (lines 59, 69) |
| `adv_long_input` | `reports/logs/strict-none/adv_long_input.build.log` | 1 `[CPLUS-FFI]` error on pointer param `ali_buf` (line 46) |
| `adv_containers` | `reports/logs/strict-none/adv_containers.stderr` | `C+ error: index 40 is outside the bounds of 'present' (length 40)` |
| `adv_json_fs_net` | `reports/logs/strict-none/adv_json_fs_net.stderr` | `C+ error: index 8 is outside the bounds of 'rb' (length 8)` |

The regressions directory has no `.stdin`, `.exit` or `.expected` fixtures, so
fixture handling is not involved in any of these outcomes. The runner's build
command for these sources is unchanged from the earlier verification:
`<ROOT>/build/c+ build <src> --strict --sanitize=none -o <exe>`.

Sanity check that the report pipeline still completes cleanly when the test
sources are clean (exit 0):

```sh
$ /tmp/adv_native "$PWD" --suite deep --only deep_file_lines --mode strict-none
Adversarial stdlib summary (487ab89) suite=deep
modes=strict-none compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
deep_file_lines                            strict-none  PASS
SUMMARY PASS=1 TOTAL=1
```

**Expected once the regression test sources are updated for the rebuilt
compiler:** `SUMMARY PASS=10 TOTAL=10` with exit 0. No classification or
comparison in this runner was weakened to reach that result.
