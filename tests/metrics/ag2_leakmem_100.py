#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL malloc-family RAII leak generator (128 cases).

Every generated program is a small, valid C (or C+ RAII) program built from a
per-case independent seeded RNG.  Generation and verification both run in
thread pools; ``pool.map`` preserves input order and case ``i`` always draws
from ``random.Random(f"{FUZZ_SEED}:{i}")``.  The corpus and the report are
therefore reproducible regardless of ``FUZZ_WORKERS``.  A unique case marker
plus seeded size/threshold/value parameters makes every program byte-distinct;
no case is copied from another corpus.

Families (all target malloc-family resource acquisition / release mismatches):

  early_if         live allocation inside nested if bodies with early returns
                   that skip the matching free                       (strict)
  early_switch     live allocation with early returns taken from switch cases
                   (including a nested case-body allocation)         (strict)
  early_loop       early return from inside for-loops, with the owner either
                   outside or inside the loop body                   (strict)
  partial_multi    two live resources where an early path releases only one
                   (two malloc, malloc+FILE*, malloc+strdup)          (strict)
  overwrite        the only pointer to a live block is overwritten by a
                   second allocation (direct / alias / chain)         (strict)
  outparam         an out-parameter helper overwrites the caller's only
                   owner (malloc / repeated malloc / strdup)          (strict)
  goto_df          goto cleanup fallthrough reaches a second free of an
                   already-released live malloc owner (proven double free)
                                                                     (reject)
  strdup_realloc   strdup overwrite leak and realloc failure / success leaks
                   (the sole owner is dropped before the free)        (strict)
  lost_owner       allocation stored in a struct field / global / inner
                   scope, then the owning reference is discarded      (strict)
  longjmp          setjmp/longjmp unwinds past a live allocation whose free
                   is skipped, plus a clean longjmp control           (strict,
                                                                     accept)
  safe             valid controls: every explicit return frees, C+
                   cplus_new_array + cplus_release, managed RAII,
                   owned resource moved with cplus_move               (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject - proven memory error (double free): must fail BOTH --default and
           --strict.
  strict - plain-C leak: MAY pass --default, but must fail --strict.  The
           current compiler frequently downgrades plain-C leak findings to
           warnings even in STRICT, so these surface as missed detections
           (safety holes) in this differential measurement.
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/ag2_leakmem/failures/.  Missed detections and
false positives are reported separately.  This is a differential measurement
against the existing binary, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xA2EE
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
OUT = ROOT / "tests/out/core/ag2_leakmem"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA2EE"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, case count, builder (filled in below).  A case index maps
# deterministically to exactly one family through these ordered ranges.
FAMILIES = (
    ("early_if", 16, None),
    ("early_switch", 12, None),
    ("early_loop", 12, None),
    ("partial_multi", 12, None),
    ("overwrite", 12, None),
    ("outparam", 12, None),
    ("goto_df", 8, None),
    ("strdup_realloc", 12, None),
    ("lost_owner", 12, None),
    ("longjmp", 8, None),
    ("safe", 12, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
STR = "#include <stdlib.h>\n#include <string.h>\n"
STDIO = "#include <stdio.h>\n#include <stdlib.h>\n"
SETJMP = "#include <setjmp.h>\n#include <stdlib.h>\n"
CPLUS = '#include "cplus/cplus.h"\n'


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in .format/f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag2_leakmem {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: live allocation in nested if bodies with early returns.
# ---------------------------------------------------------------------------
def gen_early_if(i, rng):
    variant = i % 2
    kind = (i // 2) % 2                 # malloc / calloc
    kname = ("malloc", "calloc")[kind]
    n = rng.randint(4, 24)
    t = rng.randint(1, 8)
    v = rng.randint(2, 60)
    alloc = (f"malloc({n}*sizeof(int))" if kind == 0
             else f"calloc({n},sizeof(int))")
    name = f"early_if_{kname}_{variant}_{i}"

    if variant == 0:
        # Two levels of nested if, each guarding an early return that skips
        # the single free at the end of main.
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=@@ALLOC@@;if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T@@){if(argc>@@T1@@)return 3;}"
            "if(argc>@@T2@@){if(argc>@@T3@@)return 4;}"
            "free(p);return 0;}",
            ALLOC=alloc, V=v, T=t, T1=t + 1, T2=t + 2, T3=t + 3)
    else:
        # The allocation itself is inside the nested if; the inner early
        # return bypasses its only free while the outer scope falls through.
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "if(argc>@@T@@){int *p=@@ALLOC@@;if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T1@@){if(argc>@@T2@@)return 3;}"
            "free(p);}return 0;}",
            ALLOC=alloc, V=v, T=t, T1=t + 1, T2=t + 2)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 2: live allocation with early returns from switch cases.
# ---------------------------------------------------------------------------
def gen_early_switch(i, rng):
    variant = i % 2
    n = rng.randint(4, 20)
    t = rng.randint(1, 6)
    v = rng.randint(2, 48)
    name = f"early_switch_{variant}_{i}"

    if variant == 0:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int k=argc+@@V@@;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "switch(k){case 1:if(argc>@@T@@)return 3;break;"
            "case 2:return 4;default:break;}"
            "free(p);return 0;}", N=n, V=v, T=t)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int k=argc+@@V@@;"
            "switch(k){case 1:{int *p=calloc(@@N@@,sizeof(int));"
            "if(!p)return 2;p[0]=@@V@@;if(argc>@@T@@)return 3;"
            "free(p);break;}default:break;}return 0;}", N=n, V=v, T=t)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 3: early return from inside for-loops with a live allocation.
