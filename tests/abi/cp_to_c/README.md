# Task 4 — C+ → C interoperability lab

A real C+ translation unit is class-lowered by `./build/c+` in **strict mode**,
archived with `/usr/bin/ar`, and called from an ordinary C program compiled by
`/usr/bin/clang`. No source inspection, no `|| true` masking. All generated
evidence lives in `artifacts/`.

This lab uses `tests/abi/abi_compat_cplus_lib.cp` as its reference pattern and
adds new cases: five C+ classes (lowered to plain `<Class>_<method>` symbols),
a 7-argument mixed calling-convention probe, a bit-identical pointer
round-trip, a two-sided struct-layout comparison, element access through a
pointer parameter via `__builtin_memcpy`, an ownership contract with tracked
`cplus_calloc` buffers and a checked destroy, and error-code round-tripping.

## Files (all under `tests/abi/cp_to_c/`)

| File | Role |
| --- | --- |
| `cp_to_c_shared.h` | Plain-C header consumed by both compilers; structs, error codes, prototypes |
| `cp_to_c_lib.cp` | C+ source with 5 classes + 13 exported plain-C functions |
| `cp_to_c_driver.c` | Ordinary C99 driver compiled with `/usr/bin/clang` |
| `expected_symbols.txt` | Exact `nm` symbol names asserted by the runner |
| `run_cp_to_c.sh` | Runner; records commands, exit codes and `nm` output |
| `artifacts/results.log` | Authoritative command/exit-code/`nm` log |
| `artifacts/nm_cp_to_c.txt` | Exact `nm -gU` output of the archive |
| `artifacts/cp_to_c_lib.lowered.c` | Emitted lowered C (shows class lowering) |
| `artifacts/cp_to_c_lib.o`, `artifacts/libcp_to_c.a` | C+ object / archive |
| `artifacts/cp_to_c_driver` | Binary linked by `/usr/bin/clang` |
| `artifacts/driver.out` | Full stdout of the C driver (63 checks) |

## Environment (captured in `artifacts/results.log`)

```
arch=arm64 os=Darwin
PATH=/usr/bin:/bin
CPLUS_CLANG=/usr/bin/clang
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0
C+ 0.1.0   Language: C+   Backend: host Clang/LLVM
```

