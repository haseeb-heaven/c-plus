# Task 4 — C-to-C+ interoperability lab

A real, compiled interoperability lab: a C library is built with `/usr/bin/clang`
into a real object/archive, then linked from a C+ program by `./build/c+` through
its supported `-- <archive>` forwarding path and executed. Both directions of the
boundary are exercised at runtime, with exact commands, symbol names, exit codes,
stdout and stderr recorded.

Runner: `sh tests/abi/c_to_cp/run.sh`
Final result: **`c_to_cp: 24 passed, 0 failed`** (runner exit code **0**).

All generated artifacts are confined to `tests/abi/c_to_cp/artifacts/`. No compiler,
runtime, stdlib, header or other test file was modified.

---

## 1. Files

| file | role |
| --- | --- |
| `c_to_cp_shared.h` | plain-C ABI contract shared by clang and c+ (no C+ syntax, no cplus headers) |
| `c_to_cp_lib.c` | the C side: struct/pointer/callback/function-pointer/opaque-handle/variadic/allocator APIs |
| `c_to_cp_caller.cp` | genuine C+ caller (`.cp`), lowered by `./build/c+`, linked to the C archive |
| `run.sh` | restricted-PATH, bounded-timeout, exit-code-accounting runner |
| `artifacts/results.log` | exact commands, exit codes, warnings, checks (machine-readable transcript) |
| `artifacts/caller.stdout` | exact stdout of the C+ program (30 checks + final summary) |
| `artifacts/caller.stderr` | exact stderr of the C+ program (empty, 0 bytes) |
| `artifacts/nm_libc_to_cp.txt` | exported symbol names of the C archive (`nm -gU`) |
| `artifacts/allocator_boundary.txt` | symbol evidence for the UNSUPPORTED allocator-boundary cases |
| `artifacts/c_to_cp_lib.o`, `artifacts/libc_to_cp.a`, `artifacts/c_to_cp_caller`, `artifacts/caller_direct` | build products |

---

## 2. Environment / toolchain (captured by the runner)

| item | value |
| --- | --- |
| date (UTC) | `2026-11-23T04:01:03Z` |
| arch / os | `arm64` / `Darwin` |
| C compiler | `/usr/bin/clang` — Apple clang 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin27.2.0` |
| C+ driver | `./build/c+` → `C+ 0.1.0`, backend host Clang/LLVM |
| archiver / symbols | `/usr/bin/ar`, `/usr/bin/nm` |
| PATH during run | `/usr/bin:/bin` (restricted), `CPLUS_CLANG=/usr/bin/clang` |
| build modes | `--strict --sanitize=none` (strict rewriter, no sanitizer instrumentation) |
| timeouts | compile 120–180 s, archive/nm 60 s, program run 60 s |

---

## 3. Exact commands and exit codes

Reproduced from `artifacts/results.log`; `$ROOT` = `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.
Every command below exited **0**.

```sh
# 1. C side: real object + real archive
/usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
  -I$ROOT/tests/abi/c_to_cp \
  -c tests/abi/c_to_cp/c_to_cp_lib.c \
  -o $ROOT/tests/abi/c_to_cp/artifacts/c_to_cp_lib.o          # rc=0

/usr/bin/ar rcs \
  $ROOT/tests/abi/c_to_cp/artifacts/libc_to_cp.a \
  $ROOT/tests/abi/c_to_cp/artifacts/c_to_cp_lib.o             # rc=0

# 2. symbol audit
/usr/bin/nm -gU $ROOT/tests/abi/c_to_cp/artifacts/libc_to_cp.a \
  > $ROOT/tests/abi/c_to_cp/artifacts/nm_libc_to_cp.txt       # rc=0

# 3. direct-link probe (no suppression flag): proves the .a is linked directly
$ROOT/build/c+ build --strict --sanitize=none \
  tests/abi/c_to_cp/c_to_cp_caller.cp \
  -o $ROOT/tests/abi/c_to_cp/artifacts/caller_direct \
  -- $ROOT/tests/abi/c_to_cp/artifacts/libc_to_cp.a           # rc=0

# 4. production C+ build (suppresses compile-step-only warning) + run
$ROOT/build/c+ build --strict --sanitize=none \
  tests/abi/c_to_cp/c_to_cp_caller.cp \
  -o $ROOT/tests/abi/c_to_cp/artifacts/c_to_cp_caller \
  -- $ROOT/tests/abi/c_to_cp/artifacts/libc_to_cp.a \
  -Wno-unused-command-line-argument                           # rc=0

$ROOT/tests/abi/c_to_cp/artifacts/c_to_cp_caller              # rc=0
```

### The direct link

The C+ driver links the precompiled C archive **directly** through its
`-- <archive>` forwarding path — no manual linker workaround. As in the sibling
`tests/abi` suite, the *compile* step also receives the forwarded archive and
emits one benign warning (preserved verbatim in `results.log`); the *link* step
consumes it correctly:

