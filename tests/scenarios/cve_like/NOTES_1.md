# CVE-like crash cases 01/02/05/10 — crash vs safe evidence

## Environment caveats (read before the table)

- The original files contain **no explicit example input** in their comments. The
  inputs below were chosen to trigger exactly the bug each file contains; they
  are stated explicitly so the evidence is reproducible.
- `/usr/bin/clang` is Apple clang 16 here. Its ASan runtime is broken on this
  host: even `int main(void){return 0;}` built with `-fsanitize=address` exits
  134 with `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189
  "((!asan_init_is_running)) != (0)"`. To obtain genuine crash evidence I used
  `/opt/homebrew/opt/llvm/bin/clang` 23.1.6 with the **same** flags
  (`-O1 -g -std=c99 -fsanitize=address -fno-omit-frame-pointer -isysroot
  /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`). Safe variants were
  built with the exact verify commands using `/usr/bin/clang` (no ASan needed).
- `case_02.c` does not compile as written: `memcpy(f->buffer, ...)` passes a
  `char` where `void *` is required. For the baseline run only, a temporary
  copy in `/tmp` was changed to `memcpy(&f->buffer, ...)` (the evident intent);
  the checked-in original was not touched.
- `tests/libc/build_libc.sh` and `scripts/build_safecrt.sh` both fail on this
  host because they default to a nonexistent `MacOSX27.sdk`, so system headers
  are not found. The prebuilt `build/libc/libcplus_hardened.a` and
  `build/safe/libcplus_safecrt.a` were used; all eight safe binaries linked and
  ran successfully against them.
- At `-O1`, LLVM eliminates the dead writes in `case_01` and `case_10` (the
  buffers are freed without ever being read), so those two originals exit 0
  with no ASan report. Supplementary `-O0` ASan runs show the real overflow;
  both results are reported honestly below.

## Evidence table

| case | baseline input | baseline result (exact) | safe-layer result (SafeCRT) | plain-layer result (`-DPLAIN_LIBC`) | what the library caught | caller-side checks required | classification |
|---|---|---|---|---|---|---|---|
| 01 | `4294967297` (2^32+1) | `-O1`: exit 0, no ASan report (dead store removed). `-O0`: exit 134, first line `==17779==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x604000000434 at pc ...`, `WRITE of size 32 at 0x604000000434 thread T0` | exit 0, `count: case_01: refused (count exceeds 32-bit; 64->32 truncation)`; valid `3` → `case_01: ok (items=3)` | exit 0, same refusal / same ok line | `calloc`/`calloc_s` rejects a wrapping `n*size` product (EOVERFLOW); hardened `memset` resolves `payload + i*32` to remaining tracked capacity and would refuse an out-of-range slot | Validate 64→32 truncation (`count > UINT32_MAX`); reject zero count; iterate over the validated `uint32_t` count, not the original 64-bit value | IMPLEMENTED |
| 02 | `-1` | exit 134, first line `==17793==ERROR: AddressSanitizer: negative-size-param: (size=-1)` | exit 0, `length: case_02: refused (negative signed length before size_t cast)`; valid `0` → `case_02: ok (length=0)` | exit 0, same refusal / same ok line | Hardened `memcpy` bounds `&f->buffer` (interior pointer, 4 remaining bytes) and refuses any larger `n` with ERANGE rather than corrupting the heap | Reject negative signed `length` **before** the cast to `size_t`; original also needs `&f->buffer` to compile at all | IMPLEMENTED |
| 05 | `65536 65536` | exit 134, first line `==17810==ERROR: AddressSanitizer: BUS on unknown address (pc ... T0)`, `==17810==The signal is caused by a WRITE memory access.` | exit 0, `rows cols: case_05: refused (32-bit element-count product overflow)`; valid `3 4` → `case_05: ok (rows=3 cols=4)` | exit 0, same refusal / same ok line | `calloc`/`calloc_s` rejects a wrapping element-count × `sizeof(double)` product (EOVERFLOW), so the allocation cannot silently shrink | Compute `rows*cols` in 64 bits and reject values above `UINT32_MAX`/`SIZE_MAX/sizeof(double)`; verify `last = (rows-1)*cols + (cols-1)` is below the element count before the direct `m.cells[last]` write — the library cannot bounds-check a raw pointer-index write | PARTIAL |
| 10 | `8` | `-O1`: exit 0, no ASan report (dead store removed). `-O0`: exit 134, first line `==17830==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60c000000240 at pc ...`, `READ of size 4294967280 at 0x60c000000240 thread T0` | exit 0, `len: case_10: refused (length below header size)`. Probe `100` (payload 76 > 64): `[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 76 > capacity 64, ESNOSPC 406)` then `case_10: refused (capacity check on oversized payload)`, exit 0 | exit 0, same refusal; probe `100` → `case_10: refused (capacity check on oversized payload)` (ERANGE), exit 0 | Hardened `memcpy` knows `out` is a 64-byte tracked block and refuses the 76-byte write (SafeCRT ESNOSPC/ERANGE, plain libc ERANGE) with no copy performed | Reject `total < HEADER_SIZE` **before** subtracting; reserve a destination at least as large as the allowed payload and/or check `payload` against it | IMPLEMENTED |

## Notes on the refusal mechanism

- Both layers map ordinary C names: `safe_c.h` → `malloc_s`/`calloc_s`/
  `cplus_memcpy_s_auto`, `cplus_libc_safe.h` → `cplus_malloc`/`cplus_calloc`/
  `cplus_memcpy_bounded`. The safe files never use a raw game/library offset.
- SafeCRT's violation trap is enabled by default and would call `_exit(70)` on
  a capacity violation. Each safe file calls
  `cplus_runtime_set_violation_trap(0)` under `CPLUS_SAFE_C_ENABLE` (the
  documented mode for inspecting NULL/ERANGE results) so a refusal is reported
  by the program and exits 0. No unguarded write is performed in that path.
- No genuine UB remains in the safe files: all bad inputs are rejected before
  narrowing, subtraction, or indexing, and the one write deliberately allowed
  to reach the hardened API (`case_10`, payload 76) is refused before copying.
- Reproduce safe results with the verify commands from the task; run each
  binary both with the bad input above and with no stdin (EOF → defined
  `refused` outcome, exit 0).
