#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of pointer-object-gap C programs.

Produces 155 distinct small, valid C programs (>=100) from per-case independent
seeded RNGs.  Generation and checking both run in thread pools; ``pool.map``
preserves input order and every case draws from ``random.Random(f"{SEED}:{i}")``,
so a run is fully reproducible regardless of ``FUZZ_WORKERS``.  Every program
carries a unique case marker plus case-suffixed helper names and varied
sizes/indices/values, so no case is a copy of another.  All programs are plain C
(no C+ extension syntax).

Families (declared outcome in parentheses):

  cross_sub       subtraction across distinct arrays / allocations    (reject)
  cross_rel       relational comparison across distinct objects       (reject)
  one_past_deref  dereference of a one-past-the-end pointer          (reject)
  null_arith      pointer arithmetic on / from a null pointer         (reject)
  forged_int      integer-forged pointer dereference / write         (reject)
  uintptr_bump    uintptr_t round trip with an added/subtracted offs. (reject)
  tagged_deref    tagged pointer dereferenced / freed without untag   (reject)
  misaligned      misaligned cast and dereference                     (reject)
  void_arith      arithmetic directly on a void *                     (reject)
  container_null  container_of computed from a null pointer           (reject)
  restrict_overlap two restrict-qualified pointers alias the same obj (reject)
  funcptr_cast    call through an incompatible function-pointer type  (reject)
  uintptr_rt      uintptr_t round trip of a real pointer             (strict)
  tagged_mask     tagged pointer untagged by masking, then deref'd    (strict)
  type_pun        strict-aliasing type punning via a cast pointer     (strict)
  ok_same_array   safe control: arithmetic within one array          (accept)
  ok_cast         safe control: valid void */char */union casts      (accept)
  ok_one_past     safe control: one-past formation and walk-back     (accept)
  ok_restrict     safe control: disjoint restrict-qualified pointers  (accept)
  ok_funcptr      safe control: matching function-pointer call       (accept)

Each case declares one expected outcome:

  reject - proven pointer-provenance / object-rule violation: must fail
           BOTH --default and --strict.  Acceptance in either mode is a hole.
  strict - conditional / implementation-defined pointer rule: MAY pass
           --default, but must fail --strict.
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full both-mode diagnostics
are saved under tests/out/core/hw_ptr/failures/.  False positives (safe controls
rejected by the checker) are counted and reported separately.  This is a
differential measurement of the checker, not a proof of pointer safety.

Environment:
  FUZZ_SEED    default 0x48575054 ("HWPT")
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
OUT = ROOT / "tests/out/core/hw_ptr"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x48575054"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

STAGE = None

STD = "#include <stdlib.h>\n"
SZ = "#include <stdint.h>\n"
OFF = "#include <stddef.h>\n"
SZH = "#include <stdlib.h>\n#include <stdint.h>\n"


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hw_ptr {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


# ---------------------------------------------------------------------------
# Proven pointer-object violations (declared "reject").
# ---------------------------------------------------------------------------
def gen_cross_sub(i, rng):
    n = rng.randint(5, 14)
    m = n + rng.randint(1, 9)
    k = rng.randint(1, 4)
    v = rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = tpl(
            "int main(void){int a[@@N@@]={0},b[@@M@@]={0};"
            "b[@@K@@]=@@V@@;return &a[0]-&b[@@K@@];}", N=n, M=m, K=k, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int*p=malloc(@@N@@*sizeof(int));"
            "int*q=malloc(@@M@@*sizeof(int));if(!p||!q)return 2;"
            "p[0]=@@V@@;q[@@K@@]=@@V@@;int d=(int)(p-q);free(p);free(q);return d;}",
            N=n, M=m, K=k, V=v)
    else:
        body = STD + tpl(
            "int main(void){int a[@@N@@]={0};int*p=malloc(@@M@@*sizeof(int));"
            "if(!p)return 2;p[0]=@@V@@;int d=(int)(&a[0]-p);free(p);return d;}",
            N=n, M=m, V=v)
    return f"cross_sub_{i}", body


def gen_cross_rel(i, rng):
    n = rng.randint(5, 14)
    m = n + rng.randint(1, 9)
    k = rng.randint(1, 4)
    op = ("<", ">", "<=", ">=")[rng.randrange(4)]
    shape = i % 3
    if shape == 0:
        body = tpl(
            "int main(void){int a[@@N@@]={0},b[@@M@@]={0};"
            "return &a[0] @@OP@@ &b[@@K@@];}", N=n, M=m, K=k, OP=op)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int*p=malloc(@@N@@*sizeof(int));"
            "int*q=malloc(@@M@@*sizeof(int));if(!p||!q)return 2;"
            "int r=(p @@OP@@ q);free(p);free(q);return r;}", N=n, M=m, OP=op)
    else:
        body = tpl(
            "int main(void){int a[@@N@@]={0},b[@@M@@]={0};"
            "int A[@@N@@];return (a+@@N@@) @@OP@@ (b+@@M@@);}", N=n, M=m, OP=op)
    return f"cross_rel_{i}", body


