#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator for double/invalid release (160 cases).

This is the second-generation (``ag2``) double-free / invalid-release corpus.
Every generated program is a small C (or C+ RAII) program built from a
per-case independent seeded RNG.  Generation and verification both run in
thread pools; ``pool.map`` preserves input order and case ``i`` always draws
from ``random.Random(f"{FUZZ_SEED}:{i}")``, so the corpus is reproducible
regardless of ``FUZZ_WORKERS``.  A unique marker comment plus seeded size,
offset and value parameters make every program byte-distinct.  No case is
copied from another corpus.

Families (all target double release and invalid deallocation):

  df_loop       repeated release inside for / while / do / nested loops  (reject)
  df_switch     second release reached through a switch case body        (reject)
  df_callee     callee frees, caller frees again (direct/wrapper/fnptr)  (reject)
  df_realloc    stale pointer released after realloc / realloc(...,0)    (strict/reject)
  df_move       release after cplus_move / manual ownership move         (reject)
  cp_release    cplus_release misuse: double, alias, malloc/stack/interior (reject)
  fi_storage    free of stack, global, literal, array, field, cast ptrs  (reject)
  fi_interior   free(p+k), &p[k], char*/void* interior pointers          (reject)
  fi_increment  free of p++ / q+=k / pre-incremented pointers            (reject)
  fo_transfer   ownership transfer double free (return/out-param/move)  (reject)
  safe_reset    valid controls: reset-to-null, single release, realloc   (accept)
  safe_null     valid controls: free(NULL), zeroed handle, guarded free  (accept)

Each case declares an expected outcome (same contract as the ag_* corpus):

  reject - proven allocator error: must fail BOTH --default and --strict.
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/ag2_free/failures/.  Holes (unsafe accepted) and
false positives (safe rejected) are reported separately.  This is a
differential measurement against the existing binary, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xA2F2
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
OUT = ROOT / "tests/out/core/ag2_free"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA2F2"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases.  A case index maps deterministically to
# exactly one family via the cumulative counts below.
FAMILIES = (
    ("df_loop", 12),
    ("df_switch", 12),
    ("df_callee", 12),
    ("df_realloc", 12),
    ("df_move", 12),
    ("cp_release", 12),
    ("fi_storage", 24),
    ("fi_interior", 12),
    ("fi_increment", 12),
    ("fo_transfer", 12),
    ("safe_reset", 15),
    ("safe_null", 13),
)
TOTAL = sum(count for _name, count in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
CPLUS = '#include "cplus/cplus.h"\n'
BOTH = CPLUS + STD
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
    return f"/* ag2_free {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: repeated release inside loops.
# ---------------------------------------------------------------------------
def gen_df_loop(i, rng):
    n = rng.randint(4, 24)
    k = rng.randint(2, 5)
    v = rng.randint(2, 60)
    shape = i % 4
    name = f"df_loop_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;for(int j=0;j<@@K@@;j++)free(p);return 0;}", N=n, K=k, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int j=@@K@@;while(j-->0)free(p);return 0;}", N=n, K=k)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int j=@@K@@;do{free(p);}while(--j>0);return 0;}",
            N=n, K=k, V=v)
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "for(int a=0;a<2;a++)for(int b=0;b<@@K@@;b++)free(p);return 0;}",
            N=n, K=k)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 2: second release reached through a switch case body.
