# ABI compatibility evidence (ordinary C pointers and C-compatible layouts)

Suite entry point: `sh tests/abi/run_abi_compat.sh` (executable; `set -u`,
restricted `PATH=/usr/bin:/bin`, `CPLUS_CLANG=/usr/bin/clang`, every command
wrapped in `/usr/bin/timeout`, no `|| true` masking).

Final run: **69 passed, 0 failed** (`abi_compat: 69 passed, 0 failed`).
Machine-readable command / exit-code transcript:
`build/abi/compat/results.log`.

## 1. Environment and toolchain (captured by the runner)

| item | value |
| --- | --- |
| date (UTC) | `2026-11-23T00:22:05Z` (from `results.log`) |
| compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6) |
| target | `arm64-apple-darwin27.2.0`, thread model posix |
| backend install dir | `/Library/Developer/CommandLineTools/usr/bin` |
| arch / os | `arm64` / `Darwin` |
| C+ driver | `./build/c+` → `C+ 0.1.0`, backend host Clang/LLVM |
| archiver / symbol tool | `/usr/bin/ar`, `/usr/bin/nm` |
| PATH during run | `/usr/bin:/bin` (restricted, explicit) |
| `CPLUS_CLANG` | `/usr/bin/clang` (exported by the runner) |
| timeouts | compile steps 120–300 s, program runs 60 s, `diff`/`nm` 60 s |

No compiler, runtime, stdlib header, `tests/abi/abi_layouts.c` or
`tests/abi/run_abi_layouts.sh` file was modified. All generated artifacts live
in `build/abi/compat/`.

## 2. Files created

| file | role |
| --- | --- |
| `tests/abi/abi_compat_shared.h` | plain-C declarations shared by both languages (no C+ syntax, no C+ headers) |
| `tests/abi/abi_compat_pointers.c` | Test 1 — pointer sizes and byte-representation equality |
| `tests/abi/abi_compat_layouts.c` | Test 2 — captured constants for every public C-compatible struct/union/enum |
| `tests/abi/abi_compat_cplus_lib.cp` | C+ implementation (class lowering) of functions called from C |
| `tests/abi/abi_compat_c_driver.c` | Test 3a/4a — real C calls the C+ archive |
| `tests/abi/abi_compat_c_part.c` | plain C implementation compiled to a `.a` by `/usr/bin/clang` |
| `tests/abi/abi_compat_cplus_caller.cp` | Test 3b/4b — C+ links and calls that precompiled `.a` |
| `tests/abi/abi_compat_header_neutrality.c` | Test 6 — same layout probe compiled with / without cplus headers |
| `tests/abi/abi_compat_expected_libc_symbols.txt` | audited symbol names in `libcplus_hardened.a` |
| `tests/abi/abi_compat_expected_safecrt_symbols.txt` | audited symbol names in `libcplus_safecrt.a` |
| `tests/abi/abi_compat_expected_compat_symbols.txt` | audited symbol names in the two boundary archives |
| `tests/abi/run_abi_compat.sh` | runner (restricted PATH, timeouts, exit-code accounting) |
| `tests/abi/ABI_COMPAT_NOTES.md` | this document |

## 3. Exact commands and exit codes

Commands are reproduced from `results.log` with `$ROOT` = repository root.
**Every command below exited 0** unless stated otherwise.

