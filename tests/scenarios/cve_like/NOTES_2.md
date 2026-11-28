# Safe variants and evidence — cases 3, 4, 14 (realloc failure / stale pointer / cursor range)

Files produced (only these were created/modified):

- `tests/scenarios/cve_like/case_03_safe.c`
- `tests/scenarios/cve_like/case_04_safe.c`
- `tests/scenarios/cve_like/case_14_safe.c`
- `tests/scenarios/cve_like/NOTES_2.md` (this file)

Each safe file starts with `/* BASELINE: case_NN.c crashes; SAFE: defined refusal/valid result. */`,
uses ordinary C spellings with a safe header (`stdlib/safe/safe_c.h` by default,
`cplus_libc_safe.h` when compiled with `-DPLAIN_LIBC`), prints an outcome line,
and contains no genuine UB.

## Baseline evidence (original files, ASan)

Compiled with:

```
clang -fsanitize=address -g -std=c99 \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  tests/scenarios/cve_like/case_NN.c -o /tmp/cNN_base
```

fed the example input from the scenario, exit code and first ASan ERROR line:

| case | input | exit | first ASan ERROR line |
|------|-------|------|------------------------|
| 03 | `8` | **134** | `ERROR: AddressSanitizer: SEGV on unknown address 0x0000deadbedf` — `free()` of forged `0xDEADBEEF` (`case_03.c:10`) |
| 04 | `1000` | **134** | `ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000970` — read of `tok.start[0]` after `realloc()` moved the block (`case_04.c:11`) |
| 14 | `100` | **134** | `ERROR: AddressSanitizer: heap-buffer-overflow on address 0x611000000360` — 8-byte read 544 bytes past the 256-byte ring (`case_14.c:8`) |

**Environment caveat:** the requested `/usr/bin/clang` is Apple clang 16.0.0 on
macOS 27.2.1; its ASan runtime aborts at startup on *every* program with
`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)"`
(exit 134, even for `printf("hi")`). The baseline evidence above therefore uses
the equivalent `/opt/homebrew/opt/llvm/bin/clang` (Homebrew clang 23.1.6) with
the same `-fsanitize=address` and SDK flags. The safe-variant verification below
uses `/usr/bin/clang` exactly as requested.

## Safe results (both layers)

Libraries built first with `C_INCLUDE_PATH=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include` prefixed to both
`sh tests/libc/build_libc.sh` and `sh scripts/build_safecrt.sh` (needed because
the scripts' compiler defaults to a missing `MacOSX27.sdk`; build otherwise
succeeds with sysroot warnings only).

SafeCRT layer command (per the requested pattern, plus the available SDK):

```
/usr/bin/clang -O1 -g -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  tests/scenarios/cve_like/case_NN_safe.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread -o /tmp/cNNs && /tmp/cNNs
```

Plain-libc layer: same command with `-DPLAIN_LIBC`, linking only
`build/libc/libcplus_hardened.a -lm -pthread` (no SafeCRT archive).

| case | input | SafeCRT result (exit) | PLAIN_LIBC result (exit) | mechanism that caught it | residual limitation |
|------|-------|----------------------|--------------------------|--------------------------|---------------------|
| 03 | `8` | injected-failure refusal: original pointer, contents **and generation** preserved, then `result: ok capacity=8 (new base used; no forged address)` (**0**) | same two lines (**0**) | temp variable for `realloc`; `tmp == NULL` checked **before** owner replacement; allocator's `fail_after(0)` injection + generation/metadata readback confirms preservation; only live pointers freed | Cannot prove *all* future realloc failures are handled — overflow guard is explicit, OOM is checked; a caller that ignores `realloc == NULL` is still unsafe |
| 03 | `18446744073709551615` | `result: refusal - requested capacity overflows a safe byte count` (**0**) | same (**0**) | explicit `nc > (SIZE_MAX >> 1)/sizeof(uint64_t)` guard before `realloc` | Guard assumes 64-bit `size_t`/`uint64_t`; on other ABIs re-check the bound |
| 04 | `1000` | `result: refusal - stale token alias detected by generation check`, then `byte (re-derived): h`, `result: ok - byte read from new base; stale alias not dereferenced` (**0**) | same three lines (**0**) | generation check (`cplus_safe_ptr_check_generation` / `cplus_alloc_check_generation`) with the pre-realloc generation; token re-derived as `new base + saved offset` with a range check | Generation check only covers blocks from the tracked allocator; pointer provenance of arbitrary raw pointers is not provable. In-place realloc correctly yields a conservative "stale" refusal (generation bumps even when the address is unchanged) |
| 14 | `100` | `slot: 0`, `result: ok cursor=4 (modulo wrap, in-range access)` (**0**) | same two lines (**0**) | `cursor = (cursor + skip) % capacity` replaces equality-only reset; overflow guard on `cursor + skip`; explicit `cursor >= capacity` check before indexed read | Modulo is valid only for a non-zero, fixed capacity; a resized ring must re-derive the cursor (not addressed by this scenario) |

Additional edge probes (SafeCRT + ASan build of the safe files, all exit 0, no ASan report):

- case 03: `cap=40` → refusal line + `ok capacity=40`; `cap=huge` → overflow refusal.
- case 04: `extra=0` → stale-alias refusal + `byte (re-derived): h`; `extra=SIZE_MAX` → "grow size overflows" refusal.
- case 14: `skip=0,32,64` → `cursor=0`; `skip=SIZE_MAX` → `cursor=31`, all in range.

The safe files were also compiled with ASan (`clang 23 -fsanitize=address`, same
includes/libs) and run with the baseline inputs above: **no ASan error, exit 0**
in all three cases.

## Warnings / caveats

1. `/usr/bin/clang`'s ASan is unusable on this host (startup CHECK failure); baseline
   ASan evidence uses Homebrew clang 23 instead. Non-ASan builds use `/usr/bin/clang`.
2. `tests/libc/build_libc.sh` and `scripts/build_safecrt.sh` alternatively need
   `C_INCLUDE_PATH` pointed at the available `MacOSX.sdk/usr/include` because the
   scripts' compiler looks for a non-existent `MacOSX27.sdk`; only sysroot warnings remain.
3. Case 03's failure path is exercised deterministically via the allocator's
   injection API on a scratch block; this verifies preservation but does not
   simulate a real OS-level OOM. The real user-supplied capacity still goes
   through the temp-then-check path in the same run.
4. Generation checks in case 04 are conservative: a successful in-place realloc
   is reported as "stale" because both allocators bump the generation on every
   successful realloc. This is a defined refusal, never an unsafe dereference.
5. No git staging/commit was performed; no files outside the four listed above
   were created or modified.
