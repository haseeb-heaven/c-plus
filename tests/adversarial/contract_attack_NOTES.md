# Contract-validation adversarial findings

Test driver: `python3 tests/libc/test_contract_integrity.py` (52 attacks, 25 findings,
exit 1). All mutations are performed on temporary overlays; the repo is untouched.
The real validator still reports `contract validation: ok (4 files validated)`.

## Validator weaknesses (real findings)

1. **`c17_contracts.json` is never read.** `validate_contracts.py` validates only the
   four names in `REQUIRED_FILES`. Invalid JSON, wrong `schema_version`, empty
   `functions`, bogus classification, missing `name`, duplicate names, missing
   source/test paths, empty `host_backend` and empty `safety_limitations` in that
   file are ALL silently accepted (9 separate silent accepts). This contradicts
   `docs/libc/COVERAGE.md`, which claims the C17 matrix JSON is "validated".
2. **Schema enforcement relies on `assert`.** Under `python3 -O` every
   version/required-key check disappears: wrong version and missing keys are
   silently accepted. Even without `-O`, a wrong version exits with a bare
   `AssertionError` traceback and no useful diagnostic.
3. **Empty collections pass.** `apis: []`, `functions: []`, `functions: {}`,
   `mappings: []` are all accepted — an attacker can empty the entire contract set.
4. **Symbol declaration check is a raw regex over the whole header.** It accepts
   anything matching `\b<sym>\s*\(`, including
   - macro-only `cplus_alloc_len` (a macro, no compiled symbol),
   - host macro `malloc` (from `#define malloc(sz) cplus_malloc(sz)`),
   - a fake symbol planted in a comment (`/* cplus_ghost_fn( */`),
   - an empty string (`""`) — the empty regex matches any `(` in the header.
   Near-miss `cplus_mallocX`, trailing-space `cplus_malloc ` and unknown
   `cplus_no_such_wrapper` are correctly rejected.
5. **`safe_symbol` is never validated.** A forged `cplus_fake_safe_symbol` is accepted.
6. **Wrong types crash instead of failing cleanly.** `bundled_symbol: 123` exits
   nonzero via an unhandled `TypeError: decoding to str...` traceback.

## Cross-checks (scenarios 3-6): all clean

- 202 c17 entries, unique names, all `PLATFORM_BACKED`, non-empty `host_backend`
  and `safety_limitations`; every `source`/`test` path exists on disk.
- All 202 matrix symbols have a compiled `T` symbol in `build/libc/libcplus_hardened.a`;
  all exported `cplus_*` definitions in the 11 new source files are in the matrix.
- All 202 symbols have a header declaration with matching normalized return type
  and parameter types/arity.
- `docs/libc/API_TEST_MATRIX.md` has exactly 202 `| cplus_* |` rows.

Note: `cplus_libc_log` is compiled but not declared in the public header, and
`cplus_alloc_len` is declared only as a macro; neither is in the 202-entry matrix.
