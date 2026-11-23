# Native compiler contract alignment test

This document records the native C+ replacement for
`tests/libc/test_compiler_contract_alignment.py` and the evidence collected
while porting it. The Python file is intentionally left in place (not deleted)
for comparison; the new source is:

- `tests/libc/test_compiler_contract_alignment.cp`

Only those two files were created for this task. No git add/commit was
performed.

## 1. What was ported

The Python test validates `libc/contracts/compiler_libc_contracts.json`. Every
check was ported explicitly (no bare `assert`, so optimised builds cannot
silently disable it). The C+ source verifies:

1. **File + parse** — contract file is readable (`stdlib/file.hp` RAII handle);
   JSON parses via `stdlib/json.hp::json` (yyjson). Corrupt JSON or a missing
   file is a hard failure. An optional `argv[1]` overrides the default
   repo-relative path `libc/contracts/compiler_libc_contracts.json`.
2. **Top level** — object; `schema_version == 1`; `source == "Agent 2 hardened
   libc"`; non-empty `note`.
3. **`contract_semantics`** — object; `additive == true`; `per_api_keys` string
   contains all nine API keys; `aspects` object contains all 11 required
   semantic aspects; every aspect value is a non-empty string.
4. **`implementation_symbols`** — non-empty object.
5. **`apis`** — non-empty list.
6. **Per-entry schema** — for every entry: exact nine-key set (`name`,
   `params`, `ownership`, `transfer`, `extent`, `effects`, `failure`,
   `diagnostics`, `dynamic`); unique non-empty `name`; `params` list whose
   members have the exact four-key set (`name`, `type`, `nullable`, `extent`)
   with correct value types; non-empty `ownership`; `transfer` string-or-null;
   non-empty `extent`; non-empty string list `effects`; `failure` with exact
   two-key set (`result_code`, `conditions`), non-empty result code and
   non-empty string-list conditions; `diagnostics` list whose every member is
   one of the five allowed `CPLUS-SAFE-*` strings; `dynamic` with exact two-key
   set (`checks`, `limitations`), non-empty string-list `checks` and a
   `limitations` list.
7. **Name coverage** — all 29 baseline names and all 19 P0 names are present.
8. **Symbol coverage** — every contract name resolves to a non-empty symbol;
   the five checked allocators (`malloc_s`, `calloc_s`, `realloc_s`, `free_s`,
   `aligned_alloc_s`) must map to their own name; all other names must map to a
   `cplus_*` symbol; no unknown/extra symbol keys.
