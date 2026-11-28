# Legacy-C differential probe report

- Reference compiler: `/usr/bin/clang -std=c11`
- Compiler under test: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/c+ --legacy-c --sanitize=none`
- Total probes: 25
- Mismatches: 0

## Summary

| probe | status | reason |
|-------|--------|--------|
| p01_integer_promotion | ok |  |
| p02_unsigned_wrap | ok |  |
| p03_shifts | ok |  |
| p04_struct_padding | ok |  |
| p05_bitfields | ok |  |
| p06_designated_init | ok |  |
| p07_compound_literal | ok |  |
| p08_function_pointers | ok |  |
| p09_variadics | ok |  |
| p10_comma_operator | ok |  |
| p11_short_circuit | ok |  |
| p12_ternary | ok |  |
| p13_typedef | ok |  |
| p14_macros | ok |  |
| p15_enum_cast | ok |  |
| p16_precedence | ok |  |
| p17_sizeof_align | ok |  |
| p18_signed_arith | ok |  |
| p19_signed_bitfield_nested_init | ok |  |
| p20_variadic_promotions | ok |  |
| p21_ternary_types | ok |  |
| p22_generic | ok |  |
| p23_vla | ok |  |
| p24_union_punning | ok |  |
| p25_static_recursion | ok |  |

No mismatches detected.

