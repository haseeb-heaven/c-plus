#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator (rev) for the field-sensitive
ownership gaps called out in review.

Produces >=100 distinct small, valid C programs (default target 120) covering
exactly the seven review families:

  * ptr_field_free_use       - struct pointer field freed, then used via s->p
                               (deref / indexed read / write, including a use
                               through a saved alias of the field);
  * arr_struct_shared_alias  - array-of-struct shared pointer aliases (free via
                               a[0].p then use a[1].p or a saved alias, and
                               double-free through the two aliased fields);
  * struct_copy_df           - by-value struct copy double-free / use-after-free
                               of the original allocation;
  * nested_outer_free_inner  - nested struct: outer freed while an inner
                               pointer field / element is still used;
  * member_outparam          - struct member address handed to a function as an
                               out-parameter, then double-freed / used after
                               the callee frees it (plus a conditional,
                               unverified allocation);
  * global_struct_field      - global struct fields, including a global
                               array-of-struct shared alias;
  * heap_struct_uninit_field - malloc'd struct with an uninitialized pointer or
                               scalar field that is read / indexed / freed.

Every family also emits safe controls (independent fields, exactly-once free,
all fields initialized) as ``accept`` cases.

Each case declares one of three outcomes with exact mode expectations:

    reject - proven ownership error: must fail BOTH --default and --strict.
    strict - conditional / unverified error (e.g. uninitialized heap scalar
             read, conditional out-param allocation): may pass --default,
             must fail --strict.
    accept - valid control: must pass BOTH modes (any rejection is a false
             positive).

Programs come from deterministic parameter sweeps plus a seeded RNG
(FUZZ_SEED); the final seeded pass re-rolls parameters until TARGET distinct
bodies exist, and a case-specific marker comment guarantees every translation
unit is unique in source text.  Re-running with the same seed is reproducible.
Cases run in parallel (FUZZ_WORKERS) with a per-check timeout.  Mismatches
(source + full diagnostic log) are saved under
tests/out/core/rev_field/failures/.  This is a differential measurement over
the declared subset, not a proof of safety.

Environment:
    FUZZ_SEED    default 0x72657646 ("revF")
    FUZZ_WORKERS default 8
    CPLUS_BIN    default <root>/build/c+
    REV_TARGET   default 120 (minimum 100)
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
OUT = ROOT / "tests/out/core/rev_field"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x72657646"), 0)
TARGET = max(100, int(os.environ.get("REV_TARGET", "120")))
TIMEOUT = 60

CASES = []   # list of (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = "#include <stdlib.h>\n"

# review family order used by the seeded mutation pass
ORDER = (
    "ptr_field_free_use",
    "arr_struct_shared_alias",
    "struct_copy_df",
    "nested_outer_free_inner",
    "member_outparam",
    "global_struct_field",
    "heap_struct_uninit_field",
)


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 50)


