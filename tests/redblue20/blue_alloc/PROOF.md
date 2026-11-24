# BLUE20 alloc — modern allocator safety proof (agent 11/20)

**Verdict: `BLUE20 alloc 2400 2400 0` — all 2400 cases PASS, 0 failures.**

## Scope

Proof target (modern layer only, no source edits made):

- `stdlib/alloc_iface.hp` (allocator vtable / dispatch)
- `stdlib/arena.hp` (chained bump arena + generation handles)
- `stdlib/mempool.hp` (slab MemoryPool + Goose-style slot_pool)
- `stdlib/datastack.hp` (thread scratch stack, heap + VM backing)

Deliverables (all under `tests/redblue20/blue_alloc/`):

- `proof.cp` — the deterministic harness
- `run.sh` — build + run + verify orchestrator
- `out/` — generated binary, build log, per-case log, digest (gitignored artifacts)

## Method

- **2400 fixed-seed cases**: SplitMix64 `SEED = 0xA11CE00000DEAD55`, per-case
  seed `SEED ^ ((id+1) * 0x9E3779B97F4A7C15)`. No clock, PID, or external input.
- **8 categories × 300 cases** (case id → category `id / 300`).
- **fork-per-case**: parent forks; child installs `alarm(60)`, fills a 32-byte /
  2048-byte / 32-byte `0xA5` canary band, runs one case, re-checks the canary,
  writes the token to a pipe, then `_exit(0 | 1)`.
- **Exact token**: a case counts as PASS only if the child exits 0 **and** writes
  exactly `BLUE20_ALLOC_PASS <id> <CATEGORY>`. Signals, timeouts, wrong tokens,
  canary overwrites, and partial reads are all FAIL. The parent writes one
  `PASS <token>` / `FAIL ...` record per case to `out/results.log`.
- **Bounded**: `alarm(60)` per child; `timeout 60` around build and around the
  harness in `run.sh`. Measured wall time ≈ 6 s (well under the bound).

## Coverage (what each category actually asserts)

| Cat | Name | Assertions |
|-----|------|------------|
| 0 | ALIGN | arena + VM/heap datastack allocations at `align ∈ {1,2,4,8,16}` satisfy `(uintptr_t)p & (align-1) == 0`; written payloads survive; pool slot is 16-byte aligned (slot size 16/32/48 so the documented stride guarantee holds). |
| 1 | ARENA | 24×32-byte allocations from a 32–63-byte-capacity arena force ≥3 chained blocks (`block->prev` walk); pointers are pairwise non-overlapping; per-block fill patterns are intact across chains; `mark()` → `do_reset()` bumps `generation`, invalidates the old handle, and rewinds `used`/`remaining`; post-reset allocation still works. |
| 2 | BUF | `from_buffer` with 512–1024 bytes of caller storage: the first block has `owns == 0` / `owns_first == 0` (structural proof it can never be `cplus_release`d), allocation lands inside the caller range, data survives, `destroy()` clears the arena without freeing caller storage, and the buffer is still writable afterwards. Canaries catch out-of-range writes. |
| 3 | POOL | double release of the same slot is refused with code **104** (`CPLUS_EXIT_DOUBLE_FREE`) and does not change `live`; a foreign pointer (zeroed readable buffer) and a pointer from a different pool are both refused with code **106** (`CPLUS_EXIT_OWNERSHIP`); counts unchanged; the freed slot is reused. |
| 4 | SLOT | a stale `stdlib_slot_ref` passed to `get()` is refused with code **103** (`CPLUS_EXIT_USE_AFTER_FREE`) and returns NULL; passing the stale ref to `free_ref()` is refused with code **104**; neither refusal mutates `free_count`/`count`; slot index 0 is reused with a bumped generation and resolves correctly. |
| 5 | SCOPE | nested `scratch_enter` / `scratch_leave`: inner leave restores `top` exactly and the next allocation reuses the same address; the outer allocation's 0x7E payload is intact; outer leave restores `top` to `base`; a stale mark taken before `destroy` is ignored after the generation bump. |
| 6 | REALLOC | top-of-stack `arena_realloc` and `datastack_realloc` grow 64→256 bytes **in place** (returned pointer identical), preserve payload bytes, advance `cursor`/`top` by exactly 192; shrink 256→128 also stays in place and moves `cursor`/`top` back by 128. |
| 7 | VM | `init_vm` starts with `committed == 0`; first 1-byte bump commits exactly one page; a second same-page bump and `ensure(need <= committed)` do not change `committed`; a bump crossing the page boundary commits exactly a second page; `top - base` accounting is exact; `destroy` clears state. |

## Exact commands and outputs