```text
clang: warning: .../libc_to_cp.a: 'linker' input unused [-Wunused-command-line-argument]
```

The production build adds `-Wno-unused-command-line-argument` to keep the
transcript clean. Both link variants produce a working executable; `caller_direct`
and `c_to_cp_caller` both ran with rc=0.

---

## 4. Exported C symbols (unmangled)

`nm -gU artifacts/libc_to_cp.a` — 17 defined text symbols, all plain C names
(no Itanium `T __Z` mangling; `no_cxx_mangling` check passed):

```text
0000000000000000 T _cto_point_translate
0000000000000014 T _cto_rect_from_points
0000000000000018 T _cto_rect_area
000000000000005c T _cto_sum_ints
0000000000000108 T _cto_scale_in_place
000000000000018c T _cto_register_callback
00000000000001b4 T _cto_fire_callback
0000000000000208 T _cto_clear_callback
0000000000000220 T _cto_apply_binary
0000000000000230 T _cto_store_create
000000000000029c T _cto_store_put
00000000000002e8 T _cto_store_get
0000000000000320 T _cto_store_size
000000000000032c T _cto_store_destroy
000000000000035c T _cto_make_int_buffer
00000000000003a8 T _cto_consume_and_free_int_buffer
0000000000000458 T _cto_vsum
```

(Mach-O C symbols carry a leading underscore; addresses are from this build.)

---

## 5. Exact program output

`artifacts/caller.stdout` (30 checks, all `OK`), final line
`c_to_cp_caller: 30/30 calls match`:

```text
c_to_cp_caller: begin
struct return: cto_point_translate -> {7,7}                    OK
struct return: cto_rect_from_points lo                         OK
struct return: cto_rect_from_points hi                         OK
struct ptr arg + error code: rect_area -> 20                   OK
error code: rect_area(NULL) -> CTO_ERR_NULL                    OK
pointer arg: cto_sum_ints -> 15                                OK
pointer arg: scale_in_place then sum -> 30                     OK
error code: sum_ints(NULL) -> CTO_ERR_NULL                     OK
callback: register C+ function into C                          OK
callback: C fires C+ callback -> 110                           OK
error code: fire after clear -> CTO_ERR_CALLBACK               OK
function pointer arg: apply add -> 13                          OK
function pointer arg: apply mul -> 42                          OK
error code: apply(NULL op) -> CTO_ERR_NULL                     OK
opaque handle: create returns non-NULL                         OK
opaque handle: initial size 0                                  OK
opaque handle: put(2,99)                                       OK
opaque handle: size after put == 3                             OK
opaque handle: get(2) -> 99                                    OK
error code: get out of range -> CTO_ERR_RANGE                  OK
opaque handle: destroy completed                               OK
C->C+ memory: make buffer -> OK, len 4                         OK
C->C+ memory: C+ reads C buffer -> 100                         OK
C->C+ memory: C+ free() of C malloc block completed            OK
error code: make buffer(NULL out) -> CTO_ERR_NULL              OK
C+->C memory: host malloc non-NULL                             OK
C+->C memory: C consumed values, checksum 10, freed            OK
variadic: cto_vsum(5, 1..5) -> 15                              OK
variadic: cto_vsum(3, -1, 0, 1) -> 0                           OK
variadic: cto_vsum(0) -> 0                                     OK
c_to_cp_caller: 30/30 calls match
```

`artifacts/caller.stderr` is **empty (0 bytes)**. Exit code **0**.

---

## 6. VERIFIED

All items below are exercised by actual cross-language calls in
`c_to_cp_caller.cp` against the clang-built archive, under `--strict`.

| # | Feature | Evidence (call → observed result) | Status |
| --- | --- | --- | --- |
| 1 | struct arguments & returns by value | `cto_point_translate({3,9},4,-2)` → `{7,7}`; `cto_rect_from_points` → lo `{1,2}`, hi `{5,7}` | VERIFIED |
| 2 | struct pointer arg + error code | `cto_rect_area(&rect,&area)` → `CTO_OK`, `area=20`; NULL → `CTO_ERR_NULL` | VERIFIED |
| 3 | pointer arguments / mutation | `cto_sum_ints(arr,5)` → `15`; `cto_scale_in_place(...,2)` then sum → `30`; NULL → `CTO_ERR_NULL` | VERIFIED |
| 4 | callback registration | C+ `lab_on_event` registered via `cto_register_callback`, C fires it → `110`; after clear → `CTO_ERR_CALLBACK` | VERIFIED |
| 5 | function pointers as arguments | `cto_apply_binary(6,7, lab_add_op)` → `13`; `lab_mul_op` → `42`; NULL op → `CTO_ERR_NULL` | VERIFIED |
| 6 | opaque handles | `cto_store_create(4)` non-NULL; size 0→3; put/get `99`; out-of-range get → `CTO_ERR_RANGE`; destroy | VERIFIED |
| 7 | **memory allocated by C, released by C+** | C `malloc` via `cto_make_int_buffer(4)`; C+ reads `100` then calls `free()` itself — same host allocator, rc 0 | VERIFIED |
| 8 | **memory allocated by C+, released by C** | C+ `malloc(4*int)`, fills `1,2,3,4`, passes `cto_span` to `cto_consume_and_free_int_buffer`; C reads checksum `10` and calls `free()` — same host allocator | VERIFIED |
| 9 | variadic interface | `cto_vsum(5,1,2,3,4,5)` → `15`; `(3,-1,0,1)` → `0`; `(0)` → `0` | VERIFIED |
| 10 | error-code convention | negative codes (`CTO_ERR_NULL/RANGE/STATE/NOMEM/CALLBACK`) returned across the boundary and checked by C+ | VERIFIED |
| 11 | unmangled symbols / direct archive link | 17 plain `_cto_*` symbols; C+ links the `.a` directly; both link variants run rc 0 | VERIFIED |

