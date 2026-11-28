#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator for double-free / invalid-release (165 cases).

Every generated program is a small C (or C+ RAII) program built from a
per-case independent seeded RNG.  Generation and verification both run in
thread pools; ``pool.map`` preserves input order and case ``i`` always draws
from ``random.Random(f"{FUZZ_SEED}:{i}")``, so the corpus is reproducible
regardless of ``FUZZ_WORKERS``.  A unique marker comment plus seeded size,
offset and value parameters make every program byte-distinct.  No case is
copied from another corpus.

Families (all target double release and invalid deallocation):

  df_same       release the same pointer twice (malloc/calloc/triple)  (reject)
  df_alias      release through aliases, chains, void* and pointer arrays (reject)
  df_branch     second release reached via if/else path selection       (reject)
  df_loop       repeated release inside for / while / do / nested loops (reject)
  df_callee     callee frees, caller frees again (direct, wrapper, fnptr) (reject)
  df_realloc    stale pointer released after realloc / realloc(...,0)    (strict/reject)
  fi_interior   free(p+k), &p[k], char*/void* interior pointers          (reject)
  fi_increment  free of p++ / q+=k / pre-incremented pointers            (reject)
  fs_storage    free of stack, global, string literal, array, field,
                cast-to-void pointers                                    (reject)
  fo_transfer   free after ownership transfer (return, out-param, move)  (reject)
  cp_release    cplus_release misuse: double, alias, malloc/stack/interior (reject)
  safe          valid controls: free(NULL), free(p+0), reset-to-null,
                single free, managed RAII, realloc-with-temporary         (accept)

Each case declares an expected outcome (same contract as redteam_100.py):

  reject - proven allocator error: must fail BOTH --default and --strict.
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/ag_free/failures/.  Holes (unsafe accepted) and
false positives (safe rejected) are reported separately.  This is a
differential measurement against the existing binary, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xF2EE
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
OUT = ROOT / "tests/out/core/ag_free"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xF2EE"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases, builder.  A case index maps deterministically
# to exactly one family via the cumulative counts below.
FAMILIES = (
    ("df_same", 12, None),
    ("df_alias", 12, None),
    ("df_branch", 12, None),
    ("df_loop", 12, None),
    ("df_callee", 12, None),
    ("df_realloc", 12, None),
    ("fi_interior", 12, None),
    ("fi_increment", 12, None),
    ("fs_storage", 24, None),
    ("fo_transfer", 12, None),
    ("cp_release", 12, None),
    ("safe", 21, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
CPLUS = '#include "cplus/cplus.h"\n'
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in .format/f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def lit(n):
    """A distinct non-empty string literal that varies with n."""
    return ALPHABET[:(n % (len(ALPHABET) - 1)) + 1]


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag_free {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: release the same pointer twice.
# ---------------------------------------------------------------------------
def gen_df_same(i, rng):
    n = rng.randint(4, 32)
    v = rng.randint(2, 60)
    shape = i % 3
    name = f"df_same_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);free(p);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
            "*p=@@V@@;free(p);free(p);return 0;}", N=n, V=v)
    else:
        body = STD + tpl(
            "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            "free(p);free(p);free(p);return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 2: release through aliases / alias chains / void* / pointer arrays.
# ---------------------------------------------------------------------------
def gen_df_alias(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 55)
    shape = i % 4
    name = f"df_alias_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;free(p);free(q);return 0;}", N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *a=p;int *b=a;int *c=b;free(p);free(c);return 0;}", N=n)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *v=p;free(p);free(v);return 0;}", N=n)
    else:
        body = STD + tpl(
            "int main(void){int *a[2];a[0]=malloc(@@N@@*sizeof(int));if(!a[0])return 2;"
            "a[1]=a[0];*a[0]=@@V@@;free(a[0]);free(a[1]);return 0;}", N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 3: second release reached through if/else branch paths.
# ---------------------------------------------------------------------------
def gen_df_branch(i, rng):
    n = rng.randint(4, 24)
    t = rng.randint(1, 6)
    v = rng.randint(2, 50)
    shape = i % 4
    name = f"df_branch_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(int c,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;if(c>@@T@@)free(p);free(p);return 0;}", N=n, T=t)
    elif shape == 1:
        body = STD + tpl(
            "int main(int c,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;*p=@@V@@;if(c>@@T@@){free(p);free(p);}"
            "else{free(p);}return 0;}", N=n, T=t, V=v)
    elif shape == 2:
        body = STD + tpl(
            "int main(int c,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;if(c>@@T@@)free(p);else free(p);free(p);return 0;}",
            N=n, T=t)
    else:
        body = STD + tpl(
            "int main(int c,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;int *q=(c>@@T@@)?p:p;free(p);free(q);return 0;}",
            N=n, T=t)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 4: repeated release inside loops.
# ---------------------------------------------------------------------------
def gen_df_loop(i, rng):
    n = rng.randint(4, 24)
    k = rng.randint(2, 5)
    shape = i % 4
    name = f"df_loop_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int j=0;j<@@K@@;j++)free(p);return 0;}", N=n, K=k)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int j=@@K@@;while(j-->0)free(p);return 0;}", N=n, K=k)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int j=@@K@@;do{free(p);}while(--j>0);return 0;}", N=n, K=k)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int a=0;a<2;a++)for(int b=0;b<@@K@@;b++)free(p);return 0;}",
            N=n, K=k)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 5: interprocedural callee frees, caller releases again.