```sh
# prerequisites (absolute path shown: $ROOT/build/libc/libcplus_hardened.a)
sh tests/libc/build_libc.sh                          # rc=0
sh scripts/build_safecrt.sh                          # rc=0

# Test 1 — native C
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic \
  tests/abi/abi_compat_pointers.c -o build/abi/compat/pointers          # rc=0
build/abi/compat/pointers                                               # rc=0

# Test 2 — native C, real libraries linked
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic \
  -I$ROOT/libc -I$ROOT/libc/include -I$ROOT/libc/install-include \
  -I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include \
  -I$ROOT/third_party/intel-safestringlib/include -I$ROOT/tests/abi \
  tests/abi/abi_compat_layouts.c build/libc/libcplus_hardened.a \
  build/safe/libcplus_safecrt.a -lm -pthread -o build/abi/compat/layouts # rc=0
build/abi/compat/layouts                                                # rc=0

# Tests 3a/4a — C+ object/archive, then ordinary C driver
./build/c+ build --sanitize=none tests/abi/abi_compat_cplus_lib.cp \
  -c -o build/abi/compat/abi_compat_cplus_lib.o \
  -- -I$ROOT/libc/install-include                                      # rc=0
/usr/bin/ar rcs build/abi/compat/libabi_compat_cplus.a \
  build/abi/compat/abi_compat_cplus_lib.o                              # rc=0
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic <same -I flags> \
  tests/abi/abi_compat_c_driver.c build/abi/compat/libabi_compat_cplus.a \
  build/libc/libcplus_hardened.a -lm -pthread \
  -o build/abi/compat/c_driver                                         # rc=0
build/abi/compat/c_driver                                               # rc=0

# Tests 3b/4b — precompiled C archive linked from C+
/usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
  -I$ROOT/tests/abi -c tests/abi/abi_compat_c_part.c \
  -o build/abi/compat/abi_compat_c_part.o                              # rc=0
/usr/bin/ar rcs build/abi/compat/libabi_compat_cpart.a \
  build/abi/compat/abi_compat_c_part.o                                 # rc=0
# direct link probe (no suppression flag): rc=0, one benign warning, quoted below
./build/c+ build --sanitize=none tests/abi/abi_compat_cplus_caller.cp \
  -o build/abi/compat/cplus_caller_direct \
  -- build/abi/compat/libabi_compat_cpart.a                            # rc=0
# production build (suppresses the compile-step-only warning)
./build/c+ build --sanitize=none tests/abi/abi_compat_cplus_caller.cp \
  -o build/abi/compat/cplus_caller \
  -- build/abi/compat/libabi_compat_cpart.a \
  -Wno-unused-command-line-argument                                    # rc=0
build/abi/compat/cplus_caller                                           # rc=0

# Test 5 — symbol names
/usr/bin/nm -gU build/libc/libcplus_hardened.a                          # rc=0
/usr/bin/nm -gU build/safe/libcplus_safecrt.a                           # rc=0
/usr/bin/nm -gU build/abi/compat/libabi_compat_cplus.a \
  build/abi/compat/libabi_compat_cpart.a                                # rc=0

# Test 6 — header neutrality, identical source compiled twice
/usr/bin/clang -O1 -std=gnu11 -Wall -Wextra \
  tests/abi/abi_compat_header_neutrality.c \
  -o build/abi/compat/neutral_plain                                    # rc=0
/usr/bin/clang -O1 -std=gnu11 -Wall -Wextra \
  -include cplus_libc.h -include cplus_libc_safe.h \
  -I$ROOT/libc/install-include \
  tests/abi/abi_compat_header_neutrality.c \
  -o build/abi/compat/neutral_cplus                                    # rc=0
build/abi/compat/neutral_plain                                          # rc=0
build/abi/compat/neutral_cplus                                          # rc=0
/usr/bin/diff -u build/abi/compat/neutral_plain.out \
  build/abi/compat/neutral_cplus.out                                   # rc=0 (empty)
```

### The `.a` link question (Test 3b), answered

The C+ driver **can** link a precompiled C archive directly through its
supported `--` forwarding path — the backend link line in `--verbose` mode
places `libabi_compat_cpart.a` next to the generated object and the runtime
archive. No manual linker workaround was needed. The *only* diagnostic comes
from the backend **compile** step, which also receives `--` arguments; the raw
probe preserved it verbatim in `results.log`:

```
clang: warning: build/abi/compat/libabi_compat_cpart.a: 'linker' input unused [-Wunused-command-line-argument]
```

This is benign (the second, link invocation consumes the `.a`), and the
production build adds `-Wno-unused-command-line-argument` to keep the transcript
clean. `cplus_caller_direct` ran correctly.

