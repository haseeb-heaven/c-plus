#!/usr/bin/env python3
"""Deterministic INTEGER/SHIFT UB generator: >=260 distinct small C programs.

Every case is built from parameter sweeps plus a seeded RNG (FUZZ_SEED,
default 0x0BAD): array extents, offsets, literal operands, shift counts,
identifier names and function shapes all vary, so no program is hand-copied
and no two generated programs are identical.  The generator covers:

  * signed overflow (int / long, constant and interprocedural)
  * unsigned wrap used as an array index or as an allocation size
  * shift count >= type width, negative shift, signed left-shift overflow
  * division / modulo by zero, including constant-folded zero divisors
  * INT_MIN / -1 (division and remainder)
  * unsequenced modifications (i++ + i++, conflicting assignments, call args)
  * signed-to-unsigned conversion used to bypass a bounds check or size check
  * negative ptrdiff_t pointer difference reused as a size / index
  * size_t underflow in index computations
  * off-by-one loop bounds (write and read), plus valid loop controls

Each case declares an expected outcome, using the convention of
tests/core/redteam_100.py and tests/metrics/bounds_350.py:

  reject - statically proven UB: must fail BOTH --default and --strict.
  strict - value-dependent / interprocedural UB: DEFAULT may pass, but
           --strict must fail (unverified operations fail closed).
  accept - valid control: must pass BOTH modes (a rejection is a false
           positive).  Defined unsigned wrap that is *not* used as an index
           or size belongs here, not in 'reject'.

Mismatches (unsafe accepted, safe rejected, crash, timeout) are printed and
the generated source plus both diagnostic logs are saved under
tests/out/core/ub_arith/failures/.  Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout.  The run is reproducible for a fixed
FUZZ_SEED.  This is a differential measurement of the declared subset, not a
proof of arbitrary-C safety.

Environment:
  FUZZ_SEED    default 0x0BAD
  FUZZ_WORKERS default 8
  CPLUS_BIN    default <root>/build/c+
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/ub_arith"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x0BAD"), 0)
TIMEOUT = 60
MIN_CASES = 260

CASES = []   # dict(name, body, expect, family)
SEEN = set()  # exact generated bodies, used to enforce distinctness


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({
        "name": name,
        "body": normalized,
        "expect": expect,
        "family": family,
    })


# ---------------------------------------------------------------------------
# Seeded generation helpers.  Every body below embeds N (the sweep index) so
# parameter steps are distinct even when the RNG repeats a small offset.
# ---------------------------------------------------------------------------
RNG = random.Random(SEED)
NS = range(2, 11)             # 9 sweep steps per family
TYPES = ("int", "short", "long", "char")
NAMES = ("a", "buf", "arr", "data", "table", "v")
FNS = ("f", "g", "h", "calc")


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def fn():
    return RNG.choice(FNS)


def off(hi=6):
    return RNG.randint(1, hi)


# --- signed overflow: constants, proven at compile time -------------------
for n in NS:
    k, v = off(5), nm()
    add("signed_overflow", f"ovf_int_add_{n}",
        tpl("int main(void){{ int @@V@@ = 2147483647 + @@K@@ * @@N@@; (void)@@V@@; return 0; }}",
            V=v, K=k, N=n), "reject")
    add("signed_overflow", f"ovf_int_sub_{n}",
        tpl("int main(void){{ int @@V@@ = (-2147483647 - 1) - @@K@@ * @@N@@; (void)@@V@@; return 0; }}",
            V=v, K=k, N=n), "reject")
    add("signed_overflow", f"ovf_int_mul_{n}",
        tpl("int main(void){{ int @@V@@ = 1000000000 * (@@N@@ + @@K@@); (void)@@V@@; return 0; }}",
            V=v, K=k, N=n), "reject")
    add("signed_overflow", f"ovf_long_add_{n}",
        tpl("int main(void){{ long @@V@@ = 9223372036854775807L + @@K@@ * @@N@@; (void)@@V@@; return 0; }}",
            V=v, K=k, N=n), "reject")
    add("signed_overflow", f"ovf_long_mul_{n}",
        tpl("int main(void){{ long @@V@@ = 4611686018427387903L * (@@N@@ + @@K@@); (void)@@V@@; return 0; }}",
            V=v, K=k, N=n), "reject")
    # Interprocedural: requires propagation through a call -> STRICT-only.
    add("signed_overflow", f"ovf_var_add_{n}",
        tpl("int @@F@@(int a){{ return a + 2147483600; }} int main(void){{ return @@F@@(2147483630 - @@N@@); }}",
            F=fn(), N=n), "strict")
    add("signed_overflow", f"ovf_var_mul_{n}",
        tpl("long @@F@@(long a){{ return a * 4611686018427387903L; }} int main(void){{ return (int)@@F@@(@@N@@ + 2); }}",
            F=fn(), N=n), "strict")

# --- unsigned wrap used as array index or allocation size ------------------
for n in NS:
    t, v, k = ty(), nm(), off(6)
    add("unsigned_wrap", f"wrap_idx_sub_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; unsigned i = 0u - @@K@@u; return @@V@@[i]; }}",
            T=t, V=v, N=n, K=k), "reject")
    add("unsigned_wrap", f"wrap_idx_max_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; unsigned i = 4294967295u - @@K@@u; return @@V@@[i]; }}",
            T=t, V=v, N=n, K=k), "reject")
    add("unsigned_wrap", f"wrap_size_mul_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; (void)a; unsigned m = 0u - @@K@@u; "
            "@@T@@ *p = malloc(m * sizeof(@@T@@)); if(!p) return 2; p[0]=(@@T@@)1; free(p); return 0; }}",
            T=t, N=n, K=k), "reject")
    add("unsigned_wrap", f"wrap_size_cast_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; (void)a; size_t m = (size_t)(0u - @@K@@u); "
            "@@T@@ *p = malloc(m); if(!p) return 2; p[0]=(@@T@@)1; free(p); return 0; }}",
            T=t, N=n, K=k), "reject")

# --- shift UB: width, sign, count ------------------------------------------
for n in NS:
    k, v = off(6), nm()
    add("shift_ub", f"shift_width_int_{n}",
        tpl("int main(void){{ int @@V@@ = 1 << (32 + (@@N@@ % 5) + @@K@@); (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("shift_ub", f"shift_width_long_{n}",
        tpl("int main(void){{ long @@V@@ = 1L << (64 + (@@N@@ % 5) + @@K@@); (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("shift_ub", f"shift_width_uint_{n}",
        tpl("int main(void){{ unsigned @@V@@ = 1u << (32 + (@@N@@ % 5) + @@K@@); (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("shift_ub", f"shift_neg_const_{n}",
        tpl("int main(void){{ int s = -@@K@@ * @@N@@; int @@V@@ = 1 << s; (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("shift_ub", f"shift_sign_ovf_{n}",
        tpl("int main(void){{ int @@V@@ = (2 + (@@N@@ % 3)) << 30; (void)@@V@@; return 0; }}",
            V=v, N=n), "reject")
    add("shift_ub", f"shift_neg_var_{n}",
        tpl("int @@F@@(int s){{ return 1 << s; }} int main(void){{ return @@F@@(-@@K@@ * @@N@@); }}",
            F=fn(), N=n, K=k), "strict")

# --- division / modulo by zero, including folded zero ----------------------
for n in NS:
    k, v = off(6), nm()
    add("div_zero", f"div_zero_const_{n}",
        tpl("int main(void){{ int @@V@@ = (@@N@@ * 100 + @@K@@) / 0; (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("div_zero", f"mod_zero_const_{n}",
        tpl("int main(void){{ int @@V@@ = (@@N@@ * 100 + @@K@@) % 0; (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("div_zero", f"div_zero_fold_{n}",
        tpl("int main(void){{ int d = @@K@@ - @@K@@; int @@V@@ = (@@N@@ * 100 + @@K@@) / d; (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("div_zero", f"mod_zero_fold_{n}",
        tpl("int main(void){{ int d = 0 * @@K@@; int @@V@@ = (@@N@@ * 100 + @@K@@) % d; (void)@@V@@; return 0; }}",
            V=v, N=n, K=k), "reject")
    add("div_zero", f"div_zero_arg_{n}",
        tpl("int @@F@@(int d){{ return (@@N@@ * 100 + @@K@@) / d; }} int main(void){{ return @@F@@(0); }}",
            F=fn(), N=n, K=k), "strict")
    add("div_zero", f"mod_zero_arg_{n}",
        tpl("int @@F@@(int d){{ return (@@N@@ * 100 + @@K@@) % d; }} int main(void){{ return @@F@@(0); }}",
            F=fn(), N=n, K=k), "strict")

# --- INT_MIN / -1 ----------------------------------------------------------
for n in NS:
    t, v = ty(), nm()
    add("intmin_neg1", f"intmin_div_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; int m = -2147483647 - 1; int x = m / -1; "
            "return a[0] + x; }}", T=t, N=n), "reject")
    add("intmin_neg1", f"intmin_mod_{n}",
        tpl("int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; int m = -2147483647 - 1; int x = m % -1; "
            "return @@V@@[0] + x; }}", T=t, V=v, N=n), "reject")
    add("intmin_neg1", f"intmin_div_arg_{n}",
        tpl("int @@F@@(int a){{ return a / -1; }} int main(void){{ int b[@@N@@]={{0}}; "
            "return b[0] + @@F@@(-2147483647 - 1); }}", F=fn(), N=n), "strict")

# --- unsequenced modifications ----------------------------------------------
for n in NS:
    t, k = ty(), off(6)
    add("unsequenced", f"unseq_postinc_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; int i = @@K@@; int x = i++ + i++; "
            "return a[0] + x; }}", T=t, N=n, K=k), "reject")
    add("unsequenced", f"unseq_assign_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; int i = @@K@@; int x = (i=@@K@@) + (i=@@N@@); "
            "return a[0] + x; }}", T=t, N=n, K=k), "reject")
    add("unsequenced", f"unseq_callargs_{n}",
        tpl("int g(int,int); int main(void){{ @@T@@ a[@@N@@]={{0}}; int i = @@K@@; "
            "return a[0] + g(i++, i++); }}", T=t, N=n, K=k), "reject")

# --- signed-to-unsigned conversion bypassing checks ------------------------
for n in NS:
    t, k = ty(), off(6)
    add("sign_to_unsigned", f"s2u_index_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; int i = @@N@@ + @@K@@; "
            "unsigned u = (unsigned)i; return a[u]; }}", T=t, N=n, K=k), "reject")
    add("sign_to_unsigned", f"s2u_size_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ probe[@@N@@]={{0}}; (void)probe; int d = -@@K@@; "
            "size_t m = (size_t)d; @@T@@ *p = malloc(m); if(!p) return 2; p[0]=(@@T@@)1; free(p); return 0; }}",
            T=t, N=n, K=k), "reject")

# --- negative ptrdiff_t difference used as a size or index -----------------
for n in NS:
    t = ty()
    add("ptrdiff_negative", f"ptrdiff_neg_size_{n}",
        tpl("#include <stdlib.h>\n#include <stddef.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; "
            "@@T@@ *p=&a[0], *q=&a[@@M@@]; ptrdiff_t d = p - q; "
            "@@T@@ *b = malloc((size_t)d); if(!b) return 2; b[0]=(@@T@@)1; free(b); return 0; }}",
            T=t, N=n, M=n - 1), "reject")
    add("ptrdiff_negative", f"ptrdiff_neg_index_{n}",
        tpl("#include <stdlib.h>\n#include <stddef.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; "
            "@@T@@ *p=&a[0], *q=&a[@@M@@]; ptrdiff_t d = p - q; size_t i = (size_t)d; return a[i]; }}",
            T=t, N=n, M=n - 1), "reject")

# --- size_t underflow in index computations --------------------------------
for n in NS:
    t, k = ty(), off(6)
    add("sizet_underflow", f"sizet_underflow_idx_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; size_t i = @@K@@; "
            "size_t j = i - (@@K@@ + @@N@@); return a[j]; }}", T=t, N=n, K=k), "reject")
    add("sizet_underflow", f"sizet_underflow_sub_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; size_t i = 0; "
            "i -= @@K@@ * @@N@@; return a[i]; }}", T=t, N=n, K=k), "reject")

# --- off-by-one loop bounds -------------------------------------------------
for n in NS:
    t, k = ty(), off(6)
    add("offbyone", f"offbyone_le_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; for(int i=0; i<=@@N@@; i++) a[i]=(@@T@@)i; return 0; }}",
            T=t, N=n), "reject")
    add("offbyone", f"offbyone_over_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; for(int i=0; i<@@N@@+@@K@@; i++) a[i]=(@@T@@)i; return 0; }}",
            T=t, N=n, K=k), "reject")
    add("offbyone", f"offbyone_read_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; int s=0; for(int i=0; i<=@@N@@; i++) s+=a[i]; return s; }}",
            T=t, N=n), "reject")

# --- valid controls: defined behaviour that must pass both modes -----------
for n in NS:
    t, k = ty(), RNG.randint(1, 24)
    add("valid_control", f"ok_arith_{n}",
        tpl("int main(void){{ int x = @@K@@ * 1000 + @@N@@; (void)x; return 0; }}", N=n, K=k), "accept")
    add("valid_control", f"ok_unsigned_wrap_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; unsigned u = 0u - @@K@@u; (void)u; return a[0]; }}",
            T=t, N=n, K=k), "accept")
    add("valid_control", f"ok_shift_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; int x = 1 << @@K@@; return a[0] + x; }}",
            T=t, N=n, K=k), "accept")
    add("valid_control", f"ok_index_{n}",
        tpl("#include <stdlib.h>\nint main(void){{ @@T@@ a[@@N@@]={{0}}; size_t i = (size_t)(@@N@@ - 2); "
            "return a[i]; }}", T=t, N=n), "accept")
    add("valid_control", f"ok_loop_{n}",
        tpl("int main(void){{ @@T@@ a[@@N@@]={{0}}; for(int i=0; i<@@N@@; i++) a[i]=(@@T@@)i; "
            "return a[@@N@@-1]; }}", T=t, N=n), "accept")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def _first(text):
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def run_case(case):
    name, body, expect, family = (case["name"], case["body"],
                                  case["expect"], case["family"])
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT)
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc)
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        diag = (proc.stderr + proc.stdout).strip()
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("proven UB accepted in DEFAULT")
        if not s_rej:
            reasons.append("proven UB accepted in STRICT")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        if d_rej:
            reasons.append("safe control rejected in DEFAULT")
        if s_rej:
            reasons.append("safe control rejected in STRICT")

    if not reasons:
        return name, expect, family, [], modes

    # Save the generated source and a full diagnostic log for every mismatch.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        _first(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        _first(modes["--strict"][1]),
        "",
        "===== generated source =====",
        body,
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    detail = ("%s [%s] expect=%s reasons=%s | DEFAULT rejected=%s: %s | STRICT rejected=%s: %s" % (
        name, family, expect, ";".join(reasons),
        d_rej, _first(modes["--default"][1]),
        s_rej, _first(modes["--strict"][1])))
    return name, expect, family, [detail], modes


def main():
    names = [c["name"] for c in CASES]
    bodies = [c["body"] for c in CASES]
    unique = len(set(bodies))
    assert len(names) == len(set(names)), "duplicate case name"
    assert len(CASES) >= MIN_CASES, "only %d cases generated" % len(CASES)
    if unique != len(CASES):
        raise AssertionError("%d duplicate program bodies" % (len(CASES) - unique))

    counts = Counter(c["expect"] for c in CASES)
    fam_counts = Counter(c["family"] for c in CASES)
    print("ub_arith: %d cases, %d distinct, %d families, seed=%s (0x%X), workers=%d, bin=%s"
          % (len(CASES), unique, len(fam_counts), SEED, SEED, WORKERS, CC))
    print("outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))

    mismatches = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for _name, _expect, _family, fails, _modes in pool.map(run_case, CASES):
            mismatches.extend(fails)
    mismatches.sort()

    print("results: %d cases, %d mismatches" % (len(CASES), len(mismatches)))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
