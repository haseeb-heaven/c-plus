#!/usr/bin/env python3
"""Deterministic GENERATED INITIALIZATION / LIFETIME-UB corpus (>=260 cases).

Every case is produced programmatically from parameter sweeps plus a seeded
RNG (FUZZ_SEED, default 0x1B17).  Nothing is hand-copied from another corpus:
each program varies in type, allocation/index size, literal value, scope
shape, branch/loop form, nested-struct depth, and literal contents.  All
generated bodies are distinct (enforced by a SEEN set at generation time).

Categories covered (13 families + seeded mutation fuzz):

  1  uninitialized scalar locals read
  2  conditional / loop init then read (and all-branches controls)
  3  uninitialized pointers dereferenced (read, write, subscript, alias)
  4  uninitialized struct fields read
  5  uninitialized heap reads
  6  goto bypassing initialization
  7  setjmp/longjmp clobbering non-volatile locals
  8  block-scope escapes (inner local, compound literal, switch-case local)
  9  VLA lifetime after backward goto
  10 returning address of a local
  11 use-after-scope
  12 partially initialized structs written externally
  13 missing destructor for nested resource fields
  14 seeded mutation fuzz over the composable patterns above

Each case declares an expected outcome:

  reject - proven initialization/lifetime error: must fail BOTH --default
           and --strict (accepting it in either mode is a safety hole).
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches (unsafe accepted or safe rejected) are printed and the generated
source plus a full diagnostic log is saved under
tests/out/core/ub_init/failures/.  Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout.  The generator is seeded, so a run
with the same FUZZ_SEED is reproducible.  This is a differential measurement,
not a proof of safety.
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/ub_init"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x1B17"), 0)
TIMEOUT = 60

CASES = []  # list of dict(name, body, expect, family)
SEEN = set()  # exact generated program bodies, used to enforce distinctness

STD = "#include <stdlib.h>\n"
SJ = "#include <setjmp.h>\n"
RES = '#include "cplus/cplus.h"\n'
TYPES = ("int", "short", "long", "char", "unsigned", "long long")


def tpl(template, **kw):
    """Token templating: avoids C-brace / printf escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({"name": name, "body": normalized,
                  "expect": expect, "family": family})


