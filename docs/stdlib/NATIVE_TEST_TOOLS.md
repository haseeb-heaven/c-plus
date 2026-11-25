# Native C+ test tools — conversion status

This page tracks the migration of the repository's Python test tooling to
native C+ programs. The authoritative build entry point is
[`tests/native/build_native_tools.sh`](../../tests/native/build_native_tools.sh);
per-command usage and the no-Python policy are in
[`tests/native/README.md`](../../tests/native/README.md).

## Policy

1. **No Python at test runtime.** Each tool is a single `.cp` program compiled
   with the supported `build/c+` driver using `--strict --sanitize=none`.
2. **Explicit checks only.** The Python originals deliberately avoid bare
   `assert` because `python3 -O` removes it. Native replacements preserve the
   explicit-check style so no optimisation level can silently disable a check.
3. **Supported wrappers only.** Subprocesses go through `cplus_system()` (the
   hardened libc archive), file I/O through `stdlib/file.hp`, and JSON through
   `stdlib/json.hp` (yyjson). New tools must not invoke `python3`.
4. **Python files are retained only until orchestrator removal.** The `*.py`
   originals are deliberately left in place during the conversion so the two
   implementations can be compared. Removing them is the orchestrator's
   explicit step, not a per-tool decision; tool authors must not delete them.

## Conversion status

"Native source present" refers to whether the `.cp` file existed when this page
was authored. The build script is the live source of truth: it reports each
tool as `ok`, `FAIL`, or `MISSING` and exits non-zero if any source is absent or
fails to compile.

| Native tool | Python predecessor | Native source | JSON / yyjson | libc archive |
|---|---|---|:---:|:---:|
| `validate_contracts` | `tests/libc/validate_contracts.py` | `tests/libc/validate_contracts.cp` | yes | no |
| `test_compiler_contract_alignment` | `tests/libc/test_compiler_contract_alignment.py` | `tests/libc/test_compiler_contract_alignment.cp` | yes | no |
| `test_contract_consumption` | `tests/libc/test_contract_consumption.py` | `tests/libc/test_contract_consumption.cp` | yes | yes |
| `test_contract_integrity` | `tests/libc/test_contract_integrity.py` | `tests/libc/test_contract_integrity.cp` | yes | yes |
| `coverage` | `tests/stdlib/coverage.py` | `tests/stdlib/coverage.cp` | no | no |
| `run_adversarial` | `tests/stdlib/run_adversarial.py` | `tests/stdlib/run_adversarial.cp` | yes | yes |
| `run_all` | `tests/stdlib/agent2/run_all.py` | `tests/stdlib/agent2/run_all.cp` | yes | yes |
| `fuzz100_run` | `tests/fuzz100/run.py` | `tests/fuzz100/run.cp` | yes | yes |

At authoring time all eight `.cp` sources were still being written by sibling
agents, so a verification run of the build script reported them as `MISSING`
rather than compiled or passed. Re-run the script to get the current status;
do not treat this table as proof that a binary built.

The yyjson column means the tool reads or emits JSON and therefore needs
`-Ithird_party/yyjson -Lbuild/ext -lyyjson`. The libc archive column means the
tool launches subprocesses through `cplus_system()` and therefore needs
`-- build/libc/libcplus_hardened.a` after the output path. Both flags are
emitted verbatim by the build script; the benign
`clang: ... 'linker' input unused` warnings from the lowering step are captured
in the per-tool `.build.log` files.

## Build and verify

```sh
# From the repository root (works from any directory):
sh tests/native/build_native_tools.sh

# Exact command for one tool, as printed by the script:
./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/native-tools/run_adversarial -- build/libc/libcplus_hardened.a
```

Exit status is `0` only when all eight tools compiled. Prerequisites are
`build/c+`, `third_party/yyjson/yyjson.h`, `build/ext/libyyjson.a` and (for the
subprocess tools) `build/libc/libcplus_hardened.a`. Override the driver with
`CPLUS_BIN` if needed.

## Orchestrator removal checklist

The Python files stay until the orchestrator confirms:

- every native `.cp` source exists and compiles via the build script;
- each native tool has been run against the same inputs as its Python
  predecessor with matching exit status and output shape;
- the build script exits `0` with zero `MISSING` and zero `FAIL` tools.

Only then should the orchestrator remove the `*.py` predecessors and update
any wrappers that still call them.