def gen_one_past_deref(i, rng):
    n = rng.randint(4, 12)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@;return *p;}", N=n)
    elif shape == 1:
        body = tpl("int main(void){int a[@@N@@]={0};return a[@@N@@];}", N=n)
    elif shape == 2:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@;"
                   "p[0]=@@V@@;return p[0];}", N=n, V=v)
    else:
        body = STD + tpl("int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                         "int*e=p+@@N@@;*e=@@V@@;free(p);return 0;}", N=n, V=v)
    return f"one_past_deref_{i}", body


def gen_null_arith(i, rng):
    k = rng.randint(1, 8)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int*p=0;int*q=p+@@K@@;(void)q;return 0;}", K=k)
    elif shape == 1:
        body = tpl("int main(void){int*p=0;return *(p+@@K@@);}", K=k)
    elif shape == 2:
        body = tpl("int main(void){char*p=0;p+=@@K@@;(void)p;return 0;}", K=k)
    else:
        body = tpl("int main(void){int*p=0;p-=@@K@@;*p=@@V@@;return 0;}", K=k, V=v)
    return f"null_arith_{i}", body


def gen_forged_int(i, rng):
    a = 0x1000 + rng.randint(0, 0x7FFF) * 0x40
    k = rng.randint(1, 7)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int*p=(int*)@@A@@;return *p;}", A=a)
    elif shape == 1:
        body = tpl("int main(void){int*p=(int*)(@@A@@+@@K@@);*p=@@V@@;return 0;}",
                   A=a, K=k, V=v)
    elif shape == 2:
        body = tpl("int main(void){char*p=(char*)@@A@@;p[@@K@@]=@@V@@;return 0;}",
                   A=a, K=k, V=v)
    else:
        body = tpl("int main(void){long*p=(long*)@@A@@;return (int)*p;}", A=a)
    return f"forged_int_{i}", body


def gen_uintptr_bump(i, rng):
    k = rng.randint(1, 16)
    v = rng.randint(1, 64)
    a = 0x2000 + rng.randint(0, 0x7FFF) * 0x40
    shape = i % 3
    if shape == 0:
        body = SZ + tpl("int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x;u+=@@K@@;"
                        "int*q=(int*)u;return *q;}", V=v, K=k)
    elif shape == 1:
        body = SZ + tpl("int main(void){uintptr_t u=@@A@@;int*p=(int*)u;*p=@@V@@;return 0;}",
                        A=a, V=v)
    else:
        body = SZ + tpl("int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x;u-=@@K@@;"
                        "int*q=(int*)u;return *q;}", V=v, K=k)
    return f"uintptr_bump_{i}", body


def gen_tagged_deref(i, rng):
    v = rng.randint(1, 64)
    tag = (i % 3) + 1  # low-bit tag; odd tags misalign the pointer
    shape = i % 3
    if shape == 0:
        body = SZH + tpl("int main(void){int*p=malloc(sizeof(int));if(!p)return 2;"
                         "int*t=(int*)((uintptr_t)p|@@TAG@@);return *t;}", TAG=tag)
    elif shape == 1:
        body = SZH + tpl("int main(void){int*p=malloc(sizeof(int));if(!p)return 2;"
                         "int*t=(int*)((uintptr_t)p|@@TAG@@);free(t);return 0;}", TAG=tag)
    else:
        body = SZ + tpl("int main(void){int x=@@V@@;int*p=&x;"
                        "int*t=(int*)((uintptr_t)p|@@TAG@@);*t=@@V2@@;return *t;}",
                        V=v, V2=v + 1, TAG=tag)
    return f"tagged_deref_{i}", body


