#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator for double-free GAP probes (>=100).

Produces distinct, self-contained, valid C translation units that target the
double-free / invalid-release decisions of the checker.  Each case is drawn
from an independent per-case seeded RNG; generation and verification both run
in thread pools and ``pool.map`` preserves input order, so the corpus is
reproducible regardless of ``FUZZ_WORKERS``.  A unique marker comment plus
seeded size, offset, selector and value parameters make every program
byte-distinct.

Families (all parameters drawn from an independent per-case seeded RNG):

  df_loop_after     free inside a for/while/do/nested loop, then free after  (reject)
  df_switch_case    second release reached through a switch case body       (reject/strict)
  df_nested_branch  release inside nested if/else or loop+if branches        (reject/strict)
  df_alias_array    two array slots / struct fields aliasing one allocation  (reject)
  df_cond_paths     conditional double free across merged control paths     (reject/strict)
  df_realloc_release  realloc (or realloc(...,0)) then double release        (reject/strict)
  df_move_release   release of a moved-from source / double release of dst   (reject)
  cp_release_malloc cplus_release applied to a malloc/calloc pointer         (reject)
  safe_reset        reset-to-null controls (free + null + free)              (accept)
  safe_single       ordinary single-release / managed RAII controls          (accept)
  safe_guarded      guarded release (if(p) free(p)) controls                 (accept)

Expected outcomes (same contract as tests/metrics/ag2_free_100.py):

  reject - proven allocator error: must fail BOTH --default and --strict.
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (acceptance in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a FALSE POSITIVE).

Mismatches are printed, counted by class (safety holes vs false positives),
and the generated source plus full diagnostics are saved under
tests/out/core/hw_df/failures/.  This is a differential measurement against
the existing binary, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xDF10
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
OUT = ROOT / "tests/out/core/hw_df"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xDF10"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, case count.  Case index i maps deterministically to one family
# via the cumulative counts below.
FAMILIES = (
    ("df_loop_after", 12),
    ("df_switch_case", 12),
    ("df_nested_branch", 12),
    ("df_alias_array", 12),
    ("df_cond_paths", 12),
    ("df_realloc_release", 12),
    ("df_move_release", 12),
    ("cp_release_malloc", 12),
    ("safe_reset", 10),
    ("safe_single", 8),
    ("safe_guarded", 8),
)
TOTAL = sum(count for _name, count in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
CPLUS = '#include "cplus/cplus.h"\n'
BOTH = CPLUS + STD


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in .format/f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hw_df {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: free inside a loop, then free once more after the loop.
# ---------------------------------------------------------------------------
def gen_df_loop_after(i, rng):
    n = rng.randint(4, 24)
    k = rng.randint(2, 5)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"hw_df_loop_after_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;for(int j=0;j<@@K@@;j++)free(p);free(p);return 0;}",
            N=n, K=k, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int j=@@K@@;while(j-->0)free(p);free(p);return 0;}", N=n, K=k)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int j=@@K@@;do{free(p);}while(--j>0);free(p);return 0;}",
            N=n, K=k, V=v)
    elif shape == 3:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int a=0;a<2;a++)for(int b=0;b<@@K@@;b++)free(p);"
            "free(p);return 0;}", N=n, K=k)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int j=0;j<@@K@@;j++){free(p);if(j==0)break;}"
            "free(p);return 0;}", N=n, K=k)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 2: second release reached through a switch case body.
# ---------------------------------------------------------------------------
def gen_df_switch_case(i, rng):
    n = rng.randint(4, 24)
    sel = rng.randint(1, 4)
    v = rng.randint(2, 50)
    t = rng.randint(1, 3)
    shape = i % 5
    name = f"hw_df_switch_case_{shape}_{i}"
    if shape == 0:  # constant selector: unconditional double free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int s=@@S@@;switch(s){case @@S@@:free(p);break;"
            "default:break;}free(p);return 0;}", N=n, S=sel, V=v)
        expect = "reject"
    elif shape == 1:  # argc-selected case: conditional double free
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "switch(argc){case @@S@@:free(p);break;default:break;}"
            "free(p);return 0;}", N=n, S=sel, V=v)
        expect = "strict"
    elif shape == 2:  # deliberate fallthrough into a second free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int s=@@S@@;switch(s){case @@S@@:free(p);case @@S1@@:free(p);"
            "break;default:break;}return 0;}", N=n, S=sel, S1=sel + 1)
        expect = "reject"
    elif shape == 3:  # nested switch: inner free then outer free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int s=@@S@@;switch(s){case @@S@@:"
            "switch(@@T@@){case 1:free(p);break;default:break;}"
            "free(p);break;default:break;}return 0;}", N=n, S=sel, T=t, V=v)
        expect = "reject"
    else:  # free in default, then after the switch
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "switch(argc){case @@S@@:break;default:free(p);break;}"
            "free(p);return 0;}", N=n, S=sel)
        expect = "strict"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 3: release inside nested branches (if/else, loop+if).
