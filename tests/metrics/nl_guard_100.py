#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator for NULL-GUARD FORMS (>=100 cases).

Produces distinct, self-contained, valid C translation units that test whether
a conditional NULL guard actually makes a subsequent dereference safe.  Every
case obtains a (provably NULL) pointer from a small helper, then either guards
the dereference with one of the forms below or leaves it unguarded:

  * if(p)                       positive if-guard
  * if(!p)return                negated guard with early return
  * if(p!=0)                    explicit not-equal-to-zero guard
  * if(p==NULL)return           explicit equal-to-NULL guard with early return
  * ternary p?*p:0              conditional-operator guard
  * p&&*p                       logical-and short-circuit guard
  * early return                guard placed before the dereference
  * else-branch guards          deref guarded by an if/else shape
  * nested guards               two or more nested guard tests
  * guard then free             guarded deref followed by free(p)
  * guard across loops          guard outside a loop, deref inside it
  * guard in callee             callee guards a possibly-NULL argument

Unguarded controls are interleaved in every family and MUST be rejected:

  reject - provably unsafe (dereference of a NULL pointer): must fail
           BOTH --default and --strict.  Acceptance in either mode is a hole.
  accept - valid control: the guard makes the dereference unreachable or
           provably non-NULL, so the program must pass BOTH modes.  Rejection
           is a false positive (the analyzer failed to recognise the guard).

The NULL pointer source is a helper returning 0, e.g.
``static int *nullp(void){return 0;}``.  The current analyzer tracks this
constant interprocedurally, so an unguarded ``*p`` is rejected while a guard
that is understood makes it safe.  Some syntactically valid guard forms are
known to be missed by the analyzer; those show up as false-positive mismatches
and are the point of this differential measurement (not a proof of safety).

Programs come from deterministic seeded parameter sweeps plus a seeded RNG
(FUZZ_SEED); a final seeded pass re-rolls parameters until TARGET distinct
bodies exist.  Cases run in parallel (FUZZ_WORKERS) with a per-check timeout.
Mismatches are saved under tests/out/core/nl_guard/failures/.

Environment:
  FUZZ_SEED    default 0xA6C6
  FUZZ_WORKERS default 8
  CPLUS_BIN    default <root>/build/c+
  AG_TARGET    default 140 (minimum 100)
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
OUT = ROOT / "tests/out/core/nl_guard"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA6C6"), 0)
TARGET = max(100, int(os.environ.get("AG_TARGET", "140")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = "#include <stdlib.h>\n"
STDDEF = "#include <stddef.h>\n"
TYPES = ("int", "short", "long", "char", "unsigned", "unsigned long")
NAMES = ("p", "q", "ptr", "data", "cp", "ip")


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 97)


def nlen():
    return RNG.randint(2, 9)


def nm():
    return RNG.choice(NAMES)


def ty():
    return RNG.choice(TYPES)


def src_kind():
    return RNG.randint(0, 1)


def make_src(t, kind):
    ret = f"({t}*)0" if kind else "0"
    return f"static {t} *nullp(void){{return {ret};}}\n"


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "accept"), (name, expect)
    key = body.strip()
    if key in SEEN:
        return False
    SEEN.add(key)
    normalized = (f"/* nl_guard seed=0x{SEED:X} case={name} family={family} */\n"
                  + key + "\n")
    CASES.append((family, name, normalized, expect))
    return True


