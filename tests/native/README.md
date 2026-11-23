# Native C+ test tools

This directory holds the build entry point for the **native C+ replacements**
of the repository's Python test tooling. Each tool is a single self-contained
`.cp` program under `tests/`; there is no test-run-time Python interpreter in
this path.

Build script: [`build_native_tools.sh`](build_native_tools.sh)
Shared header: [`cplus_test_native.hp`](cplus_test_native.hp) (header-only
helpers for command execution, file I/O, JSON escaping and summaries)

## Usage

From the repository root:

```sh
sh tests/native/build_native_tools.sh
```

The script works from any directory because it resolves the repository root
from its own location and `cd`s there before compiling. Every command it runs
is printed verbatim, so it can be copied and re-run directly from the root.

Override the C+ driver when needed:

```sh
CPLUS_BIN=/abs/path/to/build/c+ sh tests/native/build_native_tools.sh
```

### Outputs

All artifacts land in `build/native-tools/`:

```
build/native-tools/validate_contracts
build/native-tools/test_compiler_contract_alignment
build/native-tools/test_contract_consumption
build/native-tools/test_contract_integrity
build/native-tools/coverage
build/native-tools/run_adversarial
build/native-tools/run_all
build/native-tools/fuzz100_run
build/native-tools/<name>.build.log     full compiler stdout+stderr per tool
```

Building does **not** run the tools. Run an individual executable from the
repository root so its relative paths resolve.

## Compile pattern

Every tool is built as:

```sh
./build/c+ build <src> --strict --sanitize=none [yyjson flags] -o <out> [-- build/libc/libcplus_hardened.a]
```

| Operand | When it is used | Why |
|---|---|---|
| `--strict --sanitize=none` | every tool | fail-closed C+ semantics with no sanitizer runtime; this is the supported default for the conversion |
| `-Ithird_party/yyjson -Lbuild/ext -lyyjson` | JSON tools (reading or emitting JSON) | `stdlib/json.hp` wraps the vendored yyjson; the three operands stay separate, never one joined string |
| `-- build/libc/libcplus_hardened.a` | tools that launch subprocesses | resolves the `cplus_system()` wrapper used by `native_run_to()` in `cplus_test_native.hp` |

Per-tool flags:

| Tool | Source | yyjson | libc archive |
|---|---|:---:|:---:|
| `validate_contracts` | `tests/libc/validate_contracts.cp` | yes | no |
| `test_compiler_contract_alignment` | `tests/libc/test_compiler_contract_alignment.cp` | yes | no |
| `test_contract_consumption` | `tests/libc/test_contract_consumption.cp` | yes | yes |
| `test_contract_integrity` | `tests/libc/test_contract_integrity.cp` | yes | yes |
| `coverage` | `tests/stdlib/coverage.cp` | no | no |
| `run_adversarial` | `tests/stdlib/run_adversarial.cp` | yes | yes |
| `run_all` | `tests/stdlib/agent2/run_all.cp` | yes | yes |
| `fuzz100_run` | `tests/fuzz100/run.cp` | yes | yes |

The benign `clang: warning: ... 'linker' input unused` messages are a known
artifact of the C+ lowering's intermediate compile step; the final link
succeeds. They are captured in each `.build.log`.

## Exit status

`0` only when every tool compiled. A missing source file or any compiler error
makes the script exit `1`; missing tools are reported as `MISSING` and are
never counted as passes. This matters while the conversion is in flight:
sibling agents may still be writing `.cp` sources, so a partial run is an
honest partial result, not a green build.

## No-Python policy

The native tools replace their `*.py` predecessors deliberately:

- **No interpreter dependency at test time.** The tools compile to native
  executables with the supported `build/c+` driver, under the same
  `--strict --sanitize=none` mode used for the rest of the hardening suite.
- **Explicit checks, not `assert`.** The Python originals already avoid bare
  `assert` because `python3 -O` strips it. Native tools keep the same
  explicit-check style, so no optimisation flag can silently disable a check.
- **One execution path.** New tooling must not shell out to `python3`. Command
  execution goes through `cplus_system()` (via `native_run_to()`), file I/O
  through `stdlib/file.hp`, and JSON through `stdlib/json.hp`/yyjson.
- **Python files are transitional.** The `*.py` originals are retained only
  until the orchestrator removes them. Do not delete them from a tool change;
  do not add new Python tooling. See
  [`docs/stdlib/NATIVE_TEST_TOOLS.md`](../../docs/stdlib/NATIVE_TEST_TOOLS.md)
  for the per-tool conversion status.
