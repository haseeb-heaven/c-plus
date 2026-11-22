#!/usr/bin/env python3
"""Deterministic INTERPROCEDURAL-HEAP-EXTENT generator for C+ (340+ cases).

Scenario under test: an allocation with a KNOWN BYTE EXTENT is passed to
another function which then writes/reads beyond that extent.  Every program
is produced programmatically from parameter sweeps plus a seeded RNG
(FUZZ_SEED, default 0x1F17).  Nothing is hand-copied from another corpus:
each program varies in allocation size, element count, offset, call-chain
depth, argument order, aliasing, literal values and branch shape.

Covered families:

  pass_*     malloc(n bytes) passed to a function that writes N ints where
             N*sizeof(int) > n (single call level).
  chain_*    extents propagated through 2-3 call chains (main -> f -> g[/h]).
  mparam_*   extents through multiple parameters, swapped arguments and
             aliases created inside the callee.
  ret_*      extents through function returns (callee returns the pointer,
             caller uses it).
  realloc_*  realloc results with a new extent passed onward to a writer.
  calloc_*   calloc zero-init extents plus sizeof(int) vs sizeof(char)
             under-allocation mistakes.
  within_*   safe control: callee stays within the propagated extent.
  sizep_*    safe control: callee takes a size parameter and honors it.
  unknown_*  safe control: allocation size is not statically known.
  cond_*     conditional / unverified write driven by runtime data.
  mut_*      seeded mix of the families above with RNG-chosen parameters.

Each case declares an expected outcome (following tests/core/redteam_100.py
and the other tests/metrics generators):

  reject - proven out-of-bounds access: must fail BOTH --default and --strict.
  strict - conditional / unverified access: MAY pass --default, but must fail
           --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Any mismatch (unsafe accepted or safe rejected) is printed and the generated
source plus full both-mode diagnostics is saved under
tests/out/core/fuzz_interproc/failures/.  Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout.  The generator is seeded, so a run
with the same FUZZ_SEED is reproducible.  All programs are plain C.  This is a
differential measurement, not a proof of interprocedural bounds safety.
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
OUT = ROOT / "tests/out/core/fuzz_interproc"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x1F17"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

CASES = []   # list of (name, body, expect, family)
SEEN = set()  # normalized program bodies, used to enforce distinctness
NAMES = set()

STD = "#include <stdlib.h>\n"
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    assert name not in NAMES, name
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    NAMES.add(name)
    SEEN.add(normalized)
    CASES.append((name, normalized, expect, family))


# ---------------------------------------------------------------------------
# 1. pass: malloc(n bytes) -> function writes N ints (N*sizeof(int) > n).
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    for _over in (1, 2, 3):
        add("pass", tpl("pass_w_@@N@@_@@O@@", N=_n, O=_over), STD + tpl(
            "static void f(int *p){ for(int i=0;i<@@M@@;i++) p[i]=i; }\n"
            "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
            M=_n + _over, B=_n * 4), "reject")
    add("pass", tpl("pass_r_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ int v=p[@@I@@]; (void)v; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 3, B=_n * 4), "reject")
    add("pass", tpl("pass_off_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[@@I@@]=@@V@@; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 1, V=_n + 10, B=_n * 4), "reject")

# ---------------------------------------------------------------------------
# 2. chain: extents propagated through 2-3 call chains (main -> f -> g[/h]).
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("chain", tpl("chain2_w_@@N@@", N=_n), STD + tpl(
        "static void g(int *p){ p[@@I@@]=@@V@@; }\n"
        "static void f(int *p){ g(p); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 2, V=_n + 20, B=_n * 4), "reject")
    add("chain", tpl("chain2_r_@@N@@", N=_n), STD + tpl(
        "static void g(int *p){ int v=p[@@I@@]; (void)v; }\n"
        "static void f(int *p){ g(p); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 2, B=_n * 4), "reject")
    add("chain", tpl("chain3_w_@@N@@", N=_n), STD + tpl(
        "static void h(int *p){ p[@@I@@]=@@V@@; }\n"
        "static void g(int *p){ h(p); }\n"
        "static void f(int *p){ g(p); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 3, V=_n + 30, B=_n * 4), "reject")
    add("chain", tpl("chain3_r_@@N@@", N=_n), STD + tpl(
        "static void h(int *p){ int v=p[@@I@@]; (void)v; }\n"
        "static void g(int *p){ h(p); }\n"
        "static void f(int *p){ g(p); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 3, B=_n * 4), "reject")

# ---------------------------------------------------------------------------
# 3. mparam: multiple parameters, swapped arguments, aliases inside callee.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("mparam", tpl("mparam_swap_@@N@@", N=_n), STD + tpl(
        "static void f(int *big, int *small){ small[@@I@@]=@@V@@; }\n"
        "int main(void){ int *a=malloc(@@BA@@); int *b=malloc(@@BB@@); "
        "if(!a||!b)return 2; f(a,b); free(a); free(b); return 0; }",
        I=_n + 1, V=_n + 40, BA=(_n + 4) * 4, BB=_n * 4), "reject")
    add("mparam", tpl("mparam_alias_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ int *r=p; r[@@I@@]=@@V@@; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 2, V=_n + 50, B=_n * 4), "reject")
    add("mparam", tpl("mparam_two_@@N@@", N=_n), STD + tpl(
        "static void f(int *p, int *q){ p[0]=@@V@@; q[@@I@@]=@@V@@; }\n"
        "int main(void){ int *a=malloc(@@BA@@); int *b=malloc(@@BB@@); "
        "if(!a||!b)return 2; f(a,b); free(a); free(b); return 0; }",
        I=_n + 2, V=_n + 60, BA=(_n + 4) * 4, BB=_n * 4), "reject")
    add("mparam", tpl("mparam_reorder_@@N@@", N=_n), STD + tpl(
        "static void f(int *q, int *p){ p[0]=@@V@@; q[@@I@@]=@@V@@; }\n"
        "int main(void){ int *a=malloc(@@BA@@); int *b=malloc(@@BB@@); "
        "if(!a||!b)return 2; f(b,a); free(a); free(b); return 0; }",
        I=_n + 2, V=_n + 70, BA=_n * 4, BB=(_n + 4) * 4), "reject")

# ---------------------------------------------------------------------------
# 4. ret: extents through function returns (callee returns the pointer).
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("ret", tpl("ret_write_@@N@@", N=_n), STD + tpl(
        "static int *f(int *p){ p[@@I@@]=@@V@@; return p; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "int *q=f(p); (void)q; free(p); return 0; }",
        I=_n + 2, V=_n + 80, B=_n * 4), "reject")
    add("ret", tpl("ret_caller_@@N@@", N=_n), STD + tpl(
        "static int *f(int *p){ return p; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "int *q=f(p); q[@@I@@]=@@V@@; free(p); return 0; }",
        I=_n + 2, V=_n + 90, B=_n * 4), "reject")
    add("ret", tpl("ret_chain_@@N@@", N=_n), STD + tpl(
        "static int *g(int *p){ p[@@I@@]=@@V@@; return p; }\n"
        "static int *f(int *p){ return g(p); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "int *q=f(p); (void)q; free(p); return 0; }",
        I=_n + 2, V=_n + 100, B=_n * 4), "reject")

# ---------------------------------------------------------------------------
# 5. realloc: realloc results with a new extent passed onward.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("realloc", tpl("re_small_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[@@I@@]=@@V@@; }\n"
        "int main(void){ int *p=malloc(@@BA@@); if(!p)return 2; "
        "int *q=realloc(p,@@BB@@); if(!q)return 2; f(q); free(q); return 0; }",
        I=_n + 2, V=_n + 110, BA=(_n + 4) * 4, BB=_n * 4), "reject")
    add("realloc", tpl("re_grow_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ for(int i=0;i<@@M@@;i++) p[i]=i; }\n"
        "int main(void){ int *p=malloc(4); if(!p)return 2; "
        "int *q=realloc(p,@@BB@@); if(!q)return 2; f(q); free(q); return 0; }",
        M=_n + 2, BB=_n * 4), "reject")
    add("realloc", tpl("re_shrink_@@N@@", N=_n), STD + tpl(
        "static void g(int *p){ p[@@I@@]=@@V@@; }\n"
        "static void f(int *p){ g(p); }\n"
        "int main(void){ int *p=malloc(@@BA@@); if(!p)return 2; "
        "int *q=realloc(p,@@BB@@); if(!q)return 2; f(q); free(q); return 0; }",
        I=_n + 2, V=_n + 120, BA=(_n + 6) * 4, BB=_n * 4), "reject")

# ---------------------------------------------------------------------------
# 6. calloc: zero-init extents and sizeof(int) vs sizeof(char) mistakes.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("calloc", tpl("ca_cnt_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ for(int i=0;i<@@M@@;i++) p[i]=i; }\n"
        "int main(void){ int *p=calloc(@@N@@,sizeof(int)); if(!p)return 2; "
        "f(p); free(p); return 0; }", M=_n + 2, N=_n), "reject")
    add("calloc", tpl("ca_sz_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[@@I@@]=@@V@@; }\n"
        "int main(void){ int *p=calloc(1,@@B@@); if(!p)return 2; "
        "f(p); free(p); return 0; }", I=_n + 2, V=_n + 130, B=_n * 4), "reject")
    add("calloc", tpl("ca_szmix_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ for(int i=0;i<@@M@@;i++) p[i]=i; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p); free(p); return 0; }", M=_n + 2, B=_n), "reject")
    add("calloc", tpl("ca_charloop_@@N@@", N=_n), STD + tpl(
        "static void f(char *p){ for(int i=0;i<@@M@@;i++) p[i]=(char)i; }\n"
        "int main(void){ char *p=malloc(@@B@@); if(!p)return 2; "
        "f(p); free(p); return 0; }", M=_n * 4 + 4, B=_n * 4), "reject")

# ---------------------------------------------------------------------------
# 7. within: safe control, callee stays inside the propagated extent.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("within", tpl("within_direct_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[0]=@@V@@; p[@@L@@]=@@V@@; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p); free(p); return 0; }", L=_n, V=_n + 140, B=(_n + 1) * 4), "accept")
    add("within", tpl("within_loop_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ for(int i=0;i<@@N@@;i++) p[i]=i; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p); free(p); return 0; }", N=_n, B=_n * 4), "accept")
    add("within", tpl("within_chain_@@N@@", N=_n), STD + tpl(
        "static void g(int *p){ p[@@L@@]=@@V@@; }\n"
        "static void f(int *p){ g(p); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p); free(p); return 0; }", L=_n, V=_n + 150, B=(_n + 1) * 4), "accept")

# ---------------------------------------------------------------------------
# 8. sizep: safe control, callee takes a size parameter and honors it.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("sizep", tpl("sizep_@@N@@", N=_n), STD + tpl(
        "static void f(int *p,int n){ for(int i=0;i<n;i++) p[i]=i; }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p,@@N@@); free(p); return 0; }", N=_n, B=_n * 4), "accept")
    add("sizep", tpl("sizep_chain_@@N@@", N=_n), STD + tpl(
        "static void g(int *p,int n){ for(int i=0;i<n;i++) p[i]=i; }\n"
        "static void f(int *p,int n){ g(p,n); }\n"
        "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p,@@N@@); free(p); return 0; }", N=_n, B=_n * 4), "accept")
    add("sizep", tpl("sizep_calloc_@@N@@", N=_n), STD + tpl(
        "static void f(int *p,int n){ for(int i=0;i<n;i++) p[i]=i; }\n"
        "int main(void){ int *p=calloc(@@N@@,sizeof(int)); if(!p)return 2; "
        "f(p,@@N@@); free(p); return 0; }", N=_n), "accept")

# ---------------------------------------------------------------------------
# 9. unknown: safe control, allocation size is not statically known.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("unknown", tpl("unknown_argc_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[0]=@@V@@; p[1]=@@V@@; }\n"
        "int main(int argc,char **v){ (void)v; int *p=malloc(argc*sizeof(int)+@@B@@); "
        "if(!p)return 2; f(p); free(p); return 0; }", V=_n + 160, B=8), "accept")
    add("unknown", tpl("unknown_param_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[0]=@@V@@; }\n"
        "static int *h(int k){ int *p=malloc(k*sizeof(int)+4); if(p) f(p); return p; }\n"
        "int main(int argc,char **v){ (void)v; int *p=h(argc); free(p); return 0; }",
        V=_n + 170), "accept")
    add("unknown", tpl("unknown_ret_@@N@@", N=_n), STD + tpl(
        "static int *f(int k){ int *p=malloc(k*sizeof(int)+4); return p; }\n"
        "int main(int argc,char **v){ (void)v; int *q=f(argc); if(!q)return 2; "
        "q[0]=@@V@@; free(q); return 0; }", V=_n + 180), "accept")

# ---------------------------------------------------------------------------
# 10. cond: conditional / unverified OOB driven by runtime data.
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add("cond", tpl("cond_idx_@@N@@", N=_n), STD + tpl(
        "static void f(int *p,int idx){ p[idx]=@@V@@; }\n"
        "int main(int argc,char **v){ (void)v; int idx=argc?@@I@@:0; "
        "int *p=malloc(@@B@@); if(!p)return 2; f(p,idx); free(p); return 0; }",
        I=_n + 2, V=_n + 190, B=_n * 4), "strict")
    add("cond", tpl("cond_alloc_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){ p[@@I@@]=@@V@@; }\n"
        "int main(int argc,char **v){ (void)v; size_t b=argc?@@B@@:64; "
        "int *p=malloc(b); if(!p)return 2; f(p); free(p); return 0; }",
        I=_n + 2, V=_n + 200, B=_n * 4), "strict")
    add("cond", tpl("cond_flag_@@N@@", N=_n), STD + tpl(
        "static void f(int *p,int flag){ if(flag) p[@@I@@]=@@V@@; else p[0]=@@V@@; }\n"
        "int main(int argc,char **v){ (void)v; int *p=malloc(@@B@@); if(!p)return 2; "
        "f(p,argc); free(p); return 0; }", I=_n + 2, V=_n + 210, B=_n * 4), "strict")

# ---------------------------------------------------------------------------
# 11. mut: seeded mix of the families above with RNG-chosen parameters.
# ---------------------------------------------------------------------------
_rng = random.Random(SEED)
for _i in range(64):
    for _attempt in range(20000):  # bounded: a full collision cannot hang us
        _n = _rng.randint(1, 8)
        _over = _rng.randint(1, 4)
        _v = _rng.randint(1, 255)
        _kind = _i % 11
        if _kind == 0:      # pass loop, beyond extent
            _name = tpl("mut_pass_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p){ for(int i=0;i<@@M@@;i++) p[i]=i; }\n"
                "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
                "f(p); free(p); return 0; }", M=_n + _over, B=_n * 4)
            _exp = "reject"
        elif _kind == 1:    # 2-level chain, beyond extent
            _name = tpl("mut_chain2_@@I@@", I=_i)
            _body = STD + tpl(
                "static void g(int *p){ p[@@I2@@]=@@V@@; }\n"
                "static void f(int *p){ g(p); }\n"
                "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
                "f(p); free(p); return 0; }", I2=_n + _over, V=_v, B=_n * 4)
            _exp = "reject"
        elif _kind == 2:    # 3-level chain, beyond extent
            _name = tpl("mut_chain3_@@I@@", I=_i)
            _body = STD + tpl(
                "static void h(int *p){ p[@@I2@@]=@@V@@; }\n"
                "static void g(int *p){ h(p); }\n"
                "static void f(int *p){ g(p); }\n"
                "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
                "f(p); free(p); return 0; }", I2=_n + _over, V=_v, B=_n * 4)
            _exp = "reject"
        elif _kind == 3:    # swapped args / multiple params
            _name = tpl("mut_swap_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *a, int *b){ b[@@I2@@]=@@V@@; }\n"
                "int main(void){ int *x=malloc(@@BA@@); int *y=malloc(@@BB@@); "
                "if(!x||!y)return 2; f(x,y); free(x); free(y); return 0; }",
                I2=_n + _over, V=_v, BA=(_n + 4) * 4, BB=_n * 4)
            _exp = "reject"
        elif _kind == 4:    # alias inside callee
            _name = tpl("mut_alias_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p){ int *r=p; r[@@I2@@]=@@V@@; }\n"
                "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
                "f(p); free(p); return 0; }", I2=_n + _over, V=_v, B=_n * 4)
            _exp = "reject"
        elif _kind == 5:    # pointer returned to caller
            _name = tpl("mut_ret_@@I@@", I=_i)
            _body = STD + tpl(
                "static int *f(int *p){ return p; }\n"
                "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
                "int *q=f(p); q[@@I2@@]=@@V@@; free(p); return 0; }",
                I2=_n + _over, V=_v, B=_n * 4)
            _exp = "reject"
        elif _kind == 6:    # realloc then onward
            _name = tpl("mut_realloc_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p){ p[@@I2@@]=@@V@@; }\n"
                "int main(void){ int *p=malloc(@@BA@@); if(!p)return 2; "
                "int *q=realloc(p,@@BB@@); if(!q)return 2; f(q); free(q); return 0; }",
                I2=_n + _over, V=_v, BA=(_n + 4) * 4, BB=_n * 4)
            _exp = "reject"
        elif _kind == 7:    # calloc / sizeof mistake
            _name = tpl("mut_calloc_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p){ p[@@I2@@]=@@V@@; }\n"
                "int main(void){ int *p=calloc(@@N@@,sizeof(int)); if(!p)return 2; "
                "f(p); free(p); return 0; }", I2=_n + _over, V=_v, N=_n)
            _exp = "reject"
        elif _kind == 8:    # conditional / unverified
            _name = tpl("mut_cond_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p,int idx){ p[idx]=@@V@@; }\n"
                "int main(int argc,char **v){ (void)v; int idx=argc?@@I2@@:0; "
                "int *p=malloc(@@B@@); if(!p)return 2; f(p,idx); free(p); return 0; }",
                I2=_n + _over, V=_v, B=_n * 4)
            _exp = "strict"
        elif _kind == 9:    # safe, honors size param
            _name = tpl("mut_sizep_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p,int n){ for(int i=0;i<n;i++) p[i]=@@V@@; }\n"
                "int main(void){ int *p=malloc(@@B@@); if(!p)return 2; "
                "f(p,@@N@@); free(p); return 0; }", N=_n, B=_n * 4, V=_v)
            _exp = "accept"
        else:               # safe, unknown-size allocation
            _name = tpl("mut_unknown_@@I@@", I=_i)
            _body = STD + tpl(
                "static void f(int *p){ p[0]=@@V@@; }\n"
                "int main(int argc,char **v){ (void)v; int *p=malloc(argc*sizeof(int)+4); "
                "if(!p)return 2; f(p); free(p); return 0; }", V=_v)
            _exp = "accept"

        if (_body.strip() + "\n") not in SEEN:
            add("mut", _name, _body, _exp)
            break


# ---------------------------------------------------------------------------
# Evaluation
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
    name, body, expect, family = case
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

    # Save generated source + full both-mode diagnostics for every mismatch.
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
    detail = ("%s [expect=%s] reasons=%s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  name, expect, ";".join(reasons),
                  d_rej, first_diag(modes["--default"][1]),
                  s_rej, first_diag(modes["--strict"][1])))
    return name, expect, [detail], modes


def main():
    bodies = [b for _, b, _, _ in CASES]
    unique = len(set(bodies))
    outcomes = Counter(e for _, _, e, _ in CASES)
    families = Counter(f for _, _, _, f in CASES)

    print("fuzz_interproc: %d generated cases (%d distinct), seed=%s, workers=%d"
          % (len(CASES), unique, hex(SEED), WORKERS))
    print("outcome counts: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    print("family counts: " + " ".join("%s=%d" % (k, families[k])
                                       for k in sorted(families)))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for _name, _expect, fails, _modes in pool.map(run_case, CASES):
            mismatches.extend(fails)

    by_outcome = Counter(m.split("[expect=")[1].split("]")[0]
                         for m in mismatches if "[expect=" in m)
    print("mismatch count: %d" % len(mismatches))
    print("mismatches by outcome: " + " ".join(
        "%s=%d" % (k, by_outcome[k]) for k in ("reject", "strict", "accept")))
    print("first 10 mismatch names:")
    for m in mismatches[:10]:
        print("  " + m.split(" [expect=")[0])
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