def gen_misaligned(i, rng):
    m = (i % 3) + 1  # 1,2,3 -> misaligned for a 4-byte int / 8-byte double
    n = rng.randint(32, 96)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){char b[@@N@@]={0};int*p=(int*)(b+@@M@@);return *p;}",
                   N=n, M=m)
    elif shape == 1:
        body = tpl("int main(void){char b[@@N@@]={0};int*p=(int*)(b+@@M@@);*p=@@V@@;return 0;}",
                   N=n, M=m, V=v)
    elif shape == 2:
        body = SZH + tpl("int main(void){char*p=malloc(@@N@@);if(!p)return 2;"
                         "double*q=(double*)(p+@@M@@);*q=1.0;free(p);return 0;}", N=n, M=m)
    else:
        body = tpl("union U{char c[8];long l;};\n"
                   "int main(void){union U u;long*p=(long*)(u.c+@@M@@);return (int)*p;}", M=m)
    return f"misaligned_{i}", body


def gen_void_arith(i, rng):
    k = rng.randint(1, 12)
    n = rng.randint(32, 96)
    shape = i % 3
    if shape == 0:
        body = tpl("int main(void){char b[@@N@@]={0};void*p=b;void*q=p+@@K@@;(void)q;return 0;}",
                   N=n, K=k)
    elif shape == 1:
        body = tpl("int main(void){char b[@@N@@]={0};void*p=b;p+=@@K@@;(void)p;return 0;}",
                   N=n, K=k)
    else:
        body = tpl("int main(void){char b[@@N@@]={0};void*p=b;void*q=p++;(void)q;(void)p;return 0;}",
                   N=n)
    return f"void_arith_{i}", body


def gen_container_null(i, rng):
    n = rng.randint(4, 16)
    k = rng.randrange(n)
    v = rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = OFF + tpl("struct S@@I@@{int a;int b[@@N@@];};\n"
                         "int main(void){struct S@@I@@*s=(struct S@@I@@*)((char*)0-offsetof(struct S@@I@@,b));"
                         "return s->a;}", I=i, N=n)
    elif shape == 1:
        body = OFF + tpl("struct S@@I@@{int a;int b[@@N@@];};\n"
                         "int main(void){struct S@@I@@*s=(struct S@@I@@*)((char*)0-offsetof(struct S@@I@@,b));"
                         "s->b[@@K@@]=@@V@@;return 0;}", I=i, N=n, K=k, V=v)
    else:
        body = OFF + tpl("struct S@@I@@{int a;int b[@@N@@];};\n"
                         "int main(void){struct S@@I@@*s=(struct S@@I@@*)((char*)0-offsetof(struct S@@I@@,a));"
                         "return s->b[@@K@@];}", I=i, N=n, K=k)
    return f"container_null_{i}", body


def gen_restrict_overlap(i, rng):
    """Two restrict-qualified parameters that alias the same object (UB)."""
    n = rng.randint(4, 12)
    j = rng.randint(1, n - 1)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl(
            "static void f@@I@@(int *restrict a,int *restrict b){*a=@@V@@;*b=@@V2@@;}\n"
            "int main(void){int x=@@V@@;f@@I@@(&x,&x);return x;}",
            I=i, V=v, V2=v + 1)
    elif shape == 1:
        body = tpl(
            "static void f@@I@@(int *restrict a,int *restrict b,int n){"
            "for(int k=0;k<n;k++){a[k]=k;b[k]=k+1;}}\n"
            "int main(void){int x[@@N@@]={0};f@@I@@(x,x+@@J@@,@@N@@-@@J@@);return x[0];}",
            I=i, N=n, J=j)
    elif shape == 2:
        body = tpl(
            "static int g@@I@@;\n"
            "static int f@@I@@(int *restrict p){g@@I@@=@@V2@@;return *p;}\n"
            "int main(void){g@@I@@=@@V@@;return f@@I@@(&g@@I@@);}",
            I=i, V=v, V2=v + 1)
    else:
        body = tpl(
            "static void f@@I@@(double *restrict a,double *restrict b){a[0]=1.0;b[0]=2.0;}\n"
            "int main(void){double x[2]={0,0};f@@I@@(&x[0],&x[0]);return (int)x[0];}",
            I=i)
    return f"restrict_overlap_{i}", body


