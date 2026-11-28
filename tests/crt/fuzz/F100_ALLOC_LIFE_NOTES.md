# F-021..F-040 — deterministic allocation / lifecycle fuzz configurations

Scope: allocation and lifecycle APIs only — SafeCRT `malloc_s`, `calloc_s`,
`realloc_s`, `free_s`, `free_checked_s`, `aligned_alloc_s`, metadata/
ownership helpers, and the libc `cplus_malloc` / `cplus_calloc` /
`cplus_realloc` / `cplus_free` / `cplus_free_checked` / `cplus_aligned_alloc`
equivalents.

All added files are under `tests/crt/fuzz/` and use the `f100_alloc_` /
`f100_life_` prefixes (plus this notes file). No headers, archives, build
scripts or other source files were changed.

## Config map

| ID | File | API under test |
|----|------|----------------|
| F-021 | `f100_alloc_f021_malloc.c` | `malloc_s` size sweep 0..4096, zero-size normalization, metadata round-trip |
| F-022 | `f100_alloc_f022_calloc.c` | `calloc_s` count·size products at overflow edge, zero normalization, zero fill |
| F-023 | `f100_alloc_f023_realloc.c` | `realloc_s` grow/shrink/zero, injection, owner + generation preservation |
| F-024 | `f100_alloc_f024_free.c` | `free_s` / `free_checked_s` double- and invalid-free refusal, repeated drop |
| F-025 | `f100_alloc_f025_aligned.c` | `aligned_alloc_s` power-of-two alignments, invalid-alignment refusal |
| F-026 | `f100_alloc_f026_sidetable.c` | SafeCRT side table: base/interior/one-past-end, generation, untracked stack pointer |
| F-027 | `f100_alloc_f027_transfer.c` | `cplus_safe_transfer_ownership` pointer-to-pointer, generation bumps, stale refusal |
| F-028 | `f100_alloc_f028_injection.c` | `cplus_safe_alloc_fail_after/reset` at fixed sequence points via `cplus_safe_alloc_seq` |
| F-029 | `f100_alloc_f029_canary.c` | SafeCRT tracked-size boundary vs mmap guard pages in forked children |
| F-030 | `f100_life_f030_cplus_malloc.c` | `cplus_malloc` sweep 0..4096, `cplus_alloc_size`, zero normalization |
| F-031 | `f100_life_f031_cplus_calloc.c` | `cplus_calloc` overflow edge + zero normalization |
| F-032 | `f100_life_f032_cplus_realloc.c` | `cplus_realloc` resize + injection, owner/generation preservation |
| F-033 | `f100_life_f033_cplus_free.c` | `cplus_free` / `cplus_free_checked` double/invalid/interior refusal, pointer-to-pointer cleanup |
| F-034 | `f100_life_f034_cplus_aligned.c` | `cplus_aligned_alloc` alignment + side-table registration (**DEFECT — see below**) |
| F-035 | `f100_life_f035_cplus_meta.c` | libc side table: `get_meta`, `remaining_capacity`, `query_length`, generation |
| F-036 | `f100_life_f036_cplus_transfer.c` | `cplus_transfer_ownership`, stale-alias refusal, empty-handle repeated drop |
| F-037 | `f100_life_f037_cplus_injection.c` | `cplus_alloc_fail_after/reset` with explicit allocation-attempt counting |
| F-038 | `f100_life_f038_repeated_drop.c` | mixed SafeCRT + libc repeated alloc/release, balance + generation checks |
| F-039 | `f100_life_f039_canary.c` | libc tracked-size boundary vs mmap guard pages in forked children |
| F-040 | `f100_life_f040_custom_alloc_skip.c` | explicit SKIP (no custom allocator plug-in) + full deterministic mixed sweep |

Shared support: `f100_alloc_harness.h` (LCG, check counters, canary fork helper),
`f100_alloc_life_run.sh` (build + run driver).

## Determinism and safety properties

- Fixed-seed LCG (`6364136223846793005` / `1442695040888963407`,
  high 32 bits), unique per config (`F100_SEED ^ 0x0NN`). No time, randomness,
  stdin or environment-driven branching.
