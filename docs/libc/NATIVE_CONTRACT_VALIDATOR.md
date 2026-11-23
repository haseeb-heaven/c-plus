# Native contract validator

`tests/libc/validate_contracts.cp` is the native C+ replacement for
`tests/libc/validate_contracts.py`. It validates the machine-readable contract
schemas in `libc/contracts/` and the bundled `cplus_*` symbol declarations in
`libc/install-include/cplus_libc.h` with no Python interpreter in the
correctness path.

## Files

| File | Role |
|---|---|
| `tests/libc/validate_contracts.cp` | C+ program implementing every check from the Python original |
| `tests/libc/run_validate_contracts.sh` | POSIX wrapper: resolves the repo root, sets `PATH`, builds strict, runs |
| `docs/libc/NATIVE_CONTRACT_VALIDATOR.md` | This record |

The Python file is intentionally left in place; the orchestrator removes it
after all conversions land.

## Build and run

From the repository root, with the yyjson operands kept separate:

```sh
mkdir -p build/native-tools
./build/c+ build tests/libc/validate_contracts.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/native-tools/validate_contracts
build/native-tools/validate_contracts
```

Or simply use the wrapper (which performs exactly those steps from the
repository root and exports `build/` on `PATH`):

```sh
sh tests/libc/run_validate_contracts.sh
```

The two benign `clang: warning: ... 'linker' input unused` messages are the
known C+ lowering artefact documented in `tests/native/README.md`; the final
link succeeds.

## Strict-build bounds regression (fixed)

With the rebuilt strict compiler the executable aborted before printing its
summary:

```text
C+ error: index 1053 is outside the bounds of 'buf' (length 1024)
rc 134
```

Root cause (found by lowering the program with `--emit-c`): the C+ `--strict`
bounds instrumentation keys indexed writes on the **variable name**, reusing
the size of any local array with that name. `failf()` has a genuine 1024-byte
`char buf[1024]`, so the two indexed writes in `read_text()` to its **output
parameter**, which was also named `buf`, were checked against 1024 instead of
the real `cap` argument. Both callers pass `TEXT_CAP` (2 MiB) buffers; the
28,913-byte `libc/install-include/cplus_libc.h` crossed offset 1053 and tripped
a false positive. No memory was actually overwritten: `read_text()` already
guards every append with `if (used + n + 2 > cap)`.

The fix is a name change only, in `tests/libc/validate_contracts.cp`: the output
parameter of `read_text()` is now `dst` (with a comment recording why), so the
compiler no longer borrows `failf()`'s 1024-byte bound. The `cap` guard, the
2 MiB caller buffers, the `failf()` buffer, and every validation check are
unchanged; lowering the fixed source emits no bogus `__cplus_bounds_check` on
`dst`, and the failure path below still fails as expected.

## Verification (exact commands and results)

Run from the repository root on branch `agent/modern-stdlib`:

```sh
$ ./build/c+ build tests/libc/validate_contracts.cp --strict --sanitize=none \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson -o /tmp/validate_contracts
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
$ echo $?
0
```

```sh
$ /tmp/validate_contracts
contract validation: ok (5 files validated, 203 C17 APIs)
$ echo $?
0
```

```sh
$ python3 tests/libc/validate_contracts.py
contract validation: ok (5 files validated, 203 C17 APIs)
$ echo $?
0
```

The two stdout lines are byte-for-byte identical. Wrapper check:

```sh
$ chmod +x tests/libc/run_validate_contracts.sh
$ sh tests/libc/run_validate_contracts.sh
contract validation: ok (5 files validated, 203 C17 APIs)
$ echo $?
0
```

Failure path (run from a directory without the contract files; no repository
file was modified):

```sh
$ (cd /tmp && /tmp/validate_contracts)
contract validation FAILED: missing contract file: libc/contracts/contracts.json
$ echo $?
1
```

### Required regression commands (post-fix)

Exact output of the two commands used to verify this fix, from the repository
root:

