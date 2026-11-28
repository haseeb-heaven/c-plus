# Legacy C ABI corpus -- Part 1 notes (lc01..lc25)

This directory holds a 50-program legacy C99 ABI corpus split across two
agents. This file covers **Part 1: lc01 through lc25** only. Each program is
an independent, self-contained C99 translation unit with its own `main`,
exits 0 only when all of its internal checks pass, and prints exactly one
stable result line beginning with its source-file stem followed by `: OK`.

No C++ headers, no project headers, and no compiler/runtime/stdlib changes
are involved. The only compiler extension used is `__attribute__((packed))`
in `lc15` (needed to express a packed record); it is a reserved-identifier
extension and produces no diagnostics under the flags below.

## Environment (captured from this host)

Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0
Thread model: posix

## Coverage map

| Program | C ABI feature exercised |
| --- | --- |
| lc01_primitives.c | primitive scalar types, sizes, ranges, arithmetic |
| lc02_pointer_basics.c | pointer indirection, aliasing, pointer-based swap |
| lc03_pointer_to_pointer.c | pointer-to-pointer, rebinding, `char **` walk |
| lc04_arrays.c | 1-D arrays, array parameters decaying to pointers |
| lc05_multidim_arrays.c | 2-D arrays, fixed-column array parameters |
| lc06_struct_basics.c | structs, aggregate init, by-value and by-pointer |
| lc07_unions.c | unions, shared storage, tagged-union style record |
| lc08_nested_structs.c | nested structs, `offsetof`, member chains |
| lc09_bitfields.c | bitfields, widths, overflow truncation |
| lc10_enums.c | enums, explicit/implicit values, switch, int conversion |
| lc11_function_pointers.c | function-pointer table and dispatch |
| lc12_variadic.c | variadic functions, `va_start`/`va_arg`/`va_end` |
| lc13_callbacks.c | callbacks, `qsort` comparator, visitor callback |
| lc14_flexible_array.c | C99 flexible array member, single-block allocation |
| lc15_packed_struct.c | packed structure, compact layout, field round trip |
| lc16_alignment.c | alignment-sensitive struct, padding, `memcpy` round trip |
| lc17_array_of_structs.c | arrays of structs, struct-keyed sort, stride |
| lc18_string_struct.c | inline char arrays in structs, struct copy |
| lc19_pointer_arithmetic.c | pointer offset/difference, array and string decay |
| lc20_struct_return.c | aggregates returned by value through call chains |
| lc21_union_bitfield.c | union overlaid on a bitfield record |
| lc22_typedef_fnptr.c | typedef'd function pointers, predicate registry |
| lc23_variadic_va_list.c | `va_list` forwarding through variadic wrappers |
| lc24_nested_flexible.c | flexible arrays reached through a pointer-to-pointer table |
| lc25_abi_mixed.c | mixed enum + fnptr + bitfield + flexible-array command record |

## Verification method

