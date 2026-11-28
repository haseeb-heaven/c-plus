#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator for NULL-STATE TRANSITIONS (>=100 cases).

Generates distinct, self-contained, valid C translation units that exercise the
null-state transitions covered by the recent null-guard / null-invalidation
fixes: assigning NULL, casting NULL, freeing NULL, guarding with every common
comparison form, invalidating NULL by taking an address, passing NULL to libc,
and dereferencing NULL struct fields.

Every case has an expected outcome under both checker modes:

  reject - provably unsafe (e.g. p = NULL followed by an unguarded *p, or a
           NULL argument to a libc function that dereferences it).  A correct
           checker must fail BOTH --default and --strict.  Acceptance in either
           mode is a hole / false negative.
  accept - safe control (proper guard, free(NULL), NULL replaced by &local,
           etc.).  A correct checker must pass BOTH modes; rejection here is a
           false positive.

Families (one per requested transition, plus safe controls):

  * null_assign_deref - p = NULL, then *p / p[0]                       (reject)
  * cast_null_deref   - p = (T *)NULL / (void *)0, then deref            (reject)
  * free_null         - free(p) where p is NULL (defined no-op)         (accept)
  * eq_null_log       - if (p == NULL) { log(); return; } guard         (accept)
  * not_p_return      - if (!p) return ... guard                        (accept)
  * null_eq_p         - if (NULL == p) / (NULL != p) guard              (accept)
  * ternary           - p ? *p : 0 short-circuit                        (accept)
  * logic_and         - p && *p short-circuit                           (accept)
  * null_then_addr    - p = NULL; p = &local; deref (state invalidation) (accept)
  * libc_null_arg     - unguarded NULL to strlen/strcmp/strcpy/...      (reject)
  * field_null        - struct field set to NULL, then dereferenced     (reject)
  * field_null_guard  - same field, but guarded before the deref        (accept)
  * safe_control      - explicit guards / malloc checks / &local        (accept)

Parameters (types, values, identifiers, loop bounds, struct/helper names) come
from a seeded RNG so every program is distinct.  Cases run in parallel
(FUZZ_WORKERS) with a per-check timeout.  Mismatches and their diagnostics are
saved under tests/out/core/hv_null/failures/.  This is a differential
measurement, not a proof of safety, and does not modify the compiler.

Environment:
  FUZZ_SEED        default 0x48564E30
  FUZZ_WORKERS     default 8
  CPLUS_BIN        default <root>/build/c+
  HV_NULL_TARGET   default 128 (minimum 100)
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+"))
OUT = ROOT / "tests" / "out" / "core" / "hv_null"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x48564E30"), 0)
TARGET = max(100, int(os.environ.get("HV_NULL_TARGET", "128")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STDDEF = "#include <stddef.h>\n"
STDLIB = "#include <stdlib.h>\n"
STDIO = "#include <stdio.h>\n"
STRING = "#include <string.h>\n"
LIBC = STDDEF + STRING + STDIO + STDLIB

TYPES = ("int", "short", "long", "char", "unsigned", "unsigned long")
VARS = ("p", "q", "ptr", "vp", "np", "dp")
LOCS = ("x", "y", "v", "tmp", "cell")
STRS = ("S", "Rec", "Box", "Node")
FNS = ("get_null", "nullp", "make_null", "zero_ptr")
LGS = ("hv_log", "log_null", "note_null", "record")


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 97)


def nlen():
    return RNG.randint(2, 9)


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(VARS)


def loc():
    return RNG.choice(LOCS)


def src_kind():
    return RNG.randint(0, 1)


def make_src(t, fn, sk):
    ret = "(%s*)0" % t if sk else "0"
    return "static %s *%s(void){return %s;}\n" % (t, fn, ret)


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "accept"), (name, expect)
    key = body.strip()
    if key in SEEN:
        return False
    SEEN.add(key)
    normalized = ("/* hv_null seed=0x%X case=%s family=%s */\n"
                  % (SEED, name, family)) + key + "\n"
    CASES.append((family, name, normalized, expect))
    return True


# --- Transition families ------------------------------------------------------
# Each family is (expect, [templates]).  Tokens: @@STDDEF@@ @@STDLIB@@
# @@STDIO@@ @@STRING@@ @@LIBC@@ @@SRC@@ @@LOG@@ @@T@@ @@V@@ @@N@@ @@P@@
# @@LOC@@ @@ST@@ @@FN@@ @@LG@@ @@ID@@.

