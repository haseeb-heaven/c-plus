#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of interprocedural ownership/lifetime gaps (130).

This corpus targets one core property: a heap object is RELEASED (or its alias is
allowed to escape) INSIDE ANOTHER FUNCTION and a stale alias survives at the
caller.  A sound checker must propagate release / escape facts *across function
boundaries*, including through 2-hop call chains, function-pointer callees,
callbacks that store pointers, factories that return already-released pointers,
out-parameters that free, and calls to unknown (extern) functions.  Every
program is plain, valid C (no C+ syntax) and carries a unique case marker plus
case-suffixed helper names, so no case is a copy of another.

Generation and checking both run in thread pools.  ``pool.map`` preserves input
order and case ``i`` always draws from ``random.Random(f"{SEED}:{i}")``, so a
run is fully reproducible regardless of ``FUZZ_WORKERS``.

Families (counts are in FAMILIES):

  chain2_free      2-hop (and 3-hop) call chains that free a parameter       (reject)
  fp_callee_free   release through a function-pointer callee / table / field (reject)
  callback_store   callbacks that STORE the pointer, then stale use          (reject)
  factory_freed    factory returns a pointer it already released             (reject)
  outparam_free    out-parameter frees, then the caller uses the stale alias (reject)
  alias_unknown    alias escapes an unknown (extern) function, then is used  (strict)
  strict_cond      conditional (argc) release/escape through a callee        (strict)
  ok_transfer      safe control: ownership explicitly transferred/consumed   (accept)
  ok_outparam_null safe control: out-param frees AND nulls; caller checks    (accept)
  ok_factory_owner safe control: factory allocates; caller frees once        (accept)