# ---------------------------------------------------------------------------
def gen_df_callee(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 55)
    shape = i % 4
    name = f"df_callee_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "static void rel@@I@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;rel@@I@@(p);rel@@I@@(p);return 0;}", I=i, N=n, V=v)
    elif shape == 1:
        body = STD + tpl(
            "static void rel@@I@@(int *p){free(p);}\n"
            "static void wrap@@I@@(int *p){rel@@I@@(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "wrap@@I@@(p);rel@@I@@(p);return 0;}", I=i, N=n)
    elif shape == 2:
        body = STD + tpl(
            "static void rel@@I@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void (*fn)(int *)=rel@@I@@;fn(p);fn(p);return 0;}", I=i, N=n)
    else:
        body = STD + tpl(
            "static void rel@@I@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;rel@@I@@(p);free(p);return 0;}", I=i, N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 6: stale-pointer release after realloc / realloc(ptr,0).
# ---------------------------------------------------------------------------
def gen_df_realloc(i, rng):
    n = rng.randint(4, 24)
    m = n + rng.randint(1, 8)
    shape = i % 4
    name = f"df_realloc_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,@@M@@*sizeof(int));(void)t;free(p);return 0;}",
            N=n, M=m)
        expect = "strict"
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,@@M@@*sizeof(int));if(t){free(t);free(p);}else free(p);"
            "return 0;}", N=n, M=m)
        expect = "reject"
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;void *t=realloc(p,@@M@@*sizeof(int));if(t)p=t;"
            "free(q);free(p);return 0;}", N=n, M=m)
        expect = "reject"
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,0);(void)t;free(p);return 0;}", N=n)
        expect = "strict"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 7: free of an interior pointer.
# ---------------------------------------------------------------------------
def gen_fi_interior(i, rng):
    n = rng.randint(4, 20)
    off = rng.randint(1, 3)
    shape = i % 4
    name = f"fi_interior_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "free(p+@@O@@);return 0;}", N=n, O=off)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "char *c=(char *)p;free(c+@@O@@);return 0;}", N=n, O=off)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "free(&p[@@O@@]);return 0;}", N=n, O=off)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "void *v=p;free((char *)v+@@O@@);return 0;}", N=n, O=off)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: free of incremented / advanced pointers.
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
            "int *q=p;for(int j=0;j<@@O@@;j++)q++;free(q);return 0;}", N=n, O=off)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc((@@N@@+8)*sizeof(int));if(!p)return 2;"
            "p+=@@O@@;free(p);return 0;}", N=n, O=off)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 9: free of non-heap / invalid storage (six storage kinds).