FAMILIES = {
    "null_assign_deref": ("reject", [
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; return (int)*@@P@@;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; *@@P@@=@@V@@; return 0;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=0; @@P@@[0]=@@V@@; return 0;}",
    ]),
    "cast_null_deref": ("reject", [
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=(@@T@@*)NULL; return (int)*@@P@@;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=(void*)0; *@@P@@=@@V@@; return 0;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=(@@T@@*)0; return (int)@@P@@[0];}",
    ]),
    "free_null": ("accept", [
        "@@STDLIB@@int main(void){@@T@@ *@@P@@=NULL; free(@@P@@); return 0;}",
        "@@STDLIB@@int main(void){free(NULL); return 0;}",
        "@@STDLIB@@@@STDDEF@@int main(void){@@T@@ *@@P@@=(@@T@@*)NULL; free(@@P@@); free(NULL); return @@V@@;}",
    ]),
    "eq_null_log": ("accept", [
        "@@STDDEF@@@@LOG@@int main(void){@@T@@ *@@P@@=NULL; if(@@P@@==NULL){@@LG@@(); return @@V@@;} return (int)*@@P@@;}",
        "@@STDDEF@@@@LOG@@int main(void){@@T@@ *@@P@@=NULL; if(NULL==@@P@@){@@LG@@(); return @@V@@;} *@@P@@=@@V@@; return 0;}",
        "@@STDDEF@@@@LOG@@int main(void){@@T@@ *@@P@@=NULL; if(@@P@@==NULL){@@LG@@();} else {return (int)*@@P@@;} return @@V@@;}",
    ]),
    "not_p_return": ("accept", [
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; if(!@@P@@)return @@V@@; return (int)*@@P@@;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; if(!@@P@@){return @@V@@;} *@@P@@=@@V@@; return 0;}",
        "@@SRC@@int main(void){@@T@@ *@@P@@=@@FN@@(); if(!@@P@@)return @@V@@; @@P@@[0]=@@V@@; return 0;}",
    ]),
    "null_eq_p": ("accept", [
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; if(NULL==@@P@@)return @@V@@; return (int)*@@P@@;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=(@@T@@*)NULL; if(NULL==@@P@@){return @@V@@;} *@@P@@=@@V@@; return 0;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; if(NULL!=@@P@@)return (int)*@@P@@; return @@V@@;}",
    ]),
    "ternary": ("accept", [
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; int r=@@P@@?*@@P@@:0; return r;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; return @@P@@?*@@P@@:@@V@@;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; int r=@@P@@?@@P@@[0]:@@V@@; return r;}",
    ]),
    "logic_and": ("accept", [
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; int r=@@P@@&&*@@P@@; return r;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; if(@@P@@ && *@@P@@)return @@V@@; return 0;}",
        "@@STDDEF@@int main(void){@@T@@ *@@P@@=NULL; int r=0; if(@@P@@ && (*@@P@@=@@V@@))r=1; return r;}",
    ]),
    "null_then_addr": ("accept", [
        "@@STDDEF@@int main(void){@@T@@ @@LOC@@=@@V@@; @@T@@ *@@P@@=NULL; @@P@@=&@@LOC@@; return (int)*@@P@@;}",
        "@@STDDEF@@int main(void){@@T@@ @@LOC@@=@@V@@; @@T@@ *@@P@@=NULL; @@P@@=&@@LOC@@; *@@P@@=@@V@@; return 0;}",
        "@@STDDEF@@int main(void){int @@LOC@@=@@V@@; int *@@P@@=(int*)NULL; @@P@@=&@@LOC@@; return @@P@@?*@@P@@:0;}",
    ]),
    "libc_null_arg": ("reject", [
        "@@LIBC@@int main(void){char *@@P@@=NULL; return (int)strlen(@@P@@);}",
        "@@LIBC@@int main(void){char *@@P@@=NULL; return strcmp(@@P@@, \"abc\");}",
        "@@LIBC@@int main(void){char d[@@N@@]; char *@@P@@=NULL; strcpy(d, @@P@@); return 0;}",
        "@@LIBC@@int main(void){char *@@P@@=NULL; return atoi(@@P@@);}",
        "@@LIBC@@int main(void){char *@@P@@=NULL; puts(@@P@@); return 0;}",
        "@@LIBC@@int main(void){char *@@P@@=NULL; memset(@@P@@, 0, (size_t)@@N@@); return 0;}",
        "@@LIBC@@int main(void){char *@@P@@=NULL; printf(\"%s\", @@P@@); return 0;}",
    ]),
    "field_null": ("reject", [
        "@@STDDEF@@struct @@ST@@{@@T@@ *f;}; int main(void){struct @@ST@@ s; s.f=NULL; return (int)*s.f;}",
        "@@STDDEF@@struct @@ST@@{@@T@@ *f;}; int main(void){struct @@ST@@ s; s.f=(@@T@@*)NULL; s.f[0]=@@V@@; return 0;}",
        "@@STDDEF@@struct @@ST@@{@@T@@ *f;}; int main(void){struct @@ST@@ s; s.f=0; return (int)s.f[0];}",
    ]),
    "field_null_guard": ("accept", [
        "@@STDDEF@@struct @@ST@@{@@T@@ *f;}; int main(void){struct @@ST@@ s; s.f=NULL; if(s.f)return (int)*s.f; return @@V@@;}",
        "@@STDDEF@@struct @@ST@@{@@T@@ *f;}; int main(void){struct @@ST@@ s; s.f=NULL; if(!s.f)return @@V@@; *s.f=@@V@@; return 0;}",
        "@@STDDEF@@struct @@ST@@{@@T@@ *f;}; int main(void){struct @@ST@@ s; s.f=(@@T@@*)NULL; return s.f?*s.f:@@V@@;}",
    ]),
    "safe_control": ("accept", [
        "@@STDDEF@@int main(void){@@T@@ @@LOC@@=@@V@@; @@T@@ *@@P@@=&@@LOC@@; if(@@P@@!=NULL)return (int)*@@P@@; return @@V@@;}",
        "@@STDLIB@@int main(void){@@T@@ *@@P@@=(@@T@@*)malloc(sizeof(@@T@@)); if(@@P@@){*@@P@@=@@V@@; free(@@P@@);} return @@V@@;}",
        "@@STDDEF@@int main(void){@@T@@ @@LOC@@=@@V@@; @@T@@ *@@P@@=&@@LOC@@; if(@@P@@==NULL)return @@V@@; return (int)*@@P@@;}",
        "@@STDDEF@@int main(void){int @@LOC@@=@@V@@; int *@@P@@=NULL; if(@@P@@==NULL){@@P@@=&@@LOC@@;} return *@@P@@;}",
    ]),
}


