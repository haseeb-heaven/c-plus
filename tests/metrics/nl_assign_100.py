#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator for NULL-ASSIGNMENT forms (>=100 cases).

Produces distinct, self-contained, valid C translation units that exercise the
different syntactic forms of assigning a null pointer to an object pointer.

Every case falls into one of two outcome classes:

  reject - the pointer is assigned a null value and is then DEREFERENCED
           unguarded.  The dereference is undefined behaviour at runtime, so a
           correct checker must fail BOTH --default and --strict.  Acceptance
           in either mode is a false negative / hole.
  accept - safe control: the pointer may hold null, but every dereference is
           guarded (or never performed).  A correct checker must pass BOTH
           modes; rejection here is a false positive.

NULL-ASSIGNMENT FORMS (one family each):

  * null_macro    - p = NULL
  * zero_lit      - p = 0
  * void_cast     - p = (void *)0
  * int_cast_null - p = (int *)NULL
  * hex_zero      - p = 0x0
  * uintptr_cast  - p = (int *)(uintptr_t)0
  * cond_assign   - p = c ? NULL : p      (conditional assignment)
  * chained       - p = q = NULL          (chained assignment)
  * in_if         - assignment inside an if statement
  * in_loop       - assignment inside a for loop
  * comma_expr    - p = (x, NULL)         (comma expression)
  * macro_null    - project-defined macro that expands to null
  * typedef_null  - assignment through a pointer typedef
  * struct_field  - s.p = NULL            (struct field)
  * out_param     - *out = NULL           (out-parameter)

Each family contributes reject cases and interleaved safe controls; parameters
(type, value, identifier, loop bounds) come from a seeded RNG so every program
is distinct.  Cases run in parallel (FUZZ_WORKERS) with a per-check timeout.
Mismatches and their combined diagnostics are saved under
tests/out/core/nl_assign/failures/.  This is a differential measurement, not a
proof of safety, and does not modify the compiler.