The allocator boundary is documented explicitly at the API surface: `cto_span`
carries a **host-C-allocator** (`malloc`/`free`) buffer, and both transfer tests
(7 and 8) keep allocation and release inside that single allocator. This is the
"same allocator only" rule the task requires.

---

## 7. UNSUPPORTED (not executed — documented boundary violations)

These are deliberately **not** run: they are undefined behaviour or a detected
fatal runtime violation by design. They are separated from VERIFIED above.

| case | why unsupported |
| --- | --- |
| **C++ managed allocator ↔ host `free`/`malloc`** | The C+ runtime's managed allocator (`cplus_alloc` / `cplus_release`) keeps a private metadata/live-block side table. A block from it must be released with `cplus_release`, never host `free()`; conversely a host `malloc` block is not a live managed block (`cplus_check_live` aborts on it). Mixing is a fatal ownership/type-confusion error. |
| **hardened-libc allocator ↔ C+ managed allocator** | `build/libc/libcplus_hardened.a` exports a *different* allocator (`cplus_malloc` / `cplus_free` / `cplus_alloc_get_meta` / `cplus_transfer_ownership`) with its own metadata. It is not interchangeable with the C+ runtime allocator above. |
| **hardened-libc `cplus_malloc` block freed by host `free()` (or vice versa)** | Different allocators / metadata tables; corrupt bookkeeping or undefined behaviour. |
| **stack/static/internally-owned pointer handed across and released** | Non-heap or foreign-owner pointers cannot be released by the receiver; the ABI never transfers ownership of such memory. |

Symbol evidence (`artifacts/allocator_boundary.txt`) shows the two allocator
families are distinct symbol sets:

```text
# /usr/bin/nm -gU .../libcplus_runtime.a   (C+ managed allocator)
_cplus_alloc   _cplus_zalloc   _cplus_alloc_array   _cplus_realloc
_cplus_release _cplus_alloc_bytes   _cplus_alloc_count
(no _cplus_malloc / _cplus_free / _cplus_alloc_get_meta)

# /usr/bin/nm -gU build/libc/libcplus_hardened.a   (hardened libc allocator)
_cplus_malloc  _cplus_realloc  _cplus_free  _cplus_alloc_get_meta
_cplus_transfer_ownership
```

Consequently every memory transfer in this lab uses the **host C allocator on
both sides** (the only allocator shared identically by the clang-built C library
and the C+ program in this configuration). A future lab may add a VERIFIED
managed-allocator transfer only if both sides link the *same* allocator archive;
with the current driver/runtime split that would cross allocator boundaries and
is therefore classified UNSUPPORTED here.

---

## 8. C+ rewriter constraints honoured

The lab was authored for the strict `./build/c+` rewriter and builds with
`--strict`:

- **unique names** — all C+ locals/helpers use a `lab_`-prefixed, distinct name.
- **no pointer-parameter subscripts / `__builtin_memcpy`** — the C+ side never
  subscripts a pointer parameter; buffers are written with a walking pointer
  (`*p`, `p++`), and no raw pointer indexing is used.
- **no variable division/modulo** — none appears in the C+ source.
- **no chained temporaries** — call results are assigned to locals before use.

A portability finding worth recording: C+ reserves `.len` as a managed-length
operator, so a plain C struct field named `len` is rejected in strict mode
(`'.len' requires a static array or a C+ managed allocation`). The boundary
struct therefore names its count field `nelems` instead. The C library is
compiled by clang and is unaffected; only the C+ translation unit needs this.

---

## 9. Scope / what this does not claim

Evidence is for the host toolchain above (`arm64-apple-darwin`, Apple clang 16)
and the supported `./build/c+` driver path. It establishes working mixed-language
calls for the exercised ordinary-C subsets (byte-compatible structs, pointers,
function pointers, opaque handles, variadics, error codes) and a single shared
allocator for memory transfers. It does not claim: Windows x86/MSVC/UCRT layout,
`-fshort-enums` or other ABI-flavour variants, C++ name-mangled linkage,
type-safe variadics, or safety of arbitrary raw pointers. Crossing distinct
allocators remains UNSUPPORTED as documented in section 7.