# ---------------------------------------------------------------------------
def gen_df_nested_branch(i, rng):
    n = rng.randint(4, 26)
    v = rng.randint(2, 60)
    t = rng.randint(1, 6)
    shape = i % 5
    name = f"hw_df_nested_branch_{shape}_{i}"
    if shape == 0:  # nested ifs on argc, then a release on the merged path
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(argc>@@T@@){if(argc>@@T1@@)free(p);free(p);}return 0;}",
            N=n, V=v, T=t, T1=t + 1)
        expect = "strict"
    elif shape == 1:  # constant nested conditions: unconditional
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;if(@@V@@>0){if(@@N@@>0){free(p);}free(p);}return 0;}",
            N=n, V=v)
        expect = "reject"
    elif shape == 2:  # both arms free, then a further free on the merge
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>@@T@@){free(p);}else{free(p);}free(p);return 0;}",
            N=n, T=t)
        expect = "reject"
    elif shape == 3:  # conditional free inside a loop, then free after
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int j=0;j<3;j++){if(argc>@@T@@)free(p);}free(p);return 0;}",
            N=n, T=t)
        expect = "strict"
    else:  # guarded inner release plus unconditional outer release
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int x=@@V@@;if(x){if(x>1)free(p);}free(p);return 0;}",
            N=n, V=v)
        expect = "reject"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 4: two array slots / struct fields aliasing one allocation.
# ---------------------------------------------------------------------------
def gen_df_alias_array(i, rng):
    n = rng.randint(4, 24)
    k = rng.randint(2, 4)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"hw_df_alias_array_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *a[2];a[0]=malloc(@@N@@*sizeof(int));"
            "if(!a[0])return 2;a[1]=a[0];*a[0]=@@V@@;"
            "free(a[0]);free(a[1]);return 0;}", N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *a[@@K@@];a[0]=malloc(@@N@@*sizeof(int));"
            "if(!a[0])return 2;for(int j=1;j<@@K@@;j++)a[j]=a[0];"
            "free(a[0]);free(a[@@K1@@]);return 0;}", N=n, K=k, K1=k - 1)
    elif shape == 2:
        body = STD + tpl(
            "struct H@@I@@{int *v[2];};"
            "int main(void){struct H@@I@@ s;s.v[0]=malloc(@@N@@*sizeof(int));"
            "if(!s.v[0])return 2;s.v[1]=s.v[0];*s.v[0]=@@V@@;"
            "free(s.v[0]);free(s.v[1]);return 0;}", I=i, N=n, V=v)
    elif shape == 3:
        body = STD + tpl(
            "int main(void){char *a[2];a[0]=malloc(@@N@@);"
            "if(!a[0])return 2;a[0][0]=(char)@@V@@;a[1]=a[0];"
            "free(a[0]);free(a[1]);return 0;}", N=n, V=v)
    else:
        body = STD + tpl(
            "int main(void){int **pp=malloc(@@N@@*sizeof(int *));if(!pp)return 2;"
            "pp[0]=malloc(@@N@@*sizeof(int));if(!pp[0])return 2;"
            "pp[1]=pp[0];free(pp[0]);free(pp[1]);free(pp);return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 5: conditional double free across merged control paths.
# ---------------------------------------------------------------------------
def gen_df_cond_paths(i, rng):
    n = rng.randint(4, 26)
    v = rng.randint(2, 60)
    t = rng.randint(1, 6)
    shape = i % 5
    name = f"hw_df_cond_paths_{shape}_{i}"
    if shape == 0:  # both arms free, then the merged path frees again
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(argc>@@T@@){free(p);}else{free(p);}free(p);return 0;}",
            N=n, V=v, T=t)
        expect = "reject"
    elif shape == 1:  # only the taken path double frees
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>@@T@@)free(p);free(p);return 0;}", N=n, T=t)
        expect = "strict"
    elif shape == 2:  # constant condition: unconditional double free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(@@V@@>0)free(p);if(@@V@@>0)free(p);return 0;}", N=n, V=v)
        expect = "reject"
    elif shape == 3:  # goto merge: fallthrough double frees
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>@@T@@)goto done;free(p);done:free(p);return 0;}",
            N=n, T=t)
        expect = "reject"
    else:  # loop over branches, each path releases
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int j=0;j<3;j++){if(argc>@@T@@)free(p);else free(p);}"
            "return 0;}", N=n, T=t)
        expect = "reject"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 6: realloc then a double release (incl. realloc(ptr, 0)).
