# Native C+ fuzz100 runner

`tests/fuzz100/run.cp` is a native C+ replacement for `tests/fuzz100/run.py`.
`tests/fuzz100/run_native.sh` is its shell wrapper. The Python runner is left
in place; both runners discover and classify the same corpus and write the same
`build/fuzz100/` layout.

## Files

| File | Role |
| --- | --- |
| `tests/fuzz100/run.cp` | Native runner (discovery, build, run, classification, JSON + terminal summaries). |
| `tests/fuzz100/run_native.sh` | Wrapper: resolves the repo root, builds `run.cp`, executes it. |

The runner uses `cplus_system()` (from `build/libc/libcplus_hardened.a`) for
every child process and `stdlib/file.hp` for all file reads/writes. Time bounds
are enforced by prefixing each command with `/usr/bin/timeout 30` (with
`gtimeout` fallbacks); a 124 exit is classified `TIMEOUT`. Exit status decoding
maps `128+N` (and signal deaths) to `-N`, matching Python's `subprocess`
negative-signal return values.

## Build (strict mode)

The archive must follow `--` so the C+ driver forwards it to the linker only;
a bare `.a` operand is treated as another source file by the backend.

```
$ cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
$ ./build/c+ build tests/fuzz100/run.cp --strict --sanitize=none \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o /tmp/native_fuzz100 -- build/libc/libcplus_hardened.a
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
BUILD_EXIT=0
```

The three warnings come from the backend's compile step (`-l`/`-L`/archive are
link-only inputs); the link step succeeds and the binary contains `cplus_system`:

```
$ /usr/bin/nm -gU /tmp/native_fuzz100 | grep cplus_system
000000010000b104 T _cplus_system
```

`sh tests/fuzz100/run_native.sh` performs the equivalent build with absolute
paths into `build/fuzz100/run_native.bin`.

## Dry run (exact output)

```
$ /tmp/native_fuzz100 --root "$PWD" --dry-run
fuzz100: discovered 110, executed 0
fuzz100: DRY RUN -- no build or run performed
config   case                               kind   json  status        stage       b/r    
------------------------------------------------------------------------------------------
F021     f100_alloc_f021_malloc.c           c      no    SKIPPED       discovery   -/-    
F022     f100_alloc_f022_calloc.c           c      no    SKIPPED       discovery   -/-    
... (110 rows total) ...
F073     f100_vec_f073_faults.cp            cplus  no    SKIPPED       discovery   -/-    
------------------------------------------------------------------------------------------
fuzz100: pass=0, fail=0, build_failure=0, timeout=0, skipped=110, blocked=0
fuzz100: JSON summary: build/fuzz100/summary.json
fuzz100: logs: build/fuzz100/cases
```

Exit status: `0`. The wrapper (`sh tests/fuzz100/run_native.sh --dry-run`) emits
the same rows; its stderr additionally carries the three harmless compile-step
warnings above. `--json-stdout` emits the same `summary.json` object on stdout
and is valid JSON (verified with `python3 -c 'json.load(...)'`).

## Full verification against the Python runner

Both runners were run on the same corpus, back to back, from the repository
root. Each wrote `build/fuzz100/summary.json`; copies were compared.

```
$ python3 tests/fuzz100/run.py --root "$PWD"          # PY_EXIT=0
fuzz100: discovered 110, executed 100
fuzz100: pass=100, fail=0, build_failure=0, timeout=0, skipped=10, blocked=0

$ sh tests/fuzz100/run_native.sh                      # NAT_EXIT=0
fuzz100: discovered 110, executed 100
fuzz100: pass=100, fail=0, build_failure=0, timeout=0, skipped=10, blocked=0
```

Comparison results:

- Top-level summary fields match except `runner` (intentionally
  `tests/fuzz100/run.py` vs `tests/fuzz100/run.cp`): `schema=1`, same `root`,
  `timeout_s=30`, `filter_configs=[]`, `dry_run=false`, `discovered=110`,
  `executed=100`, identical `counts`, same `case_logs_root` and `binary_root`.
- Case order/paths match for all 110 cases.
- Per-case fields `name`, `path`, `slug`, `config`, `kind`, `json_case`,
  `skip_reason`, `build_command`, `run_command`, `status`, `stage`, `reason`,
  and `build.{rc,timed_out,spawn_error}` / `run.{rc,timed_out,spawn_error}`
  match for all 110 cases: **0 differences**.
- The human-readable tables are byte-identical after excluding the wrapper's
  compile-step warnings (`diff` exit 0).

`--config F021,F022` also matched: both reported `discovered=110, executed=2`,
`pass=2, skipped=108`, and executed the same two harnesses.

## Discrepancy investigation: 95/6/101 vs 110/100/10

The handoff text cited the Python runner's "last result" as 95 PASS,
6 SKIPPED, 0 FAIL/BUILD_FAILURE from 101 discovered. The on-disk
`build/fuzz100/summary.json` at the start of this work already showed
110 discovered / 100 PASS / 10 SKIPPED, and a fresh Python run confirmed
those numbers. This is corpus growth, not a runner mismatch: the fuzz corpus
is still being extended by sibling agents (the fuzz100 README explicitly says
discovered counts grow between runs). The 10 current SKIPPED cases are
helper/asset files with unsupported extensions (`.h`, `.hp`, `.sh`, `.json`);
the 4 extra SKIPPED entries relative to the cited snapshot are the `.sh`
runner helpers. The native runner matches the Python runner on the current,
larger corpus exactly.

Two implementation discrepancies were found and fixed during comparison:

1. Recursive discovery initially recorded paths relative to each search
   directory instead of the repo root; fixed by seeding the walk with
   `tests/crt/fuzz` / `tests/stdlib/fuzz`.
2. JSON-case detection initially tested the whole basename, which misflagged
   `f100_thread_configs.json` (its `.json` extension matched "json"). Fixed by
   testing the filename stem, as `run.py` does.

After both fixes, the per-case and table comparisons are identical.
