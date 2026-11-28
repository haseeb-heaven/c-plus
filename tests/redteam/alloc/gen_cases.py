#!/usr/bin/env python3
"""Deterministic adversarial case generator for the checked-allocator red team.

Emits one manifest line per case to results/cases.manifest:

    <idx> <family_id> <seed_hex> <p1_hex> <p2_hex> <p3_hex> <p4_hex>

All numeric fields are hexadecimal uint64 values.  The harness reads this
manifest, so the generator is the single source of truth for what is tested.
A fixed PRNG seed makes the entire corpus reproducible byte-for-byte.
"""
import os
import sys

SEED = 0x9E3779B97F4A7C15  # fixed, never from environment/time
SZMAX = (1 << 64) - 1
RSIZE = SZMAX >> 1  # RSIZE_MAX_MEM default (SIZE_MAX >> 1)

FAMILIES = [
    "OVR_MALLOC",       # 0  malloc_s(size > RSIZE_MAX_MEM)
    "OVR_CALLOC",       # 1  calloc_s(count,size) overflow
    "OVR_REALLOC",      # 2  realloc_s(ptr, huge) atomicity
    "ZERO_SIZE",        # 3  zero-size edge cases
    "ALIGN_BAD",        # 4  misaligned / non-power-of-two alignment
    "ALIGN_OK",         # 5  valid alignment really honoured
    "DOUBLE_FREE",      # 6  double free must be refused
    "INVALID_PTR",      # 7  forged / interior / static pointers
    "REALLOC_ATOMIC",   # 8  realloc failure preserves original
    "META_CONSISTENCY", # 9  metadata query correctness
    "QUERY_NULL",       # 10 null out-params / null pointers
    "CYCLES",           # 11 metadata cache exhaustion
    "STALE_GEN",        # 12 use-after-free via stale generation
    "INJECT_ALL",       # 13 failure injection at every stage
    "INTERIOR_CAP",     # 14 remaining_capacity interior pointers
    "FORGED_BATCH",     # 15 many fabricated pointers in one child
    "MIXED",            # 16 randomized mixed op sequence + invariants
    "REALLOC_ELEM",     # 17 realloc preserves elem_size / elem_count
    "CACHE_EXHAUST",    # 18 >1024 distinct addresses then double-free each
    "HARDENED",         # 19 direct attacks on the hardened-archive allocator
]
NFAM = len(FAMILIES)

MASK = SZMAX


class Rng:
    """xorshift64* — deterministic, platform independent."""

    def __init__(self, seed):
        self.s = seed & MASK

    def next(self):
        z = (self.s + 0x9E3779B97F4A7C15) & MASK
        self.s = z
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)

    def pick(self, seq):
        return seq[self.next() % len(seq)]


MALLOC_BAD = [
    SZMAX, SZMAX - 1, SZMAX - 2, RSIZE + 1, RSIZE + 2,
    RSIZE + 0x1000, 0xFFFFFFFFFFFFFFF0, 0x8000000000000001,
    RSIZE,  # RSIZE itself is allowed -> tested separately; kept out below
]
MALLOC_BAD = [x for x in MALLOC_BAD if x > RSIZE]

CALLOC_PAIRS = [
    (SZMAX, 2), (2, SZMAX), (SZMAX, SZMAX), (SZMAX // 2 + 1, 2),
    (SZMAX // 2, 2), (1, RSIZE + 1), (RSIZE + 1, 1),
    (0, SZMAX), (SZMAX, 0), (0, 0), (1, 1), (1000000, 64),
    (SZMAX // 3 + 1, 3), ((SZMAX >> 2) + 1, 4), (SZMAX // 64 + 1, 64),
    (4096, 4096), (1 << 40, 1 << 24),
]

BAD_ALIGN = [0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 12, 24, 48,
             0x8000000000000001, (1 << 63) + 2, 100, 200]
GOOD_ALIGN = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
SMALL_SIZES = [1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 1000, 4096]
K_VALUES = [0, 1, 2, 3, 5, 8]


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 6000
    here = os.path.dirname(os.path.abspath(__file__))
    res = os.path.join(here, "results")
    os.makedirs(res, exist_ok=True)
    out_path = os.path.join(res, "cases.manifest")
    with open(out_path, "w") as out:
        for i in range(n):
            fam = i % NFAM
            r = Rng((SEED ^ ((i + 1) * 0xD1B54A32D192ED03)) & MASK)
            seed = r.next()
            p = [0, 0, 0, 0]
            if fam == 0:            # OVR_MALLOC
                p[0] = r.pick(MALLOC_BAD)
            elif fam == 1:          # OVR_CALLOC
                c, s = r.pick(CALLOC_PAIRS)
                p[0], p[1] = c, s
            elif fam == 2:          # OVR_REALLOC
                p[0] = r.pick(MALLOC_BAD)
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 3:          # ZERO_SIZE
                p[0] = r.next() % 5
                p[1] = r.pick(GOOD_ALIGN)
            elif fam == 4:          # ALIGN_BAD
                p[0] = r.pick(BAD_ALIGN)
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 5:          # ALIGN_OK
                p[0] = r.pick(GOOD_ALIGN)
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 6:          # DOUBLE_FREE
                p[0] = r.pick(SMALL_SIZES)
                p[1] = r.next() % 3
            elif fam == 7:          # INVALID_PTR
                p[0] = r.next() % 8
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 8:          # REALLOC_ATOMIC
                p[0] = r.next() % 4
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 9:          # META_CONSISTENCY
                p[0] = r.next() % 4
                p[1] = r.pick(SMALL_SIZES)
                p[2] = r.next() % 16 + 1
                p[3] = r.pick([1, 2, 4, 8, 16, 32])
            elif fam == 10:         # QUERY_NULL
                p[0] = r.next() % 6
            elif fam == 11:         # CYCLES
                p[0] = 200 + (r.next() % 600)
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 12:         # STALE_GEN
                p[0] = r.pick(SMALL_SIZES)
                p[1] = r.pick(SMALL_SIZES)
            elif fam == 13:         # INJECT_ALL
                p[0] = r.pick(K_VALUES)
            elif fam == 14:         # INTERIOR_CAP
                p[0] = r.pick([16, 32, 64, 128, 256])
            elif fam == 15:         # FORGED_BATCH
                p[0] = 20 + (r.next() % 100)
            elif fam == 16:         # MIXED
                p[0] = 100 + (r.next() % 300)
                p[1] = r.next()
            elif fam == 17:         # REALLOC_ELEM
                p[0] = r.next() % 16 + 1          # count
                p[1] = r.pick([1, 2, 4, 8, 16])   # elem size
                p[2] = r.pick(SMALL_SIZES)        # new byte size
            elif fam == 18:         # CACHE_EXHAUST
                p[0] = 1100 + (r.next() % 500)    # distinct live blocks
            elif fam == 19:         # HARDENED
                p[0] = r.next() % 5               # subcase
                p[1] = r.pick(SMALL_SIZES)
            out.write("%d %d %016x %016x %016x %016x %016x\n" %
                      (i, fam, seed, p[0], p[1], p[2], p[3]))
    print("generated %d cases -> %s" % (n, out_path))


if __name__ == "__main__":
    main()
