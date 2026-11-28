# str_vec per-method test notes

Scope: **100% of public callables** in `stdlib/string.hp` and
`stdlib/vector.hp`, invoked and verified one by one.

## Enumeration (source of truth)

`sh tests/stdlib/run_coverage.sh` reports:

```
string.hp                24       24  100.0%
vector.hp                23       23  100.0%
```

Those are exactly the entries in `methods.txt` (47 lines). Cross-checked by
re-implementing the coverage tool's `CLASS`/`SIG`/`CTOR` scan and `is_skipped()`
filter in Python against both headers: 24 string entries + 23 vector entries,
matching the file byte-for-byte in order.

`string.hp` has two additional public `static inline` free functions,
`stdlib_utf8_validate` and `stdlib_string_calculate_growth`. The coverage
tool's `is_skipped()` intentionally discards all `stdlib_`-prefixed names, so
they are **not** part of the 24. They are nevertheless called and asserted on
inside the `string::is_valid_utf8` and `string::reserve` cases respectively
(ASCII/overlong/NULL validation; growth values 6 and 9). No public callable is
left uninvoked.

## Deliverables

| File | Purpose |
|------|---------|
| `string_methods.cp` | 24 string methods, deterministic verified cases |
| `vector_methods.cp` | 23 vector methods, deterministic verified cases |
| `methods.txt` | 47-line manifest, one fully-qualified method per line |
| `run.sh` | builds + runs both binaries, prints aggregate final line |
| `RUN_NOTES.md` | this file |

Translation units are split deliberately: the C+ compiler resolves method
calls by *variable name across the whole translation unit* rather than by
lexical scope, so mixing `string.hp` and `vector.hp` objects in one file made a
`vector empty` variable hijack `slice::valid()` (`vector_valid`) and made a
`vector v` hijack `string_view::data()`. Keeping one header per TU removes the
collision; within each TU variable names are unique per type.

## Exact commands and results

Run from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`),
branch `agent/modern-stdlib`:

```sh
./build/c+ build tests/stdlib/method_tests/str_vec/string_methods.cp \
    --strict --sanitize=none -o tests/stdlib/method_tests/str_vec/string_methods
# exit 0, no diagnostics

./build/c+ build tests/stdlib/method_tests/str_vec/vector_methods.cp \
    --strict --sanitize=none -o tests/stdlib/method_tests/str_vec/vector_methods
# exit 0, no diagnostics

timeout 30 tests/stdlib/method_tests/str_vec/string_methods
# exit 0
# METHOD_TEST str_vec 24 24 0 0

timeout 30 tests/stdlib/method_tests/str_vec/vector_methods
# exit 0
# METHOD_TEST str_vec 23 23 0 0

timeout 60 sh tests/stdlib/method_tests/str_vec/run.sh
# exit 0
# METHOD_TEST str_vec 47 47 0 0
```

Per-method stdout (each line `<header> <method> PASS|FAIL`):

```
string.hp string_view::string_view PASS
string.hp string_view::size PASS
string.hp string_view::data PASS
string.hp string_view::equals PASS
string.hp string_view::is_valid_utf8 PASS
string.hp string::string PASS
string.hp string::size PASS
string.hp string::capacity PASS
string.hp string::valid PASS
string.hp string::at PASS
string.hp string::c_str PASS
string.hp string::equals PASS
string.hp string::compare PASS
string.hp string::find PASS
string.hp string::reserve PASS
string.hp string::append PASS
string.hp string::clear PASS
string.hp string::copy_from PASS
string.hp string::is_valid_utf8 PASS
string.hp string::substr PASS
string.hp string::count_fields PASS
string.hp string::field PASS
string.hp string::to_view PASS
string.hp string::drop PASS
vector.hp vector_allocation_bytes PASS
vector.hp vector_calculate_growth PASS
vector.hp slice::slice PASS
vector.hp slice::size PASS
vector.hp slice::stale PASS
vector.hp slice::valid PASS
vector.hp slice::at_checked PASS
vector.hp slice::sub PASS
vector.hp vector::vector PASS
vector.hp vector::size PASS
vector.hp vector::capacity PASS
vector.hp vector::reserve PASS
vector.hp vector::push_back PASS
vector.hp vector::at PASS
vector.hp vector::at_checked PASS
vector.hp vector::pop_back PASS
vector.hp vector::insert PASS
vector.hp vector::erase PASS
vector.hp vector::clear PASS
vector.hp vector::copy_from PASS
vector.hp vector::shrink_to_fit PASS
vector.hp vector::view PASS
vector.hp vector::drop PASS
```

## Determinism and boundaries

* No random input; fixed literals and fixed loop bounds only.
* `STDLIB_ALLOC_LIMIT` pinned to 4096 in both TUs so refusal boundaries are
  reachable cheaply (string capacity 4097 refused; vector element count 1025
  refused; vector growth refused once `space > 512`).
* `stdlib_set_error_callback(NULL)`, `stdlib_alloc_fail_reset()` and
  `stdlib_telemetry_set_enabled(0)` at the start of each `main`.
* Per method, applicable boundaries are exercised: empty string/vector,
  exact-fit capacity (`string("hello")` cap 6; 4 ints cap 4), one-over growth
  (5th push grows 4→8), `at()`/`at_checked()` refusal at and past the end,
  self-append and self-suffix append (both in-place and reallocating paths),
  NULL arguments, over-limit reserve/insert/push refusal with failure
  atomicity, `copy_from` self/NULL/empty, `substr`/`field` exact-fit buffer,
  one-byte-too-small buffer and out-of-range start, stale slice detection
  across reallocation, slice `sub()` clamping, `shrink_to_fit` no-op/to-empty,
  and double `drop()`.
* Additive upstream helpers are covered directly: `vector_allocation_bytes`
  (0, 4→16 bytes, 1024→4096, 1025 refusal), `vector_calculate_growth`
  (demand floor, 2× geometric step, ceiling saturation), plus the two
  `stdlib_`-prefixed string helpers noted above.

## Summary

```
total=47 passed=47 failed=0 skipped=0
```

METHOD_TEST str_vec 47 47 0 0
