# `system_safety` suite runner

`run.sh` is the single entry point for the system/stdlib safety regression
harnesses in this directory. It discovers `*.cp` files **at run time** — there
is no hard-coded test list — so it can be used while other agents are still
adding harnesses. Every harness is built and run under a hard time bound, and
every case is classified rather than assumed to pass.

See [`docs/stdlib/system-api-safety.md`](../../../docs/stdlib/system-api-safety.md)
for the exact commands, observed results, the external runtime blocker and the
limitations of the evidence.

## Usage

From any directory (paths are resolved relative to the script):

```sh
sh tests/stdlib/system_safety/run.sh                  # all tests, strict-none
sh tests/stdlib/system_safety/run.sh --only net_invalid
sh tests/stdlib/system_safety/run.sh --only net_invalid,thread_lifecycle
sh tests/stdlib/system_safety/run.sh --mode all
sh tests/stdlib/system_safety/run.sh --mode strict-none,default,ubsan
sh tests/stdlib/system_safety/run.sh --json-stdout
```

| Option | Meaning |
| --- | --- |
| `--only NAME` | Select a harness by exact filename stem or substring. Repeatable; comma-separated values are also accepted. A filter matching nothing is a setup error (exit 2). |
| `--mode LIST` | `strict-none` (default), `default`, `ubsan`, or `all`. Repeatable and comma-separated. |
| `--json PATH` | Override the JSON summary path (default `build/system-safety/summary.json`). |
| `--json-stdout` | Also print the JSON object on stdout. |
| `-h`, `--help` | Print usage. |

Environment overrides: `CPLUS_BIN` (default `<root>/build/c+`), `SS_OUT`
(default `<root>/build/system-safety`), `SS_BUILD_TIMEOUT` (30 s),
`SS_RUN_TIMEOUT` (30 s), `SS_KILL_AFTER` (5 s). The two timeout values are
applied through `timeout -k <kill-after> <seconds>` to *both* build and run.

## Modes (exact build commands)

`c+` is the repo-built driver; `<extra>` is `-I<root>/third_party/yyjson
-L<root>/build/ext -lyyjson` for harnesses whose name or source mentions
JSON/yyjson (three separate argv operands):

| Mode | Command |
| --- | --- |
| `strict-none` (default) | `timeout -k 5 30 build/c+ build <src> --strict --sanitize=none <extra> -o <bin>` |
| `default` | `timeout -k 5 30 build/c+ build <src> <extra> -o <bin>` |
| `ubsan` | `timeout -k 5 30 build/c+ build <src> --strict --sanitize=undefined <extra> -o <bin>` |

Each binary is then executed as `timeout -k 5 30 <bin> < <name>.stdin` when
`<name>.stdin` exists, otherwise with stdin redirected from `/dev/null`.
stdout and stderr are captured to separate files; exit codes are captured for
both build and run.

## Optional fixtures (all keyed by the `.cp` basename)

| File | Effect |
| --- | --- |
| `<name>.stdin` | Fed to the test binary as stdin. |
| `<name>.expected` | stdout is diffed byte-for-byte against this file. |
| `<name>.stderr.expected` | stderr is diffed byte-for-byte against this file. |
| `<name>.exit` / `<name>.rc` | Expected process exit code (default `0`). |
| `<name>.skip` | Case is reported `SKIPPED`; the first line is the reason. |

Without stdout/stderr fixtures a case is still verified by exit code, by
empty stderr, and — for self-checking harnesses — by a final
`<UPPER_TAG> <checks> <failures>` stdout line, whose `<failures>` field must be
`0`. `SKIP_REASON <tag> <reason>` lines are recorded in the JSON as
informational sub-check skips. The JSON `expectation` field states exactly
which evidence was used for each case.

## Statuses

| Status | Meaning |
| --- | --- |
| `PASS` | Build and run succeeded within bounds; exit code, fixtures, stderr and self-report all agree. |
| `FAIL` | Ran, but exit code, output fixture, stderr or self-reported failure count was wrong. |
| `BUILD_FAILURE` | Compiler exited nonzero for a test-local reason. |
| `TIMEOUT` | Build or run exceeded its bound (`timeout` rc 124 or signal termination). Never a pass. |
| `SKIPPED` | Explicit `<name>.skip` marker. Counted separately, never as a pass. |
| `BLOCKED` | Build failed with the known external `CPLUS_EXIT_ARITHMETIC` runtime mismatch (see the system-api-safety doc). Not a test defect. |

Failures never stop the run: all tests and all requested modes are attempted.
The terminal table and the JSON summary carry the same per-case data. JSON is
written to `build/system-safety/summary.json` and binaries/logs to
`build/system-safety/` (gitignored); nothing is written into this directory.

Exit status: `0` = at least one `PASS` and no `FAIL`/`BUILD_FAILURE`/
`TIMEOUT`/`BLOCKED`; `1` = a required case did not pass; `2` = setup or usage
error (missing `timeout`/compiler/diff, no tests, bad options).
