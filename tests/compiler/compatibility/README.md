# Native C compatibility suite (LEGACY C mode)

A self-contained harness that compiles and runs each C fixture with the real
`c+` driver in legacy mode and asserts its **exact stdout** and **exact exit
code**.

```
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/compatibility/run_compat.py
```

`CPLUS_BIN` may be absolute or relative to the repository root; when unset it
defaults to `build/c+`. The runner exits 0 when every case passes, 1 otherwise.

## Why only gnu89

`Driver.cpp::backendArgs` selects the backend standard from the compiler mode:

- `Mode::LegacyC` → `-std=gnu89` with `-DCPLUS_LEGACY_C=1`
- `Mode::Safe` → `-std=gnu17` (out of scope for this suite)

Only gnu89 is therefore tested here; no C99/C11/C17-only standard features are
assumed. A few fixtures use GNU extensions that clang's gnu89 mode supports:
designated initializers, compound literals, flexible array members and the
`__restrict` spelling of `restrict` (the bare `restrict` keyword is not a
keyword in gnu89). Every fixture carries a `#error` guard so it must be built
with `--legacy-c`.

## Coverage

20 C feature cases plus one harness-validation case for a non-zero exit code:

| id | feature |
| --- | --- |
| `integer_promotions` | integer promotions |
| `signed_unsigned` | signed/unsigned conversions |
| `pointer_conversions` | pointer conversions |
| `array_pointer_expressions` | array/pointer expressions |
| `function_pointers` | function pointers |
| `variadic_functions` | variadic functions |
| `struct_union_layout` | struct/union layout (`sizeof`/`offsetof`) |
| `bitfields` | bitfields |
| `enums` | enums |
| `designated_initializers` | designated initializers |
| `compound_literals` | compound literals |
| `flexible_array_members` | flexible array members |
| `cv_qualifiers` | const/volatile/restrict |
| `function_declarations` | function declarations (prototypes and K&R) |
| `preprocessor_macros` | preprocessor macros (`#`, `##`) |
| `conditional_compilation` | conditional compilation |
| `typedef` | typedef |
| `nested_declarations` | nested declarations / block shadowing |
| `complex_expressions` | complex expressions |
| `libc_calls` | libc calls (string/stdio/stdlib) |
| `exit_status` | harness validation: exact non-zero exit code |

## Files

- `cases.json` — manifest: fixture, expected stdout, expected exit per case.
- `fixtures/*.c` — self-contained C89/gnu89 fixtures.
- `run_compat.py` — runner; writes the two reports below.
- `summary.txt` / `summary.json` — generated expected-vs-actual report (one
  entry per case; includes stdout, exit, stderr, match flags and totals).

Compiler `stderr` is recorded for diagnosis but is not asserted: gnu89 emits
non-fatal warnings for, e.g., K&R function definitions and bitfield
truncation, which do not change the program's observable stdout or exit code.
