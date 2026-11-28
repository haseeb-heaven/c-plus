# stdc_abi — proof that the safety layer does not change the standard C runtime ABI

Branch: `agent/modern-stdlib`
Host: macOS arm64, Apple clang 16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`,
`/usr/bin/nm` (llvm-nm, GNU-compatible), `/bin/bash` 3.2.57 and `/opt/homebrew/bin/bash` 5.3.15.
All commands from the repository root. Every compile/link/run in the additional proof is
wrapped in `timeout 60`.

---

## 1. Mandated build runs (exact final lines and exit codes)

### Run 1a — `sh tools/abi_compare/run.sh capture`
```
abi_fp: wrote tools/abi_compare/.out/fingerprint.txt (1235 records) and fingerprint.json
abi_fp: capture complete. baseline candidates:
  tools/abi_compare/.out/fingerprint.txt
  tools/abi_compare/.out/fingerprint.json
```
`EXIT_CODE_CAPTURE=0`

### Run 1b — `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`
```
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
```
`EXIT_CODE_COMPARE=0`

### Run 2 — `sh tests/abi/run_abi_compat.sh`
```
abi_compat: 69 passed, 0 failed
```
`EXIT_CODE_RUN_ABI_COMPAT=0`
(All 69 steps PASS, including `T5_no_cxx_mangling:{nm_libc,nm_safecrt,nm_compat}.txt`
and `T6_neutral_plain_run` / `T6_neutral_cplus_run` / `T6_neutral_diff`.)

### Run 3 — `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh`
```
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```
`EXIT_CODE_RUN_ABI_LAYOUTS=0`
(The two warnings are from `-c` compiles that still list the archives; they do not affect the result.)

### Run 4a — `bash tests/abi/run_legacy_labs.sh` (system bash 3.2)
```
run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0  summary=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/summary.json  logs=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/logs
```
`EXIT_CODE_LEGACY_LABS_BASH3=0`
System bash 3.2 succeeded, so the conditional escalation was not required. I nevertheless
re-ran with bash 5.3 as an extra portability check:

### Run 4b — `/opt/homebrew/bin/bash tests/abi/run_legacy_labs.sh` (extra, bash 5.3.15)
```
run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0  summary=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/summary.json  logs=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/logs
```
`EXIT_CODE_LEGACY_LABS_BASH5=0`

---

## 2. Additional proof — ONE ordinary C source, three compilations

Driver: `sh tests/abi/stdc_abi/run.sh` → `EXIT_CODE_STDC_ABI=0`,
`stdc_abi: 0 failed`, `stdc_abi: RESULT PASS -- ordinary C ABI unchanged by the safety layer`
(22 `PASS`, 0 `FAIL`). Full log: `tests/abi/stdc_abi/out/proof.log`.

The single source is `tests/abi/stdc_abi/original_c.c`. It defines five ordinary external C
functions/data (`abi_add`, `abi_sum`, `abi_fill`, `abi_roundtrip`, `abi_glob`) plus `main`, and
carries `_Static_assert`s for pointer size, struct size/alignment/offsets and exact function
signature types.

### 2.1 Exact compile commands (from the run)

```
CMD  /usr/bin/clang -fno-builtin -O0 -std=c11 -Wall tests/abi/stdc_abi/original_c.c -lm -pthread -o .../out/plain
CMD  /usr/bin/clang -fno-builtin -O0 -std=c11 -Wall -I. -Ilibc/install-include -Ilibc/include -Ilibc -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include -include stdlib/safe/safe_c.h tests/abi/stdc_abi/original_c.c build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a -lm -pthread -o .../out/safe
CMD  /usr/bin/clang -fno-builtin -O0 -std=c11 -Wall -I. -Ilibc/install-include -Ilibc/include -Ilibc -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE tests/abi/stdc_abi/original_c.c build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a -lm -pthread -o .../out/disabled
```
(`.../` abbreviates `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/abi/stdc_abi/out/`.)
All three compiled and linked: `PASS build_plain`, `PASS build_safe`, `PASS build_disabled`.

### 2.2 `nm -gU` — exported ordinary C names are identical (no mangling)

Raw `nm -gU .../out/plain` (Mach-O prints a leading `_`):
```
0000000100000000 T __mh_execute_header
0000000100003b78 T _abi_add
0000000100003c00 T _abi_fill
0000000100008000 D _abi_glob
0000000100003c4c T _abi_roundtrip
0000000100003b98 T _abi_sum
0000000100003d20 T _main
```
Raw `nm -gU .../out/disabled` is **byte-identical** (same names *and* same addresses —
`diff <(nm -gU plain) <(nm -gU disabled)` produced no output).

After stripping the Mach-O underscore, the ordinary C symbol set is identical in all three
builds and equals `EXPECTED_SYMBOLS.txt`:
```
[plain]    abi_add abi_fill abi_glob abi_roundtrip abi_sum main
[safe]     abi_add abi_fill abi_glob abi_roundtrip abi_sum main
[disabled] abi_add abi_fill abi_glob abi_roundtrip abi_sum main
```
Checks: `PASS T1_ordinary_symbols:{plain,safe,disabled}`,
`PASS T2_no_cxx_mangling:{plain,safe,disabled}` (no `_Z`/`__Z` symbol in any build).
Defined-global counts: plain 7, disabled 7, safe 138 (of which still only 6 are the ordinary C API).

### 2.3 Signatures, `sizeof(void*)` and `abi_sample` layout

Compile-time, enforced in **all three** builds by `_Static_assert` in `original_c.c`:
* `sizeof(void *) == 8`, `sizeof(size_t) == 8`
* `sizeof(abi_sample) == 40`, `_Alignof(abi_sample) == 8`
* offsets: `tag=0, count=4, value=8, ptr=16, name=24, big=32`
* exact types: `abi_add : int(int,int)`, `abi_sum : size_t(const int*,size_t)`,
  `abi_fill : void(abi_sample*,int,double,void*)`, `abi_roundtrip : int(const char*,size_t)`,
  `abi_glob : int` (via `__builtin_types_compatible_p(__typeof__(&f), <type>)`).

Runtime stdout is identical for all three executables:
```
sizeof(void*)=8
sizeof(abi_sample)=40
offsets tag=0 count=4 value=8 ptr=16 name=24 big=32
abi_add=5 abi_sum=6 abi_glob=7
abi_roundtrip=0
```
Checks: `PASS T5_run:{plain,safe,disabled}`, `PASS T6_runtime_layout_identical:{safe,disabled}`,
`PASS T7_pointer_is_8_bytes`, `PASS T8_abi_sample_offsets`.

Byte-for-byte machine code (AAPCS64 calling convention) for the functions whose bodies contain
no macro-remapped library call, extracted with `objdump -d --disassemble-symbols=...` and
compared as opcode hex bytes:
```
_abi_add:  d10043ff b9000fe0 b9000be1 b9400fe8 b9400be9 0b090100 910043ff d65f03c0
_abi_sum:  d10083ff f9000fe0 f9000be1 f90007ff f90003ff 14000001 f94003e8 ... d65f03c0
_abi_fill: d10083ff f9000fe0 b90017e1 fd0007e0 f90003e2 b94017e8 ... d65f03c0
```
Identical in plain, safe and disabled builds:
`PASS T9_machine_code_byte_identical:{_abi_add,_abi_sum,_abi_fill}`.

### 2.4 The SAFE build adds only internal helper symbols

`nm -gU` set difference (safe minus plain) = 131 symbols; plain minus safe = 0 symbols
(`PASS T3_no_ordinary_symbol_removed`). Every one of those 131 symbols is defined in one of the
two shipped archives (`nm -gU build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a`
defines 1101 global symbols; `comm -13` of archive-set vs added-set is empty):
```
PASS T4_added_symbols_are_internal_helpers
PASS T4b_no_ordinary_symbol_added
```
Examples of the added (archive-defined) helpers: `malloc_s`, `calloc_s`, `free_s`, `realloc_s`,
`cplus_memset_s_auto`, `cplus_memcpy_s_auto`, `cplus_memcmp_s_auto`, `cplus_printf`,
`cplus_sc_memcpy_s`, `handle_mem_bos_chk_warn`, `mem_prim_set`, `__ubsan_on_report`.
None of them is a renamed or new `abi_*`/`main` symbol.

---

## 3. Conclusion

On the tested configuration (macOS arm64, Apple clang 16.0.0, `-fno-builtin -O0`, the two
checked-in archives), the ordinary C ABI is **unchanged** by the safety layer:

* **Symbol names:** the exported ordinary C function/data names are byte-for-byte identical
  across plain, safe and disabled builds; there is no C++ mangling.
* **Signatures:** compile-time `__builtin_types_compatible_p` asserts pass in all three builds,
  and the calling convention is evidenced by byte-identical instruction encodings for the
  unaffected functions.
* **Struct layouts:** `sizeof(void*)=8`, `sizeof(abi_sample)=40`, and every field offset are
  asserted at compile time and reproduced identically at runtime in all three builds.
* **Added symbols:** the safe executable contains 131 extra defined globals, all of which are
  internal implementation helpers already present in `libcplus_hardened.a` /
  `libcplus_safecrt.a`; no ordinary symbol is added, removed or renamed.

**Mechanism.** The safety is a *compile-time macro remapping in the including (caller)
translation unit only*. `stdlib/safe/safe_c.h` defines object-like/function-like macros such as
`#define malloc(sz) malloc_s(sz)` and
`#define memset(d,c,n) ((void)cplus_memset_s_auto(...))`; so the caller emits a call to a
differently-named helper that is already defined in the archives. It is **not symbol
interposition**: the archives define no symbol named `malloc`, `calloc`, `realloc`, `free`,
`memset`, `memcpy`, `memmove`, `memcmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `printf`,
`fprintf`, `fread` or `fwrite` (verified with `nm -gU <archives> | ... | grep -E '^(...)$'` →
no matches), nothing overrides the platform libc symbols, and the definition/name/signature of
each ordinary C function in the caller is untouched. With `-DCPLUS_SAFE_C_DISABLE` the header
degrades to declarations only and the resulting executable is byte-identical to the plain build
at the `nm -gU` level.

**Scope / limitations.** These results are evidence only for the exact toolchain, target
(`arm64-apple-darwin`), flags and archives listed above. No claim is made about untested
platforms, architectures, compilers, ABI variants (e.g. x86-64 SysV, Windows x86, i386), other
optimization levels, or dynamic-library interposition scenarios. The body of `abi_roundtrip`
does call different helper functions in the safe build because its *source call sites* are
macro-remapped; the claim is about the exported C ABI surface (names, signatures, layouts,
calling convention), not about identical machine code for functions whose bodies use remapped
calls.
