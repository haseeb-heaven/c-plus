# C+ language compatibility matrix

Status is based on the branch evidence and differential suites, not on a claim
of full C conformance. "Native" means C+ lowers or checks the feature itself;
"Clang" means the host backend implements it.

| Feature | Implementation | Differential coverage | Notes |
| --- | --- | --- | --- |
| Integer promotions / signed vs unsigned conversions | Clang backend | `compatibility/`, `differential/` | C+ static checks add rejections in Safe mode |
| Pointer conversions / casts | Rewriter + AST checker | `compatibility/`, `differential/` | `void*` cast aliases tracked for release state |
| Arrays and pointer expressions | `StaticBoundsCheck`, `AstSafetyCheck`, Clang | `differential/`, `run_bounds_matrix.sh` | Safe mode adds bounds checks; LEGACY matches C |
| Function pointers / callbacks | Clang backend | `compatibility/`, `differential/` | table bounds checked in safe subset |
| Variadic functions | Clang backend | `compatibility/` | format-string checks in Safe mode |
| Struct / union layout, bitfields, enums | Clang backend | `compatibility/`, `abi_safety/` | `sizeof`/`offsetof` verified across modes |
| Designated initializers / compound literals | Clang backend | `compatibility/`, `differential/` | no C+ rewrite |
| Flexible array members | Clang backend + AST bounds | `compatibility/` | prefix-aware bounds check in Safe mode |
| `const` / `volatile` / `restrict` | Clang backend | `compatibility/` | C+ checks restrict overlap in Safe mode |
| Function declarations / typedef / nested declarations | Rewriter + Clang | `compatibility/` | plain C unchanged |
| Preprocessor macros / conditional compilation | Clang preprocessor + `.hp` rewriter | `compatibility/` | `.hp` includes are a C+ extension, excluded from differential |
| libc calls | Clang + JSON contracts | `compatibility/`, `differential/` | unknown contracts fail closed in STRICT |
| Default arguments, `.len`, `countof`, `managed`/`owned`/`resource`/`class`, `auto`, range-for, `print`/`scan` | C+ lowering + runtime | excluded from differential | intentional semantic differences, not legacy C |

## Verified results (this branch)

- `tests/compiler/compatibility/run_compat.py`: 21/21 cases passed in LEGACY
  mode (exact stdout and exact exit code).
- `tools/compiler_differential/run_differential.py`: 125 comparisons against
  Apple clang 16, Homebrew clang 23.1.6 and GCC 14.2, all matching.
- `tests/compiler/optimizer/run_suite.py`: 11 cases × 6 configurations; all
  stdout/exit identical; 174 native-guard assertions passed; LEGACY emits no
  guards while Safe configurations do.
- `tests/compiler/fuzz/fuzz.py`: 200 deterministic cases, zero mismatches.

## C standards claimed

The driver emits `-std=gnu89` in LEGACY mode and `-std=gnu17` in Safe mode
(`compiler/Driver/Driver.cpp:735`). C99/C11/C17 features accepted by the host
Clang are therefore available through the backend, but C+ does not claim a
separate C23 implementation. Differential fixtures pin `-std=gnu89` for LEGACY
comparisons; strict ISO `-pedantic-errors` is not supported because generated C
uses GNU extensions (statement expressions, `__typeof__`, `_Static_assert`).

## Intentional differences from Clang/GCC

- Safe mode rejects valid C for documented safety rules (raw `realloc`,
  overlapping `memcpy`, plain-char `ctype`, signal-handler `printf`,
  setjmp/longjmp over live resources).
- Safe mode injects native guards and runtime side-table registration; LEGACY
  mode does not. Only LEGACY is used as a behavioural differential oracle for
  unchanged C.
- C+ extensions listed above are not valid input to Clang/GCC and are excluded.

See `docs/compiler/differential-testing.md` for the harness contract and
exclusions.