## 4. Per-test results

### Test 1 — pointer ABI (20/20, `abi_compat_pointers: 20/20 pointer ABI checks match`)

- `sizeof(void *) == sizeof(char *) == sizeof(int *) == 8`
- `sizeof(int (*)(int)) == sizeof(void (*)(void)) == 8`
- `sizeof(uintptr_t) == sizeof(size_t) == 8`, `__alignof__(void*) == 8`
- a data pointer's bytes are bit-identical to `uintptr_t` bytes and equal its
  numeric address; memcpy round-trip preserves equality and dereferenceability
- a function pointer's bytes survive `fn* -> void* -> fn*` memcpy staging with
  equality **and** callability (`fn_roundtrip(41) == 42`); no cast between the
  pointer classes is used anywhere
- null data and function pointer representations are all-zero bytes

### Test 2 — public layout constants (80/80, `abi_compat_layouts: 80/80 layouts match`)

Captured from the current checked-in headers (Apple clang 16 / arm64). These
are the expected values asserted by the new probe (identical to the golden
table in the pre-existing `abi_layouts.c`, plus the shared boundary struct):

| type | size | align | notable offsets / values |
| --- | ---: | ---: | --- |
| `cplus_alloc_meta` | 40 | 8 | byte_size 0, elem_size 8, elem_count 16, generation 24, is_live 32 |
| `cplus_safe_alloc_meta` | 40 | 8 | byte_size 0, elem_size 8, elem_count 16, generation 24, is_live 32 |
| `cplus_thrd_t` | 32 | 8 | — |
| `cplus_mtx_t` | 128 | 8 | — |
| `cplus_cnd_t` | 128 | 8 | — |
| `cplus_once_flag` | 64 | 8 | — |
| `cplus_tss_t` | 8 | 8 | — |
| `cplus_thrd_start_t` | 8 | 8 | — |
| anonymous thread enum | — | — | SUCCESS 0, BUSY 1, NOMEM 2, TIMEDOUT 3, ERROR 4 |
| `char16_t` | 2 | 2 | — |
| `char32_t` | 4 | 4 | — |
| `mbstate_t` (host) | 128 | 8 | — |
| `cplus_safe_stat` | 48 | 8 | api 0, calls 24, ok 32, fail 40 |
| `cplus_safe_platform_info` | 28 | 4 | has_fortify 0, has_object_size 4, has_annex_k 8, has_apple_libc 12, has_ucrt 16, has_glibc 20, has_musl 24 |
| `cplus_safe_log_level` | 4 | 4 | OFF 0, ERROR 1, WARN 2, INFO 3, DEBUG 4, TRACE 5 |
| `cplus_safe_constraint_mode` | 4 | 4 | IGNORE 0, ABORT 1 |
| `cplus_runtime_handler` | 8 | 8 | — |
| `errno_t` | 4 | 4 | — |
| `rsize_t` | 8 | 8 | — |
| `abi_compat_record` | 32 | 8 | i 0, u 4, d 8, p 16, n 24 |

Additionally verified: `sizeof`/key offsets of `cplus_alloc_meta` and
`cplus_safe_alloc_meta` are identical (the metadata side table has a single
C-compatible shape across the plain libc and SafeCRT headers).

### Test 3a / 4a — C calls C+ (13/13, `abi_compat_c_driver: 13/13 C->C+ boundary checks match`)

The C driver is compiled by `/usr/bin/clang` and linked against an archive
produced from a **genuine `.cp` source** lowered by `./build/c+`. Checks include
ordinary integer signatures, `abi_compat_record` passed **by value** and **by
pointer**, mutation detection for bad fields, NULL rejection, and filling a
public `cplus_alloc_meta` from C+ through a plain `void *` (byte_size 64,
elem_count 64, is_live 1, generation nonzero). The caller's struct bytes are
unchanged by a by-value call.