def build(family, template, expect, name, idn, t=None, v=None, n=None,
          p=None, lc=None, st=None, fn=None, lg=None, sk=None):
    t = t if t is not None else ty()
    v = v if v is not None else val()
    n = n if n is not None else nlen()
    p = p if p is not None else nm()
    lc = lc if lc is not None else loc()
    st = st if st is not None else "%s_%d" % (RNG.choice(STRS), idn)
    fn = fn if fn is not None else "%s_%d" % (RNG.choice(FNS), idn)
    lg = lg if lg is not None else "%s_%d" % (RNG.choice(LGS), idn)
    sk = sk if sk is not None else src_kind()
    body = tpl(template,
               STDDEF=STDDEF, STDLIB=STDLIB, STDIO=STDIO, STRING=STRING,
               LIBC=LIBC, SRC=make_src(t, fn, sk),
               LOG="static void %s(void){}\n" % lg,
               T=t, V=v, N=n, P=p, LOC=lc, ST=st, FN=fn, LG=lg, ID=idn)
    return add(family, name, body, expect)


# --- Deterministic seeded sweeps ----------------------------------------------
PER_FAMILY = 8
_id = 0
for family in FAMILIES:
    expect, templates = FAMILIES[family]
    for k in range(PER_FAMILY):
        build(family, templates[k % len(templates)], expect,
              "%s_%03d" % (family, k), _id)
        _id += 1


# --- Seeded mutation pass: re-roll until TARGET distinct programs exist. ------
def seeded_extra(start=0):
    families = list(FAMILIES.items())
    i = start
    while True:
        family, (expect, templates) = families[RNG.randrange(len(families))]
        template = templates[RNG.randrange(len(templates))]
        yield family, ("m_%s_%04d" % (family, i)), template, expect, 10000 + i
        i += 1


_extra = seeded_extra(0)
_i = 0
while len(CASES) < TARGET:
    _family, _name, _tmpl, _expect, _idn = next(_extra)
    build(_family, _tmpl, _expect, _name, _idn)
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
    print("hv_null: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
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