Each case declares one expected outcome:

  reject - proven use-after-free / invalid release: must fail BOTH --default
           and --strict; acceptance in either mode is a safety hole ("miss").
  strict - conditional / unverified escape: MAY pass --default, but must fail
           --strict (acceptance in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Expectations describe CORRECT C semantics, not the current checker's behaviour.
A case the checker misses is a false negative (unsafe accepted); a safe case it
rejects is a false positive.  Programs rejected only for a non-safety reason
(no CPLUS-* diagnostic) are flagged separately as malformed generator cases.

Mismatches are printed and the generated source plus full both-mode diagnostics
are saved under tests/out/core/rev_interproc/failures/.  This is a differential
measurement of the checker, not a proof of interprocedural memory safety.

Environment:
  FUZZ_SEED    default 0x52494E54
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
OUT = ROOT / "tests/out/core/rev_interproc"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x52494E54"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# family, number of cases, builder name, declared outcome.  Case indices map
# deterministically to exactly one family in this order.
FAMILIES = (
    ("chain2_free",       14, "chain2_free",      "reject"),
    ("fp_callee_free",    14, "fp_callee_free",   "reject"),
    ("callback_store",    14, "callback_store",   "reject"),
    ("factory_freed",     14, "factory_freed",    "reject"),
    ("outparam_free",     14, "outparam_free",    "reject"),
    ("alias_unknown",     12, "alias_unknown",    "strict"),
    ("strict_cond",       10, "strict_cond",      "strict"),
    ("ok_transfer",       14, "ok_transfer",      "accept"),
    ("ok_outparam_null",  12, "ok_outparam_null", "accept"),
    ("ok_factory_owner",  10, "ok_factory_owner", "accept"),
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
    return f"/* rev_interproc {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Proven cross-function use-after-free (declared "reject").
# ---------------------------------------------------------------------------
def gen_chain2_free(i, rng):
    """2-hop (and deeper) call chains that free a parameter; caller uses it."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    leaf, mid, mid2 = f"leaf{i}", f"mid{i}", f"mid2_{i}"
    if shape == 0:  # main -> mid -> leaf(free), caller reuses p
        body = STD + tpl(
            "static void @@L@@(int *p){ free(p); }\n"
            "static void @@M@@(int *p){ @@L@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@M@@(p); return p[@@I@@]; }",
            L=leaf, M=mid, N=n, I=idx, V=v)
    elif shape == 1:  # 3-hop chain hides the release one level deeper
        body = STD + tpl(
            "static void @@L@@(int *p){ free(p); }\n"
            "static void @@M2@@(int *p){ @@L@@(p); }\n"
            "static void @@M@@(int *p){ @@M2@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@M@@(p); return p[@@I@@]; }",
            L=leaf, M2=mid2, M=mid, N=n, I=idx, V=v)
    elif shape == 2:  # chain with a shuffled, ignored parameter
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


def gen_fp_callee_free(i, rng):
    """Release happens through a function-pointer callee / table / struct field."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    t, fn, inv, st = f"FT{i}", f"ffree{i}", f"finv{i}", f"FS{i}"
    typedef = f"typedef void (*{t})(int *);\n"
    freefn = f"static void {fn}(int *p){{ free(p); }}\n"
    if shape == 0:  # call through a local function-pointer variable
        body = STD + typedef + freefn + tpl(
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@T@@ f=@@F@@; f(p); return p[@@I@@]; }",
            T=t, F=fn, N=n, I=idx, V=v)
    elif shape == 1:  # function pointer passed into an invoker helper
        body = STD + typedef + freefn + tpl(
            "static void @@INV@@(@@T@@ f,int *p){ f(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(@@F@@,p); return p[@@I@@]; }",
            INV=inv, T=t, F=fn, N=n, I=idx, V=v)
    elif shape == 2:  # function pointer stored in a struct field
        body = STD + typedef + freefn + tpl(
            "struct @@S@@{ @@T@@ f; };\n"
            "static void @@INV@@(struct @@S@@ *s,int *p){ s->f(p); }\n"
            "int main(void){ struct @@S@@ s; s.f=@@F@@; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(&s,p); return p[@@I@@]; }",
            S=st, T=t, INV=inv, F=fn, N=n, I=idx, V=v)
    else:  # function-pointer table indexed by a seeded constant
        k = rng.randint(0, 1)
        body = STD + typedef + freefn + tpl(
            "int main(void){ @@T@@ tab[2]; tab[0]=@@F@@; tab[1]=@@F@@; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; tab[@@K@@](p); return p[@@I@@]; }",
            T=t, F=fn, N=n, I=idx, V=v, K=k)
    return f"fp_callee_free_{i}", body


def gen_callback_store(i, rng):
    """A callback STORES the pointer into a global/context; caller later uses it."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    cb, g, g2, st, run = (f"cb{i}", f"g_saved_{i}", f"g_alias_{i}",
                          f"CS{i}", f"crun{i}")
    t = f"CT{i}"
    if shape == 0:  # callback stores pointer into a global; caller frees + reads
        body = STD + tpl(
            "static int *@@G@@;\n"
            "static void @@CB@@(int *p){ @@G@@=p; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@CB@@(p); free(p); return @@G@@[@@I@@]; }",
            G=g, CB=cb, N=n, I=idx, V=v)
    elif shape == 1:  # callback stores pointer into a context struct field
        body = STD + tpl(
            "struct @@S@@{ int *saved; };\n"
            "static void @@CB@@(void *ctx,int *p){ ((struct @@S@@*)ctx)->saved=p; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; struct @@S@@ c; c.saved=0; @@CB@@(&c,p); free(p); "
            "return c.saved[@@I@@]; }",
            S=st, CB=cb, N=n, I=idx, V=v)
    elif shape == 2:  # callback invoked via a runner, stores into a global alias
        body = STD + f"typedef void (*{t})(int *);\n" + tpl(
            "static int *@@G@@;\n"
            "static void @@CB@@(int *p){ @@G@@=p; }\n"
            "static void @@RUN@@(@@T@@ f,int *p){ f(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@RUN@@(@@CB@@,p); free(p); return @@G@@[@@I@@]; }",
            G=g2, CB=cb, RUN=run, T=t, N=n, I=idx, V=v)
    else:  # callback stores ptr and a second callback reads the stale alias
        body = STD + tpl(
            "static int *@@G@@;\n"
            "static void @@CB@@(int *p){ @@G@@=p; }\n"
            "static int @@RD@@(void){ return @@G@@[@@I@@]; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@CB@@(p); free(p); return @@RD@@(); }",
            G=g, CB=cb, RD=f"crd{i}", N=n, I=idx, V=v)
    return f"callback_store_{i}", body


def gen_factory_freed(i, rng):
    """A factory returns a pointer it already released (or a stale alias)."""
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


def gen_outparam_free(i, rng):
    """An out-parameter frees the allocation; the caller keeps a stale alias."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, st = f"rel{i}", f"OS{i}"
    if shape == 0:  # free through int** and null the handle; alias survives
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


# ---------------------------------------------------------------------------
# Conditional / unverified escape (declared "strict").
# ---------------------------------------------------------------------------
def gen_alias_unknown(i, rng):
    """An alias escapes an unknown (extern) function, then the caller uses it."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    unk = f"unknown_{i}"
    if shape == 0:  # extern function receives p; caller cannot see its body
        body = STD + tpl(
            "extern void @@U@@(int *p);\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@U@@(p); return p[@@I@@]; }",
            U=unk, N=n, I=idx, V=v)
    elif shape == 1:  # extern function returns an alias stored in a global
        body = STD + tpl(
            "static int *g_alias;\n"
            "extern int *@@U@@(int *p);\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; g_alias=@@U@@(p); return g_alias[@@I@@]; }",
            U=unk, N=n, I=idx, V=v)
    elif shape == 2:  # extern function receives a struct holding the pointer
        body = STD + tpl(
            "struct US@@I2@@{ int *p; };\n"
            "extern void @@U@@(struct US@@I2@@ *s);\n"
            "int main(void){ struct US@@I2@@ s; s.p=malloc(@@N@@*sizeof(int)); "
            "if(!s.p)return 2; s.p[@@I@@]=@@V@@; @@U@@(&s); return s.p[@@I@@]; }",
            U=unk, I2=i, N=n, I=idx, V=v)
    else:  # call through an extern function-pointer variable
        body = STD + tpl(
            "extern void (*@@U@@)(int *p);\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@U@@(p); return p[@@I@@]; }",
            U=unk, N=n, I=idx, V=v)
    return f"alias_unknown_{i}", body


def gen_strict_cond(i, rng):
    """Release/escape is conditional on argc: may pass default, must fail strict."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, cb, fac = f"rel{i}", f"cb{i}", f"fac{i}"
    if shape == 0:  # chain frees only on a runtime argc condition
        body = STD + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "int main(int argc,char **av){(void)av; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; if(argc>@@C@@)@@R@@(p); return p[@@I@@]; }",
            R=rel, N=n, I=idx, V=v, C=rng.randint(1, 8))
    elif shape == 1:  # callback stores pointer, conditional free, stale use
        body = STD + tpl(
            "static int *g_s;\n"
            "static void @@CB@@(int *p){ g_s=p; }\n"
            "int main(int argc,char **av){(void)av; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@CB@@(p); if(argc>@@C@@)free(p); return g_s[@@I@@]; }",
            CB=cb, N=n, I=idx, V=v, C=rng.randint(1, 8))
    elif shape == 2:  # factory conditionally releases on argc, caller uses
        body = STD + tpl(
            "static int *@@F@@(int n,int flag){ int *p=malloc(n*sizeof(int)); "
            "if(!p)return 0; if(flag)free(p); return p; }\n"
            "int main(int argc,char **av){(void)av; "
            "int *p=@@F@@(@@N@@,argc>@@C@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; return p[@@I@@]; }",
            F=fac, N=n, I=idx, V=v, C=rng.randint(1, 8))
    else:  # unknown function called only on argc condition; stale use follows
        body = STD + tpl(
            "extern void @@U@@(int *p);\n"
            "int main(int argc,char **av){(void)av; "
            "int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; if(argc>@@C@@)@@U@@(p); return p[@@I@@]; }",
            U=f"unknown_c{i}", N=n, I=idx, V=v, C=rng.randint(1, 8))
    return f"strict_cond_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": must pass both modes).