9. **Extended P0 semantic evidence** — the six additive entries (`vsnprintf`,
   `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `aligned_alloc_s`) must carry
   typed-field evidence for `argument_types`, `buffer_sizes`, `nullability`,
   `ownership_acquisition_release` and `error_behavior`, plus serialized-text
   marker evidence for `read_write_behavior`, `source_dest_relationships`,
   `borrowed_args`, `returned_pointer_lifetime`, `allocation_behavior` and
   `reallocation_semantics`.

Success prints exactly `compiler contract alignment: ok` on stdout. Any
failure prints `compiler contract alignment FAILED: <detail>` on stderr and
exits with status 1.

### Library usage

- `stdlib/file.hp` — `file(path, "rb")`, `is_open()`, `native()` (fseek/ftell/
  fread for a byte count), RAII close.
- `stdlib/json.hp` — `json` (authoritative parse + `root()`), `json_view_of`
  plus `json_view::get_string/get_bool/get_int/write` for typed getters and
  serialized-text evidence, `json_free` for the writer buffer.
- Raw `yyjson` (shipped via `stdlib/json.hp`) is used only for object-key
  enumeration (`yyjson_obj_iter_*`) because `json_view` intentionally exposes
  no key iterator. Member/value access still goes through `json_view` helpers.

## 2. Build and run (exact commands and results)

All commands were run from the repository root on branch `agent/modern-stdlib`.
The yyjson include/link triplet is passed as **three separate argv operands**
(never one joined string).

### Canonical build (default mode)

```
$ ./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
      -Ithird_party/yyjson -Lbuild/ext -lyyjson \
      -o build/test_compiler_contract_alignment
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
BUILD_EXIT=0
```

The four clang warnings come from the C backend seeing the link-only operands
during the lowering/compile stage. They are harmless (the final link uses
them) and also appear for the repository's existing `deep_json*.cp` tests.

### Canonical run

```
$ ./build/test_compiler_contract_alignment > out 2> err; echo "EXIT=$?"
EXIT=0
$ cat out
compiler contract alignment: ok
$ cat err
```

### Build/run matrix

The same build was repeated with the modes used by
`tests/stdlib/run_deep_tests.sh`. All succeeded; stdout was exactly
`compiler contract alignment: ok` and stderr was empty in every case.

| Mode | Build flags | build rc | run rc | stdout |
| --- | --- | --- | --- | --- |
| default | (none) | 0 | 0 | `compiler contract alignment: ok` |
| strict-none | `--strict --sanitize=none` | 0 | 0 | `compiler contract alignment: ok` |
| strict-ubsan | `--strict --sanitize=undefined` | 0 | 0 | `compiler contract alignment: ok` |
| release | `--release` | 0 | 0 | `compiler contract alignment: ok` |

Reproduction commands (run one at a time; flags are separate operands):

```
./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/cca_default
./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
    --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/cca_strict_none
./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
    --strict --sanitize=undefined -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/cca_strict_ubsan
./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
    --release -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/cca_release
```

## 3. Equivalence with the Python test

While the Python oracle was still available, both tests were run against the
same contract file:

```
$ python3 tests/libc/test_compiler_contract_alignment.py; echo "PY_EXIT=$?"
compiler contract alignment: ok
PY_EXIT=0

$ ./build/test_compiler_contract_alignment; echo "CPLUS_EXIT=$?"
compiler contract alignment: ok
CPLUS_EXIT=0
```

Both print the identical success line and exit 0.

Equivalence notes:

- The success string and exit status match the Python test exactly.
- Failure messages intentionally mirror the Python wording (e.g.
  `missing required API names: [...]`, `apis[0]: key mismatch, missing=[...],
  extra=[...]`, `... must be a cplus_* symbol, got '...'`). Python's `repr` of
  the offending value is approximated with single quotes; this affects only
  diagnostic text on failure, never pass/fail behaviour.
- The Python test does **not** reject extra API names, and neither does the C+
  port (the shipped contract has `memcpy_s`, `snprintf_s`, `strcpy_s` in
  addition to the required sets). Only missing names, symbol coverage and
  schema/semantic validity are enforced.
- `exact_keys` is a set comparison (like Python's `set(obj)`), implemented by
  enumerating keys with yyjson and testing each expected key with
  `yyjson_obj_get`, so duplicate/extra keys are reported as mismatches.
- Extended-entry text markers are matched against a lower-cased, compact
  serialization of the entry (`json_view::write`), equivalent to Python's
  `json.dumps(api).lower()` for the word/substring markers used.
- String "empty" checks use "contains at least one non-whitespace character",
  matching Python's `.strip()` guard.

## 4. Negative verification (no fabricated passes)

To prove the checks execute, mutated copies of the contract were written to a
temporary directory (the real contract file was never modified) and passed via
`argv[1]`. Every mutation failed with exit status 1:

| Mutation | Observed stderr |
| --- | --- |
| remove baseline name `malloc` (+ its symbol) | `compiler contract alignment FAILED: missing required API names: ['malloc']` |
| remove P0 name `vsnprintf` (+ its symbol) | `compiler contract alignment FAILED: missing P0 contract names: ['vsnprintf']` |
| append `[CPLUS-SAFE-BOGUS] nope` to a diagnostics list | `compiler contract alignment FAILED: apis[2].diagnostics: unknown category '[CPLUS-SAFE-BOGUS] nope'` |
| drop `dynamic` from `apis[0]` | `compiler contract alignment FAILED: apis[0]: key mismatch, missing=['dynamic'], extra=[]` |
| remove aspect `borrowed_args` | `compiler contract alignment FAILED: contract_semantics.aspects missing: ['borrowed_args']` |
| map `malloc` to `not_cplus_symbol` | `compiler contract alignment FAILED: implementation_symbols[malloc] must be a cplus_* symbol, got 'not_cplus_symbol'` |
| map `malloc_s` to `cplus_malloc_s` | `compiler contract alignment FAILED: implementation_symbols[malloc_s] must be 'malloc_s', got 'cplus_malloc_s'` |
| set a `malloc_s` param `nullable` to a string | `compiler contract alignment FAILED: apis[33].params[0].nullable: expected a bool` |
| scrub all `free_s` text markers (structural fields kept valid) | `compiler contract alignment FAILED: api 'free_s': extended entry missing read_write_behavior evidence` |
| `schema_version = 2` | `compiler contract alignment FAILED: schema_version must be 1, got '2'` |
| wrong `source` | `compiler contract alignment FAILED: source must be 'Agent 2 hardened libc'` |
| truncated/invalid JSON | `compiler contract alignment FAILED: invalid JSON in compiler_libc_contracts.json: unexpected character, expected a string key` |
| missing file | `compiler contract alignment FAILED: missing contract file: <path>` |

Index numbers in the messages (`apis[2]`, `apis[33]`, `apis[36]`) depend on the
mutation fixture order and are not part of the pass/fail contract.