def gen_funcptr_cast(i, rng):
    """Call through a function pointer whose type is incompatible with the target."""
    v = rng.randint(1, 64)
    n = rng.randint(1, 9)
    shape = i % 4
    if shape == 0:
        body = tpl("typedef void(*Fv)(void);\nint g@@I@@(int x){return x+@@V@@;}\n"
                   "int main(void){Fv f=(Fv)g@@I@@;f();return 0;}", I=i, V=v)
    elif shape == 1:
        body = tpl("typedef int(*Fi)(int);\nint g@@I@@(void){return @@V@@;}\n"
                   "int main(void){Fi f=(Fi)g@@I@@;return f(@@N@@);}", I=i, V=v, N=n)
    elif shape == 2:
        body = tpl("typedef long(*Fl)(long);\ndouble g@@I@@(double x){return x+@@V@@;}\n"
                   "int main(void){Fl f=(Fl)g@@I@@;return (int)f(@@N@@);}", I=i, V=v, N=n)
    else:
        body = tpl("typedef int(*Fi)(int,int);\nint g@@I@@(int x){return x+@@V@@;}\n"
                   "int main(void){Fi f=(Fi)g@@I@@;return f(@@V@@,@@N@@);}", I=i, V=v, N=n)
    return f"funcptr_cast_{i}", body


# ---------------------------------------------------------------------------
# Conditional / implementation-defined pointer rules (declared "strict").
# ---------------------------------------------------------------------------
def gen_uintptr_rt(i, rng):
    v = rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = SZ + tpl("int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x;"
                        "int*q=(int*)u;return *q;}", V=v)
    elif shape == 1:
        body = SZ + tpl("int main(void){int x=@@V@@;int*p=&x;uintptr_t u=(uintptr_t)p;"
                        "void*vp=(void*)u;int*q=(int*)vp;return *q;}", V=v)
    else:
        body = SZ + tpl("int main(void){char c=@@C@@;uintptr_t u=(uintptr_t)&c;"
                        "char*q=(char*)u;return *q;}", C=rng.randint(1, 127))
    return f"uintptr_rt_{i}", body


def gen_tagged_mask(i, rng):
    v = rng.randint(1, 64)
    tag = (i % 3) + 1
    shape = i % 2
    if shape == 0:
        body = SZ + tpl("int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x|@@TAG@@;"
                        "int*t=(int*)(u&~(uintptr_t)@@TAG@@);return *t;}", V=v, TAG=tag)
    else:
        body = SZH + tpl("int main(void){int*p=malloc(sizeof(int));if(!p)return 2;*p=@@V@@;"
                         "uintptr_t u=(uintptr_t)p|@@TAG@@;"
                         "int*t=(int*)(u&~(uintptr_t)@@TAG@@);return *t;}", V=v, TAG=tag)
    return f"tagged_mask_{i}", body


def gen_type_pun(i, rng):
    v = rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = tpl("int main(void){int x=@@V@@;float*f=(float*)&x;return (int)*f;}", V=v)
    elif shape == 1:
        body = tpl("int main(void){float x=1.0f;int*p=(int*)&x;*p=@@V@@;return 0;}", V=v)
    else:
        body = tpl("int main(void){long x=@@V@@;double*p=(double*)&x;return (int)*p;}", V=v)
    return f"type_pun_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": must pass both modes).