```sh
$ sh tests/native/build_native_tools.sh
root: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
driver: ./build/c+
output: build/native-tools

== validate_contracts ==
+ ./build/c+ build tests/libc/validate_contracts.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/validate_contracts
  ok   validate_contracts
== test_compiler_contract_alignment ==
+ ./build/c+ build tests/libc/test_compiler_contract_alignment.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/test_compiler_contract_alignment
  ok   test_compiler_contract_alignment
== test_contract_consumption ==
+ ./build/c+ build tests/libc/test_contract_consumption.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/test_contract_consumption -- build/libc/libcplus_hardened.a
  ok   test_contract_consumption
== test_contract_integrity ==
+ ./build/c+ build tests/libc/test_contract_integrity.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/test_contract_integrity -- build/libc/libcplus_hardened.a
  ok   test_contract_integrity
== coverage ==
+ ./build/c+ build tests/stdlib/coverage.cp --strict --sanitize=none -o build/native-tools/coverage
  ok   coverage
== run_adversarial ==
+ ./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/run_adversarial -- build/libc/libcplus_hardened.a
  ok   run_adversarial
== run_all ==
+ ./build/c+ build tests/stdlib/agent2/run_all.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/run_all -- build/libc/libcplus_hardened.a
  ok   run_all
== fuzz100_run ==
+ ./build/c+ build tests/fuzz100/run.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/native-tools/fuzz100_run -- build/libc/libcplus_hardened.a
  ok   fuzz100_run

native tools: 8 built, 0 failed, 0 missing
$ echo $?
0
```

```sh
$ sh tests/libc/run_validate_contracts.sh
contract validation: ok (5 files validated, 203 C17 APIs)
$ echo $?
0
```

## Equivalence notes

Each check in the Python original has a direct counterpart:

| Python check | Native implementation |
|---|---|
| `REQUIRED_FILES` must exist and parse | Five `path_is_file()` (via `stdlib/file.hp`) + `json_parse_file()` (`stdlib/json.hp`) calls; failure prints `missing contract file` / `invalid JSON in ...` |
| `version` 1/1/2/1 for the four versioned files | `check_version()` uses `json_view::get_int`; a non-numeric or wrong value fails |
| `contracts.json`: non-empty `apis`; each has 6 keys, non-empty string `name` | `validate_contracts_json()` |
| `ownership_contracts.json`: non-empty `functions`; each has 4 keys | `validate_ownership()` |
| `libc_contracts.json`: non-empty object `functions`; every value has 4 keys | `validate_libc_contracts()` iterates object values with `yyjson_obj_foreach()` on the `json_view`'s yyjson value (the wrapper exposes no object iterator) |
| `symbol_map.json`: non-empty `mappings`; 5 keys; classification in the 5-value set; non-null `bundled_symbol` is a declared function | `validate_symbol_map()`; JSON `null` is detected via `json_view::write()` |
| `c17_contracts.json`: `schema_version == 1`, non-empty `functions`, 9 keys per function, `cplus_*` name, no duplicates, 6-value classification set, 4 non-empty (trimmed) string fields, `source`/`test` paths exist, non-empty `contracts`/`safety_limitations`, `logging` with 3 keys and non-empty `records`/`tests`, declared-as-function, matrix row count | `validate_c17()`; duplicates via a linear `seen[]` table, matrix rows counted from `docs/libc/API_TEST_MATRIX.md` |
| `declared_as_function()` prototype regex | Hand-written scanner `declared_as_function()` + `match_prototype_tail()`: anchor at line start or after `;{}`, skip whitespace, require `[A-Za-z_]` return type, word-bounded literal symbol, `(...)` containing no `;{}`, optional `CPLUS_[A-Z_]+\(...\)` attribute, terminating `;`. It reproduces the regex's "prototype, not macro/comment mention" intent without a regex engine. |
| `print("contract validation: ok (5 files validated, N C17 APIs)")` | Identical `printf()` format and count |

Failure messages are actionable but are not required to match the Python
strings character-for-character; the success line, exit status, and the set of
accepted/rejected contract states do match. No check was relaxed: in
particular, a symbol that is not a prototype in the public header still
fails, and the validator never falls back to the Python file.

## Notes on C+ lowering

- `stdlib/json.hp` methods cannot be chained on temporary `json_view`s
  (`view.member(k).valid()`); each intermediate is bound to a local variable.
- `json` handles are owning resources, so each is declared and initialised in
  its own statement (`--strict`: "declare one owning resource per statement").
- The public header is read once in `main()` before any symbol-declaration
  check runs, so `validate_symbol_map()` and `validate_c17()` share it.
- Paths are repository-root relative, matching the native-tool convention in
  `tests/native/README.md`; the wrapper `cd`s to the computed root first.