# --- Guard-form families -----------------------------------------------------
# Each template is (template, expect).  Tokens: @@SRC@@ @@STD@@ @@STDDEF@@
# @@T@@ @@V@@ @@N@@ @@P@@.  A family may contain known analyzer blind spots;
# those cases legitimately become false-positive mismatches.
FAMILIES = {
    "if_p": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@) *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ *@@P@@=@@V@@; int x=*@@P@@; (void)x;} return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@) @@P@@[0]=@@V@@; return 0;}",
    ]),
    "not_p_return": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@) return 2; *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@){return 2;} *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@) return @@V@@; @@P@@[0]=@@V@@; return 0;}",
    ]),
    "ne_zero": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@!=0) *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@!=0){ *@@P@@=@@V@@; } return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(0!=@@P@@) @@P@@[0]=@@V@@; return 0;}",
    ]),
    "eq_null_return": ("accept", [
        "@@STDDEF@@@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@==NULL) return 2; *@@P@@=@@V@@; return 0;}",
        "@@STDDEF@@@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(NULL==@@P@@) return 2; @@P@@[0]=@@V@@; return 0;}",
        "@@STDDEF@@@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@==NULL){return 2;} *@@P@@=@@V@@; return 0;}",
    ]),
    "ternary": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); int v=@@P@@?*@@P@@:0; return v;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); return @@P@@?*@@P@@:@@V@@;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); int v=@@P@@?@@P@@[0]:(int)0; (void)v; return 0;}",
    ]),
    "logical_and": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); int v=@@P@@&&*@@P@@; return v;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@ && *@@P@@) return 1; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); int v=0; if(@@P@@ && (*@@P@@=@@V@@)) v=1; return v;}",
    ]),
    "early_return": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@==0) return 1; *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@) return 1; int x=@@V@@; *@@P@@=x; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){} else return 1; *@@P@@=@@V@@; return 0;}",
    ]),
    "else_guard": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ *@@P@@=@@V@@; } else {return 3;} return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@) return 3; else *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@){return 3;} else {*@@P@@=@@V@@;} return 0;}",
    ]),
    "nested": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ if(@@P@@!=0){ *@@P@@=@@V@@; } } return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@!=0){ if(@@P@@){ *@@P@@=@@V@@; } } return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ if(!@@P@@) return 2; *@@P@@=@@V@@; } return 0;}",
    ]),
    "guard_then_free": ("accept", [
        "@@STD@@@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ *@@P@@=@@V@@; free(@@P@@);} return 0;}",
        "@@STD@@@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@) return 2; *@@P@@=@@V@@; free(@@P@@); return 0;}",
        "@@STD@@@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ *@@P@@=@@V@@; int x=*@@P@@; free(@@P@@); (void)x;} return 0;}",
    ]),
    "guard_loop": ("accept", [
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ for(int i=0;i<@@N@@;i++) *@@P@@+=i; } return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(!@@P@@) return 2; for(int i=0;i<@@N@@;i++) @@P@@[0]=@@V@@+i; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); if(@@P@@){ int i=0; while(i<@@N@@){ *@@P@@+=1; i++; } } return 0;}",
    ]),
    "callee_guard": ("accept", [
        "@@SRC@@static void f(@@T@@ *@@P@@){ if(@@P@@) *@@P@@=@@V@@; } int main(void){@@T@@ *q=nullp(); f(q); return 0;}",
        "@@SRC@@static void f(@@T@@ *@@P@@){ if(!@@P@@) return; *@@P@@=@@V@@; } int main(void){@@T@@ *q=nullp(); f(q); return 0;}",
        "@@SRC@@static int f(@@T@@ *@@P@@){ if(@@P@@) return *@@P@@; return 0; } int main(void){@@T@@ *q=nullp(); return f(q);}",
    ]),
}

# Unguarded controls: MUST be rejected (acceptance is a false negative / hole).
UNGUARDED = ("reject", [
    "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); *@@P@@=@@V@@; return 0;}",
    "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); @@P@@[0]=@@V@@; return 0;}",
    "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); return *@@P@@+@@V@@;}",
    "@@SRC@@static void f(@@T@@ *@@P@@){ *@@P@@=@@V@@; } int main(void){@@T@@ *q=nullp(); f(q); return 0;}",
    "@@SRC@@int main(void){@@T@@ *@@P@@=0; *@@P@@=@@V@@; return 0;}",
    "@@SRC@@int main(void){@@T@@ *@@P@@=nullp(); int x=*@@P@@; return x;}",
])