# ---------------------------------------------------------------------------
def gen_ok_same_array(i, rng):
    n = rng.randint(8, 24)
    a = rng.randint(1, n - 2)
    b = rng.randint(1, n - 2)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int x[@@N@@]={0};x[@@A@@]=@@V@@;"
                   "return &x[@@A@@]-&x[@@B@@];}", N=n, A=a, B=b, V=v)
    elif shape == 1:
        body = tpl("int main(void){int x[@@N@@]={0};"
                   "return &x[@@A@@]>&x[@@B@@];}", N=n, A=a, B=b)
    elif shape == 2:
        body = tpl("int main(void){int x[@@N@@]={0};int*p=x;int*q=p+@@A@@;"
                   "q[0]=@@V@@;return q[0];}", N=n, A=a, V=v)
    else:
        body = tpl("int main(void){int x[@@N@@]={0};int*p=x;int*e=x+@@N@@;"
                   "return p<e;}", N=n)
    return f"ok_same_array_{i}", body


def gen_ok_cast(i, rng):
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int x=@@V@@;void*p=&x;int*q=(int*)p;return *q;}", V=v)
    elif shape == 1:
        body = tpl("int main(void){int x=@@V@@;char*p=(char*)&x;return p[0];}", V=v)
    elif shape == 2:
        body = tpl("union U{int i;float f;};\n"
                   "int main(void){union U u;u.i=@@V@@;return (int)u.f;}", V=v)
    else:
        body = tpl("int main(void){unsigned char b[sizeof(int)]={0};int x=@@V@@;"
                   "unsigned char*p=(unsigned char*)&x;"
                   "for(unsigned j=0;j<sizeof(int);j++)b[j]=p[j];return b[0];}", V=v)
    return f"ok_cast_{i}", body


def gen_ok_one_past(i, rng):
    n = rng.randint(5, 16)
    v = rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@;(void)p;return 0;}", N=n)
    elif shape == 1:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@;p--;*p=@@V@@;return *p;}",
                   N=n, V=v)
    else:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a;int*e=a+@@N@@;"
                   "while(p<e){*p=@@V@@;p++;}return a[@@LAST@@];}",
                   N=n, V=v, LAST=n - 1)
    return f"ok_one_past_{i}", body


def gen_ok_restrict(i, rng):
    """Valid control: restrict pointers that point to disjoint objects."""
    n = rng.randint(8, 16)
    v = rng.randint(1, 64)
    shape = i % 2
    if shape == 0:
        body = tpl(
            "static void f@@I@@(int *restrict a,int *restrict b){*a=@@V@@;*b=@@V2@@;}\n"
            "int main(void){int x=@@V@@,y=@@V2@@;f@@I@@(&x,&y);return x+y;}",
            I=i, V=v, V2=v + 1)
    else:
        body = tpl(
            "static void f@@I@@(int *restrict a,int *restrict b,int n){"
            "for(int k=0;k<n;k++){a[k]=k;b[k]=k+1;}}\n"
            "int main(void){int x[@@N@@]={0},y[@@N@@]={0};f@@I@@(x,y,@@N@@);return x[0]+y[0];}",
            I=i, N=n)
    return f"ok_restrict_{i}", body


def gen_ok_funcptr(i, rng):
    """Valid control: call through a compatible function pointer."""
    v = rng.randint(1, 64)
    n = rng.randint(1, 9)
    shape = i % 3
    if shape == 0:
        body = tpl("typedef int(*Fi)(int);\nint g@@I@@(int x){return x+@@V@@;}\n"
                   "int main(void){Fi f=g@@I@@;return f(@@N@@);}", I=i, V=v, N=n)
    elif shape == 1:
        body = tpl("typedef void(*Fv)(int*);\nvoid g@@I@@(int*p){*p=@@V@@;}\n"
                   "int main(void){Fv f=g@@I@@;int x=0;f(&x);return x;}", I=i, V=v)
    else:
        body = tpl("typedef long(*Fl)(long,long);\nlong g@@I@@(long a,long b){return a+b+@@V@@;}\n"
                   "int main(void){Fl f=g@@I@@;return (int)f(@@N@@,@@V@@);}", I=i, V=v, N=n)
    return f"ok_funcptr_{i}", body