# ---------------------------------------------------------------------------
def gen_ok_transfer(i, rng):
    """Safe control: ownership is explicitly transferred and consumed once."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, mid, t, inv, cb = (f"rel{i}", f"mid{i}", f"FT{i}",
                            f"finv{i}", f"cb{i}")
    if shape == 0:  # callee consumes (uses then frees) the transferred object
        body = STD + tpl(
            "static void consume@@I2@@(int *p,int k,int val){ p[k]=val; free(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "consume@@I2@@(p,@@I@@,@@V@@); return 0; }",
            I2=i, N=n, I=idx, V=v)
    elif shape == 1:  # 2-hop chain frees the transferred object; no stale use
        body = STD + tpl(
            "static void @@L@@(int *p){ free(p); }\n"
            "static void @@M@@(int *p){ @@L@@(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@M@@(p); return 0; }",
            L=rel, M=mid, N=n, I=idx, V=v)
    elif shape == 2:  # function-pointer callee frees; caller never touches it
        body = STD + f"typedef void (*{t})(int *);\n" + tpl(
            "static void @@R@@(int *p){ free(p); }\n"
            "static void @@INV@@(@@T@@ f,int *p){ f(p); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(@@R@@,p); return 0; }",
            R=rel, INV=inv, T=t, N=n, I=idx, V=v)
    else:  # callback frees the context; caller never uses it again
        body = STD + f"typedef void (*{t})(void *);\n" + tpl(
            "static void @@CB@@(void *ctx){ free(ctx); }\n"
            "static void @@INV@@(@@T@@ f,void *ctx){ f(ctx); }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@INV@@(@@CB@@,p); return 0; }",
            CB=cb, INV=inv, T=t, N=n, I=idx, V=v)
    return f"ok_transfer_{i}", body


def gen_ok_outparam_null(i, rng):
    """Safe control: out-parameter frees AND nulls; caller checks before use."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    rel, st = f"rel{i}", f"OS{i}"
    if shape == 0:  # int** out-param nulls; caller only tests for null
        body = STD + tpl(
            "static void @@R@@(int **pp){ free(*pp); *pp=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "@@R@@(&p); return p?1:0; }",
            R=rel, N=n)
    elif shape == 1:  # struct out-param nulls the field; caller checks it
        body = STD + tpl(
            "struct @@S@@{ int *p; };\n"
            "static void @@R@@(struct @@S@@ *s){ free(s->p); s->p=0; }\n"
            "int main(void){ struct @@S@@ s; s.p=malloc(@@N@@*sizeof(int)); "
            "if(!s.p)return 2; @@R@@(&s); return s.p?s.p[0]:0; }",
            S=st, R=rel, N=n)
    elif shape == 2:  # out-param uses a local, nulls handle; caller checks null
        body = STD + tpl(
            "static void @@R@@(int **pp){ int *old=*pp; free(old); *pp=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "@@R@@(&p); if(!p)return 0; return p[@@I@@]; }",
            R=rel, N=n, I=idx)
    else:  # release through a wrapper, null handle; caller checks null
        body = STD + tpl(
            "static void @@W@@(void *q){ free(q); }\n"
            "static void @@R@@(int **pp){ @@W@@(*pp); *pp=0; }\n"
            "int main(void){ int *p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
            "p[@@I@@]=@@V@@; @@R@@(&p); return p?p[0]:0; }",
            W=f"wrap{i}", R=rel, N=n, I=idx, V=v)
    return f"ok_outparam_null_{i}", body