### Test 3b / 4b — C+ calls precompiled C (10/10, `abi_compat_cplus_caller: 10/10 C+->C boundary checks match`)

`abi_compat_c_part.c` was compiled and archived by `/usr/bin/clang` +
`/usr/bin/ar`; the C+ program links it directly via the driver. Checks cover
the C function call, a C+ class method feeding a plain C signature, a C+ class
holding the record with unchanged layout, `sizeof(abi_compat_record) == 32`
and `alignof == 8` as seen from C+, and by-value / by-pointer / NULL / bad-field
behaviour in the C+ → C direction.

### Test 5 — exported symbol names (all audited names present, none mangled)

Audited with `nm -gU` on the freshly built archives. Match rule is an exact
defined-text symbol `_<name>` (Mach-O leading underscore). Counts:
`nm_libc.txt` 725 lines, `nm_safecrt.txt` 357 lines, `nm_compat.txt` 12 lines.

Excerpt (exact names, no C++/Itanium mangling):

```
_cplus_malloc            _cplus_calloc          _cplus_realloc
_cplus_free              _cplus_alloc_get_meta  _cplus_alloc_remaining_capacity
_cplus_alloc_size        _cplus_alloc_live_count _cplus_transfer_ownership
_cplus_memcpy            _cplus_printf          _cplus_sqrt
_cplus_memcpy_s          _cplus_sc_memcpy_s     _cplus_safe_alloc_get_meta
_cplus_safe_stats_get    _cplus_safe_platform_probe _cplus_runtime_set_handler
_abi_cplus_add           _abi_cplus_record_by_value _abi_cplus_record_by_ptr
_abi_cplus_meta_write    _abi_c_mul             _abi_c_record_by_value
```

The full expected lists are in `abi_compat_expected_*_symbols.txt`; all 22 + 10
+ 7 = 39 required names were found (each counted as a passing check), and
`grep ' T __Z'` found **no** mangled C++ symbols in any audited archive. The C+
class from `abi_compat_cplus_lib.cp` lowers to the plain symbol
`_AbiAdder_apply` rather than an Itanium-mangled name.

### Test 6 — header/metadata neutrality (diff empty)

`abi_compat_header_neutrality.c` prints 37 deterministic `LAYOUT`/`OFFSET`
lines for fundamental types, pointers, `va_list`, `div_t`/`ldiv_t`/`lldiv_t`,
`max_align_t`, `mbstate_t`, `fpos_t`, `time_t`, `struct tm`, `sigset_t`,
`struct sigaction`, `jmp_buf`, `FILE *`, `pthread_t`, `pthread_mutex_t`,
`double complex`, plus field offsets. It was compiled once plainly and once
with `-include cplus_libc.h -include cplus_libc_safe.h` (which introduce the
allocation metadata side-table declarations and the plain-C safety macro
layer). `diff -u` of the two 37-line outputs was empty. Selected values:
`void *` 8/8, `int(*)(int)` 8/8, `va_list` 8/8, `mbstate_t` 128/8,
`struct tm` 56/8 (tm_year 20, tm_gmtoff 40, tm_zone 48),
`pthread_mutex_t` 64/8, `jmp_buf` 192/4, `double complex` 16/8.

## 5. ABI regression found

**None.** Every pointer size/representation check, all 80 captured layout
constants, both directions of C ↔ C+ struct/function interchange, all 39
audited exported symbol names, and the with/without-cplus-headers layout diff
passed. The only diagnostic observed was the benign compile-step
`-Wunused-command-line-argument` warning described in section 3.

## 6. Scope / what this does not claim

Evidence is for the host toolchain above (`arm64-apple-darwin`, Apple clang 16)
and the supported `./build/c+` driver path. It establishes byte-level pointer
and layout identity, unmangled symbol names and working mixed-language calls
for the exercised C-compatible subsets. It does not establish Windows x86 /
MSVC / UCRT layout, `-fshort-enums` variants, bitfield packing across
compilers, or runtime safety of arbitrary raw pointers.
