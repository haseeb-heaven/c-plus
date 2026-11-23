#!/usr/bin/env python3
"""
attack_cases.py -- deterministic adversarial case generator for the REDTEAM
memstr campaign.

Emits results/attack_cases.tsv: one case per line with fully concrete
parameters.  Fixed seed => byte-identical output every run; no wall clock, no
ASLR-dependent values.  The harness (attack_harness.c) reads this TSV and runs
each case in a forked child.

Columns (tab separated):
  id fam api cas cap dm n L O value extra
  id    : 1-based case index
  fam   : 0 = cplus_sc_* (safeclib-backed), 1 = cplus_*_s (adapter)
  api   : 0..11 index into API_NAMES
  cas   : attack class index (see CAS_NAMES)
  cap   : declared/physical live capacity of dest (8..256)
  dm    : dmax argument (decimal; may be 0 / SIZE_MAX / RSIZE_MAX+1 / cap+k)
  n     : n/slen argument (decimal; may be 0 / SIZE_MAX / cap+1)
  L     : PRNG value used for source content length / filler
  O     : PRNG value used for overlap offset / dmax delta
  value : memset fill value (may exceed 255)
  extra : reserved (embedded-NUL position)
"""
import os
import random
import sys

SEED = 0xC0FFEE1234
NCASES = 6000                      # >= 5000 required

API_NAMES = [
    "memcpy_s", "memmove_s", "memset_s", "memcmp_s", "memzero_s",
    "strcpy_s", "strncpy_s", "strcat_s", "strncat_s", "strcmp_s",
    "strnlen_s", "strtok_s",
]
CAS_NAMES = [
    "DMAX_ZERO", "NULL_DEST", "NULL_SRC", "ONE_OVER", "HUGE_N",
    "HUGE_DMAX", "OVERLAP", "UNTERMINATED", "DMAX_GT_CAP",
    "DMAX_GT_CAP_ONE", "VALUE_GT255", "DIFF_NULL", "EMBEDDED_NUL",
]

# Which attack classes are meaningful for each API.
APPLICABLE = {
    "memcpy_s":  [0, 1, 2, 3, 4, 5, 6, 8, 9, 12],
    "memmove_s": [0, 1, 2, 3, 4, 5, 8, 9, 12],
    "memset_s":  [0, 1, 3, 4, 5, 10, 12],
    "memcmp_s":  [0, 1, 2, 3, 4, 5, 8, 9, 11, 12],
    "memzero_s": [0, 1, 5, 12],
    "strcpy_s":  [0, 1, 2, 3, 5, 6, 8, 9, 12],
    "strncpy_s": [0, 1, 2, 3, 4, 5, 6, 8, 9, 12],
    "strcat_s":  [0, 1, 2, 3, 5, 7, 8, 9, 12],
    "strncat_s": [0, 1, 2, 3, 4, 5, 7, 8, 9, 12],
    "strcmp_s":  [0, 1, 2, 3, 5, 11, 12],
    "strnlen_s": [0, 1, 5, 12],
    "strtok_s":  [0, 1, 5, 12],
}

SIZE_MAX = (1 << 64) - 1
RSIZE_MAX = SIZE_MAX >> 1           # header-documented max (not the enforced one)
ENFORCED_MAX = 16 * 1024            # safeclib compiled RSIZE_MAX_MEM = 4<<10*4


def params(api, cas, cap, L, O, rng):
    """Return (dm, n, value, extra) concrete arguments for one case."""
    dm = cap
    n = 1 + (L % max(1, cap))
    value = 0x5A
    extra = 1 + (L % 8)

    if cas == 0:                       # DMAX_ZERO
        dm = 0
    elif cas == 1:                     # NULL_DEST
        dm = cap
    elif cas == 2:                     # NULL_SRC
        dm = cap
    elif cas == 3:                     # ONE_OVER
        dm = cap
        n = cap + 1 + (O % 8)
    elif cas == 4:                     # HUGE_N
        dm = cap
        n = SIZE_MAX if (L & 1) else RSIZE_MAX + 1
    elif cas == 5:                     # HUGE_DMAX
        dm = SIZE_MAX if (L & 1) else RSIZE_MAX + 1
        n = 1 + (L % max(1, min(cap, 8)))
    elif cas == 6:                     # OVERLAP
        dm = cap
        n = 1 + (O % 8) + 2            # > offset so the copy reaches the overlap
    elif cas == 7:                     # UNTERMINATED
        dm = cap
        n = 1 + (L % max(1, cap))
    elif cas == 8:                     # DMAX_GT_CAP (wild memset on violation)
        delta = 1 + (O % max(1, ENFORCED_MAX - cap))
        dm = cap + delta               # still <= enforced max (accepted)
        n = 1 + (L % max(1, min(cap, 8)))
    elif cas == 9:                     # DMAX_GT_CAP_ONE
        dm = cap + 1
        n = 1 + (L % max(1, min(cap, 8)))
    elif cas == 10:                    # VALUE_GT255
        dm = cap
        n = 1 + (L % max(1, cap))
        value = 256 + (L % 1024)
    elif cas == 11:                    # DIFF_NULL
        dm = cap
        n = 1 + (L % max(1, cap))
    elif cas == 12:                    # EMBEDDED_NUL + oversized n/dm
        if api in ("memcpy_s", "memmove_s", "memcmp_s", "strncpy_s", "strncat_s"):
            dm = cap
            n = RSIZE_MAX + 1 if (L & 1) else SIZE_MAX
        else:
            dm = RSIZE_MAX + 1 if (L & 1) else SIZE_MAX
        extra = 1 + (L % 8)            # embedded NUL position in src
    return dm, n, value, extra


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(SEED)
    rows = []
    for i in range(1, NCASES + 1):
        fam = (i - 1) % 2
        api_name = API_NAMES[(i - 1) % len(API_NAMES)]
        api = API_NAMES.index(api_name)
        applicable = APPLICABLE[api_name]
        cas = applicable[rng.randrange(len(applicable))]
        cap = rng.randint(8, 256)
        L = rng.getrandbits(16)
        O = rng.getrandbits(16)
        dm, n, value, extra = params(api_name, cas, cap, L, O, rng)
        rows.append((i, fam, api, cas, cap, dm, n, L, O, value, extra))

    path = os.path.join(out_dir, "attack_cases.tsv")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")

    # Deterministic digest printed so run.sh can record it.
    import hashlib
    h = hashlib.sha256(open(path, "rb").read()).hexdigest()
    print(f"generated {len(rows)} cases -> {path}")
    print(f"attack_cases.tsv sha256 = {h}")
    print(f"seed = 0x{SEED:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