def gen_ok_factory_owner(i, rng):
    """Safe control: factory allocates; the caller owns and frees exactly once."""
    n, idx, v = rng.randint(4, 24), 0, rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    mk, wr = f"mk{i}", f"wr{i}"
    if shape == 0:  # plain factory; caller writes then frees once
        body = STD + tpl(
            "static int *@@M@@(int n){ return malloc(n*sizeof(int)); }\n"
            "int main(void){ int *p=@@M@@(@@N@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; int x=p[@@I@@]; free(p); return x==@@V@@?0:1; }",
            M=mk, N=n, I=idx, V=v)
    elif shape == 1:  # calloc-backed factory; caller frees once
        body = STD + tpl(
            "static int *@@M@@(int n){ return calloc(n,sizeof(int)); }\n"
            "int main(void){ int *p=@@M@@(@@N@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; int x=p[@@I@@]; free(p); return x==@@V@@?0:1; }",
            M=mk, N=n, I=idx, V=v)
    elif shape == 2:  # factory + writer callee; caller frees after the write
        body = STD + tpl(
            "static int *@@M@@(int n){ return malloc(n*sizeof(int)); }\n"
            "static void @@W@@(int *p,int k,int val){ p[k]=val; }\n"
            "int main(void){ int *p=@@M@@(@@N@@); if(!p)return 2; "
            "@@W@@(p,@@I@@,@@V@@); int x=p[@@I@@]; free(p); return x==@@V@@?0:1; }",
            M=mk, W=wr, N=n, I=idx, V=v)
    else:  # factory with a size-check; caller frees exactly once
        body = STD + tpl(
            "static int *@@M@@(int n){ if(n<=0)return 0; return malloc(n*sizeof(int)); }\n"
            "int main(void){ int *p=@@M@@(@@N@@); if(!p)return 2; "
            "p[@@I@@]=@@V@@; int x=p[@@I@@]; free(p); return x==@@V@@?0:1; }",
            M=mk, N=n, I=idx, V=v)
    return f"ok_factory_owner_{i}", body


