#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of high-value invalid-free / UAF cases (116).

This is the "hv" (high-value) UAF corpus: a curated set of small, syntactically
valid C (or C+ RAII) programs that exercise the classic invalid-release and
use-after-free patterns.  Every program is built from a per-case independent
seeded RNG.  Generation and verification both run in thread pools; ``pool.map``
preserves input order and case ``i`` always draws from
``random.Random(f"{FUZZ_SEED}:{i}")``, so a run is fully reproducible regardless
of ``FUZZ_WORKERS``.  A unique marker comment plus seeded size / offset / value
parameters make every generated program byte-distinct -- no case is shared with
another corpus.

Families (each targets one of the requested categories):

  df_same          double free of the very same pointer                 (reject)
  df_alias         double free reached through aliases / pointer-to-ptr  (reject)
  df_branch        second free reached through an if / switch branch     (reject/strict)
  df_loop          repeated free inside for / while / do loops           (reject)
  df_callee        callee frees, caller frees the same pointer again     (reject)
  fi_stack         free of a stack local                                 (reject)
  fi_global        free of a global / static object                      (reject)
  fi_literal       free of a string literal                              (reject)
  fi_array         free of a stack array (decayed pointer)               (reject)
  fi_interior      free of an interior pointer into an allocation        (reject)
  fi_increment     free of an incremented / advanced (p++) pointer       (reject)
  fi_field         free of a struct field / &field element              (reject)
  safe_null        free(NULL) / zeroed-handle controls                   (accept)
  safe_reset       free then reset-to-null and free again controls       (accept)
  uaf_callee       callee frees, caller dereferences the stale pointer   (reject)
  realloc_stale    pre-realloc aliases used after a successful realloc   (reject)
  destructor       C+ resource destructor invalidates borrowed fields    (reject)
  struct_field     use of a freed struct pointer field / aliased field   (reject)
  safe             general valid controls (use-before-free, etc.)        (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject   - proven allocator error / use-after-free: must fail BOTH modes.
  strict   - conditional / unverified error: MAY pass --default, but must fail
             --strict (accepting it in STRICT is a safety hole).
  accept   - valid control: must pass BOTH modes (rejection is a false positive).

Expectations describe CORRECT C/C+ semantics, not the current checker's
behaviour.  A case the checker misses is a false negative (unsafe accepted);
a safe case it rejects is a false positive.  Programs rejected only for a
non-safety reason are flagged separately as malformed generator cases.

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/hv_uaf/failures/.  This is a differential
measurement, not a proof of safety.

Environment:
  FUZZ_SEED    default 0x1F4A
  FUZZ_WORKERS default 8
  FUZZ_TIMEOUT default 60 seconds per check
  CPLUS_BIN    default <root>/build/c+ (existing binary; no rebuild needed)
"""
import collections
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/hv_uaf"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x1F4A"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name -> number of cases.  A case index maps deterministically to
# exactly one family via the cumulative counts below.
FAMILIES = (
    ("df_same", 8),
    ("df_alias", 8),
    ("df_branch", 8),
    ("df_loop", 6),
    ("df_callee", 6),
    ("fi_stack", 6),
    ("fi_global", 5),
    ("fi_literal", 5),
    ("fi_array", 5),
    ("fi_interior", 6),
    ("fi_increment", 6),
    ("fi_field", 6),
    ("safe_null", 6),
    ("safe_reset", 6),
    ("uaf_callee", 8),
    ("realloc_stale", 6),
    ("destructor", 6),
    ("struct_field", 6),
    ("safe", 8),
)
TOTAL = sum(count for _name, count in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
PRN = "#include <stdio.h>\n"
STR = "#include <string.h>\n"
CPL = '#include "cplus/cplus.h"\n'
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def lit(n):
    """A distinct non-empty string literal that varies with n."""
    return ALPHABET[:(n % (len(ALPHABET) - 1)) + 1]


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hv_uaf {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# 1. Double free of the same pointer (no alias needed).
# ---------------------------------------------------------------------------
def gen_df_same(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"df_same_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;free(p);free(p);return 0;}", N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);int x=@@V@@;(void)x;free(p);return 0;}", N=n, V=v)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);free(p);return 0;}", N=n)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;free(p);free(p);return *p;}", N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 2. Double free reached through aliases / pointer-to-pointer / struct fields.
# ---------------------------------------------------------------------------
def gen_df_alias(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"df_alias_{shape}_{i}"
    if shape == 0:  # one alias
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;free(p);free(q);return 0;}", N=n, V=v)
    elif shape == 1:  # deep alias chain
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;int *r=q;int *s=r;free(p);free(s);return 0;}", N=n)
    elif shape == 2:  # pointer-to-pointer hides the second free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int **pp=&p;free(p);free(*pp);return 0;}", N=n)
    else:  # aliases split across two struct fields
        body = STD + tpl(
            "struct A@@I@@{int *x;int *y;};"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "struct A@@I@@ a={p,p};free(a.x);free(a.y);return 0;}",
            I=i, N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 3. Second free reached through an if / switch branch.
# ---------------------------------------------------------------------------
def gen_df_branch(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sel = rng.randint(1, 4)
    shape = i % 4
    name = f"df_branch_{shape}_{i}"
    expect = "reject"
    if shape == 0:  # constant if: free is certain
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;if(1)free(p);free(p);return 0;}", N=n, V=v)
    elif shape == 1:  # constant switch selector
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int s=@@S@@;switch(s){case @@S@@:free(p);break;default:break;}"
            "free(p);return 0;}", N=n, S=sel)
    elif shape == 2:  # argc-driven free, then unconditional free
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>0)free(p);free(p);return 0;}", N=n)
        expect = "strict"
    else:  # nested constant branches
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(1){if(1)free(p);}free(p);return 0;}", N=n)
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# 4. Repeated free inside loops.
# ---------------------------------------------------------------------------
def gen_df_loop(i, rng):
    n = rng.randint(4, 24)
    k = rng.randint(2, 5)
    shape = i % 3
    name = f"df_loop_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int j=0;j<@@K@@;j++)free(p);return 0;}", N=n, K=k)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int j=@@K@@;while(j-->0)free(p);return 0;}", N=n, K=k)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int j=@@K@@;do{free(p);}while(--j>0);return 0;}", N=n, K=k)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 5. Callee frees, caller releases the same pointer again.
# ---------------------------------------------------------------------------
def gen_df_callee(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 55)
    shape = i % 3
    name = f"df_callee_{shape}_{i}"
    rel = f"rel{i}"
    if shape == 0:
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;@@REL@@(p);@@REL@@(p);return 0;}", REL=rel, N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@REL@@(p);free(p);return 0;}", REL=rel, N=n)
    else:  # two-hop wrapper frees, caller frees
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "static void mid@@I@@(int *p){@@REL@@(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "mid@@I@@(p);free(p);return 0;}", REL=rel, I=i, N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 6. free of a stack local.
# ---------------------------------------------------------------------------
def gen_fi_stack(i, rng):
    v = rng.randint(2, 60)
    shape = i % 3
    name = f"fi_stack_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int x=@@V@@;free(&x);return 0;}", V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int x=@@V@@;void *p=(void *)&x;free(p);return 0;}", V=v)
    else:
        body = STD + tpl(
            "int main(void){{int x=@@V@@;free(&x);}return 0;}", V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 7. free of a global / static object.
# ---------------------------------------------------------------------------
def gen_fi_global(i, rng):
    n = rng.randint(3, 16)
    v = rng.randint(2, 48)
    shape = i % 3
    name = f"fi_global_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "static int g@@I@@;int main(void){int x=@@V@@;(void)x;"
            "free(&g@@I@@);return 0;}", I=i, V=v)
    elif shape == 1:
        body = STD + tpl(
            "static int g@@I@@[@@N@@];int main(void){free(g@@I@@);return 0;}",
            I=i, N=n)
    else:
        body = STD + tpl(
            "struct G@@I@@{int x[@@N@@];};static struct G@@I@@ g@@I@@;"
            "int main(void){free(&g@@I@@.x[1]);return 0;}", I=i, N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 8. free of a string literal.
# ---------------------------------------------------------------------------
def gen_fi_literal(i, rng):
    n = rng.randint(3, 20)
    shape = i % 3
    name = f"fi_literal_{shape}_{i}"
    if shape == 0:
        body = STD + tpl('int main(void){free((void *)"@@S@@");return 0;}', S=lit(n))
    elif shape == 1:
        body = STD + tpl(
            'int main(void){char *p="@@S@@";free(p);return 0;}', S=lit(n))
    else:
        body = STD + tpl(
            'int main(void){const char *p="@@S@@";free((void *)p);return 0;}',
            S=lit(n))
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 9. free of a stack array (decayed pointer).
# ---------------------------------------------------------------------------
def gen_fi_array(i, rng):
    n = rng.randint(3, 16)
    shape = i % 3
    name = f"fi_array_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int a[@@N@@]={0};free(a);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int a[@@N@@]={0};free(&a[0]);return 0;}", N=n)
    else:
        body = STD + tpl(
            "int main(void){int a[2][@@N@@]={{0},{0}};free(a[1]);return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 10. free of an interior pointer into a heap allocation.
# ---------------------------------------------------------------------------
def gen_fi_interior(i, rng):
    n = rng.randint(4, 20)
    off = rng.randint(1, 3)
    shape = i % 3
    name = f"fi_interior_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "free(p+@@O@@);return 0;}", N=n, O=off)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "free(&p[@@O@@]);return 0;}", N=n, O=off)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "char *c=(char *)p;free(c+@@O@@);return 0;}", N=n, O=off)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 11. free of an incremented / advanced (p++) pointer.
# ---------------------------------------------------------------------------
def gen_fi_increment(i, rng):
    n = rng.randint(4, 20)
    off = rng.randint(1, 4)
    shape = i % 3
    name = f"fi_increment_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "p++;free(p);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "p+=@@O@@;free(p);return 0;}", N=n, O=off)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "int *q=p;for(int j=0;j<@@O@@;j++)q++;free(q);return 0;}", N=n, O=off)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 12. free of a struct field / &field element.
# ---------------------------------------------------------------------------
def gen_fi_field(i, rng):
    n = rng.randint(4, 16)
    v = rng.randint(2, 48)
    off = rng.randint(1, 3)
    shape = i % 3
    name = f"fi_field_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "struct S@@I@@{int x[@@N@@];};"
            "int main(void){struct S@@I@@ s;free(&s.x[@@O@@]);return 0;}",
            I=i, N=n, O=off)
    elif shape == 1:
        body = STD + tpl(
            "struct S@@I@@{int *p;};"
            "int main(void){int x=@@V@@;struct S@@I@@ s={0};s.p=&x;"
            "free(s.p);return 0;}", I=i, V=v)
    else:
        body = STD + tpl(
            "struct S@@I@@{int x[@@N@@];};"
            "int main(void){struct S@@I@@ *s=malloc(sizeof*s);if(!s)return 2;"
            "free(&s->x[@@O@@]);return 0;}", I=i, N=n, O=off)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 13. free(NULL) / zeroed-handle controls.
# ---------------------------------------------------------------------------
def gen_safe_null(i, rng):
    n = rng.randint(3, 20)
    shape = i % 4
    name = f"safe_null_{shape}_{i}"
    if shape == 0:
        body = STD + "int main(void){free(0);free(NULL);return 0;}"
    elif shape == 1:
        body = STD + "int main(void){int *p=0;free(p);return 0;}"
    elif shape == 2:
        body = STD + tpl(
            "int main(void){void *p=NULL;(void)p;free(p);free(0);return 0;}", N=n)
    else:
        body = STD + tpl(
            "static void maybe@@I@@(int *p){if(p)free(p);}\n"
            "int main(void){int x=@@N@@;(void)x;maybe@@I@@(0);"
            "maybe@@I@@(NULL);return 0;}", I=i, N=n)
    return name, marked(name, body), "accept"


# ---------------------------------------------------------------------------
# 14. free then reset-to-null, then free again controls.
# ---------------------------------------------------------------------------
def gen_safe_reset(i, rng):
    n = rng.randint(4, 24)
    shape = i % 3
    name = f"safe_reset_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;free(p);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=NULL;if(p)free(p);return 0;}", N=n)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;return p?*p:0;}", N=n)
    return name, marked(name, body), "accept"


# ---------------------------------------------------------------------------
# 15. Callee frees; caller dereferences the stale pointer.
# ---------------------------------------------------------------------------
def gen_uaf_callee(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 4
    name = f"uaf_callee_{shape}_{i}"
    rel = f"rel{i}"
    if shape == 0:
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[@@I@@]=@@V@@;@@REL@@(p);return p[@@I@@];}",
            REL=rel, N=n, I=idx, V=v)
    elif shape == 1:  # two-hop free hidden in a mid function
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "static void mid@@I@@(int *p){@@REL@@(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "mid@@I@@(p);return *p;}", REL=rel, I=i, N=n)
    elif shape == 2:  # alias used after the callee frees through a real handle
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;q[@@I@@]=@@V@@;@@REL@@(p);return q[@@I@@];}",
            REL=rel, N=n, I=idx, V=v)
    else:  # callee frees through an int ** out-param; caller keeps a stale alias
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);*pp=0;}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;@@REL@@(&p);return *q;}", REL=rel, N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 16. Pre-realloc aliases used after a successful realloc.
# ---------------------------------------------------------------------------
def gen_realloc_stale(i, rng):
    n = rng.randint(4, 20)
    idx = rng.randrange(n)
    shape = i % 3
    name = f"realloc_stale_{shape}_{i}"
    if shape == 0:  # single stale alias read
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[@@I@@]=@@V@@;int *q=p;void *t=realloc(p,(@@N@@+4)*sizeof(int));"
            "if(t){p=t;return q[@@I@@];}free(p);return 0;}", N=n, I=idx, V=7)
    elif shape == 1:  # two stale aliases; use the second
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *a=p;int *b=a;void *t=realloc(p,(@@N@@+8)*sizeof(int));"
            "if(t){p=t;return *b;}free(p);return 0;}", N=n)
    else:  # free the stale old pointer after realloc succeeds
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *old=p;void *t=realloc(p,(@@N@@+4)*sizeof(int));"
            "if(t){free(old);p=t;return *p;}return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 17. C+ resource destructor invalidates borrowed fields.
# ---------------------------------------------------------------------------
def gen_destructor(i, rng):
    n = rng.randint(4, 12)
    idx = rng.randrange(n)
    v = rng.randint(2, 60)
    shape = i % 3
    name = f"destructor_{shape}_{i}"
    rt = f"R{i}"
    drop = f"{rt}_drop"
    preamble = (CPL + f"resource struct {rt}{{int x[{n}];}};"
                f"void {drop}({rt} *r){{(void)r;}}\n")
    if shape == 0:  # borrow a field, drop owner, read the borrow
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];r.x[@@I@@]=@@V@@;"
            "@@DROP@@(&r);return *b;}", R=rt, I=idx, V=v, DROP=drop)
    elif shape == 1:  # borrow a field, drop owner, write through the borrow
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];"
            "@@DROP@@(&r);*b=@@V@@;return 0;}", R=rt, I=idx, V=v, DROP=drop)
    else:  # borrow escapes into a helper before the owner is dropped
        peek = f"peek{i}"
        body = preamble + f"static int {peek}(int *p){{return *p;}}\n" + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];r.x[@@I@@]=@@V@@;"
            "@@DROP@@(&r);return @@PEEK@@(b);}",
            R=rt, I=idx, V=v, DROP=drop, PEEK=peek)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 18. Use-after-free through struct pointer fields.
# ---------------------------------------------------------------------------
def gen_struct_field(i, rng):
    n = rng.randint(4, 16)
    idx = rng.randrange(n)
    v = rng.randint(2, 60)
    shape = i % 3
    name = f"struct_field_{shape}_{i}"
    if shape == 0:  # alias of a freed pointer field
        body = STD + tpl(
            "struct S@@I@@{int *p;};"
            "int main(void){struct S@@I@@ s={0};s.p=malloc(@@N@@*sizeof(int));"
            "if(!s.p)return 2;s.p[@@J@@]=@@V@@;int *q=s.p;free(s.p);"
            "return q[@@J@@];}", I=i, N=n, J=idx, V=v)
    elif shape == 1:  # two fields share one allocation
        body = STD + tpl(
            "struct S@@I@@{int *x;int *y;};"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "struct S@@I@@ s={p,p};s.x[@@J@@]=@@V@@;free(s.x);"
            "return s.y[@@J@@];}", I=i, N=n, J=idx, V=v)
    else:  # array of structs sharing a pointer field
        body = STD + tpl(
            "struct S@@I@@{int *p;};"
            "int main(void){struct S@@I@@ a[2];"
            "a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;"
            "a[0].p[@@J@@]=@@V@@;free(a[0].p);return a[1].p[@@J@@];}",
            I=i, N=n, J=idx, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# 19. General valid controls -- must pass BOTH modes.
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    n = rng.randint(4, 20)
    depth = rng.randint(2, 5)
    v = rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 8
    name = f"safe_{shape}_{i}"
    if shape == 0:  # use before free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[@@I@@]=@@V@@;int r=p[@@I@@];free(p);return r==@@V@@?0:1;}",
            N=n, I=idx, V=v)
    elif shape == 1:  # deep alias chain used before the single free
        names = ["p"] + [f"a{j}" for j in range(depth)]
        decls = " ".join(f"int *a{j}={names[j]};" for j in range(depth))
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@DEC@@ *@@L@@=@@V@@;int r=*p;free(p);return r==@@V@@?0:1;}",
            N=n, DEC=decls, L=names[-1], V=v)
    elif shape == 2:  # correct realloc with failure handling and one free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,(@@N@@+2)*sizeof(int));"
            "if(!t){free(p);return 2;}p=t;free(p);return 0;}", N=n)
    elif shape == 3:  # callee frees, caller does not touch the pointer again
        rel = f"rel{i}"
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@REL@@(p);return 0;}", REL=rel, N=n)
    elif shape == 4:  # managed RAII resource, no explicit release
        rt = f"R{i}"
        drop = f"{rt}_drop"
        preamble = (CPL + f"resource struct {rt}{{int x[{n}];}};"
                    f"void {drop}({rt} *r){{(void)r;}}\n")
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};r.x[@@I@@]=@@V@@;"
            "int a=r.x[@@I@@];@@DROP@@(&r);return a==@@V@@?0:1;}",
            R=rt, I=idx, V=v, DROP=drop)
    elif shape == 5:  # out-parameter allocation used once then freed once
        mk = f"mk{i}"
        body = STD + tpl(
            "static void @@MK@@(int **pp){*pp=malloc(@@N@@*sizeof(int));}\n"
            "int main(void){int *p=0;@@MK@@(&p);if(!p)return 2;"
            "p[@@I@@]=@@V@@;int r=p[@@I@@];free(p);return r==@@V@@?0:1;}",
            MK=mk, N=n, I=idx, V=v)
    elif shape == 6:  # struct pointer field allocated, used, released once
        body = STD + tpl(
            "struct S@@I@@{int *p;};"
            "int main(void){struct S@@I@@ s={0};"
            "s.p=malloc(@@N@@*sizeof(int));if(!s.p)return 2;"
            "s.p[@@J@@]=@@V@@;int r=s.p[@@J@@];free(s.p);"
            "return r==@@V@@?0:1;}", I=i, N=n, J=idx, V=v)
    else:  # interior pointer used (not freed) while the base stays live
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "int *q=p+1;q[@@I@@]=@@V@@;int r=q[@@I@@];free(p);"
            "return r==@@V@@?0:1;}", N=n, I=idx, V=v)
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_df_same,
    gen_df_alias,
    gen_df_branch,
    gen_df_loop,
    gen_df_callee,
    gen_fi_stack,
    gen_fi_global,
    gen_fi_literal,
    gen_fi_array,
    gen_fi_interior,
    gen_fi_increment,
    gen_fi_field,
    gen_safe_null,
    gen_safe_reset,
    gen_uaf_callee,
    gen_realloc_stale,
    gen_destructor,
    gen_struct_field,
    gen_safe,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body, expect = builder(i, rng)
            assert expect in ("reject", "strict", "accept"), (name, expect)
            return name, body, expect, family
        start += count
    raise IndexError(i)


# ---------------------------------------------------------------------------
# Verification: check each generated case in DEFAULT and STRICT.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def check_case(case):
    """Run one case in both modes; return a mismatch record or None."""
    name, body, expect, family = case
    path = STAGE / f"{name}.c"
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run(
                [CC, "check", mode, str(path)],
                capture_output=True, text=True, timeout=TIMEOUT,
            )
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
    log = [
        f"case: {name}",
        f"family: {family}",
        f"expect: {expect}",
        f"reasons: {'; '.join(reasons)}",
        f"--default rejected={d_rej}: {first_diag(modes['--default'][1])}",
        f"--strict rejected={s_rej}: {first_diag(modes['--strict'][1])}",
        "",
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

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the output.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"
    assert unique == TOTAL, "duplicate generated program body"

    print(f"hv_uaf: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count in FAMILIES:
        print(f"  {family}: {families[family]}")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hv_uaf_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]

    false_negatives = [m for m in mismatches if m["expect"] != "accept"]
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
    print(f"FINAL: {'PASS' if not mismatches else 'FAIL'} "
          f"({len(cases)} cases, {len(mismatches)} mismatches, "
          f"{len(false_positives)} false positives)")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