Build (from repo root, exact required invocation; the archive is forwarded to
the linker after `--`):

```
$ timeout 60 ./build/c+ build tests/redblue20/blue_alloc/proof.cp \
    --default --sanitize=none -I. \
    -o tests/redblue20/blue_alloc/out/proof \
    -- build/libc/libcplus_hardened.a
$ echo $?
0
```

Build emits 10 **non-fatal** static-analysis warnings (header-internal provenance /
division-by-zero false positives, one harness `argv` extent warning, one false
"log leaks" warning on the `fopen`-failed early return). No errors; binary is
120952 bytes. Full text: `out/build.log`.

Run and verification:

```
$ timeout 60 tests/redblue20/blue_alloc/out/proof tests/redblue20/blue_alloc/out/results.log
BLUE20_DIGEST 6e135418fe29170b
BLUE20 alloc 2400 2400 0
$ echo $?
0
$ awk '{print $1}' out/results.log | sort | uniq -c
2400 PASS
```

Full orchestrator (includes build, per-category counts, sha256):

```
$ tests/redblue20/blue_alloc/run.sh
BLUE20: root=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
BLUE20: compiler=C+ 0.1.0
BLUE20: cases=2400 (300/category x 8), seed=0xA11CE00000DEAD55, per-case alarm=60s
+ timeout 60 ./build/c+ build tests/redblue20/blue_alloc/proof.cp --default --sanitize=none -I. -o tests/redblue20/blue_alloc/out/proof -- build/libc/libcplus_hardened.a
BLUE20: build rc=0, binary=tests/redblue20/blue_alloc/out/proof (  120952 bytes)
+ timeout 60 tests/redblue20/blue_alloc/out/proof tests/redblue20/blue_alloc/out/results.log
BLUE20: run rc=0
BLUE20: summary=BLUE20 alloc 2400 2400 0
BLUE20: digest=6e135418fe29170b
BLUE20: pass lines=2400, other lines=0
BLUE20: per-category:
  ALIGN    300
  ARENA    300
  BUF      300
  POOL     300
  REALLOC  300
  SCOPE    300
  SLOT     300
  VM       300
BLUE20: log sha256=86653617e3631f4ba40be88b812ca9cee22eabf37d29037b70b7243b3c54f49e
BLUE20 alloc 2400 2400 0
$ echo $?
0
```

Integrity:

- Per-case log: `out/results.log`, 2400 lines, all `PASS BLUE20_ALLOC_PASS <id> <CAT>`.
- SHA-256: `86653617e3631f4ba40be88b812ca9cee22eabf37d29037b70b7243b3c54f49e`
  (also in `out/digest.sha256`).
- FNV-1a over all case records: `6e135418fe29170b`.
- Two independent runs produced byte-identical logs and the same digest.

## Honest limitations / adjacent observations (outside the enumerated proof scope)

These are **not** claimed as proven safe by this suite; they are reported to the
parent so the red/blue comparison is complete. None of them is exercised by the
enumerated properties above, which is why all 2400 scoped cases still pass.

1. **`stdlib_slot_ref_get()` on a valid ref with `index > 0` aborts.** It calls
   `cplus_check_live(items + index*slot_size)`, but `cplus_find_live()` matches
   exact base pointers only, so any interior pointer is treated as dangling and
   triggers `CPLUS_EXIT_TYPE_CONFUSION` (105). Observed in a local probe:
   `get r1 (index 1)` terminated with exit code 105. The proof only tests the
   enumerated stale-handle (103) / double-free (104) refusal paths, which return
   before that call. Index 0 resolves correctly.
2. **Pool slot alignment depends on `slot_size`.** Slot stride is
   `STDLIB_POOL_HEADER_SIZE (32) + slot_size`; only when `slot_size % 16 == 0` is
   every slot 16-byte aligned. With `slot_size == 8`, slots alternate between
   0 and 8 modulo 16 (observed: `...ec8`, `...ea0`, `...e78`, `...e50`). The ALIGN
   category uses 16-multiple slot sizes, matching the header's documented
   guarantee; non-multiple slot sizes are not part of this proof.
3. **The hardened archive is forwarded but not pulled in.** The C+ driver always
   links `libcplus_runtime.a`, which also defines `cplus_realloc`; referencing a
   hardened-only diagnostic (e.g. `cplus_alloc_live_count`) creates a duplicate
   symbol. Consequently caller-storage non-ownership is proved structurally from
   the arena's `owns`/`owns_first` flags rather than via a heap live-count delta.
   The modern allocators themselves are header-only (`static inline`) and are
   fully compiled into the proof binary regardless.

## Final line

```
BLUE20 alloc 2400 2400 0
```