# ---------------------------------------------------------------------------
def gen_fs_storage(i, rng):
    n = rng.randint(3, 16)
    v = rng.randint(2, 48)
    shape = i % 6
    name = f"fs_storage_{shape}_{i}"
    if shape == 0:  # stack local
        body = STD + tpl(
            "int main(void){int x=@@V@@;free(&x);return 0;}", V=v)
    elif shape == 1:  # global
        body = STD + tpl(
            "static int g@@N@@;int main(void){free(g@@N@@);return 0;}", N=n)
    elif shape == 2:  # string literal (vary the literal)
        body = STD + tpl(
            'int main(void){free((void *)"@@S@@");return 0;}', S=lit(n))
    elif shape == 3:  # stack array
        body = STD + tpl(
            "int main(void){int a[@@N@@]={0};free(a);return 0;}", N=n)
    elif shape == 4:  # struct field (stack object)
        body = STD + tpl(
            "struct S@@I@@{int x[@@N@@];};int main(void){struct S@@I@@ s;"
            "free(&s.x[1]);return 0;}", I=i, N=n)
    else:  # cast stack pointer to void*
        body = STD + tpl(
            "int main(void){int x=@@V@@;void *p=(void *)&x;free(p);return 0;}", V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 10: free after ownership transfer.
# ---------------------------------------------------------------------------
def gen_fo_transfer(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 55)
    shape = i % 4
    name = f"fo_transfer_{shape}_{i}"
    if shape == 0:  # callee frees the returned handle, caller frees again
        body = STD + tpl(
            "static int *make@@I@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;free(p);return p;}\n"
            "int main(void){int *q=make@@I@@();if(!q)return 2;free(q);return 0;}",
            I=i, N=n, V=v)
    elif shape == 1:  # out-parameter hands ownership to the caller, freed twice
        body = STD + tpl(
            "static int *take@@I@@(int **pp){int *p=*pp;*pp=0;return p;}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=take@@I@@(&p);*q=@@V@@;free(q);free(q);return 0;}",
            I=i, N=n, V=v)
    elif shape == 2:  # original owner cleared, consumed handle released again
        body = STD + tpl(
            "static void consume@@I@@(int *p){free(p);}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;p=0;consume@@I@@(q);free(q);return 0;}", I=i, N=n)
    else:  # ownership passed through an identity function, both handles freed
        body = STD + tpl(
            "static int *pass@@I@@(int *p){return p;}\n"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=pass@@I@@(p);*q=@@V@@;free(p);free(q);return 0;}",
            I=i, N=n, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 11: C+ cplus_release misuse.
# ---------------------------------------------------------------------------
def gen_cp_release(i, rng):
    n = rng.randint(3, 20)
    v = rng.randint(2, 50)
    shape = i % 5
    name = f"cp_release_{shape}_{i}"
    if shape == 0:  # explicit double release
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new(int);if(!p)return 2;"
            "*p=@@V@@;cplus_release(p);cplus_release(p);return 0;}", V=v)
    elif shape == 1:  # double release through an alias
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "int *q=p;cplus_release(p);cplus_release(q);return 0;}", N=n)
    elif shape == 2:  # release a pointer that is not a managed allocation
        body = CPLUS + STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "cplus_release(p);return 0;}", N=n)
    elif shape == 3:  # release stack storage
        body = CPLUS + tpl(
            "int main(void){int x=@@V@@;cplus_release(&x);return 0;}", V=v)
    else:  # release an interior managed pointer
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@+2);if(!p)return 2;"
            "cplus_release(p+1);return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 12: safe controls (must pass both modes).
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 7
    name = f"safe_{shape}_{i}"
    if shape == 0:  # free(NULL)
        body = STD + "int main(void){free(0);free(NULL);return 0;}"
    elif shape == 1:  # free of a zero-offset pointer
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p+0);return 0;}", N=n)
    elif shape == 2:  # reset to null, then conditionally release
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;if(p)free(p);return 0;}", N=n)
    elif shape == 3:  # ordinary single release
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int x=*p;free(p);return x==@@V@@?0:1;}", N=n, V=v)
    elif shape == 4:  # C+ managed RAII (no explicit release)
        body = CPLUS + tpl(
            "int main(void){managed int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "p[0]=@@V@@;return p[0]==@@V@@?0:1;}", N=n, V=v)
    elif shape == 5:  # realloc via a temporary, single final release
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,(@@N@@+2)*sizeof(int));if(t)p=t;else return 2;"
            "free(p);return 0;}", N=n)
    else:  # reset to null, then unconditional free(NULL)
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;free(p);return 0;}", N=n)
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_df_same,
    gen_df_alias,
    gen_df_branch,
    gen_df_loop,
    gen_df_callee,
    gen_df_realloc,
    gen_fi_interior,
    gen_fi_increment,
    gen_fs_storage,
    gen_fo_transfer,
    gen_cp_release,
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
    keywords = ("error:", "warning:", "double", "free", "release", "leak",
                "runtime", "sanit", "UB", "invalid")
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
            "kind": kind, "detail": detail, "hard": hard}


def main():
    global STAGE
    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the corpus.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    names = [c[0] for c in cases]
    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    assert len(set(names)) == TOTAL, "duplicate case name"

    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    print(f"ag_free: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    with tempfile.TemporaryDirectory(prefix="ag_free_") as stage:
        STAGE = pathlib.Path(stage)
        with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
            mismatches = [m for m in pool.map(check_case, cases) if m]
    mismatches.sort(key=lambda m: m["name"])

    holes = [m for m in mismatches if m["kind"] == "hole"]
    false_positives = [m for m in mismatches if m["kind"] == "false_positive"]
    by_family = collections.Counter(m["family"] for m in mismatches)

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"({len(holes)} safety holes, {len(false_positives)} false positives)")
    if by_family:
        print("mismatches by family: " + " ".join(
            f"{k}={by_family[k]}" for k in sorted(by_family)))
    for line in mismatches[:12]:
        print("  " + line["detail"])
    if len(mismatches) > 12:
        print(f"  ... {len(mismatches) - 12} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    return 1 if mismatches or unique != len(cases) else 0


if __name__ == "__main__":
    sys.exit(main())
