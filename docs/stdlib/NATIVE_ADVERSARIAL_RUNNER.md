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