Prerequisite (not built or modified here): `build/libc/libcplus_hardened.a`
(the repo's prebuilt `tests/libc/build_libc.sh` output).

## Exact commands and exit codes

Run from the repo root. `ROOT` expands to
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

```
$ sh tests/abi/cp_to_c/run_cp_to_c.sh

CMD[T1_cplus_lower_compile] timeout=180s: ./build/c+ build --strict --sanitize=none tests/abi/cp_to_c/cp_to_c_lib.cp --emit-c tests/abi/cp_to_c/artifacts/cp_to_c_lib.lowered.c -c -o tests/abi/cp_to_c/artifacts/cp_to_c_lib.o -- -I$ROOT/libc/install-include -I$ROOT/tests/abi/cp_to_c
RC[T1_cplus_lower_compile]=0
CMD[T1_ar_archive] timeout=60s: /usr/bin/ar rcs tests/abi/cp_to_c/artifacts/libcp_to_c.a tests/abi/cp_to_c/artifacts/cp_to_c_lib.o
RC[T1_ar_archive]=0
CMD[T2_c_driver_link] timeout=180s: /usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic -I$ROOT/libc/install-include -I$ROOT/tests/abi/cp_to_c tests/abi/cp_to_c/cp_to_c_driver.c tests/abi/cp_to_c/artifacts/libcp_to_c.a build/libc/libcplus_hardened.a -lm -pthread -o tests/abi/cp_to_c/artifacts/cp_to_c_driver
RC[T2_c_driver_link]=0
CMD[T3_driver_run] timeout=60s: tests/abi/cp_to_c/artifacts/cp_to_c_driver > tests/abi/cp_to_c/artifacts/driver.out
RC[T3_driver_run]=0
CMD[T4_nm_archive] timeout=60s: /usr/bin/nm -gU tests/abi/cp_to_c/artifacts/libcp_to_c.a
RC[T4_nm_archive]=0
CMD[T6_file_object] timeout=60s: /usr/bin/file tests/abi/cp_to_c/artifacts/cp_to_c_lib.o
RC[T6_file_object]=0
```

`./build/c+ build` also emits the lowered C to `artifacts/cp_to_c_lib.lowered.c`;
e.g. `CpAdder` lowers to `struct CpAdder` + `int32_t CpAdder_apply(CpAdder *self,
int32_t x)`, and every method call is rewritten to `CpAdder_apply(&add_eng, b)`.
The runner greps the lowered source for all five class/struct/method pairs.

## `nm` output (exact, `nm -gU libcp_to_c.a`, rc=0)

```
cp_to_c_lib.o:
0000000000000000 T _CpAdder_apply
0000000000000278 T _CpBuffer_read
000000000000020c T _CpBuffer_write
0000000000000308 T _CpLayoutProbe_fill
0000000000000024 T _CpRecordValidator_check
0000000000000180 T _CpScaler_apply
00000000000003a8 T _cp_to_c_add
0000000000000748 T _cp_to_c_buffer_create
0000000000000a90 T _cp_to_c_buffer_destroy
00000000000009e4 T _cp_to_c_buffer_meta
0000000000000964 T _cp_to_c_buffer_read
0000000000000838 T _cp_to_c_buffer_write
0000000000000b20 T _cp_to_c_echo_err
0000000000000654 T _cp_to_c_layout_report
00000000000003dc T _cp_to_c_mixcall
00000000000005e4 T _cp_to_c_ptr_roundtrip
0000000000000580 T _cp_to_c_record_by_ptr
0000000000000530 T _cp_to_c_record_by_value
000000000000067c T _cp_to_c_scale_sum
```

All 19 names in `expected_symbols.txt` match the rule
`^[0-9A-Fa-f]+ T _<name>$` exactly; `grep -Eq ' T __Z'` found no mangled
symbols. The first six are class methods emitted by class lowering; the rest
are the plain-C exported entry points. Object type:
`Mach-O 64-bit object arm64`.

## C driver result (exact, `artifacts/driver.out`, rc=0)

```
cp_to_c_driver: sizeof(void*)=8 sizeof(record)=48 align(record)=8
cp_to_c_driver: C+ layout size=48 align=8 i=0 u=4 d=8 p=16 n=24 q=32 tag=40
...
cp_to_c: 63/63 boundary checks match
```

Coverage of the 63 checks (all `OK`, none `MISMATCH`):

* **Symbol exports** — 19 exact names above, no C++ mangling, lowered C audited.
* **Calling convention / signatures** — `add` (3 cases), `mixcall` with seven
  mixed args (int32, double, `const void *`, int32, double, size_t, int32) and
  per-argument mismatch detection (9 cases), struct by value and by pointer.
* **Struct layouts** — C+ `layout_report` agrees with the C side on
  `sizeof`/`alignof` and all eight `offsetof` fields (`48 / 8`, offsets
  `0,4,8,16,24,32,40`); caller's struct bytes unchanged by a by-value call.
* **Pointer representation** — `sizeof(void *) == 8` on both sides; a `void *`
  passed to C+ and written back through `cp_to_c_ptr_roundtrip` is
  bit-identical; short buffer → `ERR_SIZE`, null out → `ERR_NULL`.
* **Ownership contracts** — C+ `cplus_calloc(4, sizeof(int32_t))` + ownership
  transfer to an opaque C handle; C queries tracked metadata (byte_size 16,
  elem_count 4), writes/reads 4 elements, gets `ERR_RANGE` on out-of-range
  indices, and returns the handle through `cp_to_c_buffer_destroy`, which
  clears the C pointer; double-destroy → `ERR_NULL`.
* **Error propagation** — distinct nonzero codes for each bad field, null
  pointer, short buffer, and out-of-range index; `cp_to_c_echo_err` round-trips
  `OK`, `ERR_FREE` and `26` unchanged.

## Strict-mode constraints observed and applied

The `./build/c+ build --strict --sanitize=none` build rejects unverifiable
operations; the lab is written to satisfy all of them. Development-time probes
(scratch files removed) reproduced these exact compiler diagnostics:

* `p[i]` on a pointer parameter → `[CPLUS-FFI] pointer parameter 'p' has
  unverifiable extent; subscript cannot be proven safe` — solved with
  `__builtin_memcpy(&elem, p + i, sizeof(elem))` (used in `CpScaler::apply` and
  `CpBuffer` methods).
* `a / b` → `[CPLUS-ARITH] division by zero cannot be excluded` and `a % b` →
  `remainder by zero cannot be excluded` — no variable division or modulo
  anywhere in the lab.
* `make_vec(a, b).sum()` → `no member named 'sum' in 'struct Vec2'` — method
  calls use named temporaries with globally unique class-typed local names
  (`add_eng`, `val_eng`, `val_eng_ptr`, `scale_eng`, `layout_eng`,
  `buf_eng_write`, `buf_eng_read`).

Final result: `cp_to_c lab: 37 passed, 0 failed`, runner exit code `0`.
