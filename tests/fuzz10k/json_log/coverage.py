#!/usr/bin/env python3
"""Independent recomputation of the FUZZ10K fixed-seed parameter map.

Reimplements SplitMix64 and params_for() from fuzz10k_json_log.cp in Python to
prove that all 10,000 scenarios derive distinct (kind, r1, r2, r3) tuples, and
summarises the canonical run output. Prints a machine-checkable COVERAGE_OK=1
line and exits 0 only when every check passes.
"""
import os
import sys

MASK = (1 << 64) - 1
SEED = 0xF02A5EED1234ABCD
N = 10000

# Pinned breakers/boundaries: id -> (kind, r1, r2). Kind ids match the C enum.
PINNED = {
    0: (9, 0, 0), 1: (1, 1, 0), 2: (1, 2, 0), 3: (1, 3, 0),
    4: (5, 16385, 8), 5: (3, 16384, 0), 6: (3, 16385, 0),
    7: (4, 20000, 8), 8: (4, 20000, 12), 9: (6, 131072, 0),
    10: (12, 0, 0), 11: (12, 4096, 0), 12: (13, 0, 0),
    13: (2, 5, 0), 14: (8, 131072, 0), 15: (10, 0, 0),
}
KINDS = 16


def mix64(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    return (z ^ (z >> 31)) & MASK


def params_for(i):
    if i < 16:
        kind, r1, r2 = PINNED[i]
        return (kind, r1, r2, 0)
    k = i
    r1 = mix64((SEED + k * 0x9E3779B97F4A7C15) & MASK)
    r2 = mix64(SEED ^ ((k * 0xD1B54A32D192ED03) & MASK))
    inner = (SEED + k * 0x2545F4914F6CDD1D + 0x1234567) & MASK
    r3 = mix64((mix64(inner) + k * 0xD6E8FEB86659FD93) & MASK)
    return (r3 % KINDS, r1, r2, r3)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    tuples = [params_for(i) for i in range(N)]
    distinct = len(set(tuples))
    kinds_seen = sorted({t[0] for t in tuples})
    print(f"scenarios={N} distinct_parameter_tuples={distinct}")
    print(f"kinds_seen={len(kinds_seen)} kind_ids={','.join(map(str, kinds_seen))}")

    out_path = os.path.join(here, "results", "scenarios.out")
    ok = distinct == N and len(kinds_seen) == KINDS
    if os.path.exists(out_path):
        with open(out_path, "r", encoding="utf-8") as fh:
            lines = [ln.strip() for ln in fh if ln.strip()]
        s_lines = [ln for ln in lines if ln.startswith("S ")]
        finals = [ln for ln in lines if ln.startswith("FUZZ10K json_log ")]
        kinds_out = sorted({ln.split()[2] for ln in s_lines})
        results = {}
        for ln in s_lines:
            r = ln.split()[3]
            results[r] = results.get(r, 0) + 1
        print(f"S_lines={len(s_lines)} final_lines={len(finals)} "
              f"kinds_in_output={len(kinds_out)}")
        print("result_mix=" + " ".join(f"{k}:{v}" for k, v in sorted(results.items())))
        print("final=" + (finals[0] if finals else "(missing)"))
        ok = ok and len(s_lines) == N and len(finals) == 1
        ok = ok and len(kinds_out) == KINDS and "FAIL" not in results
        ok = ok and "CRASH" not in results and "HANG" not in results
        ok = ok and "CORRUPT" not in results and "OOM" not in results
    else:
        print(f"note: {out_path} not present; parameter-map checks only")

    print("COVERAGE_OK=" + ("1" if ok else "0"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
