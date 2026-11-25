#!/usr/bin/env python3
"""Brutal, deterministic memory-bug fuzzing for the C+ SAFE subset.

Generates hundreds of memory-bug and memory-safe programs across eight
categories, then checks every program with ``c+ check`` in BOTH modes:

  * ``--default``  - proven errors must be rejected; unverified ops may warn.
  * ``--strict``   - unverified operations must be rejected (no downgrade).

Every generated case declares an expected outcome:

  reject - must fail DEFAULT (a proven memory error)
  strict - may pass DEFAULT, but must fail STRICT (unverified operation)
  accept - valid C, must pass BOTH (demonstrably safe / outside tracked subset)

A mismatch means either a safety hole (unsafe program accepted) or a false
positive (safe program rejected). Mismatches are printed and saved as
regression fixtures under tests/out/core/brutal/failures/.

The generator is seeded (FUZZ_SEED) and runs in a thread pool (FUZZ_WORKERS,
default 8). Only standard C is generated, except in the "ownership" category
where the C+ resource/move extensions are deliberately exercised and the
source includes "cplus/cplus.h". This is evidence for the declared subset,
not a proof of arbitrary-C safety.
"""
import collections
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/brutal"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xF00DBAD"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))
N_VALUES = range(2, 10)  # eight parameter values per template

CATEGORIES = ("uaf", "double_free", "invalid_free", "overflow",
              "null_uninit", "provenance", "ownership", "threads")

CASES = []  # (name, body, expect, category)