For every source file `f` in `lc01..lc25`, run from this directory:

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 "$f" -o "$BIN_O0"
/usr/bin/timeout 10 "$BIN_O0"
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 "$f" -o "$BIN_O2"
/usr/bin/timeout 10 "$BIN_O2"
```

`BIN_O0`/`BIN_O2` were per-file binaries under a scratch temp directory
outside the repository. Compile diagnostics were captured and checked to be
empty; run exit status and stdout were captured and checked. The full raw
transcript with the exact commands and outputs follows the summary table.

## Results summary

All 25 files x 2 optimization levels = 50 compile-and-run checks passed.
Every compile exited 0 with no diagnostics; every run exited 0 under the
10-second timeout; every stdout line starts with `OK`; the `-O0` and `-O2`
stdout lines are byte-identical for each program.

| File | Opt | Compile RC | Diagnostics | Run RC | stdout |
| --- | --- | --- | --- | --- | --- |
| lc01_primitives.c | -O0 | 0 | clean | 0 | lc01_primitives: OK sizes=1/2/4/8/8 calc=122222 |
| lc01_primitives.c | -O2 | 0 | clean | 0 | lc01_primitives: OK sizes=1/2/4/8/8 calc=122222 |
| lc02_pointer_basics.c | -O0 | 0 | clean | 0 | lc02_pointer_basics: OK x=32 y=11 p=32 |
| lc02_pointer_basics.c | -O2 | 0 | clean | 0 | lc02_pointer_basics: OK x=32 y=11 p=32 |
| lc03_pointer_to_pointer.c | -O0 | 0 | clean | 0 | lc03_pointer_to_pointer: OK a=3 b=12 tail=b |
| lc03_pointer_to_pointer.c | -O2 | 0 | clean | 0 | lc03_pointer_to_pointer: OK a=3 b=12 tail=b |
| lc04_arrays.c | -O0 | 0 | clean | 0 | lc04_arrays: OK n=5 sum=45 first=3 last=15 |
| lc04_arrays.c | -O2 | 0 | clean | 0 | lc04_arrays: OK n=5 sum=45 first=3 last=15 |
| lc05_multidim_arrays.c | -O0 | 0 | clean | 0 | lc05_multidim_arrays: OK total=21 dims=2x3 corner=6 |
| lc05_multidim_arrays.c | -O2 | 0 | clean | 0 | lc05_multidim_arrays: OK total=21 dims=2x3 corner=6 |
| lc06_struct_basics.c | -O0 | 0 | clean | 0 | lc06_struct_basics: OK a=3,4 b=13,24,3.0 size=16 |
| lc06_struct_basics.c | -O2 | 0 | clean | 0 | lc06_struct_basics: OK a=3,4 b=13,24,3.0 size=16 |
| lc07_unions.c | -O0 | 0 | clean | 0 | lc07_unions: OK i=42 d=2.5 tag=7 sz=8 |
| lc07_unions.c | -O2 | 0 | clean | 0 | lc07_unions: OK i=42 d=2.5 tag=7 sz=8 |
| lc08_nested_structs.c | -O0 | 0 | clean | 0 | lc08_nested_structs: OK size=24 off_in=4 off_d=16 val=7z |
| lc08_nested_structs.c | -O2 | 0 | clean | 0 | lc08_nested_structs: OK size=24 off_in=4 off_d=16 val=7z |
| lc09_bitfields.c | -O0 | 0 | clean | 0 | lc09_bitfields: OK size=4 v=1,1,10,200 |
| lc09_bitfields.c | -O2 | 0 | clean | 0 | lc09_bitfields: OK size=4 v=1,1,10,200 |
| lc10_enums.c | -O0 | 0 | clean | 0 | lc10_enums: OK values=1,2,5,6 name=GREEN |
| lc10_enums.c | -O2 | 0 | clean | 0 | lc10_enums: OK values=1,2,5,6 name=GREEN |
| lc11_function_pointers.c | -O0 | 0 | clean | 0 | lc11_function_pointers: OK r=13,5,36 fpsize=8 |
| lc11_function_pointers.c | -O2 | 0 | clean | 0 | lc11_function_pointers: OK r=13,5,36 fpsize=8 |
| lc12_variadic.c | -O0 | 0 | clean | 0 | lc12_variadic: OK sum=15 max=22 |
| lc12_variadic.c | -O2 | 0 | clean | 0 | lc12_variadic: OK sum=15 max=22 |
| lc13_callbacks.c | -O0 | 0 | clean | 0 | lc13_callbacks: OK sorted=2,4,6,8,10 |
| lc13_callbacks.c | -O2 | 0 | clean | 0 | lc13_callbacks: OK sorted=2,4,6,8,10 |
| lc14_flexible_array.c | -O0 | 0 | clean | 0 | lc14_flexible_array: OK len=4 bytes=ABCD hdr=4 |
| lc14_flexible_array.c | -O2 | 0 | clean | 0 | lc14_flexible_array: OK len=4 bytes=ABCD hdr=4 |
| lc15_packed_struct.c | -O0 | 0 | clean | 0 | lc15_packed_struct: OK size=7 a=1 b=01020304 c=0506 |
| lc15_packed_struct.c | -O2 | 0 | clean | 0 | lc15_packed_struct: OK size=7 a=1 b=01020304 c=0506 |
| lc16_alignment.c | -O0 | 0 | clean | 0 | lc16_alignment: OK size=24 off=0,8,16 vals=q,3.5,77 |
| lc16_alignment.c | -O2 | 0 | clean | 0 | lc16_alignment: OK size=24 off=0,8,16 vals=q,3.5,77 |
| lc17_array_of_structs.c | -O0 | 0 | clean | 0 | lc17_array_of_structs: OK keys=10,20,30,40 stride=8 |
| lc17_array_of_structs.c | -O2 | 0 | clean | 0 | lc17_array_of_structs: OK keys=10,20,30,40 stride=8 |
| lc18_string_struct.c | -O0 | 0 | clean | 0 | lc18_string_struct: OK p=Ada/36 q=Grace/45 cap=16 |
| lc18_string_struct.c | -O2 | 0 | clean | 0 | lc18_string_struct: OK p=Ada/36 q=Grace/45 cap=16 |
| lc19_pointer_arithmetic.c | -O0 | 0 | clean | 0 | lc19_pointer_arithmetic: OK third=30 diff=5 slen=5 |
| lc19_pointer_arithmetic.c | -O2 | 0 | clean | 0 | lc19_pointer_arithmetic: OK third=30 diff=5 slen=5 |
| lc20_struct_return.c | -O0 | 0 | clean | 0 | lc20_struct_return: OK in=100,23 out=123,46 size=16 |
| lc20_struct_return.c | -O2 | 0 | clean | 0 | lc20_struct_return: OK in=100,23 out=123,46 size=16 |
| lc21_union_bitfield.c | -O0 | 0 | clean | 0 | lc21_union_bitfield: OK low=10 high=3 flag=1 raw=314 size=4 |
| lc21_union_bitfield.c | -O2 | 0 | clean | 0 | lc21_union_bitfield: OK low=10 high=3 flag=1 raw=314 size=4 |
| lc22_typedef_fnptr.c | -O0 | 0 | clean | 0 | lc22_typedef_fnptr: OK even=3/4 pos=5/15 |
| lc22_typedef_fnptr.c | -O2 | 0 | clean | 0 | lc22_typedef_fnptr: OK even=3/4 pos=5/15 |
| lc23_variadic_va_list.c | -O0 | 0 | clean | 0 | lc23_variadic_va_list: OK sum=20 avg=5.0 |
| lc23_variadic_va_list.c | -O2 | 0 | clean | 0 | lc23_variadic_va_list: OK sum=20 avg=5.0 |
| lc24_nested_flexible.c | -O0 | 0 | clean | 0 | lc24_nested_flexible: OK ids=1,2 labels=aa,bbbb hdr=4 |
| lc24_nested_flexible.c | -O2 | 0 | clean | 0 | lc24_nested_flexible: OK ids=1,2 labels=aa,bbbb hdr=4 |
| lc25_abi_mixed.c | -O0 | 0 | clean | 0 | lc25_abi_mixed: OK neg=-6 dbl=12 tag=5 sel=1 size=16 |
| lc25_abi_mixed.c | -O2 | 0 | clean | 0 | lc25_abi_mixed: OK neg=-6 dbl=12 tag=5 sel=1 size=16 |

## Raw verification transcript

The following is the unedited capture produced by the verification loop,
including the exact command lines, compile return codes and diagnostics, run
return codes, and stdout for each of the 50 checks.

```
=== lc01_primitives.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc01_primitives.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc01_primitives.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc01_primitives.O0
RUN_RC: 0
STDOUT: lc01_primitives: OK sizes=1/2/4/8/8 calc=122222
=== lc01_primitives.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc01_primitives.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc01_primitives.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc01_primitives.O2
RUN_RC: 0
STDOUT: lc01_primitives: OK sizes=1/2/4/8/8 calc=122222
=== lc02_pointer_basics.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc02_pointer_basics.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc02_pointer_basics.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc02_pointer_basics.O0
RUN_RC: 0
STDOUT: lc02_pointer_basics: OK x=32 y=11 p=32
=== lc02_pointer_basics.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc02_pointer_basics.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc02_pointer_basics.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc02_pointer_basics.O2
RUN_RC: 0
STDOUT: lc02_pointer_basics: OK x=32 y=11 p=32
=== lc03_pointer_to_pointer.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc03_pointer_to_pointer.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc03_pointer_to_pointer.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc03_pointer_to_pointer.O0
RUN_RC: 0
STDOUT: lc03_pointer_to_pointer: OK a=3 b=12 tail=b
=== lc03_pointer_to_pointer.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc03_pointer_to_pointer.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc03_pointer_to_pointer.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc03_pointer_to_pointer.O2
RUN_RC: 0
STDOUT: lc03_pointer_to_pointer: OK a=3 b=12 tail=b
=== lc04_arrays.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc04_arrays.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc04_arrays.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc04_arrays.O0
RUN_RC: 0
STDOUT: lc04_arrays: OK n=5 sum=45 first=3 last=15
=== lc04_arrays.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc04_arrays.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc04_arrays.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc04_arrays.O2
RUN_RC: 0
STDOUT: lc04_arrays: OK n=5 sum=45 first=3 last=15
=== lc05_multidim_arrays.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc05_multidim_arrays.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc05_multidim_arrays.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc05_multidim_arrays.O0
RUN_RC: 0
STDOUT: lc05_multidim_arrays: OK total=21 dims=2x3 corner=6
=== lc05_multidim_arrays.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc05_multidim_arrays.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc05_multidim_arrays.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc05_multidim_arrays.O2
RUN_RC: 0
STDOUT: lc05_multidim_arrays: OK total=21 dims=2x3 corner=6
=== lc06_struct_basics.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc06_struct_basics.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc06_struct_basics.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc06_struct_basics.O0
RUN_RC: 0
STDOUT: lc06_struct_basics: OK a=3,4 b=13,24,3.0 size=16
=== lc06_struct_basics.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc06_struct_basics.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc06_struct_basics.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc06_struct_basics.O2
RUN_RC: 0
STDOUT: lc06_struct_basics: OK a=3,4 b=13,24,3.0 size=16
=== lc07_unions.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc07_unions.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc07_unions.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc07_unions.O0
RUN_RC: 0
STDOUT: lc07_unions: OK i=42 d=2.5 tag=7 sz=8
=== lc07_unions.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc07_unions.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc07_unions.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc07_unions.O2
RUN_RC: 0
STDOUT: lc07_unions: OK i=42 d=2.5 tag=7 sz=8
=== lc08_nested_structs.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc08_nested_structs.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc08_nested_structs.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc08_nested_structs.O0
RUN_RC: 0
STDOUT: lc08_nested_structs: OK size=24 off_in=4 off_d=16 val=7z
=== lc08_nested_structs.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc08_nested_structs.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc08_nested_structs.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc08_nested_structs.O2
RUN_RC: 0
STDOUT: lc08_nested_structs: OK size=24 off_in=4 off_d=16 val=7z
=== lc09_bitfields.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc09_bitfields.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc09_bitfields.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc09_bitfields.O0
RUN_RC: 0
STDOUT: lc09_bitfields: OK size=4 v=1,1,10,200
=== lc09_bitfields.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc09_bitfields.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc09_bitfields.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc09_bitfields.O2
RUN_RC: 0
STDOUT: lc09_bitfields: OK size=4 v=1,1,10,200
=== lc10_enums.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc10_enums.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc10_enums.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc10_enums.O0
RUN_RC: 0
STDOUT: lc10_enums: OK values=1,2,5,6 name=GREEN
=== lc10_enums.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc10_enums.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc10_enums.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc10_enums.O2
RUN_RC: 0
STDOUT: lc10_enums: OK values=1,2,5,6 name=GREEN
=== lc11_function_pointers.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc11_function_pointers.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc11_function_pointers.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc11_function_pointers.O0
RUN_RC: 0
STDOUT: lc11_function_pointers: OK r=13,5,36 fpsize=8
=== lc11_function_pointers.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc11_function_pointers.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc11_function_pointers.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc11_function_pointers.O2
RUN_RC: 0
STDOUT: lc11_function_pointers: OK r=13,5,36 fpsize=8
=== lc12_variadic.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc12_variadic.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc12_variadic.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc12_variadic.O0
RUN_RC: 0
STDOUT: lc12_variadic: OK sum=15 max=22
=== lc12_variadic.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc12_variadic.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc12_variadic.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc12_variadic.O2
RUN_RC: 0
STDOUT: lc12_variadic: OK sum=15 max=22
=== lc13_callbacks.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc13_callbacks.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc13_callbacks.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc13_callbacks.O0
RUN_RC: 0
STDOUT: lc13_callbacks: OK sorted=2,4,6,8,10
=== lc13_callbacks.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc13_callbacks.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc13_callbacks.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc13_callbacks.O2
RUN_RC: 0
STDOUT: lc13_callbacks: OK sorted=2,4,6,8,10
=== lc14_flexible_array.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc14_flexible_array.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc14_flexible_array.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc14_flexible_array.O0
RUN_RC: 0
STDOUT: lc14_flexible_array: OK len=4 bytes=ABCD hdr=4
=== lc14_flexible_array.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc14_flexible_array.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc14_flexible_array.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc14_flexible_array.O2
RUN_RC: 0
STDOUT: lc14_flexible_array: OK len=4 bytes=ABCD hdr=4
=== lc15_packed_struct.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc15_packed_struct.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc15_packed_struct.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc15_packed_struct.O0
RUN_RC: 0
STDOUT: lc15_packed_struct: OK size=7 a=1 b=01020304 c=0506
=== lc15_packed_struct.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc15_packed_struct.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc15_packed_struct.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc15_packed_struct.O2
RUN_RC: 0
STDOUT: lc15_packed_struct: OK size=7 a=1 b=01020304 c=0506
=== lc16_alignment.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc16_alignment.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc16_alignment.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc16_alignment.O0
RUN_RC: 0
STDOUT: lc16_alignment: OK size=24 off=0,8,16 vals=q,3.5,77
=== lc16_alignment.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc16_alignment.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc16_alignment.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc16_alignment.O2
RUN_RC: 0
STDOUT: lc16_alignment: OK size=24 off=0,8,16 vals=q,3.5,77
=== lc17_array_of_structs.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc17_array_of_structs.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc17_array_of_structs.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc17_array_of_structs.O0
RUN_RC: 0
STDOUT: lc17_array_of_structs: OK keys=10,20,30,40 stride=8
=== lc17_array_of_structs.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc17_array_of_structs.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc17_array_of_structs.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc17_array_of_structs.O2
RUN_RC: 0
STDOUT: lc17_array_of_structs: OK keys=10,20,30,40 stride=8
=== lc18_string_struct.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc18_string_struct.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc18_string_struct.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc18_string_struct.O0
RUN_RC: 0
STDOUT: lc18_string_struct: OK p=Ada/36 q=Grace/45 cap=16
=== lc18_string_struct.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc18_string_struct.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc18_string_struct.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc18_string_struct.O2
RUN_RC: 0
STDOUT: lc18_string_struct: OK p=Ada/36 q=Grace/45 cap=16
=== lc19_pointer_arithmetic.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc19_pointer_arithmetic.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc19_pointer_arithmetic.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc19_pointer_arithmetic.O0
RUN_RC: 0
STDOUT: lc19_pointer_arithmetic: OK third=30 diff=5 slen=5
=== lc19_pointer_arithmetic.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc19_pointer_arithmetic.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc19_pointer_arithmetic.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc19_pointer_arithmetic.O2
RUN_RC: 0
STDOUT: lc19_pointer_arithmetic: OK third=30 diff=5 slen=5
=== lc20_struct_return.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc20_struct_return.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc20_struct_return.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc20_struct_return.O0
RUN_RC: 0
STDOUT: lc20_struct_return: OK in=100,23 out=123,46 size=16
=== lc20_struct_return.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc20_struct_return.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc20_struct_return.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc20_struct_return.O2
RUN_RC: 0
STDOUT: lc20_struct_return: OK in=100,23 out=123,46 size=16
=== lc21_union_bitfield.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc21_union_bitfield.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc21_union_bitfield.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc21_union_bitfield.O0
RUN_RC: 0
STDOUT: lc21_union_bitfield: OK low=10 high=3 flag=1 raw=314 size=4
=== lc21_union_bitfield.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc21_union_bitfield.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc21_union_bitfield.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc21_union_bitfield.O2
RUN_RC: 0
STDOUT: lc21_union_bitfield: OK low=10 high=3 flag=1 raw=314 size=4
=== lc22_typedef_fnptr.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc22_typedef_fnptr.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc22_typedef_fnptr.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc22_typedef_fnptr.O0
RUN_RC: 0
STDOUT: lc22_typedef_fnptr: OK even=3/4 pos=5/15
=== lc22_typedef_fnptr.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc22_typedef_fnptr.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc22_typedef_fnptr.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc22_typedef_fnptr.O2
RUN_RC: 0
STDOUT: lc22_typedef_fnptr: OK even=3/4 pos=5/15
=== lc23_variadic_va_list.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc23_variadic_va_list.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc23_variadic_va_list.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc23_variadic_va_list.O0
RUN_RC: 0
STDOUT: lc23_variadic_va_list: OK sum=20 avg=5.0
=== lc23_variadic_va_list.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc23_variadic_va_list.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc23_variadic_va_list.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc23_variadic_va_list.O2
RUN_RC: 0
STDOUT: lc23_variadic_va_list: OK sum=20 avg=5.0
=== lc24_nested_flexible.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc24_nested_flexible.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc24_nested_flexible.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc24_nested_flexible.O0
RUN_RC: 0
STDOUT: lc24_nested_flexible: OK ids=1,2 labels=aa,bbbb hdr=4
=== lc24_nested_flexible.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc24_nested_flexible.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc24_nested_flexible.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc24_nested_flexible.O2
RUN_RC: 0
STDOUT: lc24_nested_flexible: OK ids=1,2 labels=aa,bbbb hdr=4
=== lc25_abi_mixed.c -O0 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O0 lc25_abi_mixed.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc25_abi_mixed.O0
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc25_abi_mixed.O0
RUN_RC: 0
STDOUT: lc25_abi_mixed: OK neg=-6 dbl=12 tag=5 sel=1 size=16
=== lc25_abi_mixed.c -O2 ===
CMD: /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -O2 lc25_abi_mixed.c -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc25_abi_mixed.O2
COMPILE_RC: 0
DIAGNOSTICS: (none)
CMD: /usr/bin/timeout 10 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcverify/lc25_abi_mixed.O2
RUN_RC: 0
STDOUT: lc25_abi_mixed: OK neg=-6 dbl=12 tag=5 sel=1 size=16
```