# ---------------------------------------------------------------------------
def gen_early_loop(i, rng):
    variant = i % 2
    n = rng.randint(5, 20)
    t = rng.randint(1, n - 1)
    v = rng.randint(2, 48)
    name = f"early_loop_{variant}_{i}"

    if variant == 0:
        # Owner lives outside the loop; a loop-internal return skips free.
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;for(int j=0;j<@@N@@;j++){if(j==@@T@@)return 3;}"
            "free(p);return 0;}", N=n, V=v, T=t)
    else:
        # Owner allocated each iteration; the early return leaks that
        # iteration's block while earlier iterations are released.
        body = STD + tpl(
            "int main(void){for(int j=0;j<@@N@@;j++){"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "if(j==@@T@@)return 3;free(p);}return 0;}", N=n, V=v, T=t)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 4: multiple live resources, an early path releases only one.
# ---------------------------------------------------------------------------
def gen_partial_multi(i, rng):
    shape = i % 3
    variant = (i // 3) % 2
    n = rng.randint(4, 20)
    t = rng.randint(1, 7)
    v = rng.randint(2, 50)
    sname = ("two_malloc", "malloc_file", "malloc_strdup")[shape]
    name = f"partial_multi_{sname}_{variant}_{i}"

    if shape == 0:
        if variant == 0:
            body = STD + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));"
                "int *q=malloc((@@N@@+1)*sizeof(int));if(!p||!q)return 2;"
                "p[0]=@@V@@;q[0]=@@V1@@;if(p[0]>@@T@@){free(p);return 3;}"
                "free(p);free(q);return 0;}", N=n, V=v, V1=v + 1, T=t)
        else:
            body = STD + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));"
                "int *q=malloc((@@N@@+1)*sizeof(int));if(!p||!q)return 2;"
                "p[0]=@@V@@;q[0]=@@V1@@;if(q[0]>@@T@@){free(q);return 3;}"
                "free(p);free(q);return 0;}", N=n, V=v, V1=v + 1, T=t)
    elif shape == 1:
        if variant == 0:
            body = STDIO + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));"
                'FILE *f=fopen("ag2lm_@@I@@.txt","w");if(!p||!f)return 2;'
                "p[0]=@@V@@;if(p[0]>@@T@@){free(p);return 3;}free(p);"
                "if(fclose(f)!=0)return 4;return 0;}", N=n, V=v, T=t, I=i)
        else:
            body = STDIO + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));"
                'FILE *f=fopen("ag2lm_@@I@@.txt","w");if(!p||!f)return 2;'
                "p[0]=@@V@@;if(p[0]>@@T@@){if(fclose(f)!=0)return 3;"
                "return 4;}free(p);if(fclose(f)!=0)return 5;return 0;}",
                N=n, V=v, T=t, I=i)
    else:
        if variant == 0:
            body = STR + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));"
                'char *s=strdup("ag2lm_@@I@@");if(!p||!s)return 2;'
                "p[0]=@@V@@;if(p[0]>@@T@@){free(s);return 3;}"
                "free(p);free(s);return 0;}", N=n, V=v, T=t, I=i)
        else:
            body = STR + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));"
                'char *s=strdup("ag2lm_@@I@@");if(!p||!s)return 2;'
                "p[0]=@@V@@;if(p[0]>@@T@@){free(p);return 3;}"
                "free(p);free(s);return 0;}", N=n, V=v, T=t, I=i)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 5: the only pointer to a live block is overwritten.