- Every config performs `F100_OPS = 1200` operations and asserts an explicit
  `op_count` check. Sizes are always in the closed range `0..4096`.
- Near-overflow products use constants only (`SIZE_MAX`, `SIZE_MAX/2`,
  `SIZE_MAX/2+1`, `RSIZE_MAX`, `RSIZE_MAX+1`, etc.); the multiplication under
  test is performed by the API, never by the harness — no UB in the driver.
- Injection is applied only at deterministic op indices (127, 101); the
  SafeCRT config also verifies the exact `cplus_safe_alloc_seq()` values.
- Double-free / stale pointers are **never dereferenced**; only side-table
  queries (`is_live`, `was_freed`, `get_meta`, `check_generation`,
  `remaining_capacity`) and refusal return codes are used.
- Canary pages: `mmap` two pages, `mprotect` the second `PROT_NONE`, all inside
  forked children. Integrity child copies back exactly the API-reported tracked
  size; a second child deliberately writes one byte into the guard page and the
  parent requires it to die by `SIGSEGV` or `SIGBUS` (macOS/arm64 uses SIGBUS).
  UBSan's fatal-signal handler is reset to `SIG_DFL` in the guard child so the
  real signal is observable. The parent only calls `waitpid`.
- Custom allocator: no plug-in point exists in `safe_alloc.h` or
  `cplus_libc.h` (allocation is hard-wired to the backend allocator). F-040
  emits `SKIP F-040 custom allocator: ...` and `SKIPPED_CUSTOM_ALLOC=1`, then
  still runs the full 1200-op mixed lifecycle sweep.

## Verification commands (exact)

Driver (builds all 20, runs each under `timeout 30`):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
./tests/crt/fuzz/f100_alloc_life_run.sh
```

It prints the exact `BUILD_CMD[...]` and `RUN_CMD[...]` lines for every config.
The three families are:

```sh
# SafeCRT configs (F-021..F-029)
/usr/bin/clang -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -std=c11 \
  -Istdlib/safe -Ithird_party/safeclib/include \
  -Ithird_party/intel-safestringlib/include \
  tests/crt/fuzz/f100_alloc_f021_malloc.c \
  build/safe/libcplus_safecrt.a -pthread -o /tmp/F-021
timeout 30 /tmp/F-021

# libc configs (F-030..F-037, F-039)
/usr/bin/clang -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -std=c11 \
  -Ilibc/install-include \
  tests/crt/fuzz/f100_life_f034_cplus_aligned.c \
  build/libc/libcplus_hardened.a -lm -pthread -o /tmp/F-034
timeout 30 /tmp/F-034

# mixed configs (F-038, F-040)
/usr/bin/clang -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -std=c11 \
  -Istdlib/safe -Ithird_party/safeclib/include \
  -Ithird_party/intel-safestringlib/include -Ilibc/install-include \
  tests/crt/fuzz/f100_life_f038_repeated_drop.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/F-038