BUILDERS = (
    gen_chain2_free, gen_fp_callee_free, gen_callback_store,
    gen_factory_freed, gen_outparam_free, gen_alias_unknown,
    gen_strict_cond, gen_ok_transfer, gen_ok_outparam_null,
    gen_ok_factory_owner,
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
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    if not reasons:
        return None

    (FAILS / f"{name}.c").write_text(body)
    tags = {
        mode: " ".join(diag_tags(modes[mode][1])) or "(none)"
        for mode in ("--default", "--strict")
    }
    log = [
        f"case: {name}",
        f"family: {family}",
        f"expect: {expect}",
        f"reasons: {'; '.join(reasons)}",
        f"--default rejected={d_rej}: {first_diag(modes['--default'][1])}",
        f"--strict rejected={s_rej}: {first_diag(modes['--strict'][1])}",
        f"tags --default: {tags['--default']}",
        f"tags --strict: {tags['--strict']}",
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

    print(f"rev_interproc: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:18} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="rev_interproc_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]

    false_positives = [m for m in mismatches if m["expect"] == "accept"]
    misses = [m for m in mismatches if m["expect"] in ("reject", "strict")]
    malformed = [m for m in mismatches
                 if any("malformed" in r for r in m["reasons"])]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"({len(misses)} safety holes / unsafe-accepted, "
          f"{len(false_positives)} false positives, "
          f"{len(malformed)} malformed generator cases)")
    print("false positives (safe rejected): "
          + ("none" if not false_positives else
             ", ".join(f"{m['family']}/{m['name']}" for m in false_positives)))
    by_family = collections.Counter(m["family"] for m in mismatches)
    print("mismatches by family: "
          + ("none" if not by_family else
             " ".join(f"{k}={by_family[k]}" for k in sorted(by_family))))

    print("first 10 mismatch names:")
    for m in sorted(mismatches, key=lambda m: m["name"])[:10]:
        print(f"  {m['name']} [{m['family']}, expect={m['expect']}] "
              f"{'; '.join(m['reasons'])}")
    details = sorted(m["detail"] for m in mismatches)
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