# ---------------------------------------------------------------------------
def gen_overwrite(i, rng):
    shape = i % 3                      # direct / alias / chain
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("direct", "alias", "chain")[shape]
    name = f"overwrite_{sname}_{i}"

    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;p=malloc((@@N@@+2)*sizeof(int));if(!p)return 3;"
            "p[0]=@@V1@@;free(p);return 0;}", N=n, V=v, V1=v + 1)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;p=malloc((@@N@@+2)*sizeof(int));if(!p)return 3;"
            "q[0]=@@V@@;p[0]=@@V1@@;free(p);return 0;}",
            N=n, V=v, V1=v + 1)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;int *q=malloc((@@N@@+3)*sizeof(int));"
            "if(!q)return 3;p=q;q=malloc((@@N@@+4)*sizeof(int));"
            "if(!q)return 4;p[0]=@@V1@@;free(p);free(q);return 0;}",
            N=n, V=v, V1=v + 1)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 6: out-parameter helper overwrites the caller's only owner.
# ---------------------------------------------------------------------------
def gen_outparam(i, rng):
    shape = i % 3                      # overwrite / repeat / strdup
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("overwrite", "repeat", "strdup")[shape]
    name = f"outparam_{sname}_{i}"

    if shape == 0:
        body = STD + tpl(
            "static void make@@I@@(int **out){*out=malloc(@@N@@*sizeof(int));}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "make@@I@@(&p);if(!p)return 3;p[0]=@@V@@;free(p);return 0;}",
            I=i, N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "static void make@@I@@(int **out){*out=malloc(@@N@@*sizeof(int));}\n"
            "int main(void){int *p=0;make@@I@@(&p);if(!p)return 2;"
            "p[0]=@@V@@;make@@I@@(&p);if(!p)return 3;free(p);return 0;}",
            I=i, N=n, V=v)
    else:
        body = STR + tpl(
            'static void dup@@I@@(char **out){*out=strdup("ag2op_@@I@@");}\n'
            'int main(void){char *s=strdup("ag2seed_@@I@@");if(!s)return 2;'
            "dup@@I@@(&s);if(!s)return 3;s[0]='x';free(s);return 0;}", I=i)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 7: goto cleanup fallthrough double-free (proven -> reject).