# ---------------------------------------------------------------------------
def gen_df_realloc_release(i, rng):
    n = rng.randint(4, 24)
    m = n + rng.randint(1, 16)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"hw_df_realloc_release_{shape}_{i}"
    if shape == 0:  # stale source released after a verified cplus_realloc
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "*p=@@V@@;int *q=cplus_realloc(p,@@M@@*sizeof(int));"
            "if(!q)return 2;cplus_release(q);cplus_release(p);return 0;}",
            N=n, M=m, V=v)
        expect = "reject"
    elif shape == 1:  # two stale aliases (source and result) both released
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new(int);if(!p)return 2;"
            "*p=@@V@@;int *q=cplus_realloc(p,@@M@@);"
            "if(!q)return 2;cplus_release(p);cplus_release(q);return 0;}",
            M=m, V=v)
        expect = "reject"
    elif shape == 2:  # plain realloc with unverified result
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=realloc(p,@@M@@*sizeof(int));free(q);free(p);return 0;}",
            N=n, M=m)
        expect = "strict"
    elif shape == 3:  # realloc(ptr, 0) result released together with source
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "int *q=cplus_realloc(p,0);cplus_release(q);cplus_release(p);"
            "return 0;}", N=n)
        expect = "reject"
    else:  # released result twice
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new(int);if(!p)return 2;"
            "*p=@@V@@;int *q=cplus_realloc(p,@@M@@);"
            "if(!q)return 2;cplus_release(q);cplus_release(q);return 0;}",
            M=m, V=v)
        expect = "reject"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 7: release of a moved-from source / double release of destination.
# ---------------------------------------------------------------------------
def gen_df_move_release(i, rng):
    n = rng.randint(4, 22)
    v = rng.randint(2, 55)
    shape = i % 5
    name = f"hw_df_move_release_{shape}_{i}"
    if shape == 0:  # cplus_move zeroes source; source released anyway
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "*p=@@V@@;int *q=cplus_move(p);cplus_release(q);"
            "cplus_release(p);return 0;}", N=n, V=v)
    elif shape == 1:  # manual move (alias + null source), then double free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;p=0;free(q);free(q);return 0;}", N=n, V=v)
    elif shape == 2:  # destination released twice
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new(int);if(!p)return 2;"
            "*p=@@V@@;int *q=cplus_move(p);cplus_release(q);"
            "cplus_release(q);return 0;}", V=v)
    elif shape == 3:  # two-step move chain, intermediate owner released
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "int *q=cplus_move(p);int *r=cplus_move(q);"
            "cplus_release(r);cplus_release(q);return 0;}", N=n)
    else:  # out-parameter handoff, then both handles released
        body = STD + tpl(
            "static int *take@@I@@(int **pp){int *r=*pp;*pp=0;return r;}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=take@@I@@(&p);*q=@@V@@;free(q);free(q);return 0;}",
            I=i, N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: cplus_release applied to a malloc/calloc pointer.
# ---------------------------------------------------------------------------
def gen_cp_release_malloc(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"hw_df_cp_release_malloc_{shape}_{i}"
    if shape == 0:
        body = BOTH + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;cplus_release(p);return 0;}", N=n, V=v)
    elif shape == 1:
        body = BOTH + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "cplus_release(p);cplus_release(p);return 0;}", N=n)
    elif shape == 2:
        body = BOTH + tpl(
            "int main(void){int *p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
            "cplus_release(p);return 0;}", N=n)
    elif shape == 3:
        body = BOTH + tpl(
            "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            "p[0]=(char)@@V@@;cplus_release(p);free(p);return 0;}", N=n, V=v)
    else:
        body = BOTH + tpl(
            "int main(void){void *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "cplus_release(p);return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 9: reset-to-null controls.
# ---------------------------------------------------------------------------
def gen_safe_reset(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"hw_df_safe_reset_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;if(p)free(p);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=NULL;free(p);return 0;}", N=n)
    elif shape == 2:
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "*p=@@V@@;cplus_release(p);p=0;cplus_release(p);return 0;}",
            N=n, V=v)
    elif shape == 3:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p=0;free(p);free(p);return 0;}", N=n)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>1){free(p);p=0;}if(p)free(p);return 0;}", N=n)
    return name, marked(name, body), "accept"