# ---------------------------------------------------------------------------
def gen_df_switch(i, rng):
    n = rng.randint(4, 24)
    sel = rng.randint(1, 4)
    v = rng.randint(2, 50)
    shape = i % 4
    name = f"df_switch_{shape}_{i}"
    if shape == 0:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;*p=@@V@@;switch(argc){case @@S@@:free(p);break;"
            "default:break;}free(p);return 0;}", N=n, S=sel, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;switch(argc){case @@S@@:free(p);free(p);break;"
            "default:free(p);break;}return 0;}", N=n, S=sel)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int s=@@S@@;switch(s){case @@S@@:free(p);break;"
            "default:break;}free(p);return 0;}", N=n, S=sel, V=v)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;*p=@@V@@;switch(argc){case 0:break;case @@S@@:break;"
            "default:free(p);break;}free(p);return 0;}", N=n, S=sel, V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 3: interprocedural callee frees, caller releases again.
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
# Family 4: stale-pointer release after realloc / realloc(ptr,0).
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
# Family 5: release after an ownership move (cplus_move / manual move).
# ---------------------------------------------------------------------------
def gen_df_move(i, rng):
    n = rng.randint(4, 20)
    v = rng.randint(2, 50)
    shape = i % 3
    name = f"df_move_{shape}_{i}"
    if shape == 0:  # move the managed handle, then release the new owner twice
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "*p=@@V@@;int *q=cplus_move(p);cplus_release(q);cplus_release(q);"
            "return 0;}", N=n, V=v)
    elif shape == 1:  # manual move (alias + null source), then double free
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;p=0;free(q);free(q);return 0;}", N=n, V=v)
    else:  # two-step move chain, then double release of the final owner
        body = CPLUS + tpl(
            "int main(void){int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "int *q=cplus_move(p);int *r=cplus_move(q);cplus_release(r);"
            "cplus_release(r);return 0;}", N=n)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 6: cplus_release misuse.
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
        body = BOTH + tpl(
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
# Family 7: free of non-heap / invalid storage (six storage kinds).
# ---------------------------------------------------------------------------
def gen_fi_storage(i, rng):
    n = rng.randint(3, 16)
    v = rng.randint(2, 48)
    shape = i % 6
    name = f"fi_storage_{shape}_{i}"
    if shape == 0:  # stack local
        body = STD + tpl("int main(void){int x=@@V@@;free(&x);return 0;}", V=v)
    elif shape == 1:  # global object
        body = STD + tpl(
            "static int g@@N@@;int main(void){int x=@@V@@;(void)x;free(&g@@N@@);"
            "return 0;}", N=n, V=v)
    elif shape == 2:  # string literal (vary the literal)
        body = STD + tpl('int main(void){free((void *)"@@S@@");return 0;}', S=lit(n))
    elif shape == 3:  # stack array
        body = STD + tpl("int main(void){int a[@@N@@]={0};free(a);return 0;}", N=n)
    elif shape == 4:  # struct field (stack object)
        body = STD + tpl(
            "struct S@@I@@{int x[@@N@@];};int main(void){struct S@@I@@ s;"
            "free(&s.x[1]);return 0;}", I=i, N=n)
    else:  # cast stack pointer to void*
        body = STD + tpl(
            "int main(void){int x=@@V@@;void *p=(void *)&x;free(p);return 0;}", V=v)
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: free of an interior pointer.
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
# Family 9: free of incremented / advanced pointers.
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
# Family 11: reset-to-null / single-release safe controls (must pass both).
# ---------------------------------------------------------------------------
def gen_safe_reset(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 5
    name = f"safe_reset_{shape}_{i}"
    if shape == 0:  # reset to null, then conditionally release
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;if(p)free(p);return 0;}", N=n)
    elif shape == 1:  # reset to null, then unconditional free(NULL)
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;free(p);return 0;}", N=n)
    elif shape == 2:  # ordinary single release with read-back
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int x=*p;free(p);return x==@@V@@?0:1;}", N=n, V=v)
    elif shape == 3:  # realloc via a temporary, single final release
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,(@@N@@+2)*sizeof(int));if(!t){free(p);return 2;}p=t;"
            "free(p);return 0;}", N=n)
    else:  # managed RAII (no explicit release; handle reset by lowering)
        body = CPLUS + tpl(
            "int main(void){managed int *p=cplus_new_array(int,@@N@@);if(!p)return 2;"
            "p[0]=@@V@@;return p[0]==@@V@@?0:1;}", N=n, V=v)
    return name, marked(name, body), "accept"


# ---------------------------------------------------------------------------
# Family 12: free(NULL) / zeroed-handle safe controls (must pass both).
# ---------------------------------------------------------------------------
def gen_safe_null(i, rng):
    n = rng.randint(3, 20)
    shape = i % 4
    name = f"safe_null_{shape}_{i}"
    if shape == 0:  # direct free of both null spellings
        body = STD + "int main(void){free(0);free(NULL);return 0;}"
    elif shape == 1:  # free of a zero-initialized typed pointer
        body = STD + "int main(void){int *p=0;free(p);return 0;}"
    elif shape == 2:  # guarded release through a helper, with null arguments
        body = STD + tpl(
            "static void maybe_free@@I@@(int *p){if(p)free(p);}\n"
            "int main(void){int x=@@N@@;(void)x;maybe_free@@I@@(0);"
            "maybe_free@@I@@(NULL);return 0;}", I=i, N=n)
    else:  # void* null handle, reset and released again
        body = STD + "int main(void){void *p=0;free(p);p=NULL;free(p);return 0;}"
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_df_loop,
    gen_df_switch,
    gen_df_callee,
    gen_df_realloc,
    gen_df_move,
    gen_cp_release,
    gen_fi_storage,
    gen_fi_interior,
    gen_fi_increment,
    gen_fo_transfer,
    gen_safe_reset,
    gen_safe_null,
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

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    assert len(set(c[0] for c in cases)) == TOTAL, "duplicate case name"

    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    print(f"ag2_free: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    with tempfile.TemporaryDirectory(prefix="ag2_free_") as stage:
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
    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches, "
          f"{len(false_positives)} false positives)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
