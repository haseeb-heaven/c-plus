# Libc contract enforcement matrix (issue #25)

Reproduce: `python3 tools/verify.sh`, `python3 tests/metrics/measure.py`.
Contract source: `compiler/Analysis/libc_contracts.json` (v2, 29 functions).
The checker consumes contracts via `libcEffect()` in `compiler/Analysis/OwnershipAnalysis.cpp`.

Status keys: **I** implemented, **T** tested (positive+negative diagnostics),
**V** verified by full regression, **P** partial, **U** unsupported.

| function | contract fields enforced | status | tests |
| --- | --- | --- | --- |
| `malloc` | allocation, may-fail, NULL return, extent | I T V | alloc_failure_guard, libccontract_null_path, malloc_valid, len_malloc_constant |
| `calloc` | count*size extent, zero-init, NULL return | I T V | libccontract tests, len_malloc_constant, measure corpus |
| `realloc` | success-only invalidation, failure preserves original | P T V | realloc_success, realloc_success_pattern, realloc_failure, interproc realloc families |
| `free` | frees arg 0, interior/invalid pointers, NULL allowed | I T V | libccontract_free_interior_offset, free_zero_offset, invalid-free families |
| `memcpy`/`memmove` | dst extent, src extent, NULL nullability | I T V | libc bounds matrix, hv_libc corpus (178/178) |
| `memset` | dst extent | I T V | memset_ok, memset_overflow, hv_libc |
| `strcpy`/`strncpy` | dst capacity incl. NUL, literal and local string sources | I T V | strcpy_* fixtures, hv_libc |
| `strcat` | dst capacity for concatenation | I T V | strcat_overflow, hv_libc |
| `strlen`/`strcmp`/`strnlen` | null source, read effect | I T V | null argument tests, fuzz corpora |
| `fopen`/`fclose` | owned FILE*, failure NULL, double/after-close | I T V | file leak/close suites, hw_leak corpus |
| `fread`/`fwrite` | return-value check, buffer access | I T | unchecked I/O fixtures, hw_leak |
| `snprintf` | dst capacity, return value | I T | snprintf_overflow, libc_valid |
| checked `_s` variants | contract present; not all enforced | P | JSON only, no dedicated live fixtures |

## Exact acceptance checks (issue #25)

1. NULL allocation-failure path: `tests/core/positive/libccontract_null_path.c` — no leak warning (verified).
2. Genuine leak: `tests/safety/libccontract_genuine_leak.c` — STRICT error (verified).
3. `free(base+1)` / provable `free(p+offset)`: `tests/safety/libccontract_free_interior_offset.c` — compile error (verified).
4. `realloc` failure preserves original; success invalidates aliases: `realloc_failure`, `realloc_success_alias_uaf` (verified).
5. Every claimed supported contract has positive and negative regression assertions (above).
6. Full results: 109 repo tests, 121 core-safety, 22 mode, 24 breaker, 224 adversarial, 110 red-team, 688 brutal, 0 false positives.

## Boundary

Compiler contracts cover **known functions and supported analysis cases only**.
They do not make all libc, third-party libraries, or arbitrary C memory-safe.
Unknown functions are not assumed safe. Unprovable operations fail closed in
STRICT mode; DEFAULT reports proven errors and warns on unverified operations.
Field-sensitive and multi-level interprocedural ownership remains PARTIAL and is
documented in `SAFE_MODE_SPEC.md` and `BUG_CORPORA.md`.