def add(name, body, expect, category):
    """Register one generated case."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    assert category in CATEGORIES, (name, category)
    CASES.append((name, body.strip() + "\n", expect, category))


# ---------------------------------------------------------------------------
# 1. use-after-free: aliases, chains, subscripts, members, casts, returns,
#    realloc success/failure branches.
# ---------------------------------------------------------------------------
def build_uaf(rng):
    c = "uaf"
    for n in N_VALUES:
        add(f"uaf_direct_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p); return *p; }}""",
            "reject", c)
        add(f"uaf_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; int *q=p; free(p); return q[0]; }}""",
            "reject", c)
        add(f"uaf_chain_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; int *a=p,*b=a,*d=b; free(p); return *d; }}""",
            "reject", c)
        add(f"uaf_subscript_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p); return p[{n-1}]; }}""",
            "reject", c)
        add(f"uaf_member_{n}", f"""#include <stdlib.h>
struct S{{ int x; }};
int main(void){{ struct S *s=malloc(sizeof *s); if(!s)return 2; free(s); return s->x; }}""",
            "reject", c)
        add(f"uaf_field_ptr_{n}", f"""#include <stdlib.h>
struct S{{ int x[{n}]; }};
int main(void){{ struct S *s=malloc(sizeof *s); if(!s)return 2; int *f=&s->x[0]; free(s); return *f; }}""",
            "reject", c)
        add(f"uaf_interproc_return_{n}", f"""#include <stdlib.h>
int *make_freed(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 0; free(p); return p; }}
int main(void){{ int *q=make_freed(); if(!q)return 1; return *q; }}""",
            "reject", c)
        add(f"uaf_interproc_callee_{n}", f"""#include <stdlib.h>
void release(int *p){{ free(p); }}
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; release(p); return *p; }}""",
            "reject", c)
        add(f"uaf_realloc_ok_branch_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; int *q=p; void *t=realloc(p,{2*n}); if(t){{ p=t; return *q; }} free(p); return 0; }}""",
            "reject", c)
        add(f"uaf_realloc_fail_branch_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; void *t=realloc(p,{n}*100000); (void)t; free(p); return *p; }}""",
            "reject", c)
        add(f"uaf_branch_{n}", f"""#include <stdlib.h>
int main(int argc,char **argv){{ (void)argv; int *p=malloc({n}*sizeof(int)); if(!p)return 2; if(argc>1) free(p); return *p; }}""",
            "strict", c)
        add(f"uaf_loop_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; for(int i=0;i<{n};i++){{ if(i==1) free(p); }} return *p; }}""",
            "strict", c)

    for i in range(24):
        k = rng.randint(1, 4)
        n = rng.randint(max(4, k + 1), 24)
        idx = rng.randint(0, k - 1)
        aliases = " ".join(f"int *a{j}=p;" for j in range(k))
        if rng.random() < 0.5:
            add(f"mut_uaf_bad_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; {aliases} free(p); return a{idx}[{idx}]; }}""",
                "reject", c)
        else:
            writes = " ".join(f"a{j}[{j}]={j+1};" for j in range(k))
            add(f"mut_uaf_ok_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; {aliases} {writes} int v=a{idx}[{idx}]; free(p); return v=={idx+1}?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 2. double free: aliases, branches, loops, interprocedural calls, realloc.
# ---------------------------------------------------------------------------
def build_double_free(rng):
    c = "double_free"
    for n in N_VALUES:
        add(f"df_same_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; free(p); free(p); return 0; }}""",
            "reject", c)
        add(f"df_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; int *q=p; free(p); free(q); return 0; }}""",
            "reject", c)
        add(f"df_branch_{n}", f"""#include <stdlib.h>
int main(int argc,char **argv){{ (void)argv; int *p=malloc({n}); if(!p)return 2; if(argc>1) free(p); free(p); return 0; }}""",
            "reject", c)
        add(f"df_interproc_{n}", f"""#include <stdlib.h>
void rel(int *p){{ free(p); }}
int main(void){{ int *p=malloc({n}); if(!p)return 2; rel(p); rel(p); return 0; }}""",
            "reject", c)
        add(f"df_realloc_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; void *t=realloc(p,{2*n}); (void)t; free(p); free(p); return 0; }}""",
            "reject", c)
        add(f"df_null_reset_ok_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; free(p); p=0; free(p); return 0; }}""",
            "accept", c)

    for i in range(32):
        k = rng.randint(1, 4)
        n = rng.randint(4, 32)
        aliases = " ".join(f"int *a{j}=p;" for j in range(k))
        j = rng.randint(0, k - 1)
        if rng.random() < 0.65:
            add(f"mut_df_bad_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; {aliases} free(p); free(a{j}); return 0; }}""",
                "reject", c)
        else:
            resets = " ".join(["p=0;"] + [f"a{j2}=0;" for j2 in range(k)])
            add(f"mut_df_ok_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; {aliases} free(p); {resets} free(p); return 0; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 3. invalid frees: stack, global, string literal, array, interior pointer,
#    struct field, cast pointer.
# ---------------------------------------------------------------------------
def build_invalid_free(rng):
    c = "invalid_free"
    for n in N_VALUES:
        add(f"free_stack_{n}", f"""#include <stdlib.h>
int main(void){{ int local[{n}]={{0}}; int *p=&local[0]; free(p); return 0; }}""",
            "reject", c)
        add(f"free_global_{n}", f"""#include <stdlib.h>
static int g[{n}];
int main(void){{ free(&g[0]); return 0; }}""",
            "reject", c)
        add(f"free_string_{n}", """#include <stdlib.h>
int main(void){ const char *s="abcdefghijklmnopqrstuvwxyz"; free((void*)s); return 0; }""",
            "reject", c)
        add(f"free_array_{n}", f"""#include <stdlib.h>
int main(void){{ char a[{n}]={{0}}; free(a); return 0; }}""",
            "reject", c)
        add(f"free_interior_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p+1); return 0; }}""",
            "reject", c)
        add(f"free_field_{n}", f"""#include <stdlib.h>
struct S{{ int x[{n}]; }};
int main(void){{ struct S *s=malloc(sizeof *s); if(!s)return 2; free(&s->x[0]); return 0; }}""",
            "reject", c)
        add(f"free_cast_{n}", f"""#include <stdlib.h>
int main(void){{ int x[{n}]={{0}}; void *p=(void*)&x[0]; free(p); return 0; }}""",
            "reject", c)

    for i in range(24):
        n = rng.randint(4, 24)
        off = rng.randint(1, 4)
        if rng.random() < 0.25:
            add(f"mut_free_ok_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p+0); free(0); return 0; }}""",
                "accept", c)
        else:
            add(f"mut_free_bad_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p+{off}); return 0; }}""",
                "reject", c)


# ---------------------------------------------------------------------------
# 4. buffer overflows: constant/loop/multidim/variable indices plus the libc
#    copy family with literal and computed sizes.
# ---------------------------------------------------------------------------
def build_overflow(rng):
    c = "overflow"
    alpha = "abcdefghijklmnopqrstuvwxyz0123456789"
    for n in N_VALUES:
        add(f"oob_const_{n}",
            f"int main(void){{ int a[{n}]={{0}}; return a[{n+1}]; }}", "reject", c)
        add(f"oob_loop_{n}",
            f"int main(void){{ int a[{n}]={{0}}; for(int i=0;i<{n+2};i++) a[i]=i; return 0; }}",
            "reject", c)
        add(f"oob_multidim_{n}",
            f"int main(void){{ int g[2][{n}]={{0}}; return g[1][{n+1}]; }}", "reject", c)
        add(f"oob_var_index_{n}",
            f"int main(void){{ int a[{n}]={{0}}; int i={n}+1; return a[i]; }}", "reject", c)
        add(f"oob_negative_{n}",
            f"int main(void){{ int a[{n}]={{0}}; return a[-1]; }}", "reject", c)
        add(f"oob_ptr_arith_{n}",
            f"int main(void){{ int a[{n}]={{0}}; int *p=a+{n+2}; return *p; }}", "reject", c)
        src = alpha[:n + 2]
        add(f"strcpy_oob_{n}",
            f'#include <string.h>\nint main(void){{ char d[{n}]; strcpy(d,"{src}"); return 0; }}',
            "reject", c)
        cnt = n + 2
        add(f"memcpy_oob_lit_{n}", f"""#include <string.h>
int main(void){{ char d[{n}]; char s[{max(16, cnt)}]={{0}}; memcpy(d,s,{cnt}); return 0; }}""",
            "reject", c)
        add(f"memcpy_oob_computed_{n}", f"""#include <string.h>
int main(void){{ char d[{n}]; char s[{max(16, cnt)}]={{0}}; int cnt={cnt}; memcpy(d,s,cnt); return 0; }}""",
            "reject", c)
        add(f"memmove_oob_{n}", f"""#include <string.h>
int main(void){{ char d[{n}]={{0}}; char s[{max(16, cnt)}]={{0}}; int cnt={cnt}; memmove(d,s,cnt); return 0; }}""",
            "reject", c)
        add(f"memset_oob_{n}", f"""#include <string.h>
int main(void){{ char d[{n}]; memset(d,0,{cnt}); return 0; }}""",
            "reject", c)
        add(f"strcat_oob_{n}",
            f'#include <string.h>\nint main(void){{ char d[{n}]="a"; strcat(d,"{alpha}"); return 0; }}',
            "reject", c)
        add(f"strncpy_oob_{n}",
            f'#include <string.h>\nint main(void){{ char d[{n}]; strncpy(d,"{alpha}",{cnt}); return 0; }}',
            "reject", c)
        add(f"snprintf_oob_{n}",
            f'#include <stdio.h>\nint main(void){{ char d[{n}]; snprintf(d,{cnt},"%s","abcdefgh"); return 0; }}',
            "reject", c)

    for i in range(16):
        n = rng.randint(8, 24)
        if i % 2 == 0:
            add(f"mut_ovf_bad_{i}", f"""#include <string.h>
int main(void){{ char d[{n}]; char s[{n+8}]={{0}}; int cnt={n+rng.randint(1,4)}; memcpy(d,s,cnt); return 0; }}""",
                "reject", c)
        else:
            add(f"mut_ovf_ok_{i}", f"""#include <string.h>
int main(void){{ char d[{n}]={{0}}; char s[4]={{'a','b','c',0}}; int cnt=4; memcpy(d,s,cnt); return d[0]=='a'?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 5. null / uninitialized: null deref, null subscript, null libc args,
#    uninitialized branches.
# ---------------------------------------------------------------------------
def build_null_uninit(rng):
    c = "null_uninit"
    for n in N_VALUES:
        add(f"null_deref_{n}",
            f"int main(void){{ int *p=0; for(int i=0;i<{n};i++) *p=i; return 0; }}", "reject", c)
        add(f"null_subscript_{n}", "int main(void){ int *p=0; return p[0]; }", "reject", c)
        add(f"null_member_{n}", f"""#include <stdlib.h>
struct S{{ int x[{n}]; }};
int main(void){{ struct S *s=0; return s->x[0]; }}""",
            "reject", c)
        add(f"null_libc_{n}", """#include <string.h>
int main(void){ char d[8]; char *s=0; memcpy(d,s,4); return 0; }""",
            "reject", c)
        add(f"null_fread_{n}", """#include <stdio.h>
int main(void){ FILE *f=0; char d[8]; fread(d,1,4,f); return 0; }""",
            "reject", c)
        add(f"uninit_var_{n}", "int main(void){ int x; return x; }", "reject", c)
        add(f"uninit_branch_{n}",
            f"int main(int argc,char **argv){{ (void)argv; int x; if(argc>{n}) x=3; return x; }}",
            "reject", c)
        add(f"uninit_ptr_{n}", "int main(void){ int *p; *p=1; return 0; }", "reject", c)

    for i in range(16):
        n = rng.randint(2, 20)
        if rng.random() < 0.5:
            add(f"mut_nu_bad_{i}",
                f"int main(int argc,char **argv){{ (void)argv; int x; if(argc>{n}) x=3; return x; }}",
                "reject", c)
        else:
            add(f"mut_nu_ok_{i}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; *p=3; int v=*p; free(p); return v==3?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 6. provenance: integer-forged pointers and casts through uintptr_t.
# ---------------------------------------------------------------------------
def build_provenance(rng):
    c = "provenance"
    for n in N_VALUES:
        add(f"int_to_ptr_{n}",
            f"int main(void){{ int *p=(int*){0x1000+n}; *p=1; return 0; }}", "strict", c)
        add(f"uintptr_to_ptr_{n}", f"""#include <stdint.h>
int main(void){{ uintptr_t x={0x2000+n}; int *p=(int*)x; *p=1; return 0; }}""",
            "strict", c)
        add(f"int_cast_arith_{n}",
            f"int main(void){{ int *p=(int*)(0x3000+{n}); *p=1; return 0; }}", "strict", c)
        add(f"forged_no_deref_{n}", f"""#include <stdint.h>
int main(void){{ uintptr_t x={0x4000+n}; int *p=(int*)x; (void)p; return 0; }}""",
            "strict", c)

    for i in range(24):
        addr = rng.randint(0x1000, 0x8000)
        if rng.random() < 0.7:
            add(f"mut_prov_deref_{i}", f"""#include <stdint.h>
int main(void){{ uintptr_t x={addr}; int *p=(int*)x; *p=1; return *p; }}""",
                "strict", c)
        else:
            add(f"mut_prov_noderef_{i}", f"""#include <stdint.h>
int main(void){{ uintptr_t x={addr}; int *p=(int*)x; (void)p; return 0; }}""",
                "strict", c)


# ---------------------------------------------------------------------------
# 7. ownership: moves, resource handles, escapes, destructor-invalidated
#    borrows.  These deliberately use the C+ extension syntax.
# ---------------------------------------------------------------------------
def build_ownership(rng):
    c = "ownership"
    for n in N_VALUES:
        add(f"resource_copy_{n}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ if(r->v[0]) r->v[0]=0; }}
int main(void){{ R a={{{{1}}}}; R b=a; (void)b; return 0; }}""",
            "reject", c)
        add(f"resource_move_{n}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ if(r->v[0]) r->v[0]=0; }}
int main(void){{ R a={{{{1}}}}; R b=cplus_move(a); (void)b; return 0; }}""",
            "accept", c)
        add(f"managed_escape_{n}", f"""#include "cplus/cplus.h"
int *bad(void){{ managed int *p=cplus_new_array(int,{n}); return p; }}
int main(void){{ (void)bad(); return 0; }}""",
            "reject", c)
        add(f"borrow_after_move_{n}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ (void)r; }}
int main(void){{ R a={{{{1}}}}; int *b=&a.v[0]; R c=cplus_move(a); return *b; }}""",
            "reject", c)
        add(f"use_after_move_{n}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ (void)r; }}
int main(void){{ R a={{{{1}}}}; R b=cplus_move(a); return a.v[0]; }}""",
            "strict", c)
        add(f"countof_pointer_{n}", f"""#include "cplus/cplus.h"
size_t f(int *p){{ return countof(p)+{n}; }}
int main(void){{ return 0; }}""",
            "reject", c)
        add(f"release_stack_{n}", f"""#include "cplus/cplus.h"
int main(void){{ int x[{n}]={{0}}; cplus_release(&x[0]); return 0; }}""",
            "reject", c)
        add(f"raii_early_return_{n}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ (void)r; }}
int main(void){{ R a={{{{1}}}}; if(a.v[0]) return 0; return 1; }}""",
            "accept", c)

    for i in range(16):
        n = rng.randint(2, 16)
        if rng.random() < 0.5:
            add(f"mut_own_bad_{i}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ (void)r; }}
int main(void){{ R a={{{{1}}}}; R b=a; (void)b; return 0; }}""",
                "reject", c)
        else:
            add(f"mut_own_ok_{i}", f"""#include "cplus/cplus.h"
resource struct R{{ int v[{n}]; }};
void R_drop(R *r){{ (void)r; }}
int main(void){{ R a={{{{1}}}}; R b=cplus_move(a); (void)b; return 0; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 8. threads: shared globals across multiple pthread_create, stack borrows.
# ---------------------------------------------------------------------------
def build_threads(rng):
    c = "threads"
    for n in N_VALUES:
        add(f"thread_shared_global_{n}", f"""#include <pthread.h>
static int g;
static void *worker(void *arg){{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void){{ pthread_t a,b; pthread_create(&a,0,worker,0); pthread_create(&b,0,worker,0); pthread_join(a,0); pthread_join(b,0); return g; }}""",
            "reject", c)
        add(f"thread_shared_volatile_{n}", f"""#include <pthread.h>
static volatile int g;
static void *worker(void *arg){{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void){{ pthread_t a,b; pthread_create(&a,0,worker,0); pthread_create(&b,0,worker,0); pthread_join(a,0); pthread_join(b,0); return 0; }}""",
            "strict", c)
        add(f"thread_stack_borrow_{n}", f"""#include <pthread.h>
static void *worker(void *arg){{ int *p=(int*)arg; for(int i=0;i<{n};i++) *p=*p+1; return 0; }}
int main(void){{ int local=7; pthread_t t; pthread_create(&t,0,worker,&local); pthread_join(t,0); return local=={7+n}?0:1; }}""",
            "strict", c)
        add(f"thread_double_free_share_{n}", f"""#include <pthread.h>
#include <stdlib.h>
static int *g;
static void *worker(void *arg){{ (void)arg; free(g); g=0; return 0; }}
int main(void){{ g=malloc({n}*sizeof(int)); if(!g)return 2; pthread_t a,b; pthread_create(&a,0,worker,0); pthread_create(&b,0,worker,0); pthread_join(a,0); pthread_join(b,0); return 0; }}""",
            "reject", c)
        add(f"thread_single_ok_{n}", f"""#include <pthread.h>
static volatile int g;
static void *worker(void *arg){{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void){{ pthread_t t; pthread_create(&t,0,worker,0); pthread_join(t,0); return 0; }}""",
            "accept", c)
        add(f"thread_borrow_joined_{n}", f"""#include <pthread.h>
static void *worker(void *arg){{ int *p=(int*)arg; for(int i=0;i<{n};i++) p[i]=i; return 0; }}
int main(void){{ int buf[{n}]={{0}}; pthread_t t; pthread_create(&t,0,worker,buf); pthread_join(t,0); return buf[0]; }}""",
            "strict", c)

    for i in range(16):
        n = rng.randint(2, 16)
        two = rng.random() < 0.6
        volatile = rng.random() < 0.5
        qty = 2 if two else 1
        creates = " ".join(f"pthread_create(&t[{k}],0,worker,0);" for k in range(qty))
        joins = " ".join(f"pthread_join(t[{k}],0);" for k in range(qty))
        vol = "volatile " if volatile else ""
        expect = ("reject" if not volatile else "strict") if two else "accept"
        add(f"mut_thread_{i}", f"""#include <pthread.h>
static {vol}int g;
static void *worker(void *arg){{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void){{ pthread_t t[{qty}]; {creates} {joins} return g; }}""",
            expect, c)


# ---------------------------------------------------------------------------
# Execution: check every case in DEFAULT and STRICT, classify mismatches.
# ---------------------------------------------------------------------------
def check_case(case):
    name, body, expect, category = case
    path = OUT / f"{name}.c"
    path.write_text(body)
    results = {}
    for mode in ("--default", "--strict"):
        try:
            p = subprocess.run([CC, "check", mode, str(path)],
                               capture_output=True, text=True, timeout=TIMEOUT)
            first = p.stderr.strip().splitlines()[:1]
            results[mode] = (p.returncode != 0, first, p.returncode < 0)
        except subprocess.TimeoutExpired:
            results[mode] = (False, ["TIMEOUT"], False)

    fails = []
    for mode in ("--default", "--strict"):
        _, first, crashed = results[mode]
        if crashed:
            fails.append(f"CRASH[{category}] {name}: compiler signal in {mode} {first}")

    d, s = results["--default"][0], results["--strict"][0]
    if expect == "reject" and not d:
        fails.append(f"HOLE[{category}] {name}: unsafe program accepted in DEFAULT")
    if expect in ("reject", "strict") and not s:
        fails.append(f"HOLE[{category}] {name}: expected rejection in STRICT, accepted")
    if expect == "accept" and d:
        fails.append(f"FALSE-POSITIVE[{category}] {name}: rejected in DEFAULT {results['--default'][1]}")
    if expect == "accept" and s:
        fails.append(f"FALSE-POSITIVE[{category}] {name}: rejected in STRICT {results['--strict'][1]}")

    if fails:
        (FAILS / f"{name}.c").write_text(body)
        (FAILS / f"{name}.expect.txt").write_text(
            f"expect={expect}\ncategory={category}\n" + "\n".join(fails) + "\n")
    return fails


def main():
    rng = random.Random(SEED)
    build_uaf(rng)
    build_double_free(rng)
    build_invalid_free(rng)
    build_overflow(rng)
    build_null_uninit(rng)
    build_provenance(rng)
    build_ownership(rng)
    build_threads(rng)

    counts = collections.Counter(case[3] for case in CASES)
    outcomes = collections.Counter(case[2] for case in CASES)
    print(f"brutal memory fuzz: {len(CASES)} cases, seed={SEED}, workers={WORKERS}")
    for cat in CATEGORIES:
        print(f"  {cat}: {counts[cat]}")
    print(f"  outcomes: reject={outcomes['reject']}, strict={outcomes['strict']}, "
          f"accept={outcomes['accept']}")

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for fails in pool.map(check_case, CASES):
            failures.extend(fails)

    for f in failures[:60]:
        print(f)
    if len(failures) > 60:
        print(f"... and {len(failures) - 60} more")
    if failures:
        print(f"saved fixtures in {FAILS}")
    print(f"FINAL: {'FAIL' if failures else 'PASS'} "
          f"({len(CASES)} cases, {len(failures)} mismatches)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
