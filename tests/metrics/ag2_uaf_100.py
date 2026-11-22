#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of ADVANCED use-after-free cases (124).

This is the "ag2" (second-generation) UAF corpus.  Every generated program is
a small, syntactically valid C (or C+ RAII) program built from a per-case
independent seeded RNG.  Generation and checking both run in thread pools;
``pool.map`` preserves input order and case ``i`` always draws from
``random.Random(f"{SEED}:{i}")``, so a run is fully reproducible regardless of
``FUZZ_WORKERS``.  The per-case marker plus the seeded parameters guarantee
that no two generated programs are identical.

Families (all target advanced use-after-free / dangling-borrow patterns):

  alias_chain      deep multi-level alias chains; free via one link, use via
                   another (including pointer-to-pointer and struct-held
                   aliases)                                            (reject)
  branch_loop      conditional frees in switch / for / while / do-while and
                   nested branches (runtime-dependent ones are ``strict``)
  interproc        two-hop callee frees (main -> mid -> callee), aliases and
                   int ** hand-offs                                    (reject)
  libc_sink        freed pointer handed to printf/fprintf/sprintf/strlen/
                   memcpy/strcmp sinks (often via an alias)            (reject)
  ret_freed        functions returning a freed pointer or a freed alias,
                   including two-hop and out-parameter variants        (reject)
  destructor       C+ resource destructor invalidates one or more borrowed
                   fields, incl. borrow passed to a helper              (reject)
  outparam         free performed by a callee through an ``int **`` out-
                   parameter (struct field / array slot / two-hop)      (reject)
  realloc_stale    successful realloc invalidates pre-realloc aliases,
                   including stale aliases held in struct fields        (reject)
  struct_array     aliases that cross struct fields and array elements,
                   incl. arrays of structs and pointer arrays            (reject)
  safe             valid controls (use-before-free, reset, correct realloc,
                   safe out-params, guarded conditional frees)         (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject - proven use-after-free: must fail BOTH --default and --strict.
  strict - conditional / unverified use-after-free: MAY pass --default, but
           must fail --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Expectations describe CORRECT C/C+ semantics, not the current checker's
behaviour.  Consequently a case the checker misses is reported as a false
negative (unsafe accepted), and a safe case it rejects is reported as a false
positive.  Programs rejected only for a non-safety reason are flagged
separately as malformed generator cases.

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/ag2_uaf/failures/.  This is a differential
measurement, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xA9B2
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
OUT = ROOT / "tests/out/core/ag2_uaf"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA9B2"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases, builder.  Case index -> family deterministically.
FAMILIES = (
    ("alias_chain", 16, None),
    ("branch_loop", 14, None),
    ("interproc", 14, None),
    ("libc_sink", 12, None),
    ("ret_freed", 10, None),
    ("destructor", 10, None),
    ("outparam", 12, None),
    ("realloc_stale", 10, None),
    ("struct_array", 12, None),
    ("safe", 14, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
PRN = "#include <stdio.h>\n"
STR = "#include <string.h>\n"
CPL = '#include "cplus/cplus.h"\n'


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag2_uaf {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: deep multi-level alias chains (including pointer-to-pointer and
# struct-held aliases).  Free via one link, deref via another.
# ---------------------------------------------------------------------------
def gen_alias_chain(i, rng):
    n = rng.randint(4, 24)
    depth = rng.randint(3, 8)
    v = rng.randint(2, 80)
    idx = rng.randrange(n)
    shape = i % 4
    name = f"alias_chain_{shape}_{i}"
    names = ["p"] + [f"a{j}" for j in range(depth)]
    decls = " ".join(f"int *a{j}={names[j]};" for j in range(depth))
    freed = rng.randrange(depth + 1)
    used = rng.randrange(depth + 1)
    while used == freed:
        used = rng.randrange(depth + 1)
    if shape == 0:  # deep linear chain, read through a non-freed link
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@DEC@@ free(@@F@@);return *@@U@@;}",
            N=n, DEC=decls, F=names[freed], U=names[used],
        )
    elif shape == 1:  # write through a later link after an earlier link frees
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@DEC@@ *@@F@@=@@V@@;free(@@F@@);@@U@@[@@I@@]=@@V@@;return 0;}",
            N=n, DEC=decls, F=names[freed], U=names[used], V=v, I=idx,
        )
    elif shape == 2:  # pointer-to-pointer indirection hides the freed alias
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int **pp=&p;int *q=*pp;int *r=q;free(*pp);return *r;}",
            N=n,
        )
    else:  # aliases split across struct fields; free one, read the other
        body = STD + tpl(
            "struct A{int *x;int *y;};"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "struct A a={p,p};a.x[@@I@@]=@@V@@;free(a.x);return a.y[@@I@@];}",
            N=n, I=idx, V=v,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 2: switch / loop conditional frees.  Runtime-dependent paths are
# "strict"; provably reachable frees are "reject".
# ---------------------------------------------------------------------------
def gen_branch_loop(i, rng):
    n = rng.randint(4, 20)
    v = rng.randint(2, 60)
    t = rng.randint(1, 6)
    idx = rng.randrange(n)
    shape = i % 5
    name = f"branch_loop_{shape}_{i}"
    expect = "reject"
    if shape == 0:  # argc-driven switch with free on one case
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "switch(argc){case 1:break;case 2:free(p);break;default:break;}"
            "return *p;}", N=n, V=v)
        expect = "strict"
    elif shape == 1:  # switch on a compile-time constant: free is certain
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;switch(1){case 1:free(p);break;default:break;}return *p;}",
            N=n, V=v)
    elif shape == 2:  # fixed-count loop frees on its last iteration
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;for(int j=0;j<@@N@@;j++){if(j==@@N@@-1)free(p);}"
            "return p[@@I@@];}", N=n, V=v, I=idx)
    elif shape == 3:  # while loop with a runtime-dependent break after free
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int j=argc;while(j-->0){if(j==@@T@@){free(p);break;}}return *p;}",
            N=n, V=v, T=t)
        expect = "strict"
    else:  # nested if + switch; alias freed on one branch
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int *q=p;if(argc>1){switch(argc){case 2:free(q);break;"
            "default:break;}}return *p;}", N=n, V=v)
        expect = "strict"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 3: two-hop interprocedural frees.
# ---------------------------------------------------------------------------
def gen_interproc(i, rng):
    n = rng.randint(4, 20)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"interproc_{shape}_{i}"
    rel = f"rel{i}"
    mid = f"mid{i}"
    use = f"use{i}"
    if shape == 0:  # main -> mid -> rel -> free
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}"
            "static void @@MID@@(int *p){@@REL@@(p);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;@@MID@@(p);return *p;}",
            REL=rel, MID=mid, N=n, V=v)
    elif shape == 1:  # alias passed through two hops, free hidden in mid
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}"
            "static void @@MID@@(int *p){int *q=p;@@REL@@(q);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;@@MID@@(q);return *p;}",
            REL=rel, MID=mid, N=n, V=v)
    elif shape == 2:  # int ** hand-off through two hops, callee frees
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);}"
            "static void @@MID@@(int **pp){@@REL@@(pp);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;@@MID@@(&p);return *q;}",
            REL=rel, MID=mid, N=n, V=v)
    else:  # freed pointer travels two hops into a dereferencing callee
        body = STD + tpl(
            "static int @@USE@@(int *p){return *p;}"
            "static int @@MID@@(int *p){return @@USE@@(p);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;free(p);return @@MID@@(p);}",
            USE=use, MID=mid, N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 4: freed buffers handed to libc / stdio sinks.
# ---------------------------------------------------------------------------
def gen_libc_sink(i, rng):
    n = rng.randint(8, 32)
    shape = i % 6
    name = f"libc_sink_{shape}_{i}"
    if shape == 0:  # printf("%s", alias-of-freed)
        body = PRN + STD + tpl(
            "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            'p[0]=0;char *q=p;free(p);printf("%s",q);return 0;}', N=n)
    elif shape == 1:  # fprintf(stderr, "%s", freed)
        body = PRN + STD + tpl(
            "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            'p[0]=0;free(p);fprintf(stderr,"%s",p);return 0;}', N=n)
    elif shape == 2:  # strlen(freed alias)
        body = STR + STD + tpl(
            "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            "p[0]=0;int *dummy=0;char *q=p;free(p);(void)dummy;"
            "return (int)strlen(q);}", N=n)
    elif shape == 3:  # memcpy from freed buffer
        body = STR + STD + tpl(
            "int main(void){char *p=malloc(@@N@@);char d[@@N@@];if(!p)return 2;"
            "p[0]=0;char *q=p;free(p);memcpy(d,q,4);return 0;}", N=n)
    elif shape == 4:  # sprintf into a live buffer from a freed format arg
        body = PRN + STD + tpl(
            "int main(void){char *p=malloc(@@N@@);char d[@@N@@];if(!p)return 2;"
            'p[0]=0;free(p);sprintf(d,"%s",p);return 0;}', N=n)
    else:  # strcmp on a freed alias against a live string
        body = STR + STD + tpl(
            "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            'p[0]=0;char *q=p;char s[2]="x";free(p);return strcmp(q,s);}',
            N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 5: functions returning freed pointers / freed aliases.
# ---------------------------------------------------------------------------
def gen_ret_freed(i, rng):
    n = rng.randint(4, 20)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"ret_freed_{shape}_{i}"
    fn = f"f{i}"
    rel = f"rel{i}"
    if shape == 0:  # return the freed pointer itself
        body = STD + tpl(
            "static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;free(p);return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;return *q;}",
            FN=fn, N=n, V=v)
    elif shape == 1:  # return an alias of the freed pointer
        body = STD + tpl(
            "static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;int *q=p;free(p);return q;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;q[0]=@@V@@;return 0;}",
            FN=fn, N=n, V=v)
    elif shape == 2:  # two-hop free inside the returning function
        body = STD + tpl(
            "static void @@REL@@(int *p){free(p);}"
            "static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;@@REL@@(p);return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;return *q;}",
            REL=rel, FN=fn, N=n, V=v)
    else:  # allocation arrives via out-parameter, then is freed and returned
        body = STD + tpl(
            "static void @@REL@@(int **pp){*pp=malloc(@@N@@*sizeof(int));}"
            "static int *@@FN@@(void){int *p=0;@@REL@@(&p);if(!p)return 0;"
            "*p=@@V@@;free(p);return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;return q[0];}",
            REL=rel, FN=fn, N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 6: C+ resource destructor invalidates borrowed fields.
# ---------------------------------------------------------------------------
def gen_destructor(i, rng):
    n = rng.randint(4, 12)
    idx = rng.randrange(n)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"destructor_{shape}_{i}"
    rt = f"R{i}"
    drop = f"{rt}_drop"
    preamble = (CPL + f"resource struct {rt}{{int x[{n}];}};"
                f"void {drop}({rt} *r){{(void)r;}}\n")
    if shape == 0:  # borrow field, drop owner, read borrow
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];r.x[@@I@@]=@@V@@;"
            "@@DROP@@(&r);return *b;}", R=rt, I=idx, V=v, DROP=drop)
    elif shape == 1:  # borrow field, drop owner, write through borrow
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];"
            "@@DROP@@(&r);*b=@@V@@;return 0;}", R=rt, I=idx, V=v, DROP=drop)
    elif shape == 2:  # decayed array borrow, drop owner, subscript
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};int *b=r.x;"
            "@@DROP@@(&r);return b[@@I@@];}", R=rt, I=idx, DROP=drop)
    elif shape == 3:  # two borrows invalidated by a single drop
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];int *c=r.x;"
            "@@DROP@@(&r);return *b+c[@@I@@];}", R=rt, I=idx, DROP=drop)
    else:  # borrow escapes into a helper, then the owner is dropped
        peek = f"peek{i}"
        body = preamble + f"static int {peek}(int *p){{return *p;}}\n" + tpl(
            "int main(void){@@R@@ r={0};int *b=&r.x[@@I@@];r.x[@@I@@]=@@V@@;"
            "@@DROP@@(&r);return @@PEEK@@(b);}",
            R=rt, I=idx, V=v, DROP=drop, PEEK=peek)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 7: free performed through an int ** out-parameter.