def build(family, template, expect, name, t=None, v=None, n=None, p=None, sk=None):
    t = t if t is not None else ty()
    v = v if v is not None else val()
    n = n if n is not None else nlen()
    p = p if p is not None else nm()
    sk = sk if sk is not None else src_kind()
    body = tpl(template, SRC=make_src(t, sk), STD=STD, STDDEF=STDDEF,
               T=t, V=v, N=n, P=p)
    return add(family, name, body, expect)


# --- Deterministic seeded sweeps ---------------------------------------------
PER_FAMILY = 10
for family in FAMILIES:
    expect, templates = FAMILIES[family]
    for k in range(PER_FAMILY):
        build(family, templates[k % len(templates)], expect,
              f"{family}_{k:03d}")

PER_UNGUARDED = 20
ung_expect, ung_templates = UNGUARDED
for k in range(PER_UNGUARDED):
    build("unguarded", ung_templates[k % len(ung_templates)], ung_expect,
          f"unguarded_{k:03d}")


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll until TARGET distinct programs exist.
# ---------------------------------------------------------------------------
def seeded_extra(i):
    while True:
        families = list(FAMILIES.items())
        use_unguarded = (i % 7 == 0)
        if use_unguarded:
            family, expect, templates = "unguarded", ung_expect, ung_templates
        else:
            family, (expect, templates) = families[RNG.randrange(len(families))]
        template = templates[RNG.randrange(len(templates))]
        t, v, n, p, sk = ty(), val(), nlen(), nm(), src_kind()
        body = tpl(template, SRC=make_src(t, sk), STD=STD, STDDEF=STDDEF,
                   T=t, V=v, N=n, P=p)
        name = f"m_{family}_{i:04d}"
        yield family, name, body, expect


_extra = seeded_extra(0)
_i = 0
while len(CASES) < TARGET:
    _family, _name, _body, _expect = next(_extra)
    add(_family, _name, _body, _expect)
    _i += 1
    if _i > TARGET * 50:
        raise RuntimeError("could not reach TARGET distinct cases")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def run_case(case):
    family, name, body, expect = case
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
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    false_positive = False
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
    else:  # accept
        false_positive = d_rej or s_rej
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    result = {
        "name": name, "family": family, "expect": expect,
        "reasons": reasons, "false_positive": false_positive,
        "d_rej": d_rej, "s_rej": s_rej, "modes": modes,
    }
    if not reasons:
        return result

    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        first_diag(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        first_diag(modes["--strict"][1]),
        "",
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    result["detail"] = (
        "%s [%s] expect=%s reasons=%s | DEFAULT rejected=%s: %s | "
        "STRICT rejected=%s: %s" % (
            name, family, expect, ";".join(reasons),
            d_rej, first_diag(modes["--default"][1]),
            s_rej, first_diag(modes["--strict"][1])))
    return result


def main():
    bodies = [b for _, _, b, _ in CASES]
    unique = len(set(bodies))
    counts = Counter(e for _, _, _, e in CASES)
    families = Counter(f for f, _, _, _ in CASES)
    print("nl_guard: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d accept=%d"
          % (counts["reject"], counts["accept"]))
    print("families (%d): %s" % (len(families),
          ", ".join("%s=%d" % kv for kv in sorted(families.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(run_case, CASES))

    mismatches = sorted(r["detail"] for r in results if r["reasons"])
    mismatch_names = sorted(r["name"] for r in results if r["reasons"])
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    false_negatives = sum(1 for r in results if r["expect"] != "accept" and r["reasons"])
    hard = sum(1 for r in results for reason in r["reasons"]
               if "TIMEOUT" in reason or "OS ERROR" in reason or "CRASH" in reason)

    print("results: %d cases, %d mismatches (%d false negatives, %d false positives, %d hard errors)"
          % (len(CASES), len(mismatches), false_negatives, len(false_positives), hard))
    print("false positives: %d" % len(false_positives))
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print("  ... %d more" % (len(false_positives) - 20))
    print("first 10 mismatch names:")
    for name in mismatch_names[:10]:
        print("  " + name)
    if len(mismatch_names) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatch_names) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        "FAIL" if (mismatches or unique != len(CASES)) else "PASS",
        len(CASES), len(mismatches), len(false_positives)))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
