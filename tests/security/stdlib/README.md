# Security regression runner

`run.sh` is the single entry point for the security regression suites produced
by the modern-stdlib workstream. It is a discovery runner: it does not maintain
a hand-written test list, so tests added by other agents are picked up without
editing this file.

## Usage

```sh
sh tests/security/stdlib/run.sh
sh tests/security/stdlib/run.sh --only hostile --mode strict-none,ubsan
sh tests/security/stdlib/run.sh --mode default --only sec_vector
```

| Flag | Meaning |
| --- | --- |
| `--only PATTERN` | Run only tests whose `<name>` or `<group>/<name>` contains the literal `PATTERN`. Repeatable. Non-matching tests are reported as `SKIPPED`, not deleted. |
| `--mode MODE[,MODE...]` | `strict-none` (the default), `default`, or `ubsan`. Repeatable and comma-separated. |
| `-h`, `--help` | Print usage. |

Environment overrides:

| Variable | Default |
| --- | --- |
| `CPLUS_BIN` | `<repo>/build/c+` |
| `SECURITY_OUT` | `<repo>/build/security` |
| `SECURITY_TEST_ROOTS` | the three default roots below (newline-separated) |
| `SECURITY_BUILD_TIMEOUT` / `SECURITY_RUN_TIMEOUT` | `30` seconds |
| `SECURITY_KILL_AFTER` | `5` seconds (`timeout -k 5 30`) |

### Modes

| Mode | Compiler flags |
| --- | --- |
| `strict-none` | `--strict --sanitize=none` |
| `default` | none (the compiler's default safe mode) |
| `ubsan` | `--strict --sanitize=undefined` |

Build command shape (flags word-split exactly as shown):

```sh
timeout -k 5 30 "$CPLUS" build <source> <mode flags> -o <binary>
```

Run command shape:

```sh
timeout -k 5 30 <binary> < <name>.stdin   # when the fixture exists
timeout -k 5 30 <binary> < /dev/null      # otherwise
```

## Discovery

Every `.c` and `.cp` file is discovered **recursively** under:

| Directory | Group name |
| --- | --- |
| `tests/security/stdlib/` | `stdlib` |
| `tests/security/hostile_inputs/` | `hostile_inputs` |
| `tests/security/resource_exhaustion/` | `resource_exhaustion` |

Excluded: `NOTES.*`, `README.*`, hidden files/paths, anything inside a
`fixtures/` directory, and all non-source sidecars. A missing group directory,
or a group with zero discovered sources, is reported as `EMPTY` in the table,
as a `warning:` line on stderr, and as `"status": "EMPTY"` in the JSON group
record. **Empty groups are never silently green**, but they are not counted as
a required failure — other agents may still be adding tests. If no tests at
all are discovered, an extra `zero tests discovered; this is NOT a passing
result` warning is printed.

Recursive subpaths are flattened into artifact names by replacing `/` with
`__`, so `tests/security/stdlib/deep/foo.cp` becomes
`build/security/stdlib/deep__foo.<mode>.*`.

## Sidecar fixtures

All sidecars sit next to the source and are named after its basename
(`foo.cp` → `foo.*`):

| File | Effect |
| --- | --- |
| `foo.stdin` | Fed to the program's stdin. Without it, stdin is `/dev/null`. |
| `foo.exit` | Expected exit code (default `0`). Non-numeric contents fall back to `0`. |
| `foo.expected` | Exact expected stdout (`diff -u`). |
| `foo.stderr.expected` | Literal substring that must appear in stderr (`grep -F`). |
| `foo.skip` | Classify as `SKIPPED`; file contents become the detail (first 160 chars). |
| `foo.blocked` | Classify as `BLOCKED`; file contents become the detail. |
| `foo.modes` | Space/comma-separated allowlist of modes; other modes are `SKIPPED`. |

stderr is captured but not judged unless `foo.stderr.expected` exists: security
fixtures may legitimately print diagnostics while still passing.

## Classification

| Status | Meaning | Counts toward nonzero exit? |
| --- | --- | --- |
| `PASS` | Build and run exited 0 (or the `.exit` value), and any `.expected` / `.stderr.expected` checks passed. | no |
| `FAIL` | Built and ran, but the exit code or output expectation was wrong. A crash (e.g. rc 139) is a `FAIL`, not a timeout. | **yes** |
| `BUILD_FAILURE` | `c+ build` exited nonzero (not a timeout). | **yes** |
| `TIMEOUT` | `timeout` returned 124 (TERM) or 137 (KILL). Applies to both build and run. | **yes** |
| `SKIPPED` | Explicit `.skip`, `--only` filter, or mode not in `.modes`. | no |
| `BLOCKED` | Per-test `.blocked` marker, or a global environment block (missing `c+` / `timeout`). | **yes** |

The runner never uses `|| true`, and always continues after a failure so the
table and JSON cover every discovered test in every selected mode.

## Artifacts (all under `build/security/`)

- `security_summary.json` — machine-readable summary: toolchain, limits,
  modes, filters, totals, per-group discovery status, and one `results` entry
  per invocation with status, detail, exit code, exact command lines and
  artifact paths.
- `security_table.txt` — exact copy of the terminal table.
- `<group>/<safe-name>.<mode>.build.log` — exact compiler stdout+stderr.
- `<group>/<safe-name>.<mode>.out` / `.err` — exact program stdout / stderr.
- `<group>/<safe-name>.<mode>.rc` — captured exit status.
- `<group>/<safe-name>.<mode>.diff` — only when a `.expected` mismatch occurs.

## Exit status

- `0` — no required failures (note: stderr warnings may still be present).
- `1` — at least one `FAIL`, `BUILD_FAILURE`, `TIMEOUT`, or `BLOCKED`.
- `2` — bad invocation (unknown flag/mode) or missing toolchain. Even then,
  every test is recorded as `BLOCKED` and the JSON/table are still written.