# ---------------------------------------------------------------------------
def gen_outparam(i, rng):
    n = rng.randint(4, 20)
    v = rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 5
    name = f"outparam_{shape}_{i}"
    rel = f"rel{i}"
    rel2 = f"rel2_{i}"
    if shape == 0:  # callee frees *pp but leaves the caller's pointer intact
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;@@REL@@(&p);return *p;}", REL=rel, N=n, V=v)
    elif shape == 1:  # callee nulls *pp, but a pre-call alias stays dangling
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);*pp=0;}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;@@REL@@(&p);return *q;}", REL=rel, N=n, V=v)
    elif shape == 2:  # struct pointer field freed through its address
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);*pp=0;}"
            "struct S{int *p;};"
            "int main(void){struct S s={0};s.p=malloc(@@N@@*sizeof(int));"
            "if(!s.p)return 2;*s.p=@@V@@;int *q=s.p;@@REL@@(&s.p);"
            "return *q;}", REL=rel, N=n, V=v)
    elif shape == 3:  # array slot freed through its address, sibling slot used
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);*pp=0;}"
            "int main(void){int *a[2];a[0]=a[1]=malloc(@@N@@*sizeof(int));"
            "if(!a[0])return 2;a[0][@@I@@]=@@V@@;@@REL@@(&a[0]);"
            "return a[1][@@I@@];}", REL=rel, N=n, V=v, I=idx)
    else:  # two-hop out-parameter free
        body = STD + tpl(
            "static void @@REL@@(int **pp){free(*pp);}"
            "static void @@REL2@@(int **pp){@@REL@@(pp);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;@@REL2@@(&p);return *p;}", REL=rel, REL2=rel2, N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: successful realloc invalidates pre-realloc aliases.
# ---------------------------------------------------------------------------
def gen_realloc_stale(i, rng):
    n = rng.randint(4, 20)
    shape = i % 5
    name = f"realloc_stale_{shape}_{i}"
    grow = f"grow{i}"
    if shape == 0:  # single stale alias used after successful realloc
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;void *t=realloc(p,(@@N@@+4)*sizeof(int));"
            "if(t){p=t;return *q;}free(p);return 0;}", N=n)
    elif shape == 1:  # two stale aliases; use the second one
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *a=p;int *b=a;void *t=realloc(p,(@@N@@+8)*sizeof(int));"
            "if(t){p=t;return *b;}free(p);return 0;}", N=n)
    elif shape == 2:  # free the stale old pointer after realloc succeeds
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *old=p;void *t=realloc(p,(@@N@@+4)*sizeof(int));"
            "if(t){free(old);p=t;return *p;}return 0;}", N=n)
    elif shape == 3:  # realloc through an int ** helper, stale alias remains
        body = STD + tpl(
            "static void @@GROW@@(int **pp){*pp=realloc(*pp,(@@N@@+8)*sizeof(int));}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;@@GROW@@(&p);if(p)return *q;return 0;}", GROW=grow, N=n)
    else:  # struct pointer field reallocated, stale alias used
        body = STD + tpl(
            "struct S{int *p;};"
            "int main(void){struct S s={0};s.p=malloc(@@N@@*sizeof(int));"
            "if(!s.p)return 2;int *q=s.p;void *t=realloc(s.p,(@@N@@+4)*sizeof(int));"
            "if(t){s.p=t;return *q;}return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 9: aliases that cross struct fields and array elements.
# ---------------------------------------------------------------------------
def gen_struct_array(i, rng):
    n = rng.randint(4, 16)
    idx = rng.randrange(n)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"struct_array_{shape}_{i}"
    if shape == 0:  # three array slots share one allocation
        body = STD + tpl(
            "int main(void){int *a[3];a[0]=a[1]=a[2]=malloc(@@N@@*sizeof(int));"
            "if(!a[0])return 2;a[0][@@I@@]=@@V@@;free(a[1]);"
            "return a[2][@@I@@];}", N=n, I=idx, V=v)
    elif shape == 1:  # struct field alias copied into an array
        body = STD + tpl(
            "struct S{int *p;};"
            "int main(void){struct S s={0};s.p=malloc(@@N@@*sizeof(int));"
            "if(!s.p)return 2;int *a[2];a[0]=a[1]=s.p;a[0][@@I@@]=@@V@@;"
            "free(s.p);return a[1][@@I@@];}", N=n, I=idx, V=v)
    elif shape == 2:  # array of structs sharing one pointer field
        body = STD + tpl(
            "struct S{int *p;};"
            "int main(void){struct S a[2];a[0].p=a[1].p=malloc(@@N@@*sizeof(int));"
            "if(!a[0].p)return 2;a[0].p[@@I@@]=@@V@@;free(a[0].p);"
            "return a[1].p[@@I@@];}", N=n, I=idx, V=v)
    elif shape == 3:  # nested struct field borrow outlives the outer object
        body = STD + tpl(
            "struct I{int v[@@N@@];};struct O{struct I in;};"
            "int main(void){struct O *o=malloc(sizeof*o);if(!o)return 2;"
            "int *q=&o->in.v[@@I@@];*q=@@V@@;free(o);return *q;}",
            N=n, I=idx, V=v)
    else:  # pointer array holds borrowed struct fields; owner is freed
        body = STD + tpl(
            "struct S{int v[@@N@@];};"
            "int main(void){struct S *s=malloc(sizeof*s);if(!s)return 2;"
            "int *a[2];a[0]=a[1]=s->v;a[0][@@I@@]=@@V@@;free(s);"
            "return a[1][@@I@@];}", N=n, I=idx, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 10: valid controls -- must pass BOTH modes.
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    n = rng.randint(4, 20)
    depth = rng.randint(2, 5)
    v = rng.randint(2, 60)
    idx = rng.randrange(n)
    shape = i % 7
    name = f"safe_{shape}_{i}"
    names = ["p"] + [f"a{j}" for j in range(depth)]
    decls = " ".join(f"int *a{j}={names[j]};" for j in range(depth))
    if shape == 0:  # use before free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int r=*p;free(p);return r==@@V@@?0:1;}", N=n, V=v)
    elif shape == 1:  # deep alias chain used before free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@DEC@@ *@@L@@=@@V@@;int r=*p;free(p);return r==@@V@@?0:1;}",
            N=n, DEC=decls, L=names[-1], V=v)
    elif shape == 2:  # free then null-reset
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;return p?*p:0;}", N=n)
    elif shape == 3:  # correct realloc success/failure handling
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,(@@N@@+2)*sizeof(int));"
            "if(t)p=t;else{free(p);return 2;}free(p);return 0;}", N=n)
    elif shape == 4:  # ownership escapes via return, freed by caller
        fn = f"mk{i}"
        body = STD + tpl(
            "static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;"
            "int r=*q;free(q);return r==@@V@@?0:1;}", FN=fn, N=n, V=v)
    elif shape == 5:  # resource drop after last use
        rt = f"R{i}"
        drop = f"{rt}_drop"
        preamble = (CPL + f"resource struct {rt}{{int x[{n}];}};"
                    f"void {drop}({rt} *r){{(void)r;}}\n")
        body = preamble + tpl(
            "int main(void){@@R@@ r={0};r.x[@@I@@]=@@V@@;int a=r.x[@@I@@];"
            "@@DROP@@(&r);return a==@@V@@?0:1;}",
            R=rt, I=idx, V=v, DROP=drop)
    elif shape == 6:  # out-parameter allocation, used then freed once
        mk = f"mk{i}"
        body = STD + tpl(
            "static void @@MK@@(int **pp){*pp=malloc(@@N@@*sizeof(int));}"
            "int main(void){int *p=0;@@MK@@(&p);if(!p)return 2;"
            "p[@@I@@]=@@V@@;int r=p[@@I@@];free(p);return r==@@V@@?0:1;}",
            MK=mk, N=n, I=idx, V=v)
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_alias_chain,
    gen_branch_loop,
    gen_interproc,
    gen_libc_sink,
    gen_ret_freed,
    gen_destructor,
    gen_outparam,
    gen_realloc_stale,
    gen_struct_array,
    gen_safe,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, _builder), builder in zip(FAMILIES, BUILDERS):
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

    print(f"ag2_uaf: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag2_uaf_"))
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