Environment:
  FUZZ_SEED    default 0x4E4C4153
  FUZZ_WORKERS default 8
  CPLUS_BIN    default <root>/build/c+
  NL_TARGET    default 120 (minimum 100)
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
OUT = ROOT / "tests/out/core/nl_assign"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4E4C4153"), 0)
TARGET = max(100, int(os.environ.get("NL_TARGET", "120")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # normalized program bodies, enforces distinctness
RNG = random.Random(SEED)

STDDEF = "#include <stddef.h>\n"
STDINT = "#include <stdint.h>\n"
TYPES = ("int", "long", "short", "char")
VARS = ("p", "ptr", "vp", "xp", "np")

FAMILIES = (
    "null_macro", "zero_lit", "void_cast", "int_cast_null", "hex_zero",
    "uintptr_cast", "cond_assign", "chained", "in_if", "in_loop",
    "comma_expr", "macro_null", "typedef_null", "struct_field", "out_param",
)


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "accept"), (name, expect)
    normalized = ("/* nl_assign seed=0x%X case=%s family=%s */\n"
                  % (SEED, name, family)) + body.strip() + "\n"
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


def seeded_case(fam, i):
    """Build one deterministic (family, name, body, expect) tuple."""
    T, V, VAR = RNG.choice(TYPES), RNG.randint(1, 97), RNG.choice(VARS)
    name = "%s_%03d" % (fam, i)
    reject = (i % 2 == 0)
    h = STDDEF

    if fam == "null_macro":  # p = NULL
        if reject:
            body = h + tpl("int main(void){@@T@@ *@@VAR@@=NULL;return (int)*@@VAR@@;}",
                           T=T, VAR=VAR)
        else:
            body = h + tpl("int main(void){@@T@@ *@@VAR@@=NULL;"
                           "if(@@VAR@@)return (int)*@@VAR@@;return @@V@@;}",
                           T=T, VAR=VAR, V=V)

    elif fam == "zero_lit":  # p = 0
        if reject:
            body = tpl("int main(void){@@T@@ *@@VAR@@=0;return (int)*@@VAR@@;}",
                       T=T, VAR=VAR)
        else:
            body = tpl("int main(void){@@T@@ *@@VAR@@=0;"
                       "if(!@@VAR@@)return @@V@@;return (int)*@@VAR@@;}",
                       T=T, VAR=VAR, V=V)

    elif fam == "void_cast":  # p = (void *)0
        if reject:
            body = h + tpl("int main(void){@@T@@ *@@VAR@@=(void*)0;return (int)*@@VAR@@;}",
                           T=T, VAR=VAR)
        else:
            body = h + tpl("int main(void){@@T@@ *@@VAR@@=(void*)0;"
                           "if(@@VAR@@)return (int)*@@VAR@@;return @@V@@;}",
                           T=T, VAR=VAR, V=V)

    elif fam == "int_cast_null":  # p = (int *)NULL
        if reject:
            body = h + tpl("int main(void){int *@@VAR@@=(int*)NULL;return *@@VAR@@;}",
                           VAR=VAR)
        else:
            body = h + tpl("int main(void){int *@@VAR@@=(int*)NULL;"
                           "if(@@VAR@@)return *@@VAR@@;return @@V@@;}",
                           VAR=VAR, V=V)

    elif fam == "hex_zero":  # p = 0x0
        if reject:
            body = tpl("int main(void){@@T@@ *@@VAR@@=0x0;return (int)*@@VAR@@;}",
                       T=T, VAR=VAR)
        else:
            body = tpl("int main(void){@@T@@ *@@VAR@@=0x0;"
                       "if(@@VAR@@)return (int)*@@VAR@@;return @@V@@;}",
                       T=T, VAR=VAR, V=V)

    elif fam == "uintptr_cast":  # p = (int *)(uintptr_t)0
        inc = STDINT + STDDEF
        if reject:
            body = inc + tpl("int main(void){int *@@VAR@@=(int*)(uintptr_t)0;"
                             "return *@@VAR@@;}", VAR=VAR)
        else:
            body = inc + tpl("int main(void){int *@@VAR@@=(int*)(uintptr_t)0;"
                             "if(@@VAR@@)return *@@VAR@@;return @@V@@;}",
                             VAR=VAR, V=V)

    elif fam == "cond_assign":  # p = c ? NULL : p
        if reject:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;int c=1;"
                           "p=c?NULL:p;return *p;}", V=V)
        else:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;int c=0;"
                           "p=c?NULL:p;if(p)return *p;return @@V@@;}", V=V)

    elif fam == "chained":  # p = q = NULL
        if reject:
            body = h + tpl("int main(void){int *p,*q;p=q=NULL;return *p;}")
        else:
            body = h + tpl("int main(void){int *p,*q;p=q=NULL;"
                           "if(p)return *p;return @@V@@;}", V=V)

    elif fam == "in_if":  # assignment inside an if statement
        if reject:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;"
                           "if(1)p=NULL;return *p;}", V=V)
        else:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;"
                           "if(0)p=NULL;if(p)return *p;return @@V@@;}", V=V)

    elif fam == "in_loop":  # assignment inside a loop
        _loops = RNG.randint(1, 3)
        if reject:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;"
                           "for(int i=0;i<@@N@@;i++)p=NULL;return *p;}", V=V, N=_loops)
        else:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;"
                           "for(int i=0;i<0;i++)p=NULL;if(p)return *p;return @@V@@;}",
                           V=V)

    elif fam == "comma_expr":  # p = (x, NULL)
        if reject:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;"
                           "p=(x,NULL);return *p;}", V=V)
        else:
            body = h + tpl("int main(void){int x=@@V@@;int *p=&x;"
                           "p=(x,&x);if(p)return *p;return @@V@@;}", V=V)

    elif fam == "macro_null":  # macro that expands to null
        _macro = "MYNULL_%d" % i
        if reject:
            body = h + tpl("#define @@M@@ ((void*)0)\n"
                           "int main(void){int *p=@@M@@;return *p;}", M=_macro)
        else:
            body = h + tpl("#define @@M@@ ((void*)0)\n"
                           "int main(void){int *p=@@M@@;if(p)return *p;return @@V@@;}",
                           M=_macro, V=V)

    elif fam == "typedef_null":  # assignment through a pointer typedef
        _td = "P%d" % i
        if reject:
            body = h + tpl("typedef @@T@@ *@@TD@@;\n"
                           "int main(void){@@TD@@ p=NULL;return (int)*p;}", T=T, TD=_td)
        else:
            body = h + tpl("typedef @@T@@ *@@TD@@;\n"
                           "int main(void){@@TD@@ p=NULL;if(p)return (int)*p;return @@V@@;}",
                           T=T, TD=_td, V=V)

    elif fam == "struct_field":  # s.p = NULL
        _st = "S%d" % i
        if reject:
            body = h + tpl("struct @@ST@@{int *p;};\n"
                           "int main(void){struct @@ST@@ s;s.p=NULL;return *s.p;}", ST=_st)
        else:
            body = h + tpl("struct @@ST@@{int *p;};\n"
                           "int main(void){struct @@ST@@ s;s.p=NULL;"
                           "if(s.p)return *s.p;return @@V@@;}", ST=_st, V=V)

    else:  # out_param: *out = NULL
        _fn = "set_null_%d" % i
        if reject:
            body = h + tpl("void @@FN@@(int **o){*o=NULL;}\n"
                           "int main(void){int *p;@@FN@@(&p);return *p;}", FN=_fn)
        else:
            body = h + tpl("void @@FN@@(int **o){*o=NULL;}\n"
                           "int main(void){int *p;@@FN@@(&p);if(p)return *p;return @@V@@;}",
                           FN=_fn, V=V)

    return (fam, name, body, "reject" if reject else "accept")


