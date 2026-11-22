# ABI layout evidence for the hardened public types

Status: **layout evidence only — not an ABI safety claim.**
Probe source: `tests/abi/abi_layouts.c`.

## What was actually run

Native compiler build (golden table captured here):

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/abi/abi_layouts.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -o <out>
```

Project compiler build, same source, C mode (`.c` suffix):

```sh
./build/c+ build --sanitize=none tests/abi/abi_layouts.c \
  -Ilibc -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -o <out> -- build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a
```

Results (Apple clang 16 / arm64-apple-darwin, both linked against the two
archives above):

| Check | Result |
| --- | --- |
| `/usr/bin/clang -std=c99` | `abi_layouts: 70/70 layouts match`, exit 0, **0 compiler warnings** |
| `./build/c+ build` (C backend) | `abi_layouts: 70/70 layouts match`, exit 0, **0 program warnings** |
| `diff -u` of the two stdout streams | **byte-identical** (72 lines each), exit 0 |

`N = 70` is the number of individual ABI datum assertions: 36 size/alignment
checks, 21 field-offset checks, 13 enumerator-value checks. The extra baseline
line (`sizeof(void*)`, `sizeof(size_t)`, `sizeof(unsigned long)`) is printed but
not asserted; it was also identical in both runs.

The c+ driver appends extra arguments to both its compile and link invocations,
so the two `.a` files are genuinely on the link line. Because the driver also
passes them to the `-c` compile step, it emits two benign warnings:
`clang: warning: <archive>: 'linker' input unused [-Wunused-command-line-argument]`.
They do not affect the compiled program or its output. The program itself
references no symbols from either archive (it uses only `sizeof`, `__alignof__`,
`offsetof` and `printf`), so the archives are not required for its correctness.

## Types covered

All named public `struct` / `enum` types defined in `libc/install-include` and
`stdlib/safe`, plus the `mbstate_t` adapter and the anonymous C11 thread-result
enum. No public `union` exists in these headers.

| Type | Declared in | size | align | Detail asserted |
| --- | --- | ---: | ---: | --- |
| `cplus_alloc_meta` | `cplus_libc.h` | 40 | 8 | offsets 0/8/16/24/32 |
| `cplus_thrd_t` | `cplus_threads.h` | 32 | 8 | 4-pointer opaque handle |
| `cplus_mtx_t` | `cplus_threads.h` | 128 | 8 | 16-pointer opaque handle |
| `cplus_cnd_t` | `cplus_threads.h` | 128 | 8 | 16-pointer opaque handle |
| `cplus_once_flag` | `cplus_threads.h` | 64 | 8 | 8-pointer opaque handle |
| `cplus_tss_t` | `cplus_threads.h` | 8 | 8 | `unsigned long` |
| `cplus_thrd_start_t` | `cplus_threads.h` | 8 | 8 | `int (*)(void *)` |
| anonymous thrd-result enum | `cplus_threads.h` | n/a | n/a | `CPLUS_THRD_*` = 0..4 |
| `char16_t` | `cplus_uchar.h` | 2 | 2 | C-mode typedef of `uint_least16_t` |
| `char32_t` | `cplus_uchar.h` | 4 | 4 | C-mode typedef of `uint_least32_t` |
| `mbstate_t` (adapter) | host `<wchar.h>` | 128 | 8 | host state type used by our API |
| `cplus_safe_alloc_meta` | `safe_alloc.h` | 40 | 8 | offsets 0/8/16/24/32 |
| `cplus_safe_stat` | `safe_stats.h` | 48 | 8 | `api`@0, `calls`@24, `ok`@32, `fail`@40 |
| `cplus_safe_platform_info` | `safe_platform.h` | 28 | 4 | 7 `int`s at 0,4,...,24 |
| `cplus_safe_log_level` | `safe_log.h` | 4 | 4 | `OFF`..`TRACE` = 0..5 |
| `cplus_safe_constraint_mode` | `safe_error.h` | 4 | 4 | `IGNORE`=0, `ABORT`=1 |
| `cplus_runtime_handler` | `safe_runtime.h` | 8 | 8 | `void (*)(const char *)` |
| `errno_t` | `safe_string.h` / `safe_memory.h` | 4 | 4 | `int` |
| `rsize_t` | `safe_string.h` / `safe_memory.h` | 8 | 8 | `size_t` |

## What this evidence does establish

* The in-memory layout (size, alignment, field offsets, enumerator values) of
  the public data types under two local toolchains: native `clang -std=c99` and
  the c+ driver's C backend (`-std=gnu17`). The c+ lowering does not perturb any
  of these layouts.
* A checked-in regression sentinel: any future header change that alters a
  size, alignment, offset or enumerator value fails the assertions and shows an
  explicit `MISMATCH` line plus a non-zero exit status.
* That the opaque thread handles are at least as large/aligned as the documented
  Darwin pthread objects (64 B mutex, 48 B cond, 16 B once).

## What this evidence does **not** establish

* **No calling convention evidence.** Nothing here tests argument passing,
  struct-by-value rules, aggregate returns, varargs, stack alignment, or
  `__cdecl` / `__stdcall` / vectorcall.
* **No symbol or linker ABI evidence.** No symbol names, mangling, exports,
  visibility, ordinals, import libraries, or symbol versioning are checked.
* **No Windows x86 evidence.** These numbers are arm64 / LP64 with Apple libc.
  Windows x86 has 4-byte `long`/pointers/`size_t`, MSVC default packing, a
  different `mbstate_t`, and different enum rules; none of those are measured.
* **No binary compatibility claim** with any prebuilt DLL, consumer, or game
  executable.
* **No runtime safety claim.** Layout agreement says nothing about pointer
  provenance, lifetime, bounds enforcement, or the correctness of any function.
* **No coverage of host/third-party types** other than `mbstate_t`: `struct tm`,
  `struct timespec`, `struct lconv`, `struct sigaction`, `sigset_t`, `fenv_t`,
  `fpos_t`, `div_t`/`ldiv_t`/`lldiv_t`/`imaxdiv_t`, `pthread_*` and exceptions4c's
  `struct e4c_exception` are declared or referenced by our headers but defined
  elsewhere, so they are deliberately out of scope.
* **No C++ ABI evidence** (mangling, vtables, namespaces) and no bitfield or
  flexible-array packing checks — none of the covered types uses those.

### Honestly skipped

* `char8_t` in `cplus_uchar.h` is gated on `__STDC_VERSION__ >= 202000L`; the
  probe is `-std=c99`, so it is not typedef'd and cannot be measured here.
* The anonymous thread-result enum has no tag or typedef, so `sizeof`/`alignof`
  are not expressible; only its enumerator values are asserted.
* There is no public `union` in the scanned headers, so none is listed.

## Plan for the remaining gaps (requires real Windows x86 tooling)

These are **planned, not executed** in this environment (macOS/arm64; no MSVC
or Windows x86 toolchain available). They must not be reported as done.

1. **Windows x86 golden table.** Compile the same probe with `clang-cl`
   (`--target=i686-pc-windows-msvc`) and with MSVC `cl.exe` against UCRT headers,
   producing a separate `ABI_GOLDEN_WIN_X86` table. Do not reuse the arm64
   numbers; expect `long`/`size_t`/pointers to be 4 bytes and `mbstate_t` to
   differ.
2. **Packing rules.** Add `#pragma pack` state reporting and static assertions
   for MSVC default `/Zp8` versus clang-cl, including a deliberately packed
   control struct to prove the probe detects packing differences.
3. **Calling-convention probe** (new standalone test, not this file): exercise
   struct-by-value, small/large aggregate returns, 8-byte-aligned parameters,
   and varargs across a DLL boundary; disassemble the call sites to verify the
   register/stack split and stack cleanup on x86.
4. **Export/symbol audit.** Dump the built DLL with `dumpbin /exports` and
   `llvm-nm`, verify every public `cplus_*` entry point is exported with the
   intended undecorated name, and record ordinals if the DLL relies on them.
5. **End-to-end DLL/EXE cross-boundary test.** Build a test consumer with the
   same Windows x86 toolchain, call at least one function from each public
   header through the import library, and confirm the layout-sensitive fields
   (`cplus_alloc_meta`, `cplus_safe_alloc_meta`, `cplus_safe_stat`) round-trip
   unchanged.
6. **Dual-table CI.** Select the golden table by preprocessor target macros so
   both Darwin/arm64 and Windows/x86 tables live in one source file, and run
   both in CI. Keep the c+/clang byte-identical diff as a third, target-local
   gate.