# ---------------------------------------------------------------------------
# Family 10: ordinary single-release / managed RAII controls.
# ---------------------------------------------------------------------------
def gen_safe_single(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"hw_df_safe_single_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int x=*p;free(p);return x==@@V@@?0:1;}", N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,(@@N@@+2)*sizeof(int));if(!t){free(p);return 2;}"
            "p=t;free(p);return 0;}", N=n)
    elif shape == 2:
        body = CPLUS + tpl(
            "int main(void){managed int *p=cplus_new_array(int,@@N@@);"
            "if(!p)return 2;p[0]=@@V@@;return p[0]==@@V@@?0:1;}", N=n, V=v)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=malloc(@@N@@*sizeof(int));if(!q){free(p);return 2;}"
            "*p=@@V@@;*q=@@V@@;free(p);free(q);return 0;}", N=n, V=v)
    return name, marked(name, body), "accept"


# ---------------------------------------------------------------------------
# Family 11: guarded-release controls (must pass both modes).
# ---------------------------------------------------------------------------
def gen_safe_guarded(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"hw_df_safe_guarded_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(p)free(p);p=NULL;if(p)free(p);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "static void maybe_free@@I@@(int *p){if(p)free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "maybe_free@@I@@(p);p=0;maybe_free@@I@@(p);return 0;}",
            I=i, N=n)
    elif shape == 2:
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "*p=@@V@@;if(p)cplus_release(p);p=0;if(p)cplus_release(p);"
            "return 0;}", N=n, V=v)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>1){free(p);p=0;}free(p);return 0;}", N=n)
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_df_loop_after,
    gen_df_switch_case,
    gen_df_nested_branch,
    gen_df_alias_array,
    gen_df_cond_paths,
    gen_df_realloc_release,
    gen_df_move_release,
    gen_cp_release_malloc,
    gen_safe_reset,
    gen_safe_single,
    gen_safe_guarded,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body, expect = builder(i, rng)
            return name, body, expect, family
        start += count
    raise IndexError(i)


# --- Verification -----------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    keywords = ("error:", "warning:", "double", "free", "release", "leak",
                "runtime", "sanit", "UB", "invalid", "owner")
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("check:") or s.startswith("/"):
            continue
        if any(k in s for k in keywords):
            return s[:200]
    lines = [ln.strip() for ln in text.splitlines()
             if ln.strip() and not ln.strip().startswith("check:")]
    return lines[0][:200] if lines else "(no diagnostics)"


def check_case(case):
    """Run both modes; return a mismatch record (or None) and persist it."""
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
                       (proc.stderr + "\n" + proc.stdout).strip())

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

    if not reasons:
        return {"name": name, "family": family, "expect": expect,
                "reasons": [], "false_positive": False}

    kind = "false_positive" if expect == "accept" else "hole"
    (FAILS / f"{name}.c").write_text(body)
    log = [
        f"case: {name}",
        f"family: {family}",
        f"expect: {expect}",
        f"kind: {kind}",
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
    detail = (f"{name} [{family}] expect={expect} kind={kind}: "
              f"{'; '.join(reasons)} | "
              f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
              f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}")
    return {"name": name, "family": family, "expect": expect,
            "kind": kind, "reasons": reasons, "false_positive": false_positive,
            "detail": detail, "hard": hard}


def main():
    global STAGE
    if not pathlib.Path(CC).exists():
        print(f"error: compiler binary not found: {CC} (set CPLUS_BIN)",
              file=sys.stderr)
        return 2

    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    for stale in list(FAILS.glob("*.c")) + list(FAILS.glob("*.diag.txt")):
        stale.unlink()

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the corpus.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    print(f"hw_df: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    with tempfile.TemporaryDirectory(prefix="hw_df_") as stage:
        STAGE = pathlib.Path(stage)
        with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
            results = list(pool.map(check_case, cases))

    mismatches = [r for r in results if r["reasons"]]
    mismatch_names = sorted(r["name"] for r in mismatches)
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    holes = [r for r in mismatches if r["expect"] != "accept"]
    by_family = collections.Counter(r["family"] for r in mismatches)
    hard = sum(1 for r in mismatches for reason in r["reasons"]
               if "TIMEOUT" in reason or "OS ERROR" in reason or "CRASH" in reason)

    print("results: %d cases, %d mismatches (%d safety holes, "
          "%d false positives, %d hard errors)"
          % (len(cases), len(mismatches), len(holes), len(false_positives), hard))
    if by_family:
        print("mismatches by family: " + " ".join(
            f"{k}={by_family[k]}" for k in sorted(by_family)))
    print(f"false positives: {len(false_positives)}")
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print(f"  ... {len(false_positives) - 20} more")
    print("first 10 mismatch names:")
    for name in mismatch_names[:10]:
        print("  " + name)
    if len(mismatch_names) > 10:
        print(f"  ... {len(mismatch_names) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        status, len(cases), len(mismatches), len(false_positives)))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
