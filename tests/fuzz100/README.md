# fuzz100 runner

Dedicated runner for the 100-configuration fuzz corpus (`f100_*` harnesses).
It discovers harnesses **at run time**, builds each with the repository's
supported compiler, runs each under a hard 30-second bound, and emits both a
machine-readable JSON summary and a human summary under `build/fuzz100/`.

Nothing about the corpus is hard-coded. If sibling agents are still writing
harnesses, the runner simply discovers whatever exists when it is invoked.
Results are never inferred: a case is `PASS` only if its build exited 0 **and**
its process exited 0 within the time bound.

## Usage

From the repository root (the shell wrapper works from any directory):

```sh
sh tests/fuzz100/run.sh                       # all discovered f100_* harnesses
sh tests/fuzz100/run.sh --config F001         # only harnesses named ...F001...
sh tests/fuzz100/run.sh --config F001,F002    # repeatable and/or comma-separated
sh tests/fuzz100/run.sh --config 001          # 3 digits are normalised to F001
sh tests/fuzz100/run.sh --dry-run             # discover + report, no build/run
```

`run.py` can also be called directly; `--root` defaults to the inferred repo root:

```sh
python3 tests/fuzz100/run.py --config F001
python3 /abs/path/tests/fuzz100/run.py --root /abs/path --dry-run
```

Options:

| Option | Meaning |
|---|---|
| `--config Fnnn` | Restrict execution to harnesses whose filename contains the config token. Repeatable and comma-separated. A bare 3-digit value is prefixed with `F`; values are upper-cased. `all` is ignored (no filter). |
| `--dry-run` | Discover and report config / kind / JSON / prerequisite status only. Every discovered case is reported `SKIPPED` with reason `dry-run (no build or run performed)`. No compiler is invoked and no binary is produced. |
| `--json-stdout` | Emit the same JSON summary object on stdout instead of the human table (the file is always written). |
| `--root PATH` | Override the repository root (default: inferred from the script location). |

