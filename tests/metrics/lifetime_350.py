#!/usr/bin/env python3
"""Deterministic GENERATED LIFETIME/OWNERSHIP corpus (350+ distinct cases).

Every case is produced programmatically from parameter sweeps plus a seeded
RNG (FUZZ_SEED, default 0xA11F3).  Nothing is hand-copied from another corpus:
each program varies in allocation size, alias count, subscript/offset, nested
scope depth, branch/loop shape, realloc pattern, literal length, etc.

Each case declares an expected outcome:

  reject - proven lifetime/ownership error: must fail BOTH --default and
           --strict (accepting it in either mode is a safety hole).
  strict - conditional / unverified lifetime error: MAY pass --default, but
           must fail --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Any mismatch (unsafe accepted or safe rejected) is printed and the generated
source plus a diagnostic log is saved under
tests/out/core/lifetime350/failures/.  Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout.  The generator is seeded, so a run is
reproducible.  This is a differential measurement, not a proof of safety.
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/lifetime350"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA11F3"), 0)
TIMEOUT = 60

CASES = []  # list of (name, body, expect)
SEEN = set()  # exact generated program bodies, used to enforce distinctness

STD = "#include <stdlib.h>\n"
RES = '#include "cplus/cplus.h"\n'
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def lit(n):
    """A distinct string literal of length >= 1."""
    return ALPHABET[:(n % (len(ALPHABET) - 1)) + 1]


def chain(count):
    """Declarations of 'count' aliases of p, returned as one C statement run."""
    return " ".join("int *a%d=p;" % j for j in range(count))


def last(count):
    return "a%d" % (count - 1)


def add(name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append((name, normalized, expect))


# ---------------------------------------------------------------------------
# Family 1: direct use-after-free sweeps over allocation size (48)
# ---------------------------------------------------------------------------
for _n in range(1, 13):
    add(tpl("uaf_direct_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;free(p);return *p;}""", N=_n), "reject")
    add(tpl("uaf_subscript_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;free(p);return p[@@K@@];}""", N=_n, K=_n - 1), "reject")
    add(tpl("uaf_write_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;free(p);*p=@@V@@;return 0;}""", N=_n, V=_n * 7), "reject")
    add(tpl("uaf_incr_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;free(p);(*p)++;return 0;}""", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 2: dangling alias chains (15)
# ---------------------------------------------------------------------------
for _a in range(1, 6):
    _n = _a + 4
    _ch = chain(_a)
    _l = last(_a)
    add(tpl("uaf_alias_chain_@@A@@", A=_a), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;@@CH@@ free(p);return *@@L@@;}""", N=_n, CH=_ch, L=_l), "reject")
    add(tpl("uaf_alias_idx_@@A@@", A=_a), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;@@CH@@ free(p);return @@L@@[@@A@@];}""", N=_n, CH=_ch, L=_l, A=_a), "reject")
    add(tpl("uaf_alias_write_@@A@@", A=_a), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;@@CH@@ free(p);*@@L@@=@@A@@;return 0;}""", N=_n, CH=_ch, L=_l, A=_a), "reject")

# ---------------------------------------------------------------------------
# Family 3: double free sweeps (16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("df_same_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@);if(!p)return 2;free(p);free(p);return 0;}""", N=_n), "reject")
    add(tpl("df_alias_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@);if(!p)return 2;int *q=p;free(p);free(q);return 0;}""", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 4: invalid free sweeps (32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("free_stack_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int a[@@N@@]={0};free(a);return 0;}""", N=_n), "reject")
    add(tpl("free_global_@@N@@", N=_n), tpl("""#include <stdlib.h>
static int g[@@N@@];int main(void){free(&g[0]);return 0;}""", N=_n), "reject")
    add(tpl("free_literal_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){const char *s="@@S@@";free((void*)s);return 0;}""", S=lit(_n)), "reject")
    add(tpl("free_interior_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc((@@N@@+1)*sizeof(int));if(!p)return 2;free(p+1);return 0;}""", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 5: realloc success / failure / loss patterns (32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("realiastale_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;int *q=p;void *t=realloc(p,(@@N@@+2)*sizeof(int));if(t){p=t;return *q;}free(p);return 0;}""", N=_n), "reject")
    add(tpl("realloc_loss_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@);if(!p)return 2;p=realloc(p,@@M@@);free(p);return 0;}""", N=_n, M=_n + 4), "reject")
    add(tpl("realloc_safe_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;void *t=realloc(p,(@@N@@+2)*sizeof(int));if(t)p=t;else{free(p);return 2;}free(p);return 0;}""", N=_n), "accept")
    add(tpl("realloc_fail_uaf_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;void *t=realloc(p,(size_t)-1);if(!t){free(p);return *p;}free(t);return 0;}""", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 6: stack / scope escapes (16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("stack_ret_@@N@@", N=_n), tpl("""#include <stdlib.h>
int *f(void){int a[@@N@@];return &a[0];}
int main(void){int *q=f();return *q;}""", N=_n), "reject")
    add(tpl("scope_escape_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p;{int x=@@N@@;p=&x;}return *p;}""", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 7: field / array-element ownership (24)
# ---------------------------------------------------------------------------
for _n in range(1, 7):
    add(tpl("field_alias_uaf_@@N@@", N=_n), tpl("""#include <stdlib.h>
struct S{int x[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;int*p=&s->x[@@K@@];free(s);return *p;}""", N=_n, K=_n - 1), "reject")
    add(tpl("arr_elem_df_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *a[2];a[0]=a[1]=malloc(@@N@@*sizeof(int));if(!a[0])return 2;free(a[0]);free(a[1]);return 0;}""", N=_n), "reject")
    add(tpl("field_free_use_@@N@@", N=_n), tpl("""#include <stdlib.h>
struct S{int *p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;free(s->p);return *s->p;}""", N=_n), "reject")
    add(tpl("nested_field_@@N@@", N=_n), tpl("""#include <stdlib.h>
struct I{int v[@@N@@];};struct O{struct I in;};int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;int*p=&o->in.v[@@K@@];free(o);return *p;}""", N=_n, K=_n - 1), "reject")

# ---------------------------------------------------------------------------
# Family 8: function returns of freed pointers (24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("ret_freed_@@N@@", N=_n), tpl("""#include <stdlib.h>
int *f(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 0;free(p);return p;}
int main(void){int *q=f();if(!q)return 1;return *q;}""", N=_n), "reject")
    add(tpl("ret_freed_branch_@@N@@", N=_n), tpl("""#include <stdlib.h>
int *f(int c){int *p=malloc(@@N@@*sizeof(int));if(!p)return 0;if(c)free(p);return p;}
int main(int argc,char **v){(void)v;int *q=f(argc);if(!q)return 1;return *q;}""", N=_n), "strict")
    add(tpl("callee_free_@@N@@", N=_n), tpl("""#include <stdlib.h>
void rel(int *p){free(p);}
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;rel(p);return *p;}""", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 9: resource moves / managed ownership (30)
# ---------------------------------------------------------------------------
for _n in range(1, 7):
    _r = "resource struct R{int x[%d];};void R_drop(R*r){(void)r;}" % _n
    add(tpl("resource_copy_@@N@@", N=_n), RES + _r + tpl("""int main(void){R a={{0}};R b=a;(void)b;return 0;}"""), "reject")
    add(tpl("resource_move_@@N@@", N=_n), RES + _r + tpl("""int main(void){R a={{0}};R b=cplus_move(a);(void)b;return 0;}"""), "accept")
    add(tpl("use_after_move_@@N@@", N=_n), RES + _r + tpl("""int main(void){R a={{0}};R b=cplus_move(a);(void)b;return a.x[@@K@@];}""", K=_n - 1), "strict")
    add(tpl("managed_escape_@@N@@", N=_n), RES + tpl("""int *f(void){managed int *p=cplus_new_array(int,@@N@@);return p;}int main(void){(void)f();return 0;}""", N=_n), "reject")
    add(tpl("resource_drop_use_@@N@@", N=_n), RES + _r + tpl("""int main(void){R a={{0}};R_drop(&a);return a.x[@@K@@];}""", K=_n - 1), "reject")

# ---------------------------------------------------------------------------
# Family 10: strict (branch/loop-shaped) possible UAF (16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("uaf_branch_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(int argc,char **v){(void)v;int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;if(argc>1)free(p);return *p;}""", N=_n), "strict")
    add(tpl("uaf_loop_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;for(int i=0;i<@@N@@;i++){if(i==@@K@@)free(p);}return *p;}""", N=_n, K=_n // 2), "strict")

# ---------------------------------------------------------------------------
# Family 11: valid lifetime controls (32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("safe_malloc_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@N@@;int v=*p;free(p);return v==@@N@@?0:1;}""", N=_n), "accept")
    add(tpl("safe_alias_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;int *q=p;*q=@@N@@;int v=*p;free(p);return v==@@N@@?0:1;}""", N=_n), "accept")
    add(tpl("safe_reset_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;free(p);p=0;return p?*p:0;}""", N=_n), "accept")
    add(tpl("safe_two_allocs_@@N@@", N=_n), tpl("""#include <stdlib.h>
int main(void){int *p=malloc(@@N@@*sizeof(int));int *q=malloc((@@N@@+1)*sizeof(int));if(!p||!q)return 2;*p=@@N@@;*q=@@N@@+1;int v=*p+*q;free(p);free(q);return v==@@R@@?0:1;}""", N=_n, R=2 * _n + 1), "accept")

# ---------------------------------------------------------------------------
# Family 12: seeded mutation fuzz over composable lifetime patterns (140)
# ---------------------------------------------------------------------------
_rng = random.Random(SEED)
for _i in range(140):
    # Re-roll the seeded parameters until the generated program is genuinely
    # distinct from every earlier one (a collision only wastes RNG draws).
    while True:
        _n = _rng.randint(2, 16)
        _a = _rng.randint(1, 5)
        _idx = _rng.randint(0, _a - 1)
        _off = _rng.randint(0, min(_a, _n) - 1)
        _k2 = _rng.randint(0, _n - 1)
        _val = _rng.randint(1, 50)
        _thresh = _rng.randint(1, 3)
        _ch = chain(_a)
        _l = last(_a)
        _kind = _i % 7
        if _kind == 0:
            _name = tpl("mut_alias_uaf_@@I@@", I=_i)
            _body = STD + tpl("""int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;@@CH@@ free(p);return @@L@@[@@O@@];}""", N=_n, CH=_ch, L=_l, O=_off)
            _expect = "reject"
        elif _kind == 1:
            _name = tpl("mut_df_alias_@@I@@", I=_i)
            _body = STD + tpl("""int main(void){int *p=malloc(@@N@@);if(!p)return 2;@@CH@@ free(p);free(@@L@@);return 0;}""", N=_n, CH=_ch, L=_l)
            _expect = "reject"
        elif _kind == 2:
            _name = tpl("mut_field_uaf_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int x[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;int*p=&s->x[@@K@@];free(s);return *p;}""", N=_n, K=_k2)
            _expect = "reject"
        elif _kind == 3:
            _writes = " ".join("a%d[%d]=%d;" % (j, j, j) for j in range(_a))
            _name = tpl("mut_safe_alias_@@I@@", I=_i)
            _body = STD + tpl("""int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;@@CH@@ @@W@@ int v=@@L@@[@@O@@];free(p);return v==@@O@@?0:1;}""", N=_n, CH=_ch, W=_writes, L=_l, O=_off)
            _expect = "accept"
        elif _kind == 4:
            _name = tpl("mut_realloc_stale_@@I@@", I=_i)
            _body = STD + tpl("""int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;int *q=p;void *t=realloc(p,(@@N@@+@@A@@)*sizeof(int));if(t){p=t;return q[@@O@@];}free(p);return 0;}""", N=_n, A=_a, O=_off)
            _expect = "reject"
        elif _kind == 5:
            _name = tpl("mut_branch_uaf_@@I@@", I=_i)
            _body = STD + tpl("""int main(int argc,char **v){(void)v;int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;if(argc>@@T@@)free(p);return p[@@O@@];}""", N=_n, T=_thresh, O=_off)
            _expect = "strict"
        else:
            _name = tpl("mut_scope_escape_@@I@@", I=_i)
            _body = STD + tpl("""int main(void){int *p;{int a[@@N@@];a[@@O@@]=@@V@@;p=&a[@@O@@];}return *p;}""", N=_n, O=_off, V=_val)
            _expect = "reject"
        if (_body.strip() + "\n") not in SEEN:
            add(_name, _body, _expect)
            break


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
    name, body, expect = case
    path = OUT / (name + ".c")
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
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT")
        if s_rej:
            reasons.append("safe rejected in STRICT")

    if not reasons:
        return name, expect, [], modes

    # Save the generated source and a full diagnostic log for every mismatch.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
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
    detail = ("%s [expect=%s] reasons=%s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  name, expect, ";".join(reasons),
                  d_rej, first_diag(modes["--default"][1]),
                  s_rej, first_diag(modes["--strict"][1])))
    return name, expect, [detail], modes


def main():
    # Identify duplicates defensively: a real generator must yield distinct
    # programs, not renaming of one identical case.
    bodies = [b for _, b, _ in CASES]
    unique = len(set(bodies))
    counts = {k: sum(1 for _, _, e in CASES if e == k)
              for k in ("reject", "strict", "accept")}

    print("lifetime fuzz: %d generated cases (%d distinct), seed=%s, workers=%d"
          % (len(CASES), unique, hex(SEED), WORKERS))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for _name, _expect, fails, _modes in pool.map(run_case, CASES):
            mismatches.extend(fails)

    print("results: %d cases, %d mismatches" % (len(CASES), len(mismatches)))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches) // 1, FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
