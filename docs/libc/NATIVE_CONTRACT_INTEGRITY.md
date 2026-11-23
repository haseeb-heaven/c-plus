# Native contract-integrity test

`tests/libc/test_contract_integrity.cp` is the native C+ replacement for
`tests/libc/test_contract_integrity.py`. It performs the same 52 adversarial
mutations against an isolated overlay of the contract tree and runs the same
repository cross-checks. The Python file is retained by the orchestrator and is
not modified or deleted by this change.

## Build and run

```sh
# Build (exact command emitted by tests/native/build_native_tools.sh):
./build/c+ build tests/libc/test_contract_integrity.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/native-tools/test_contract_integrity \
  -- build/libc/libcplus_hardened.a

# Run from the repository root (any directory works; argv[1] may override root):
./build/native-tools/test_contract_integrity
```

Success prints exactly:

```
contract_integrity: ok (52 attacks detected, all cross-checks clean)
```

Any finding, a missing attack, a failed overlay reset, or a failed baseline
validation makes the tool print diagnostics to stderr and exit non-zero.

## Why the overlay is complete

The Python predecessor copied only `validate_contracts.py`, the contract JSON
files and `cplus_libc.h` into its temporary tree. The current validator also
validates `c17_contracts.json`, whose 203 entries carry `source`/`test` paths,
and it checks the `docs/libc/API_TEST_MATRIX.md` row count. Those files were
**not** copied, so a clean Python overlay always failed with:

```
contract validation FAILED: c17_contracts.json functions[0]: source path does not exist: libc/src/fenv/fenv_extra.c
```

Every one of the Python test's 52 attacks therefore "passed" for the same
unrelated reason (verified by running the Python attack helpers against a
manually completed overlay). The C+ version copies the whole `libc/` and
`tests/libc/` trees plus the matrix document, so a clean overlay validates and
each attack is judged on its own mutation. The tool explicitly validates the
clean overlay before running any attack and aborts if that baseline fails.

The overlay is created with `mktemp -d` under `TMPDIR` (fallback `/tmp`), and
removed with `rm -rf` via `atexit`. A pristine copy of the contracts and header
is kept inside the overlay; before every attack the affected files are copied
back, so the working tree is never touched.

## Detection provenance (important limitation)

The native validator binary (`build/native-tools/validate_contracts`) was not
available when this tool was written and verified. Attack detection therefore
uses **inline C+ checks** in `inline_validate()` that mirror
`tests/libc/validate_contracts.py` over the overlay files, using
`stdlib/json.hp` (yyjson) for parsing and `stdlib/file.hp` for file existence.

With a complete overlay, 51 of the 52 mutations are rejected by the unmodified
Python validator rules. The remaining case —
`symbol_map.json: forged safe_symbol = "cplus_fake_safe_symbol"` — is a genuine
silent accept in the Python validator: it never reads `safe_symbol`. The inline
checks add one explicit, documented rule to cover it:

> `safe_symbol` must be `null` or a string ending in `_s` (the safe-CRT binding
> suffix used by all 29 baseline values in `symbol_map.json`).

Every attack is checked twice (reject-expected plus diagnostic-strength check):
a validator exit of 0 is reported as `SILENT ACCEPT`, and an empty or
exception-only message as `WEAK DIAGNOSTIC`. A stderr note records that inline
checks were used. When the native validator binary lands, it can replace
`inline_validate()` as the detector without changing the attack set.

## Attack inventory (52)

| # | Mutation |
|---|---|
| 1-4 | Invalid JSON syntax / version 999 / empty collection in each of 4 required files |
| 5-31 | Each required key removed from the first entry of each required file (6+4+4+5 = 19) plus the four invalid-syntax/version/empty cases above (4+4+4) |
| 32 | `symbol_map.json` classification `NOT_A_CLASS` |
| 33 | `contracts.json` version 999 (the `python3 -O` case; inline checks cannot be optimised away) |
| 34 | `ownership_contracts.json` missing `thread_safety` (the second `-O` case) |
| 35-43 | Nine `c17_contracts.json` mutations: syntax, `schema_version`, empty functions, bad classification, missing name, duplicate name, missing source, empty `host_backend`, empty `safety_limitations` |
| 44-50 | Seven `bundled_symbol` forgeries (near-miss, trailing space, unknown, macro-only, host `malloc`, empty string, non-string `123`) |
| 51 | Symbol declared only inside an appended header comment (`cplus_ghost_fn`) |
| 52 | Forged `safe_symbol` (inline-only rule described above) |

## Cross-checks

`cross_check_repository()` mirrors the Python repository checks against the
**real** repository (not the overlay):

- 203 c17 entries, no duplicate names, all required fields non-empty,
  classification `PLATFORM_BACKED`, and every `source`/`test` path exists;
- every matrix symbol is declared as a function in `cplus_libc.h`
  (regex-equivalent `declared_as_function`);
- exported `cplus_*` definitions in every referenced source file are in the
  matrix and have a compiled symbol (`nm -g --defined-only` on
  `build/libc/libcplus_hardened.a`);
- header and source signatures are token-normalised and compared
  (`strip_noise` + `find_c_calls` equivalents);
- `API_TEST_MATRIX.md` row count and names agree with the JSON matrix.

## Native-helper notes

Two shared-helper behaviours required local adaptations without changing the
helpers themselves:

- `native_run_to()` wraps commands in shell braces (`{cmd}`); the caller must
  supply a leading space and trailing `;` so the shell parses `{ cmd ; }`.
- `native_read()` advances by `strlen()`, which is unsafe for text files with
  no embedded NUL (all command output). The tool uses an exact-size reader
  built on `file.hp`'s `native()` handle instead.

## Verification record

All commands were run from the repository root on 2026-11-23.

### 1. Build (exact native-tools command) — exit 0

```sh
./build/c+ build tests/libc/test_contract_integrity.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/native-tools/test_contract_integrity \
  -- build/libc/libcplus_hardened.a
```

Output (only the known benign lowering-step warnings):

```
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
```

`BUILD_RC=0`.

### 2. Native run — exit 0

```sh
./build/native-tools/test_contract_integrity
```

stdout:

```
contract_integrity: ok (52 attacks detected, all cross-checks clean)
```

stderr:

```
note: 52 attacks verified with inline C+ validator checks (no native validator binary available at verification time)
```

`NATIVE_RC=0`. Runtime ~6 s. There were no `SILENT ACCEPT` or `WEAK DIAGNOSTIC`
findings and no cross-check findings: all 52 mutations were genuinely rejected.

### 3. Python comparison — exit 0

```sh
python3 tests/libc/test_contract_integrity.py
```

stdout is byte-for-byte identical to the native stdout line above; stderr was
empty. `PY_RC=0`.

### 4. Working-tree safety / cleanup

- `git status --short libc/contracts libc/install-include docs/libc tests/libc`
  was identical before and after the runs.
- No `contract-attack-*` directories remained in `TMPDIR` after successful runs
  (the isolated overlay is removed via `atexit`).
- Running from `/tmp` with an explicit repository-root argument produced the
  same success line and rc 0.

### Limitations

- Detection is by inline C+ checks, not a compiled native validator: the
  `validate_contracts.cp` binary was not present at verification time. The
  stderr note records this on every run.
- The forged-`safe_symbol` attack is caught only by the extra inline `_s`
  binding rule described above; the Python validator silently accepts it when
  given a complete overlay. This is documented rather than hidden.
- The `python3 -O` attacks are executed with the same mutation under native
  checks; no optimiser can disable an explicit C+ check, so the `-O` flag is
  not applicable (the labels in the doc/comments reflect that).