timeout 30 /tmp/F-038
```

Toolchain used: Apple clang 16.0.0 (`/usr/bin/clang`), target
`arm64-apple-darwin27.2.0`. Archives used as checked in:
`build/safe/libcplus_safecrt.a`, `build/libc/libcplus_hardened.a`.

## Results (run of 2026-11-23, repeated identically a second time)

```
SUMMARY F100 passes=19 defects=1 fails=0
driver exit status: 1 (nonzero because one real DEFECT was detected)
sanitizer diagnostics (runtime error: / DEADLYSIGNAL): 0
BUILD_CMD lines: 20 ; RUN_CMD lines: 20
normalized output of two independent full runs: identical
```

| ID | Result | Checks / note |
|----|--------|---------------|
| F-021 | PASS | CHECKS=6002 FAILS=0 |
| F-022 | PASS | CHECKS=5950 FAILS=0 |
| F-023 | PASS | CHECKS=3632 FAILS=0 |
| F-024 | PASS | CHECKS=2168 FAILS=0 |
| F-025 | PASS | CHECKS=4787 FAILS=0 |
| F-026 | PASS | CHECKS=13203 FAILS=0 |
| F-027 | PASS | CHECKS=10802 FAILS=0 |
| F-028 | PASS | CHECKS=1275 FAILS=0 |
| F-029 | PASS | CANARY_INTEGRITY=1 GUARD_DETECTED=1; CHECKS=2403 FAILS=0 |
| F-030 | PASS | CHECKS=6002 FAILS=0 |
| F-031 | PASS | CHECKS=4766 FAILS=0 |
| F-032 | PASS | CHECKS=3632 FAILS=0 |
| F-033 | PASS | CHECKS=2642 FAILS=0 |
| F-034 | **DEFECT** | `DEFECT_CONFIRMED=1`; exit 2 (distinct from harness failure) |
| F-035 | PASS | CHECKS=13206 FAILS=0 |
| F-036 | PASS | CHECKS=8402 FAILS=0 |
| F-037 | PASS | CHECKS=1263 FAILS=0 |
| F-038 | PASS | CHECKS=7203 FAILS=0 |
| F-039 | PASS | CANARY_INTEGRITY=1 GUARD_DETECTED=1; CHECKS=2403 FAILS=0 |
| F-040 | PASS (SKIP custom allocator) | CHECKS=2403 FAILS=0; `SKIPPED_CUSTOM_ALLOC=1` |

Two initial failures were harness bugs, now fixed and re-verified: (1) `count==0`
callout products were misclassified as overflow instead of zero-size
normalization; (2) macOS guard-page writes raise `SIGBUS`, not `SIGSEGV`.

## Real defect: libc `cplus_aligned_alloc` is absent from the hardened side table

Config F-034 exits 2 with:

```
F-034 PROBE p=0x... alignment_ok=1 live=0 get_meta=22 size=0 remaining=18446744073709551615
DEFECT F-034 cplus_aligned_alloc returns memory that is absent from the hardened allocation side table
```

Minimal repro (no repo files created):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
cat > /tmp/f100_align_repro.c <<'EOF'
#include <stdio.h>
#include "cplus_libc.h"
int main(void) {
    cplus_alloc_meta m;
    void *p = cplus_aligned_alloc(64, 4096);
    printf("p=%p live=%d get_meta=%d size=%zu rem=%zu\n", p,
           cplus_alloc_is_live(p), cplus_alloc_get_meta(p, &m),
           cplus_alloc_size(p), cplus_alloc_remaining_capacity(p));
    unsigned long inv = cplus_alloc_invalid_free_count();
    cplus_free(p);
    printf("invalid_frees %lu -> %lu\n", inv, cplus_alloc_invalid_free_count());
    return 0;
}
EOF
/usr/bin/clang -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -std=c11 \
  -Ilibc/install-include /tmp/f100_align_repro.c \
  build/libc/libcplus_hardened.a -lm -pthread -o /tmp/f100_align_repro
timeout 30 /tmp/f100_align_repro
```

Observed output:

```
p=0x... live=0 get_meta=22 size=0 rem=18446744073709551615
invalid_frees 0 -> 1
```

Root cause: `libc/src/stdlib/stdlib_c17.c:53-70` calls the platform
`aligned_alloc()` directly rather than the tracked `cplus_malloc` path, so
`meta_insert()` is never invoked. Consequences:

- `cplus_alloc_is_live(p)==0`, `cplus_alloc_size(p)==0`,
  `cplus_alloc_get_meta(p)` returns `EINVAL`(22),
  `cplus_alloc_remaining_capacity(p)==(size_t)-1`.
- `cplus_free(p)` is refuted as an invalid free and does **not** call `free()`,
  so the block leaks (confirmed by `cplus_alloc_invalid_free_count()` 0→1).
- `cplus_realloc(p, ...)` likewise refuses a block from this API.

Secondary conformance note (not counted as the defect): this API requires
`size % alignment == 0` (C11 wording), whereas SafeCRT `aligned_alloc_s`
accepts any size. F-034 uses multiples of the alignment so it tests the
intended contract.

No fix was applied — strict file ownership for this task forbids editing
`libc/` source or headers. The defect is reported to the parent session with
the repro above.