# --- Generate a deterministic seeded sweep, enough to reach TARGET. ---------
_PER_FAMILY = max(7, TARGET // len(FAMILIES) + 1)
for _fi, _fam in enumerate(FAMILIES):
    for _j in range(_PER_FAMILY):
        add(*seeded_case(_fam, _fi * _PER_FAMILY + _j))

# --- Fill to TARGET with additional seeded variants if needed. --------------
_extra = 0
while len(CASES) < TARGET:
    add(*seeded_case("extra_%d" % (_extra % len(FAMILIES)),
                     10000 + _extra))
    _extra += 1
    if _extra > TARGET * 50:
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
            reasons.append("null deref accepted in DEFAULT")
        if not s_rej:
            reasons.append("null deref accepted in STRICT")
    else:  # accept
        false_positive = d_rej or s_rej
        if d_rej:
            reasons.append("safe control rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe control rejected in STRICT (false positive)")

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
    print("nl_assign: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d accept=%d"
          % (counts["reject"], counts["accept"]))
    print("families (%d): %s" % (len(families),
          ", ".join("%s=%d" % kv for kv in sorted(families.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(run_case, CASES))

    mismatched = [r for r in results if r["reasons"]]
    mismatch_names = sorted(r["name"] for r in mismatched)
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    false_negatives = sum(1 for r in results if r["expect"] != "accept" and r["reasons"])
    hard = sum(1 for r in mismatched
               if any("TIMEOUT" in x or "OS ERROR" in x or "CRASH" in x
                      for x in r["reasons"]))

    print("results: %d cases, %d mismatches (%d false negatives, %d false positives, %d hard errors)"
          % (len(CASES), len(mismatched), false_negatives, len(false_positives), hard))
    print("false positives: %d" % len(false_positives))
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print("  ... %d more" % (len(false_positives) - 20))
    print("first 10 mismatch names:")
    for nm in mismatch_names[:10]:
        print("  " + nm)
    if len(mismatch_names) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatch_names) - 10))
    if mismatched:
        print("saved %d mismatch fixtures in %s" % (len(mismatched), FAILS))
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        "FAIL" if (mismatched or unique != len(CASES)) else "PASS",
        len(CASES), len(mismatched), len(false_positives)))
    return 1 if mismatched or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