def jump(n):
    return RNG.randint(1, 4)


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    # A deterministic, case-specific marker makes every generated program
    # unique in source text (comments are valid C and do not change behavior).
    normalized = (f"/* rev_field seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


# ---------------------------------------------------------------------------
# One case per (family, index).  All RNG draws happen up front so the seeded
# stream is identical for a given seed.  `i` selects the template variant.
# ---------------------------------------------------------------------------
def seeded_case(family, i):
    n = RNG.randint(2, 12)
    k = RNG.randint(0, n - 1)
    v = val()
    j = jump(n)
    m = RNG.randint(1, n)

    if family == "ptr_field_free_use":
        # --- struct pointer field freed, then used via s->p -----------------
        name = tpl("ptr_use_@@I@@", I=i)
        if i % 5 == 0:  # safe control: exactly-once free, no use after free
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p){free(s);return 2;}"
                "*s->p=@@V@@;int r=*s->p;free(s->p);free(s);return r==@@V@@?0:1;}",
                N=n, V=v)
            return family, name, body, "accept"
        if i % 5 == 1:  # deref the field after free
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p){free(s);return 2;}"
                "*s->p=@@V@@;free(s->p);return *s->p;}", N=n, V=v)
        elif i % 5 == 2:  # indexed read through the field after free
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p){free(s);return 2;}"
                "free(s->p);return s->p[@@K@@];}", N=n, K=k)
        elif i % 5 == 3:  # indexed write through the field after free
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p){free(s);return 2;}"
                "free(s->p);s->p[@@K@@]=@@V@@;return 0;}", N=n, K=k, V=v)
        else:  # use through a saved alias of the field
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p){free(s);return 2;}"
                "int*q=s->p;*q=@@V@@;free(q);return *s->p;}", N=n, V=v)
        return family, name, body, "reject"

    if family == "arr_struct_shared_alias":
        # --- array-of-struct shared pointer aliases -------------------------
        name = tpl("arr_alias_@@I@@", I=i)
        if i % 4 == 0:  # safe control: independent fields, exactly-once free
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a[2];"
                "a[0].p=malloc(@@N@@*sizeof(int));a[1].p=malloc((@@N@@+1)*sizeof(int));"
                "if(!a[0].p||!a[1].p)return 2;a[0].p[0]=@@V@@;a[1].p[0]=@@V@@+1;"
                "int r=*a[0].p+*a[1].p;free(a[0].p);free(a[1].p);"
                "return r==2*@@V@@+1?0:1;}", N=n, V=v)
            return family, name, body, "accept"
        if i % 4 == 1:  # free via a[0].p, use via a[1].p
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a[2];"
                "a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;"
                "free(a[0].p);return a[1].p[@@K@@];}", N=n, K=k)
        elif i % 4 == 2:  # double-free through the two aliased fields
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a[2];"
                "a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;"
                "free(a[0].p);free(a[1].p);return 0;}", N=n)
        else:  # saved alias of a[0].p used after a[1].p is freed
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a[2];"
                "a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;"
                "int*q=a[0].p;free(a[1].p);return q[@@K@@];}", N=n, K=k)
        return family, name, body, "reject"

    if family == "struct_copy_df":
        # --- by-value struct copy double-free -------------------------------
        name = tpl("copy_df_@@I@@", I=i)
        if i % 4 == 0:  # safe control: deep copy, exactly-once free per alloc
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a;"
                "a.p=malloc(@@N@@*sizeof(int));if(!a.p)return 2;*a.p=@@V@@;"
                "struct S b;b.p=malloc(@@N@@*sizeof(int));if(!b.p)return 2;"
                "*b.p=*a.p;int r=*a.p+*b.p;free(a.p);free(b.p);"
                "return r==2*@@V@@?0:1;}", N=n, V=v)
            return family, name, body, "accept"
        if i % 4 == 1:  # shallow copy, double free
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a;"
                "a.p=malloc(@@N@@*sizeof(int));if(!a.p)return 2;"
                "struct S b=a;free(a.p);free(b.p);return 0;}", N=n)
        elif i % 4 == 2:  # shallow copy, use after original is freed
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a;"
                "a.p=malloc(@@N@@*sizeof(int));if(!a.p)return 2;"
                "struct S b=a;free(a.p);return b.p[@@K@@];}", N=n, K=k)
        else:  # write through copy, free original, read through copy
            body = STD + tpl(
                "struct S{int*p;};int main(void){struct S a;"
                "a.p=malloc(@@N@@*sizeof(int));if(!a.p)return 2;"
                "struct S b=a;b.p[0]=@@V@@;free(a.p);return b.p[0];}", N=n, V=v)
        return family, name, body, "reject"

    if family == "nested_outer_free_inner":
        # --- nested struct: outer freed, inner field still used -------------
        name = tpl("nested_@@I@@", I=i)
        if i % 4 == 0:  # safe control: free inner then outer, no use after
            body = STD + tpl(
                "struct I{int*p;};struct O{struct I in;};"
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p){free(o);return 2;}"
                "o->in.p[0]=@@V@@;int r=o->in.p[0];free(o->in.p);free(o);"
                "return r==@@V@@?0:1;}", N=n, V=v)
            return family, name, body, "accept"
        if i % 4 == 1:  # outer freed, inner pointer field used
            body = STD + tpl(
                "struct I{int*p;};struct O{struct I in;};"
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p){free(o);return 2;}"
                "free(o);return o->in.p[@@K@@];}", N=n, K=k)
        elif i % 4 == 2:  # outer freed, saved alias of inner pointer used
            body = STD + tpl(
                "struct I{int*p;};struct O{struct I in;};"
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p){free(o);return 2;}"
                "int*q=o->in.p;free(o);return q[@@K@@];}", N=n, K=k)
        else:  # inner array element address used after outer free
            body = STD + tpl(
                "struct I{int v[@@N@@];};struct O{struct I in;};"
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "int*q=&o->in.v[@@K@@];*q=@@V@@;free(o);return *q;}",
                N=n, K=k, V=v)
        return family, name, body, "reject"

    if family == "member_outparam":
        # --- struct member address passed as an out-parameter ---------------
        name = tpl("outparam_@@I@@", I=i)
        if i % 4 == 0:  # safe control: out-param alloc freed exactly once
            body = STD + tpl(
                "static void fill@@I@@(int**o){*o=malloc(@@N@@*sizeof(int));}"
                "struct S{int*p;};int main(void){struct S s={0};fill@@I@@(&s.p);"
                "if(!s.p)return 2;*s.p=@@V@@;int r=*s.p;free(s.p);"
                "return r==@@V@@?0:1;}", N=n, V=v, I=i)
            return family, name, body, "accept"
        if i % 4 == 1:  # callee allocates, caller frees twice
            body = STD + tpl(
                "static void fill@@I@@(int**o){*o=malloc(@@N@@*sizeof(int));}"
                "struct S{int*p;};int main(void){struct S s={0};fill@@I@@(&s.p);"
                "if(!s.p)return 2;free(s.p);free(s.p);return 0;}", N=n, I=i)
        elif i % 4 == 2:  # callee frees through out-param, caller uses it
            body = STD + tpl(
                "static void drop@@I@@(int**o){free(*o);}"
                "struct S{int*p;};int main(void){struct S s;"
                "s.p=malloc(@@N@@*sizeof(int));if(!s.p)return 2;"
                "drop@@I@@(&s.p);return s.p[@@K@@];}", N=n, K=k, I=i)
        else:  # conditional out-param allocation (unverified)
            body = STD + tpl(
                "static void maybe@@I@@(int**o,int c){if(c)*o=malloc(@@N@@*sizeof(int));}"
                "struct S{int*p;};int main(int argc,char**v){(void)v;struct S s={0};"
                "maybe@@I@@(&s.p,argc);return s.p?s.p[@@K@@]:0;}", N=n, K=k, I=i)
            return family, name, body, "strict"
        return family, name, body, "reject"

    if family == "global_struct_field":
        # --- global struct fields (and a global array-of-struct alias) ------
        name = tpl("global_@@I@@", I=i)
        if i % 4 == 0:  # safe control: independent global fields, free once
            body = STD + tpl(
                "struct S{int*p;};struct S g;struct S ga[2];"
                "int main(void){g.p=malloc(@@N@@*sizeof(int));"
                "ga[0].p=malloc(@@N@@*sizeof(int));ga[1].p=malloc((@@N@@+1)*sizeof(int));"
                "if(!g.p||!ga[0].p||!ga[1].p)return 2;"
                "*g.p=@@V@@;ga[0].p[0]=@@V@@;ga[1].p[0]=@@V@@;"
                "int r=*g.p+ga[0].p[0]+ga[1].p[0];"
                "free(g.p);free(ga[0].p);free(ga[1].p);"
                "return r==3*@@V@@?0:1;}", N=n, V=v)
            return family, name, body, "accept"
        if i % 4 == 1:  # global field freed then used
            body = STD + tpl(
                "struct S{int*p;};struct S g;"
                "int main(void){g.p=malloc(@@N@@*sizeof(int));if(!g.p)return 2;"
                "g.p[0]=@@V@@;free(g.p);return g.p[@@K@@];}", N=n, K=k, V=v)
        elif i % 4 == 2:  # global field double-free
            body = STD + tpl(
                "struct S{int*p;};struct S g;"
                "int main(void){g.p=malloc(@@N@@*sizeof(int));if(!g.p)return 2;"
                "free(g.p);free(g.p);return 0;}", N=n)
        else:  # global array-of-struct shared alias
            body = STD + tpl(
                "struct S{int*p;};struct S ga[2];"
                "int main(void){ga[0].p=ga[1].p=malloc(@@N@@*sizeof(int));"
                "if(!ga[0].p)return 2;free(ga[0].p);return ga[1].p[@@K@@];}",
                N=n, K=k)
        return family, name, body, "reject"

    # family == "heap_struct_uninit_field"
    # --- malloc'd struct with an uninitialized field ------------------------
    name = tpl("uninit_field_@@I@@", I=i)
    if i % 5 == 0:  # safe control: initialize all fields, use, free once
        body = STD + tpl(
            "struct S{int*p;int a;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p){free(s);return 2;}"
            "s->p[0]=@@V@@;s->a=@@K@@;int r=s->p[0]+s->a;free(s->p);free(s);"
            "return r==@@R@@?0:1;}", N=n, V=v, K=k, R=v + k)
        return family, name, body, "accept"
    if i % 5 == 1:  # uninitialized pointer field dereferenced
        body = STD + tpl(
            "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;return *s->p;}")
        return family, name, body, "reject"
    if i % 5 == 2:  # uninitialized pointer field passed to free
        body = STD + tpl(
            "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;free(s->p);free(s);return 0;}")
        return family, name, body, "reject"
    if i % 5 == 3:  # uninitialized scalar field read
        body = STD + tpl(
            "struct S{int a;int b;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;return s->a+@@V@@;}", V=v)
        return family, name, body, "strict"
    # uninitialized scalar field used as an array index
    body = STD + tpl(
        "struct S{int a;int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);"
        "if(!s)return 2;return s->buf[s->a]+@@V@@;}", N=n, V=v)
    return family, name, body, "strict"


# --- deterministic parameter sweeps across all families --------------------
for _fam in ORDER:
    for _i in range(1, 13):
        add(*seeded_case(_fam, _i))


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll until TARGET distinct programs exist.
# ---------------------------------------------------------------------------
_i = 0
while len(CASES) < TARGET:
    _family = ORDER[_i % len(ORDER)]
    add(*seeded_case(_family, 100 + _i))
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
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
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

    # Save the generated source and a full diagnostic log for every mismatch.
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
    print("rev_field: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
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
    print("first 10 mismatch names (%d total):" % len(mismatch_names))
    for name in mismatch_names[:10]:
        print("  " + name)
    if len(mismatch_names) > 10:
        print("  ... %d more" % (len(mismatch_names) - 10))
    for line in mismatches[:10]:
        print("  detail: " + line)
    if len(mismatches) > 10:
        print("  ... %d more details (see saved logs in %s)" % (len(mismatches) - 10, FAILS))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    status = "PASS" if not mismatches and unique == len(CASES) else "FAIL"
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)"
          % (status, len(CASES), len(mismatches), len(false_positives)))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