Environment overrides (defaults match the repository's supported toolchain):

| Variable | Default | Use |
|---|---|---|
| `F100_CLANG` | `/usr/bin/clang` | C harness backend. |
| `CPLUS_BIN` | `<root>/build/c+` | C+ driver for `.cp` harnesses. |
| `CPLUS_CLANG` | `/usr/bin/clang` | Set (if unset) for the C+ driver's backend, as in `tests/stdlib/agent2/SANITIZER_MATRIX.md`. |

## Discovery

`f100_*` files are discovered recursively under:

- `tests/crt/fuzz/`
- `tests/stdlib/fuzz/`

Only regular files are considered. Every discovered file appears in the report,
including ones that are skipped. The manifest name (config token) is parsed
from the filename: the leading `f100_` is stripped, then the first `F<3 digits>`
token (case-insensitive) becomes the config, e.g. `f100_F001_strcpy.c` →
`F001`. A file with no such token has `config: null` and is never matched by
`--config`.

Build selection is by extension:

| Extension | Kind | Build command |
|---|---|---|
| `.c` | `c` | `/usr/bin/clang -std=c11 -O1 -g -DCPLUS_FUZZ_SMOKE_MAIN -I. -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include <src> [build/libc/libcplus_hardened.a] [build/safe/libcplus_safecrt.a] -pthread -lm [JSON_FLAGS] -o <bin>` |
| `.cp` | `cplus` | `build/c+ build <src> --strict --sanitize=none [JSON_FLAGS] -o <bin>` |
| anything else | `unsupported` | Not built; reported `SKIPPED`. |

The `-Ilibc/install-include` path is required for `cplus_libc.h`; the other
`-Ilibc*` paths mirror the canonical include set used by the CRT/libc test
suites. The hardened libc archive (`build/libc/libcplus_hardened.a`, providing
`cplus_*_bounded` and the plain `cplus_mem*`) and the safe-CRT archive
(`build/safe/libcplus_safecrt.a`, providing the `*_s` entry points) are each
linked only when present, so a missing artifact never turns an otherwise
self-contained harness into a false `BUILD_FAILURE`. `CPLUS_FUZZ_SMOKE_MAIN`
follows the existing `tests/crt/fuzz/run_fuzz_smoke.sh` convention and is
harmless for harnesses that define their own `main`.

### JSON cases and yyjson flags

A harness is a *JSON case* if either its filename contains `json`
(case-insensitive) **or** its source contains `json.hp` or `yyjson`. JSON cases
receive these three operands **as separate argv elements** (never as one joined
string):

```
-I<root>/third_party/yyjson   -L<root>/build/ext   -lyyjson
```

This is the same triplet used by `tests/stdlib/fuzz/run_fuzz.sh` and
`tests/stdlib/agent2/SANITIZER_MATRIX.md`. If `yyjson.h` or `libyyjson.a` is
missing, a JSON case is `BLOCKED`, not failed.

## Time bound and captured data

Both the build step and the run step are bounded independently by
`TIMEOUT_SECONDS = 30`, enforced with `subprocess.run(..., timeout=30)`. This
is equivalent to `timeout 30` and does not depend on GNU coreutils being
installed on macOS. Full stdout, full stderr, exit code, spawn errors and
wall-clock duration are captured for each step; a 30-second abort is marked
`TIMEOUT`. The runner never suppresses shell errors to mask a failure and
always continues after a failed case.

## Exact classification

Statuses are `PASS`, `FAIL`, `BUILD_FAILURE`, `TIMEOUT`, `SKIPPED`, `BLOCKED`.
Rules are applied in this order; the first match wins:

| Order | Condition | Status | Stage |
|---|---|---|---|
| 1 | Harness filtered out by `--config`, has an unsupported extension, `--dry-run` was requested, or its source disappeared after discovery (concurrent edit) | `SKIPPED` | `discovery` |
| 2 | A prerequisite is absent: selected compiler not executable, or JSON case with missing `yyjson.h` / `libyyjson.a` | `BLOCKED` | `prerequisite` |
| 3 | Compiler process could not be started (`OSError`) | `BLOCKED` | `build` |
| 4 | Build exceeded 30 s | `TIMEOUT` | `build` |
| 5 | Build exited non-zero (includes compiler/backend rejection) | `BUILD_FAILURE` | `build` |
| 6 | Built binary could not be started (`OSError`) | `BLOCKED` | `run` |
| 7 | Run exceeded 30 s | `TIMEOUT` | `run` |
| 8 | Build ok and run exited 0 | `PASS` | `run` |
| 9 | Build ok and run completed with non-zero exit (including a sanitizer/assert abort) | `FAIL` | `run` |

Notes on honesty and limits:

- There is no expected-output fixture in this corpus, so correctness is defined
  by the harness itself: exit 0 means the harness reports its configuration OK.
  stdout/stderr are recorded but do not override the exit code.
- `BUILD_FAILURE` means the compiler ran and rejected the input. A compiler
  that is missing or cannot be launched is `BLOCKED`, never a build failure.
- A skipped case is not a pass. Zero discovered harnesses is reported as
  "zero cases is not a pass"; it is not an error, because siblings may still be
  writing the corpus.
- A negative exit code means the process was killed by a signal; it is reported
  verbatim and classified `FAIL` (or `TIMEOUT` when the bound was hit).

## Outputs

All outputs live under `build/fuzz100/`:

```
build/fuzz100/summary.json                 machine-readable summary (all cases)
build/fuzz100/cases/<slug>/result.json     full record for one case
build/fuzz100/cases/<slug>/build.command.txt
build/fuzz100/cases/<slug>/build.stdout.log
build/fuzz100/cases/<slug>/build.stderr.log
build/fuzz100/cases/<slug>/run.command.txt   (only when a run was attempted)
build/fuzz100/cases/<slug>/run.stdout.log
build/fuzz100/cases/<slug>/run.stderr.log
build/fuzz100/bin/<slug>.bin                 built harness binary
```

`<slug>` is the harness path relative to the repo root with every character
outside `[A-Za-z0-9_-]` (including `/` and `.`) replaced by `_`; the extension
is deliberately retained so `probe.cp` and `probe.hp` never share a case
directory. Skipped cases have a `result.json` but no build/run logs.

`summary.json` shape (`schema: 1`):

```json
{
  "schema": 1,
  "root": "...",
  "runner": "tests/fuzz100/run.py",
  "timeout_s": 30,
  "filter_configs": ["F001"],
  "dry_run": false,
  "discovered": 12,
  "executed": 1,
  "counts": {"PASS": 1, "FAIL": 0, "BUILD_FAILURE": 0,
             "TIMEOUT": 0, "SKIPPED": 11, "BLOCKED": 0},
  "case_logs_root": "build/fuzz100/cases",
  "binary_root": "build/fuzz100/bin",
  "cases": [ { "name": "...", "path": "...", "slug": "...", "config": "F001",
               "kind": "c", "json_case": false, "status": "PASS",
               "stage": "run", "reason": "...", "build": {"rc": 0, "...": 0},
               "run": {"rc": 0, "...": 0},
               "build_command": "...", "run_command": "...",
               "logs": {"build.stdout": "...", "...": "..."} } ]
}
```

Large captured outputs are written in full to the `.log` files; the JSON record
keeps only the last 4000 characters (`stdout_tail`, `stderr_tail`) per step.

## Exit status

- `0` — no executed case had status `FAIL`, `BUILD_FAILURE`, `TIMEOUT` or
  `BLOCKED` (this includes zero discovered harnesses and dry runs).
- `1` — at least one executed case had one of those four statuses.
- `2` — invalid command-line usage (argparse).

`SKIPPED` never causes a non-zero exit, but it is always counted and visible
in both summaries.
