# Diagnostic inventory (summary)

Categories emitted (stable prefixes):
`CPLUS-ARITH`, `CPLUS-BOUNDS`, `CPLUS-SAFE-BOUNDS`, `CPLUS-LIFETIME`,
`CPLUS-SAFE-LIFETIME`, `CPLUS-OWNER`, `CPLUS-FFI`, `CPLUS-FREE`,
`CPLUS-REALLOC`, `CPLUS-BORROW`, `CPLUS-RAII`, `CPLUS-ALIAS`, `CPLUS-LIBC`.

Emission sites: `compiler/Sema/StaticBoundsCheck.cpp` (categoryOf at ~line 250)
and `compiler/Sema/AstSafetyCheck.cpp` (diagnosticCategory at ~line 3300).
STRICT promotes proven violations to errors; DEFAULT reports proven errors and
warns on unverified operations. Inconsistency: both `CPLUS-BOUNDS` and
`CPLUS-SAFE-BOUNDS` exist for related bounds errors. Changing these would
break pinned `.err` expectations; documented rather than renamed here.

Exact message strings per diagnostic are asserted by `tests/compile_fail/*.err`
(58 files), `tests/native/cplus_safety_tests.c` (44 cases), and the strict40
runner. Full mechanical inventory was captured by the diagnostics audit agent.