# family, number of cases, builder, declared outcome.  Defined after the
# builders so each name is bound when the table is constructed.
FAMILIES = (
    ("cross_sub",         9, gen_cross_sub,        "reject"),
    ("cross_rel",         9, gen_cross_rel,        "reject"),
    ("one_past_deref",    9, gen_one_past_deref,   "reject"),
    ("null_arith",        9, gen_null_arith,       "reject"),
    ("forged_int",        9, gen_forged_int,       "reject"),
    ("uintptr_bump",      8, gen_uintptr_bump,     "reject"),
    ("tagged_deref",      8, gen_tagged_deref,     "reject"),
    ("misaligned",        8, gen_misaligned,       "reject"),
    ("void_arith",        7, gen_void_arith,       "reject"),
    ("container_null",    7, gen_container_null,   "reject"),
    ("restrict_overlap",  8, gen_restrict_overlap, "reject"),
    ("funcptr_cast",      8, gen_funcptr_cast,     "reject"),
    ("uintptr_rt",        7, gen_uintptr_rt,       "strict"),
    ("tagged_mask",       6, gen_tagged_mask,      "strict"),
    ("type_pun",          6, gen_type_pun,         "strict"),
    ("ok_same_array",     9, gen_ok_same_array,    "accept"),
    ("ok_cast",           8, gen_ok_cast,          "accept"),
    ("ok_one_past",       8, gen_ok_one_past,      "accept"),
    ("ok_restrict",       6, gen_ok_restrict,      "accept"),
    ("ok_funcptr",        6, gen_ok_funcptr,       "accept"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for family, count, builder, expect in FAMILIES:
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
        if proc.returncode < 0:
            hard.append(f"{mode}: CRASH rc={proc.returncode}")
        modes[mode] = (proc.returncode != 0,
                       (proc.stderr + proc.stdout).strip())

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
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    tags = {
        "--default": diag_tags(modes["--default"][1]),
        "--strict": diag_tags(modes["--strict"][1]),
    }
    result = {
        "name": name, "family": family, "expect": expect,
        "reasons": reasons, "d_rej": d_rej, "s_rej": s_rej,
        "tags": tags, "modes": modes, "body": body,
    }
    if reasons:
        (FAILS / f"{name}.c").write_text(body)
        log = [
            f"case: {name}",
            f"family: {family}",
            f"expect: {expect}",
            f"reasons: {'; '.join(reasons)}",
            f"--default rejected={d_rej}: {first_diag(modes['--default'][1])}",
            f"--strict rejected={s_rej}: {first_diag(modes['--strict'][1])}",
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
    return result


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

    print(f"hw_ptr: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:18} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hw_ptr_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    mismatches = [r for r in results if r["reasons"]]
    mismatches.sort(key=lambda r: r["name"])
    false_positives = [r for r in mismatches if r["expect"] == "accept"]
    misses = [r for r in mismatches if r["expect"] in ("reject", "strict")]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"({len(misses)} misses / unsafe-accepted, "
          f"{len(false_positives)} false positives)")
    print("mismatches by expectation: " + " ".join(
        "%s=%d" % (k, sum(1 for r in mismatches if r["expect"] == k))
        for k in ("reject", "strict", "accept")))
    print("false positives (safe rejected): "
          + ("none" if not false_positives else
             ", ".join(f"{r['family']}/{r['name']}" for r in false_positives)))

    by_family = collections.Counter(r["family"] for r in mismatches)
    print("mismatches by family: "
          + ("none" if not by_family else
             " ".join(f"{k}={by_family[k]}" for k in sorted(by_family))))

    print("first 10 mismatch names: "
          + ("none" if not mismatches else
             ", ".join(r["name"] for r in mismatches[:10])))
    for r in mismatches[:10]:
        print(f"  {r['family']}/{r['name']} [expect={r['expect']}] "
              f"{'; '.join(r['reasons'])} | default tags={r['tags']['--default']} "
              f"strict tags={r['tags']['--strict']}")
    if len(mismatches) > 10:
        print(f"  ... {len(mismatches) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")

    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches, "
          f"{len(false_positives)} false positives)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