# ---------------------------------------------------------------------------
# Family 1: uninitialized scalar locals read (6 types x 8 sizes = 48)
# ---------------------------------------------------------------------------
for _ti, _t in enumerate(TYPES):
    for _n in range(1, 9):
        add("uninit_local", tpl("uninit_local_@@T@@_@@N@@", T=_ti, N=_n),
            tpl("""#include <stdio.h>
int main(void){@@T@@ x; @@T@@ pad[@@N@@]; (void)pad; printf("%d",(int)x); return 0;}""",
                T=_t, N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 2: conditional / loop init then read, plus all-branch controls (32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("cond_init", tpl("cond_init_branch_@@N@@", N=_n),
        tpl("int main(int argc,char**v){(void)v;int x;if(argc>@@N@@)x=@@N@@;return x;}", N=_n),
        "reject")
    add("cond_init", tpl("init_all_branches_@@N@@", N=_n),
        tpl("int main(int argc,char**v){(void)v;int x;if(argc>1)x=@@N@@;else x=@@N@@+1;return x;}", N=_n),
        "accept")
    add("cond_init", tpl("cond_init_loop_@@N@@", N=_n),
        tpl("int main(void){int x;for(int i=0;i<@@N@@;i++)if(i==@@N@@-1)x=i;return x;}", N=_n),
        "reject")
    add("cond_init", tpl("init_before_branch_@@N@@", N=_n),
        tpl("int main(int argc,char**v){(void)v;int x=@@N@@;if(argc>1)x=@@N@@+1;return x;}", N=_n),
        "accept")

# ---------------------------------------------------------------------------
# Family 3: uninitialized pointer dereferenced (4 shapes x 8 = 32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("uninit_ptr", tpl("uninit_ptr_read_@@N@@", N=_n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;return *p+@@N@@;}", N=_n), "reject")
    add("uninit_ptr", tpl("uninit_ptr_write_@@N@@", N=_n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;*p=@@N@@;return 0;}", N=_n), "reject")
    add("uninit_ptr", tpl("uninit_ptr_sub_@@N@@", N=_n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;p[1]=@@N@@;return p[0];}", N=_n), "reject")
    add("uninit_ptr", tpl("uninit_ptr_alias_@@N@@", N=_n),
        tpl("int main(void){int *p;int *q=p;int pad[@@N@@];(void)pad;return *q+@@N@@;}", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 4: uninitialized struct fields read (3 shapes x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("uninit_field", tpl("field_flat_@@N@@", N=_n),
        tpl("struct S{int a,b;};int main(void){struct S s;return s.a+@@N@@;}", N=_n), "strict")
    add("uninit_field", tpl("field_middle_@@N@@", N=_n),
        tpl("struct S{int a,b,c;};int main(void){struct S s;return s.b+@@N@@;}", N=_n), "strict")
    add("uninit_field", tpl("field_nested_@@N@@", N=_n),
        tpl("struct I{int v;};struct O{struct I in;};int main(void){struct O o;return o.in.v+@@N@@;}", N=_n),
        "strict")

# ---------------------------------------------------------------------------
# Family 5: uninitialized heap reads (3 shapes x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("uninit_heap", tpl("heap_read_@@N@@", N=_n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));return p[0]+@@N@@;}", N=_n),
        "strict")
    add("uninit_heap", tpl("heap_char_@@N@@", N=_n),
        STD + tpl("int main(void){int *p=malloc(@@N@@);return *(char*)p+@@N@@;}", N=_n), "strict")
    add("uninit_heap", tpl("heap_field_@@N@@", N=_n),
        STD + tpl("struct S{int x[@@N@@];};int main(void){struct S*p=malloc(sizeof*p);return p->x[0]+@@N@@;}", N=_n),
        "strict")

# ---------------------------------------------------------------------------
# Family 6: goto bypassing initialization (3 shapes x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("goto_bypass", tpl("goto_bypass_linear_@@N@@", N=_n),
        tpl("int main(void){int x;goto done;x=@@N@@;done:return x+@@N@@;}", N=_n), "reject")
    add("goto_bypass", tpl("goto_bypass_branch_@@N@@", N=_n),
        tpl("int main(int argc,char**v){(void)v;int x;if(argc>1)goto skip;x=@@N@@;skip:return x;}", N=_n),
        "reject")
    add("goto_bypass", tpl("goto_bypass_switch_@@N@@", N=_n),
        tpl("int main(void){int x;switch(@@N@@){default:goto done;}x=@@N@@;done:return x;}", N=_n),
        "reject")

# ---------------------------------------------------------------------------
# Family 7: setjmp/longjmp clobbering non-volatile locals (2 x 8 = 16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("setjmp_clobber", tpl("setjmp_one_@@N@@", N=_n),
        SJ + tpl("jmp_buf j;int main(void){int x=@@N@@;if(!setjmp(j)){x=@@N@@+1;longjmp(j,1);}return x;}", N=_n),
        "strict")
    add("setjmp_clobber", tpl("setjmp_two_@@N@@", N=_n),
        SJ + tpl("jmp_buf j;int main(void){int x=@@N@@,y=@@N@@+1;if(!setjmp(j)){x++;y++;longjmp(j,1);}return x+y;}", N=_n),
        "strict")

# ---------------------------------------------------------------------------
# Family 8: block-scope escapes (3 shapes x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("scope_escape", tpl("escape_inner_@@N@@", N=_n),
        tpl("int main(void){int *p;{int a[@@N@@];a[0]=@@N@@;p=&a[0];}return p[0];}", N=_n), "reject")
    add("scope_escape", tpl("escape_compound_@@N@@", N=_n),
        tpl("int *f(int n){int *p=(int[]){1,2,3};(void)n;return p;}int main(void){int*q=f(@@N@@);return q[0];}", N=_n),
        "reject")
    add("scope_escape", tpl("escape_switch_case_@@N@@", N=_n),
        tpl("int main(void){int *p;switch(1){case 1:{int x=@@N@@;p=&x;break;}default:p=0;break;}return p?*p:0;}", N=_n),
        "reject")

# ---------------------------------------------------------------------------
# Family 9: VLA lifetime after backward goto (2 shapes x 8 = 16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("vla_backgoto", tpl("vla_ptr_@@N@@", N=_n),
        tpl("int main(void){int n=@@N@@;int *p=0;int i=0;top:if(i==0){int a[n];a[0]=@@N@@;p=a;i++;goto top;}return p[0];}", N=_n),
        "reject")
    add("vla_backgoto", tpl("vla_ptrptr_@@N@@", N=_n),
        tpl("int main(void){int n=@@N@@;int **p=0;int i=0;top:if(i==0){int a[n];a[0]=@@N@@;p=&a;i++;goto top;}return (*p)[0];}", N=_n),
        "reject")

# ---------------------------------------------------------------------------
# Family 10: returning address of a local (2 shapes x 8 = 16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("ret_local", tpl("ret_local_arr_@@N@@", N=_n),
        tpl("int *f(void){int a[@@N@@];return &a[@@K@@];}int main(void){int*q=f();return *q;}", N=_n, K=_n - 1),
        "reject")
    add("ret_local", tpl("ret_local_struct_@@N@@", N=_n),
        tpl("struct S{int x[@@N@@];};struct S*f(void){struct S s;return &s;}int main(void){struct S*q=f();return q->x[0];}", N=_n),
        "reject")

# ---------------------------------------------------------------------------
# Family 11: use-after-scope (2 shapes x 8 = 16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("use_after_scope", tpl("uas_scalar_@@N@@", N=_n),
        tpl("int main(void){int *p;{int x=@@N@@;p=&x;}*p=@@N@@+1;return *p;}", N=_n), "reject")
    add("use_after_scope", tpl("uas_array_@@N@@", N=_n),
        tpl("int main(void){int *p;{int a[@@N@@];p=a;}p[0]=@@N@@;return p[0];}", N=_n), "reject")

# ---------------------------------------------------------------------------
# Family 12: partially initialized structs written externally (2 x 10 = 20)
# ---------------------------------------------------------------------------
for _n in range(1, 11):
    add("partial_ext", tpl("partial_ext_two_@@N@@", N=_n),
        tpl("struct S{int a,b;};void set(struct S*s){s->b=@@N@@;}int main(void){struct S s;set(&s);return s.a;}", N=_n),
        "strict")
    add("partial_ext", tpl("partial_ext_three_@@N@@", N=_n),
        tpl("struct S{int a,b,c;};void set(struct S*s){s->a=@@N@@;s->c=@@N@@;}int main(void){struct S s;set(&s);return s.b;}", N=_n),
        "strict")

# ---------------------------------------------------------------------------
# Family 13: missing destructor for nested resource fields (2 x 8 = 16)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("missing_drop", tpl("drop_plain_outer_@@N@@", N=_n),
        RES + tpl("resource struct In{int v;};void In_drop(In*i){(void)i;}struct Out{In in;int pad[@@N@@];};int main(void){struct Out o;(void)o;return 0;}", N=_n),
        "strict")
    add("missing_drop", tpl("drop_nested_resource_@@N@@", N=_n),
        RES + tpl("resource struct In{int v;};void In_drop(In*i){(void)i;}resource struct Out{In in;};void Out_drop(Out*o){(void)o;}int main(void){Out o={{@@N@@}};(void)o;return 0;}", N=_n),
        "strict")

