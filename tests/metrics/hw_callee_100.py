#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of callee-free / function-pointer gap cases (122).

This corpus targets one core property: a heap object is RELEASED INSIDE ANOTHER
FUNCTION and a stale alias is later used by the caller.  A sound checker must
propagate release facts *across function boundaries*, including through call
chains, wrappers, function pointers, out-parameters, callbacks, factories and
realloc.  Every program is plain, valid C (no C+ syntax) and carries a unique
case marker plus case-suffixed helper names, so no case is a copy of another.

Generation and checking both run in thread pools.  ``pool.map`` preserves input
order and case ``i`` always draws from ``random.Random(f"{SEED}:{i}")``, so a
run is fully reproducible regardless of ``FUZZ_WORKERS``.

Families (counts are in FAMILIES):

  callee_free_use     callee frees directly; caller dereferences stale pointer
  wrapper_free        callee frees via a free() wrapper (1 or 2 levels)
  fp_callee_free      release happens through a function pointer / table / field
  chain2_free         2-hop and 3-hop call chains hide the release
  outparam_free       callee frees through an int** / struct out-param
  callback_free       a callback releases its context (or the context's pointer)
  factory_freed       a factory returns a pointer it already released
  realloc_callee      callee reallocates; caller keeps and uses the stale alias
  ok_callee_free      safe control: callee frees, caller never touches it again
  ok_ownership_escape safe control: ownership transferred, no stale use

Each case declares one expected outcome (same convention as the sibling
hv_uaf_100 / hv_interproc_300 generators):

  reject - proven use-after-free / invalid release: must fail BOTH --default
           and --strict; acceptance in either mode is a safety hole (a "miss").
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Expectations describe CORRECT C semantics, not the current checker's behaviour.
A case the checker misses is a false negative (unsafe accepted); a safe case it
rejects is a false positive.  Programs rejected only for a non-safety reason
(no CPLUS-* diagnostic) are flagged separately as malformed generator cases.

Mismatches are printed and the generated source plus full both-mode diagnostics
are saved under tests/out/core/hw_callee/failures/.  This is a differential
measurement of the checker, not a proof of memory safety.

Environment:
  FUZZ_SEED    default 0x4857434C
  FUZZ_WORKERS default 8
  FUZZ_TIMEOUT default 60 seconds per check
  CPLUS_BIN    default <root>/build/c+ (existing binary; no rebuild needed)
"""
import collections
import concurrent.futures
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/hw_callee"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4857434C"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# family, number of cases, builder name, declared outcome.
FAMILIES = (
    ("callee_free_use",   14, "callee_free_use",   "reject"),
    ("wrapper_free",      12, "wrapper_free",      "reject"),
    ("fp_callee_free",    12, "fp_callee_free",    "reject"),
    ("chain2_free",       12, "chain2_free",       "reject"),
    ("outparam_free",     12, "outparam_free",     "reject"),
    ("callback_free",     12, "callback_free",     "reject"),
    ("factory_freed",     12, "factory_freed",     "reject"),
    ("realloc_callee",    12, "realloc_callee",    "reject"),
    ("ok_callee_free",    12, "ok_callee_free",    "accept"),
    ("ok_ownership_escape", 12, "ok_ownership_escape", "accept"),
)
TOTAL = sum(cnt for _f, cnt, _b, _e in FAMILIES)
STAGE = None

STD = "#include <stdlib.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hw_callee {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Proven cross-function use-after-free (declared "reject").
# ---------------------------------------------------------------------------
def gen_callee_free_use(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel = f"rel{i}"
    if shape == 0:  # direct read of the released pointer in the caller
        body = STD + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return p[@@I@@]; }",
            R=rel, N=n, I=idx, V=v)
    elif shape == 1:  # write through the released pointer in the caller
        body = STD + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "@@R@@(p); p[@@I@@]=@@V@@; return 0; }",
            R=rel, N=n, I=idx, V=v)
    elif shape == 2:  # an alias of the released pointer is read
        body = STD + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *q=p; q[@@I@@]=@@V@@; @@R@@(p); return q[@@I@@]; }",
            R=rel, N=n, I=idx, V=v)
    else:  # the released pointer is read by a second helper
        body = STD + tpl(
            "static int rd@@I2@@(int *p,int k){ return p[k]; }\n"
            "static void @@R@@(int *p){ free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return rd@@I2@@(p,@@I@@); }",
            R=rel, I2=i, N=n, I=idx, V=v)
    return f"callee_free_use_{i}", body


def gen_wrapper_free(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    w, r = f"wrap{i}", f"rel{i}"
    if shape == 0:  # one level of free() wrapper
        body = STD + tpl(
            "static void @@W@@(void *q){ free(q); }\n"
            "static void @@R@@(int *p){ @@W@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return p[@@I@@]; }",
            W=w, R=r, N=n, I=idx, V=v)
    elif shape == 1:  # wrapper takes an extra (ignored) argument
        body = STD + tpl(
            "static void @@W@@(void *q,int flag){ (void)flag; free(q); }\n"
            "static void @@R@@(int *p){ @@W@@(p,1); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return p[@@I@@]; }",
            W=w, R=r, N=n, I=idx, V=v)
    elif shape == 2:  # two nested wrappers hide the release deeper
        body = STD + tpl(
            "static void @@W1@@(void *q){ free(q); }\n"
            "static void @@W2@@(int *p){ @@W1@@(p); }\n"
            "static void @@R@@(int *p){ @@W2@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return p[@@I@@]; }",
            W1=f"w1_{i}", W2=f"w2_{i}", R=r, N=n, I=idx, V=v)
    else:  # wrapper releases; caller reads through a surviving alias
        body = STD + tpl(
            "static void @@W@@(void *q){ free(q); }\n"
            "static void @@R@@(int *p){ @@W@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *a=p; a[@@I@@]=@@V@@; @@R@@(p); return a[@@I@@]; }",
            W=w, R=r, N=n, I=idx, V=v)
    return f"wrapper_free_{i}", body


def gen_fp_callee_free(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    t, f, inv, s = f"FT{i}", f"ffree{i}", f"finv{i}", f"FS{i}"
    typedef = f"typedef void (*{t})(int *);\n"
    fn = f"static void {f}(int *p){{ free(p); }}\n"
    if shape == 0:  # call through a local function-pointer variable
        body = STD + typedef + fn + tpl(
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@T@@ fn=@@F@@; fn(p); return p[@@I@@]; }",
            T=t, F=f, N=n, I=idx, V=v)
    elif shape == 1:  # function pointer passed into an invoker helper
        body = STD + typedef + fn + tpl(
            "static void @@INV@@(@@T@@ fn,int *p){ fn(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(@@F@@,p); return p[@@I@@]; }",
            INV=inv, T=t, F=f, N=n, I=idx, V=v)
    elif shape == 2:  # function pointer stored in a struct field
        body = STD + typedef + fn + tpl(
            "struct @@S@@{ @@T@@ fn; };\n"
            "static void @@INV@@(struct @@S@@ *s,int *p){ s->fn(p); }\n"
            "int main(void){ struct @@S@@ s; s.fn=@@F@@; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(&s,p); return p[@@I@@]; }",
            S=s, T=t, INV=inv, F=f, N=n, I=idx, V=v)
    else:  # function-pointer table indexed by a seeded constant
        k = rng.randint(0, 1)
        body = STD + typedef + fn + tpl(
            "int main(void){ @@T@@ tab[2]; tab[0]=@@F@@; tab[1]=@@F@@; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; tab[@@K@@](p); return p[@@I@@]; }",
            T=t, F=f, N=n, I=idx, V=v, K=k)
    return f"fp_callee_free_{i}", body


def gen_chain2_free(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    leaf, mid, mid2 = f"leaf{i}", f"mid{i}", f"mid2_{i}"
    if shape == 0:  # 2-hop: main -> mid -> leaf (free)
        body = STD + tpl(
            "static void @@L@@(int *p){ free(p); }\n"
            "static void @@M@@(int *p){ @@L@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@M@@(p); return p[@@I@@]; }",
            L=leaf, M=mid, N=n, I=idx, V=v)
    elif shape == 1:  # 3-hop chain
        body = STD + tpl(
            "static void @@L@@(int *p){ free(p); }\n"
            "static void @@M2@@(int *p){ @@L@@(p); }\n"
            "static void @@M@@(int *p){ @@M2@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@M@@(p); return p[@@I@@]; }",
            L=leaf, M2=mid2, M=mid, N=n, I=idx, V=v)
    elif shape == 2:  # chain with shuffled, ignored parameters
        body = STD + tpl(
            "static void @@L@@(int junk,int *p){ (void)junk; free(p); }\n"
            "static void @@M@@(int *p,int junk){ @@L@@(junk,p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@M@@(p,@@J@@); return p[@@I@@]; }",
            L=leaf, M=mid, N=n, I=idx, V=v, J=rng.randint(1, 32))
    else:  # chain releases; caller reads a surviving alias
        body = STD + tpl(
            "static void @@L@@(int *p){ free(p); }\n"
            "static void @@M@@(int *p){ @@L@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *a=p; a[@@I@@]=@@V@@; @@M@@(p); return a[@@I@@]; }",
            L=leaf, M=mid, N=n, I=idx, V=v)
    return f"chain2_free_{i}", body


def gen_outparam_free(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, st = f"rel{i}", f"OS{i}"
    if shape == 0:  # free through int** and null the caller's handle
        body = STD + tpl(
            "static void @@R@@(int **pp){ free(*pp); *pp=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *q=p; q[@@I@@]=@@V@@; @@R@@(&p); return q[@@I@@]; }",
            R=rel, N=n, I=idx, V=v)
    elif shape == 1:  # out-param does not null; caller reuses the stale value
        body = STD + tpl(
            "static void @@R@@(int **pp){ free(*pp); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(&p); return p[@@I@@]; }",
            R=rel, N=n, I=idx, V=v)
    elif shape == 2:  # out-param is a struct field
        body = STD + tpl(
            "struct @@S@@{ int *p; };\n"
            "static void @@R@@(struct @@S@@ *s){ free(s->p); s->p=0; }\n"
            "int main(void){ struct @@S@@ s; s.p=malloc(@@N@@*sizeof(int)); "
            "if(!s.p)return 2; int *q=s.p; q[@@I@@]=@@V@@; @@R@@(&s); "
            "return q[@@I@@]; }",
            S=st, R=rel, N=n, I=idx, V=v)
    else:  # release through a local inside the out-param callee
        body = STD + tpl(
            "static void @@R@@(int **pp){ int *old=*pp; free(old); *pp=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *q=p; @@R@@(&p); q[@@I@@]=@@V@@; return 0; }",
            R=rel, N=n, I=idx, V=v)
    return f"outparam_free_{i}", body


def gen_callback_free(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    t, f, run, s = f"CT{i}", f"cb{i}", f"crun{i}", f"CS{i}"
    typedef = f"typedef void (*{t})(void *);\n"
    if shape == 0:  # callback frees its void* context; caller uses the pointer
        body = STD + typedef + tpl(
            "static void @@F@@(void *ctx){ free(ctx); }\n"
            "static void @@RUN@@(@@T@@ fn,void *ctx){ fn(ctx); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@RUN@@(@@F@@,p); return p[@@I@@]; }",
            F=f, RUN=run, T=t, N=n, I=idx, V=v)
    elif shape == 1:  # callback and context held in a struct
        body = STD + typedef + tpl(
            "static void @@F@@(void *ctx){ free(ctx); }\n"
            "struct @@S@@{ @@T@@ cb; void *ctx; };\n"
            "static void @@RUN@@(struct @@S@@ *s){ s->cb(s->ctx); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; struct @@S@@ s; s.cb=@@F@@; s.ctx=p; @@RUN@@(&s); "
            "return p[@@I@@]; }",
            F=f, S=s, RUN=run, T=t, N=n, I=idx, V=v)
    elif shape == 2:  # callback invoked directly through a local pointer
        body = STD + typedef + tpl(
            "static void @@F@@(void *ctx){ free(ctx); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@T@@ cb=@@F@@; void *ctx=p; cb(ctx); "
            "return p[@@I@@]; }",
            F=f, T=t, N=n, I=idx, V=v)
    else:  # callback releases the pointer owned by the passed context struct
        body = STD + typedef + tpl(
            "struct @@S@@{ int *p; int k; };\n"
            "static void @@F@@(void *ctx){ struct @@S@@ *s=(struct @@S@@*)ctx; "
            "free(s->p); }\n"
            "static void @@RUN@@(@@T@@ fn,void *ctx){ fn(ctx); }\n"
            "int main(void){ struct @@S@@ s; s.p=malloc(@@N@@*sizeof(int)); "
            "s.k=@@I@@; if(!s.p)return 2; s.p[@@I@@]=@@V@@; @@RUN@@(@@F@@,&s); "
            "return s.p[s.k]; }",
            F=f, RUN=run, T=t, S=s, N=n, I=idx, V=v)
    return f"callback_free_{i}", body


def gen_factory_freed(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    fac, rel = f"fac{i}", f"rel{i}"
    if shape == 0:  # factory frees the exact pointer it returns
        body = STD + tpl(
            "static int *@@F@@(int n){ int *p=malloc(n*sizeof(int)); "
            "if(!p)return 0; free(p); return p; }\n"
            "int main(void){ int *p=@@F@@(@@N@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; return p[@@I@@]; }",
            F=fac, N=n, I=idx, V=v)
    elif shape == 1:  # factory releases through a callee before returning
        body = STD + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "static int *@@F@@(int n){ int *p=malloc(n*sizeof(int)); "
            "if(!p)return 0; @@R@@(p); return p; }\n"
            "int main(void){ int *p=@@F@@(@@N@@); if(!p)return 2; "
            "return p[@@I@@]; }",
            R=rel, F=fac, N=n, I=idx)
    elif shape == 2:  # factory nulls its own handle but returns a stale alias
        body = STD + tpl(
            "static void @@R@@(int **pp){ free(*pp); *pp=0; }\n"
            "static int *@@F@@(int n){ int *p=malloc(n*sizeof(int)); "
            "if(!p)return 0; int *q=p; @@R@@(&p); return q; }\n"
            "int main(void){ int *p=@@F@@(@@N@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; return p[@@I@@]; }",
            R=rel, F=fac, N=n, I=idx, V=v)
    else:  # factory conditionally releases on a constant flag
        body = STD + tpl(
            "static int *@@F@@(int n,int flag){ int *p=malloc(n*sizeof(int)); "
            "if(!p)return 0; if(flag)free(p); return p; }\n"
            "int main(void){ int *p=@@F@@(@@N@@,1); if(!p)return 2; "
            "p[@@I@@]=@@V@@; return p[@@I@@]; }",
            F=fac, N=n, I=idx, V=v)
    return f"factory_freed_{i}", body


def gen_realloc_callee(i, rng):
    n = rng.randint(8, 28)
    idx = rng.randrange(n)
    v = rng.randint(2, 60)
    k = rng.randint(2, 8)
    shape = i % 4
    grow, t, apply = f"grow{i}", f"GT{i}", f"gapply{i}"
    if shape == 0:  # callee returns the moved block; caller reads the old alias
        body = STD + tpl(
            "static int *@@G@@(int *p,int n){ return realloc(p,n*sizeof(int)); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *old=p; p[@@I@@]=@@V@@; int *q=@@G@@(p,@@N@@+@@K@@); "
            "if(!q)return 1; p=q; return old[@@I@@]; }",
            G=grow, N=n, I=idx, V=v, K=k)
    elif shape == 1:  # callee updates an out-param; caller keeps a stale alias
        body = STD + tpl(
            "static int @@G@@(int **pp,int n){ int *q=realloc(*pp,n*sizeof(int)); "
            "if(q)*pp=q; return q!=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *old=p; old[@@I@@]=@@V@@; if(!@@G@@(&p,@@N@@+@@K@@))return 1; "
            "return old[@@I@@]; }",
            G=grow, N=n, I=idx, V=v, K=k)
    elif shape == 2:  # callee shrinks; caller writes through the stale alias
        body = STD + tpl(
            "static int *@@G@@(int *p,int n){ return realloc(p,n*sizeof(int)); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *old=p; old[@@I@@]=@@V@@; int *q=@@G@@(p,@@M@@); "
            "if(!q)return 1; p=q; old[@@I@@]=@@V@@; return p[0]; }",
            G=grow, N=n, M=max(2, n // 2), I=idx, V=v)
    else:  # realloc reached through a function pointer inside a helper
        body = STD + tpl(
            "typedef void *(*@@T@@)(void *,size_t);\n"
            "static void *do_grow(void *p,size_t n){ return realloc(p,n); }\n"
            "static void @@AP@@(@@T@@ fn,int **pp,size_t n){ "
            "void *q=fn(*pp,n); if(q)*pp=(int*)q; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "int *old=p; old[@@I@@]=@@V@@; "
            "@@AP@@(do_grow,&p,(size_t)((@@N@@+@@K@@)*sizeof(int))); "
            "return old[@@I@@]; }",
            T=t, AP=apply, N=n, I=idx, V=v, K=k)
    return f"realloc_callee_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": must pass both modes).
# ---------------------------------------------------------------------------
def gen_ok_callee_free(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, w, inv = f"rel{i}", f"wrap{i}", f"finv{i}"
    t = f"FT{i}"
    if shape == 0:  # callee frees directly; caller returns without touching
        body = STD + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return 0; }",
            R=rel, N=n, I=idx, V=v)
    elif shape == 1:  # release through a wrapper; no stale use
        body = STD + tpl(
            "static void @@W@@(void *q){ free(q); }\n"
            "static void @@R@@(int *p){ @@W@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(p); return 0; }",
            W=w, R=rel, N=n, I=idx, V=v)
    elif shape == 2:  # release through a function pointer; no stale use
        body = STD + tpl(
            "typedef void (*@@T@@)(int *);\n"
            "static void @@R@@(int *p){ free(p); }\n"
            "static void @@INV@@(@@T@@ fn,int *p){ fn(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(@@R@@,p); return 0; }",
            T=t, R=rel, INV=inv, N=n, I=idx, V=v)
    else:  # chain releases; caller only reads unrelated locals afterwards
        body = STD + tpl(
            "static void leaf@@I2@@(int *p){ free(p); }\n"
            "static void mid@@I2@@(int *p){ leaf@@I2@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; mid@@I2@@(p); int x=@@V@@; return x==@@V@@?0:1; }",
            I2=i, N=n, I=idx, V=v)
    return f"ok_callee_free_{i}", body


def gen_ok_ownership_escape(i, rng):
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, mk, t, run = f"rel{i}", f"mk{i}", f"CT{i}", f"crun{i}"
    if shape == 0:  # callee consumes (uses then frees) the transferred object
        body = STD + tpl(
            "static void consume@@I2@@(int *p,int k,int val){ p[k]=val; free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "consume@@I2@@(p,@@I@@,@@V@@); return 0; }",
            I2=i, N=n, I=idx, V=v)
    elif shape == 1:  # out-param nulls the handle; caller only tests for null
        body = STD + tpl(
            "static void @@R@@(int **pp){ free(*pp); *pp=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "@@R@@(&p); return p?1:0; }",
            R=rel, N=n)
    elif shape == 2:  # factory allocates; caller owns and frees exactly once
        body = STD + tpl(
            "static int *@@M@@(int n){ return malloc(n*sizeof(int)); }\n"
            "int main(void){ int *p=@@M@@(@@N@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; int x=p[@@I@@]; free(p); return x==@@V@@?0:1; }",
            M=mk, N=n, I=idx, V=v)
    else:  # callback frees the context; caller never touches it again
        body = STD + tpl(
            "typedef void (*@@T@@)(void *);\n"
            "static void cb@@I2@@(void *ctx){ free(ctx); }\n"
            "static void @@RUN@@(@@T@@ fn,void *ctx){ fn(ctx); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@RUN@@(cb@@I2@@,p); return 0; }",
            T=t, RUN=run, I2=i, N=n, I=idx, V=v)
    return f"ok_ownership_escape_{i}", body


BUILDERS = (
    gen_callee_free_use, gen_wrapper_free, gen_fp_callee_free,
    gen_chain2_free, gen_outparam_free, gen_callback_free,
    gen_factory_freed, gen_realloc_callee,
    gen_ok_callee_free, gen_ok_ownership_escape,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, _bname, expect), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body = builder(i, rng)
            return name, marked(name, body), expect, family
        start += count
    raise IndexError(i)


# ---------------------------------------------------------------------------
# Verification: check each generated case in DEFAULT and STRICT.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def diag_tags(text):
    """Unique CPLUS-* diagnostic tags emitted for a case (for reporting)."""
    return tuple(dict.fromkeys(re.findall(r"\[(CPLUS-[A-Z0-9-]+)\]", text)))


def check_case(case):
    """Run one case in both modes; return a mismatch record or None."""
    name, body, expect, family = case
    path = STAGE / f"{name}.c"
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, f"TIMEOUT after {TIMEOUT}s")
            hard.append(f"{mode}: TIMEOUT after {TIMEOUT}s")
            continue
        except OSError as exc:
            modes[mode] = (False, f"OS ERROR: {exc}")
            hard.append(f"{mode}: OS ERROR: {exc}")
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append(f"{mode}: CRASH rc={proc.returncode}")
        if proc.returncode != 0 and "[CPLUS-" not in diag:
            hard.append(f"{mode}: rejected without a CPLUS safety diagnostic "
                        "(possible malformed generated program)")
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT (false negative)")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT (false negative)")
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    if not reasons:
        return None

    (FAILS / f"{name}.c").write_text(body)
    log = [
        f"case: {name}",
        f"family: {family}",
        f"expect: {expect}",
        f"reasons: {'; '.join(reasons)}",
        f"--default rejected={d_rej}: {first_diag(modes['--default'][1])}",
        f"--strict rejected={s_rej}: {first_diag(modes['--strict'][1])}",
        f"tags --default: {' '.join(diag_tags(modes['--default'][1])) or '(none)'}",
        f"tags --strict: {' '.join(diag_tags(modes['--strict'][1])) or '(none)'}",
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
    (FAILS / f"{name}.diag.txt").write_text("\n".join(log))
    return {
        "name": name,
        "family": family,
        "expect": expect,
        "reasons": reasons,
        "detail": (
            f"{name} [{family}] expect={expect}: {'; '.join(reasons)} | "
            f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
            f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}"
        ),
    }


def main():
    global STAGE
    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    # Clear fixtures from an earlier run of this generator only.
    for stale in FAILS.glob("*.c"):
        stale.unlink()
    for stale in FAILS.glob("*.diag.txt"):
        stale.unlink()

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the output.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    print(f"hw_callee: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d accept=%d"
          % (outcomes["reject"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:20} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hw_callee_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]

    false_negatives = [m for m in mismatches if m["expect"] == "reject"]
    false_positives = [m for m in mismatches if m["expect"] == "accept"]
    malformed = [m for m in mismatches
                 if any("malformed" in r for r in m["reasons"])]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"(false negatives/unsafe-accepted={len(false_negatives)}, "
          f"false positives/safe-rejected={len(false_positives)}, "
          f"malformed generator cases={len(malformed)})")
    print("first 10 mismatch names:")
    for m in sorted(mismatches, key=lambda m: m["name"])[:10]:
        print(f"  {m['name']}")
    details = sorted(m["detail"] for m in mismatches)
    for line in details[:10]:
        print("  " + line)
    if len(details) > 10:
        print(f"  ... {len(details) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")

    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches, "
          f"{len(false_positives)} false positives)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