# ---------------------------------------------------------------------------
def gen_goto_df(i, rng):
    shape = i % 3                      # single / two / unconditional
    n = rng.randint(4, 24)
    t = rng.randint(1, 7)
    sname = ("single", "two", "unconditional")[shape]
    name = f"goto_df_{sname}_{i}"

    if shape == 0:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>@@T@@){free(p);goto cleanup;}free(p);"
            "cleanup:free(p);return 0;}", N=n, T=t)
    elif shape == 1:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));"
            "int *q=malloc((@@N@@+1)*sizeof(int));if(!p||!q)return 2;"
            "if(argc>@@T@@){free(p);free(q);goto cleanup;}free(p);"
            "cleanup:free(q);return 0;}", N=n, T=t)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);if(argc>@@T@@)goto cleanup;"
            "cleanup:free(p);return 0;}", N=n, T=t)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: strdup and realloc leak patterns.
# ---------------------------------------------------------------------------
def gen_strdup_realloc(i, rng):
    shape = i % 3                      # strdup / realloc_fail / realloc_ret
    n = rng.randint(4, 24)
    t = rng.randint(1, 8)
    v = rng.randint(2, 60)
    sname = ("strdup", "realloc_fail", "realloc_ret")[shape]
    name = f"strdup_realloc_{sname}_{i}"

    if shape == 0:
        body = STR + tpl(
            'int main(void){char *s=strdup("ag2sr_@@I@@a");if(!s)return 2;'
            's=strdup("ag2sr_@@I@@b");if(!s)return 3;'
            "s[0]='x';free(s);return 0;}", I=i)
    elif shape == 1:
        # p = realloc(p, ...): on failure the sole owner is replaced by NULL
        # and the original block is never released.
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;p=realloc(p,(@@N@@+8)*sizeof(int));if(!p)return 3;"
            "p[0]=@@V1@@;free(p);return 0;}", N=n, V=v, V1=v + 1)
    else:
        # realloc succeeds into q; an early return before the free leaks it.
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;int *q=realloc(p,(@@N@@+8)*sizeof(int));"
            "if(!q)return 3;p=q;int x=p[0];if(x>@@T@@)return 4;"
            "free(p);return 0;}", N=n, V=v, T=t)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 9: allocation stored, then the owning reference is discarded.
