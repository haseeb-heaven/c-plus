#!/usr/bin/env python3
"""Recompute ADV1000 json_log parameters and report deterministic coverage.

This mirrors adv_mix/adv_params/adv_*_from and adv_fixed_case in
adv1000_json_log.cp exactly.  It is an independent check that the 1000 scenario
ids map to distinct parameter tuples and that the required extreme ranges are
actually exercised.  Output is written to results/coverage.txt by run.sh.
"""
MASK = (1 << 64) - 1
SEED = 0xC0FFEE123456789

LEN_TBL = [0, 1, 2, 3, 4, 8, 16, 64, 255, 256, 1024, 4096, 16384,
           65536, 262144, 1048576, 2097152]
DEPTH_TBL = [0, 1, 2, 3, 8, 16, 64, 256, 1024, 4096, 16384, 16385, 20000]
BUDGET_TBL = [16384, 0, 1, 2, 8, 16, 64, 1024, 16384, 16385, 20000, 25000,
              (1 << 64) - 1]

K_JSON_TRUNC = 4
K_JSON_DEEP_DEFAULT = 5
K_JSON_DEEP_OVERRIDE = 6
K_JSON_LONG = 7
K_JSON_MALFORMED = 8
K_JSON_BUDGET_FN = 9
K_JSON_READSIZE = 11
K_JSON_VALCOUNT = 12
K_LOG_MSG = 18
K_COUNT = 21
DEPTH_KINDS = {K_JSON_TRUNC, K_JSON_DEEP_DEFAULT, K_JSON_DEEP_OVERRIDE,
               K_JSON_MALFORMED, K_JSON_BUDGET_FN}
LENGTH_KINDS = {K_JSON_LONG, K_JSON_READSIZE, K_JSON_VALCOUNT}


def mix(z):
    z &= MASK
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
    return z ^ (z >> 31)


def params(ident):
    k = ident
    r1 = mix(SEED + k * 0x9E3779B97F4A7C15)
    r2 = mix(SEED ^ (k * 0xD1B54A32D192ED03))
    r3 = mix(mix(SEED + k * 0x2545F4914F6CDD1D + 0x1234567) +
             k * 0xD6E8FEB86659FD93)
    return r1, r2, r3


def len_from(r):
    return r % 2097153 if ((r >> 11) & 3) == 0 else LEN_TBL[r % len(LEN_TBL)]


def depth_from(r):
    return r % 20001 if ((r >> 13) & 3) == 0 else DEPTH_TBL[r % len(DEPTH_TBL)]


def fixed(ident):
    r1 = r2 = r3 = 0
    kind = 0
    overrides = {
        0: (0, 0, 0, 0),
        1: (1, 0, 0, 0),
        2: (2, 0, 0, 0),
        3: (3, 0, 0, 0),
        4: (4, 16385, 8200, 0),
        5: (5, 16385, 0, 0),
        6: (5, 16384, 0, 0),
        7: (6, 20000, 8, 0),
        8: (6, 20000, 12, 0),
        9: (7, 2097152, 8, 0),
        10: (15, 0, 0, 0),
        11: (16, 0, 0, 0),
        12: (17, 0, 0, 0),
        13: (18, 0, 0, 0),
        14: (18, 4096, 0, 0),
        15: (10, 0, 0, 0),
    }
    if ident in overrides:
        kind, r1, r2, r3 = overrides[ident]
    return kind, r1, r2, r3


def main():
    tuples = []
    depths = []
    lengths = []
    msgs = []
    budgets = []
    kinds = [0] * K_COUNT
    for ident in range(1000):
        r1, r2, r3 = params(ident)
        if ident < 16:
            kind, r1, r2, r3 = fixed(ident)
            raw = 1
        else:
            kind = r3 % K_COUNT
            raw = 0
        depth = r1 if raw else depth_from(r1)
        length = r1 if raw else len_from(r1)
        budget = BUDGET_TBL[r2 % len(BUDGET_TBL)] if raw else BUDGET_TBL[r2 % len(BUDGET_TBL)]
        variant = r3 & 3
        kinds[kind] += 1
        tuples.append((kind, depth, length, budget, variant))
        budgets.append(int(budget))
        if kind in DEPTH_KINDS:
            depths.append(int(depth))
        if kind in LENGTH_KINDS:
            lengths.append(int(length))
        if kind == K_LOG_MSG:
            msgs.append(int(r1 % 4097))

    print("ADV1000 json_log coverage (recomputed from fixed seed)")
    print("scenarios=%d distinct_parameter_tuples=%d" %
          (len(tuples), len(set(tuples))))
    print("kinds=" + " ".join("%d:%d" % (i, kinds[i]) for i in range(K_COUNT)))
    print("depth_scenarios=%d depth_min=%d depth_max=%d depth_0=%s depth_16384=%s depth_16385=%s depth_20000=%s" %
          (len(depths), min(depths), max(depths),
           0 in depths, 16384 in depths, 16385 in depths, 20000 in depths))
    print("length_scenarios=%d length_min=%d length_max=%d length_0=%s length_2097152=%s" %
          (len(lengths), min(lengths), max(lengths),
           0 in lengths, 2097152 in lengths))
    print("msg_scenarios=%d msg_min=%d msg_max=%d msg_0=%s msg_4096=%s" %
          (len(msgs), min(msgs), max(msgs), 0 in msgs, 4096 in msgs))
    print("budget_override_values=" + " ".join(str(v) for v in sorted(set(budgets))))
    ok = (len(tuples) == 1000 and len(set(tuples)) == 1000 and
          min(depths) == 0 and max(depths) == 20000 and
          0 in depths and 16384 in depths and 16385 in depths and 20000 in depths and
          min(lengths) == 0 and max(lengths) == 2097152 and
          min(msgs) == 0 and max(msgs) == 4096)
    print("COVERAGE_OK=%d" % (1 if ok else 0))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