# ---------------------------------------------------------------------------
# Family 14: seeded mutation fuzz over composable init/lifetime patterns (60)
# ---------------------------------------------------------------------------
FZ_RNG = random.Random(SEED)
FZ_TYPES = ("int", "short", "long", "char", "unsigned")
for _i in range(60):
    while True:
        _n = FZ_RNG.randint(2, 12)
        _v = FZ_RNG.randint(1, 60)
        _t = FZ_RNG.choice(FZ_TYPES)
        _kind = _i % 7
        if _kind == 0:
            _name = tpl("mut_uninit_local_@@I@@", I=_i)
            _body = STD + tpl("int main(void){@@T@@ x; @@T@@ pad[@@N@@]; (void)pad; return (int)x+@@V@@;}",
                              T=_t, N=_n, V=_v)
            _expect = "reject"
        elif _kind == 1:
            _name = tpl("mut_uninit_ptr_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p;int *q=p;int pad[@@N@@];(void)pad;return *q+@@V@@;}",
                              N=_n, V=_v)
            _expect = "reject"
        elif _kind == 2:
            _name = tpl("mut_cond_init_@@I@@", I=_i)
            _body = tpl("int main(int argc,char**v){(void)v;int x;if(argc>@@N@@)x=@@V@@;return x;}",
                        N=_n, V=_v)
            _expect = "reject"
        elif _kind == 3:
            _name = tpl("mut_struct_field_@@I@@", I=_i)
            _body = tpl("struct S{int a,b,c;};int main(void){struct S s;return s.b+@@N@@+@@V@@;}",
                        N=_n, V=_v)
            _expect = "strict"
        elif _kind == 4:
            _name = tpl("mut_heap_uninit_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));return p[0]+@@V@@;}",
                              N=_n, V=_v)
            _expect = "strict"
        elif _kind == 5:
            _name = tpl("mut_setjmp_@@I@@", I=_i)
            _body = SJ + tpl("jmp_buf j;int main(void){int x=@@V@@;if(!setjmp(j)){x+=@@N@@;longjmp(j,1);}return x;}",
                             N=_n, V=_v)
            _expect = "strict"
        else:
            _name = tpl("mut_scope_@@I@@", I=_i)
            _body = tpl("int main(void){int *p;{int a[@@N@@];a[0]=@@V@@;p=&a[0];}return p[0];}",
                        N=_n, V=_v)
            _expect = "reject"
        if (_body.strip() + "\n") not in SEEN:
            add("mutation", _name, _body, _expect)
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
    name, body, expect, family = case["name"], case["body"], case["expect"], case["family"]
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
        return family, name, expect, [], modes

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
    detail = ("%s/%s [expect=%s] reasons=%s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  family, name, expect, ";".join(reasons),
                  d_rej, first_diag(modes["--default"][1]),
                  s_rej, first_diag(modes["--strict"][1])))
    return family, name, expect, [detail], modes


def main():
    bodies = [c["body"] for c in CASES]
    unique = len(set(bodies))
    counts = {k: sum(1 for c in CASES if c["expect"] == k)
              for k in ("reject", "strict", "accept")}
    fams = {}
    for c in CASES:
        fams[c["family"]] = fams.get(c["family"], 0) + 1

    print("ub-init fuzz: %d generated cases (%d distinct), seed=%s, workers=%d"
          % (len(CASES), unique, hex(SEED), WORKERS))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families: " + ", ".join("%s=%d" % (k, v) for k, v in sorted(fams.items())))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    by_family = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for family, _name, _expect, fails, _modes in pool.map(run_case, CASES):
            if fails:
                mismatches.extend(fails)
                by_family[family] = by_family.get(family, 0) + 1

    print("results: %d cases, %d mismatches" % (len(CASES), len(mismatches)))
    if by_family:
        print("mismatches by family: " + ", ".join(
            "%s=%d" % (k, v) for k, v in sorted(by_family.items())))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs in %s)" % (len(mismatches) - 10, FAILS))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