# ---------------------------------------------------------------------------
def gen_lost_owner(i, rng):
    shape = i % 3                      # field / global / scope
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("field", "global", "scope")[shape]
    name = f"lost_owner_{sname}_{i}"

    if shape == 0:
        body = STD + tpl(
            "struct Box@@I@@{int *p;};\n"
            "int main(void){struct Box@@I@@ b;"
            "b.p=malloc(@@N@@*sizeof(int));if(!b.p)return 2;"
            "b.p[0]=@@V@@;b.p=0;return 0;}", I=i, N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "static int *g@@I@@;\n"
            "int main(void){g@@I@@=malloc(@@N@@*sizeof(int));"
            "if(!g@@I@@)return 2;g@@I@@[0]=@@V@@;g@@I@@=0;return 0;}",
            I=i, N=n, V=v)
    else:
        body = STD + tpl(
            "int main(void){{int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;p[0]=@@V@@;}return 0;}", N=n, V=v)
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 10: setjmp/longjmp unwinding past a live allocation (plus control).
# ---------------------------------------------------------------------------
def gen_longjmp(i, rng):
    shape = i % 4                      # deep / nested / same / clean
    n = rng.randint(4, 32)
    v = rng.randint(2, 12)
    sname = ("deep", "nested", "same", "clean")[shape]
    name = f"longjmp_{sname}_{i}"

    if shape == 0:
        body = SETJMP + tpl(
            "static jmp_buf env@@I@@;\n"
            "static void deep@@I@@(void){int *t=malloc(@@N@@);"
            "longjmp(env@@I@@,1);free(t);}\n"
            "int main(void){if(setjmp(env@@I@@)==0)deep@@I@@();return 0;}",
            I=i, N=n)
        expect = "strict"
    elif shape == 1:
        body = SETJMP + tpl(
            "static jmp_buf out@@I@@,in@@I@@;\n"
            "static void mid@@I@@(void){int *t=malloc(@@N@@);"
            "if(setjmp(in@@I@@)==0)longjmp(out@@I@@,1);free(t);}\n"
            "int main(void){if(setjmp(out@@I@@)==0)mid@@I@@();return 0;}",
            I=i, N=n)
        expect = "strict"
    elif shape == 2:
        body = SETJMP + tpl(
            "static jmp_buf env@@I@@;\n"
            "int main(void){int *p=malloc(@@N@@);if(!p)return 2;"
            "if(setjmp(env@@I@@)==0)longjmp(env@@I@@,1);free(p);return 0;}",
            I=i, N=n)
        expect = "strict"
    else:
        # Clean control: longjmp with no live allocation to skip.
        body = SETJMP.replace("#include <stdlib.h>\n", "") + tpl(
            "static jmp_buf env@@I@@;\n"
            "static void deep@@I@@(void){longjmp(env@@I@@,@@V@@);}\n"
            "int main(void){int r=setjmp(env@@I@@);if(r==0)deep@@I@@();"
            "return r==@@V@@?0:1;}", I=i, V=v)
        expect = "accept"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 11: C+ resource / managed types used correctly (safe controls).
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    shape = i % 4                      # explicit / managed / resource / plain
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    t = rng.randint(1, 8)
    sname = ("explicit", "managed", "resource", "plain")[shape]
    name = f"safe_{sname}_{i}"

    if shape == 0:
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "p[0]=@@V@@;cplus_release(p);return 0;}", N=n, V=v)
    elif shape == 1:
        body = CPLUS + tpl(
            "int main(void){managed int *p=cplus_new_array(int,@@N@@);"
            "if(!p)return 2;p[0]=@@V@@;return p[0]==@@V@@?0:1;}", N=n, V=v)
    elif shape == 2:
        body = CPLUS + tpl(
            "resource struct H@@I@@{int id;};\n"
            "void H@@I@@_drop(H@@I@@ *h){if(h->id)h->id=0;}\n"
            "int main(void){H@@I@@ a={@@V@@};H@@I@@ b=cplus_move(a);"
            "return b.id==@@V@@?0:1;}", I=i, V=v)
    else:
        # Every explicit return releases the owner before returning.
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T@@){free(p);return 0;}free(p);return 1;}",
            N=n, V=v, T=t)
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_early_if,
    gen_early_switch,
    gen_early_loop,
    gen_partial_multi,
    gen_overwrite,
    gen_outparam,
    gen_goto_df,
    gen_strdup_realloc,
    gen_lost_owner,
    gen_longjmp,
    gen_safe,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, _builder), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body, expect = builder(i, rng)
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
            return s[:220]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:220] if lines else "(no diagnostics)"


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
            reasons.append("safe rejected in DEFAULT")
        if s_rej:
            reasons.append("safe rejected in STRICT")

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
    return (f"{name} [{family}] expect={expect}: {'; '.join(reasons)} | "
            f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
            f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}")


def main():
    global STAGE
    if not pathlib.Path(CC).exists():
        print(f"error: compiler binary not found: {CC} (set CPLUS_BIN)",
              file=sys.stderr)
        return 2

    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    # Remove only fixtures this generator previously wrote so a second run
    # reports exactly the mismatches of that run.
    for stale in list(FAILS.glob("*.c")) + list(FAILS.glob("*.diag.txt")):
        stale.unlink()

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the corpus.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    print(f"ag2_leakmem: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag2_leakmem_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]
    mismatches.sort()

    holes = sum(1 for m in mismatches if "expect=reject" in m or
                "expect=strict" in m)
    false_pos = sum(1 for m in mismatches if "expect=accept" in m)
    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches")
    print(f"missed detections (safety holes): {holes}")
    print(f"false positives (safe rejected): {false_pos}")
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print(f"  ... {len(mismatches) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
