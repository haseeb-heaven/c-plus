#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of allocation-size C programs.

Produces 170 distinct small C programs (>=100) from per-case independent seeded
RNGs.  Generation and checking both run in thread pools; ``pool.map`` preserves
input order and every case draws from ``random.Random(f"{SEED}:{i}")``, so a run
is fully reproducible regardless of ``FUZZ_WORKERS``.  Every program carries a
unique case marker and varied sizes/indices/values, so no case is a copy of
another.  All programs are plain C (no C+ extension syntax).

Families (declared outcome in parentheses):

  malloc_short   malloc(n) too small for the number of int elements  (reject)
  missing_sz     allocation in bytes with the element sizeof omitted (reject)
  sizeof_ptr     sizeof(pointer) used instead of sizeof(*p)          (reject)
  calloc_wrong   calloc(count, size) with a missing/wrong factor      (reject)
  realloc_shr    write past the new extent after a shrinking realloc  (reject)
  realloc_growo  write past even the grown extent after realloc       (reject)
  wrap           count*size / size multiplication wraps around        (reject)
  neg_zero       negative and zero allocation sizes                   (reject)
  trailing       struct with a trailing array sized without header    (reject)
  fam_bad        flexible array member written past sizeof(struct)    (reject)
  stackheap      stack-array size confused with a heap element size   (reject)
  strict_cond    argc-conditional / unverified size violation         (strict)
  ok_malloc      safe control: malloc(n * sizeof(int))                (accept)
  ok_calloc      safe control: calloc(n, sizeof(int))                 (accept)
  ok_realloc     safe control: realloc grow/shrink honours new extent (accept)
  ok_sizeof      safe control: sizeof(*p) used for the element type    (accept)
  ok_trailing    safe control: sizeof(*p) covers the trailing array    (accept)
  ok_fam         safe control: sizeof(struct) + n * sizeof(int)        (accept)
  ok_stackheap   safe control: stack extent kept separate from heap    (accept)
  ok_loop        safe control: bounded loop over a correct allocation  (accept)

Each case declares one expected outcome:

  reject - proven allocation-size error: must fail BOTH --default and --strict.
           Acceptance in either mode is a safety hole (a "miss").
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (acceptance in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are saved
under tests/out/core/hv_allocsize/failures/.  False positives (safe controls
rejected by the checker) are counted and reported separately.  This is a
differential measurement of the checker, not a proof of allocation safety.

Environment:
  FUZZ_SEED    default 0xA15A
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
OUT = ROOT / "tests/out/core/hv_allocsize"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA15A"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# family, number of cases, builder, declared outcome.
FAMILIES = (
    ("malloc_short",  12, "malloc_short",  "reject"),
    ("missing_sz",    12, "missing_sz",    "reject"),
    ("sizeof_ptr",    10, "sizeof_ptr",    "reject"),
    ("calloc_wrong",  10, "calloc_wrong",  "reject"),
    ("realloc_shr",   10, "realloc_shr",   "reject"),
    ("realloc_growo",  8, "realloc_growo", "reject"),
    ("wrap",          12, "wrap",          "reject"),
    ("neg_zero",      12, "neg_zero",      "reject"),
    ("trailing",      10, "trailing",      "reject"),
    ("fam_bad",       10, "fam_bad",       "reject"),
    ("stackheap",     10, "stackheap",     "reject"),
    ("strict_cond",    8, "strict_cond",   "strict"),
    ("ok_malloc",      8, "ok_malloc",     "accept"),
    ("ok_calloc",      6, "ok_calloc",     "accept"),
    ("ok_realloc",     6, "ok_realloc",    "accept"),
    ("ok_sizeof",      6, "ok_sizeof",     "accept"),
    ("ok_trailing",    5, "ok_trailing",   "accept"),
    ("ok_fam",         5, "ok_fam",        "accept"),
    ("ok_stackheap",   4, "ok_stackheap",  "accept"),
    ("ok_loop",        4, "ok_loop",       "accept"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)
STAGE = None

STD = "#include <stdlib.h>\n"
SZH = "#include <stdlib.h>\n#include <stdint.h>\n"


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hv_allocsize {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


# ---------------------------------------------------------------------------
# Proven allocation-size errors (declared "reject").
# ---------------------------------------------------------------------------
def gen_malloc_short(i, rng):
    n = rng.randint(5, 16)
    alloc = n - rng.randint(1, n - 2)  # too few elements, last index is n-1
    v = rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@A@@*sizeof(int));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=p[@@LAST@@];free(p);return x==@@V@@?0:1;}",
        A=alloc, LAST=n - 1, V=v)
    return f"malloc_short_{i}", body


def gen_missing_sz(i, rng):
    n, v = rng.randint(5, 16), rng.randint(1, 64)
    if i % 2 == 0:
        alloc = n  # bytes, not n * sizeof(int)
        body = STD + tpl(
            "int main(void){int*p=malloc(@@A@@);if(!p)return 2;"
            "p[@@LAST@@]=@@V@@;free(p);return 0;}", A=alloc, LAST=n - 1, V=v)
    else:
        alloc = max(1, n // 2)
        body = STD + tpl(
            "int main(void){int*p=malloc(@@A@@*sizeof(char));if(!p)return 2;"
            "p[@@LAST@@]=@@V@@;free(p);return 0;}", A=alloc, LAST=n - 1, V=v)
    return f"missing_sz_{i}", body


def gen_sizeof_ptr(i, rng):
    n, v = rng.randint(5, 12), rng.randint(1, 64)
    if i % 2 == 0:
        # 4-int struct needs sizeof(struct)=16 per element, but sizeof(p)=8.
        body = STD + tpl(
            "struct S@@I@@{int a,b,c,d;};\n"
            "int main(void){struct S@@I@@*p=malloc(@@N@@*sizeof(p));if(!p)return 2;"
            "p[@@LAST@@].d=@@V@@;free(p);return 0;}", I=i, N=n, LAST=n - 1, V=v)
    else:
        # int* element needs 8 bytes, but sizeof(int) is used.
        body = STD + tpl(
            "int main(void){int x=@@V@@;int**p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;p[@@LAST@@]=&x;free(p);return 0;}",
            N=n, LAST=n - 1, V=v)
    return f"sizeof_ptr_{i}", body


def gen_calloc_wrong(i, rng):
    n, v = rng.randint(5, 14), rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = STD + tpl(
            "int main(void){int*p=calloc(@@N@@,sizeof(char));if(!p)return 2;"
            "p[@@LAST@@]=@@V@@;free(p);return 0;}", N=n, LAST=n - 1, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int*p=calloc(1,@@N@@);if(!p)return 2;"
            "p[@@LAST@@]=@@V@@;free(p);return 0;}", N=n, LAST=n - 1, V=v)
    else:
        body = STD + tpl(
            "struct S@@I@@{int a[4];};\n"
            "int main(void){struct S@@I@@*p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
            "p[@@LAST@@].a[3]=@@V@@;free(p);return 0;}", I=i, N=n, LAST=n - 1, V=v)
    return f"calloc_wrong_{i}", body


def gen_realloc_shr(i, rng):
    n = rng.randint(6, 16)
    m = n - rng.randint(1, n - 3)  # strictly smaller, still >= 3
    v = rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void*t=realloc(p,@@M@@*sizeof(int));if(!t){free(p);return 1;}p=t;"
        "p[@@LAST@@]=@@V@@;free(p);return 0;}", N=n, M=m, LAST=n - 1, V=v)
    return f"realloc_shr_{i}", body


def gen_realloc_growo(i, rng):
    n = rng.randint(4, 12)
    m = n + rng.randint(2, 8)
    v = rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void*t=realloc(p,@@M@@*sizeof(int));if(!t){free(p);return 1;}p=t;"
        "p[@@M@@]=@@V@@;free(p);return 0;}", N=n, M=m, V=v)
    return f"realloc_growo_{i}", body


def gen_wrap(i, rng):
    n, d, v = rng.randint(3, 12), rng.randint(1, 4), rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = SZH + tpl(
            "int main(void){size_t k=SIZE_MAX/(size_t)@@N@@+@@D@@;"
            "int*p=malloc(k*sizeof(int));if(!p)return 2;p[0]=@@V@@;free(p);return 0;}",
            N=n, D=d, V=v)
    elif shape == 1:
        body = SZH + tpl(
            "int main(void){size_t c=SIZE_MAX/(size_t)@@N@@+@@D@@;"
            "int*p=calloc(c,@@N@@);if(!p)return 2;p[0]=@@V@@;free(p);return 0;}",
            N=n, D=d, V=v)
    elif shape == 2:
        body = SZH + tpl(
            "int main(void){size_t k=(size_t)-@@N@@;"
            "void*p=malloc(k);if(p)free(p);return 0;}", N=n)
    else:
        body = SZH + tpl(
            "int main(void){size_t k=SIZE_MAX-@@D@@;"
            "int*p=malloc(k);if(!p)return 2;p[0]=@@V@@;free(p);return 0;}", D=d, V=v)
    return f"wrap_{i}", body


def gen_neg_zero(i, rng):
    n, d, v = rng.randint(2, 9), rng.randint(1, 5), rng.randint(1, 64)
    shape = i % 5
    if shape == 0:
        body = STD + tpl(
            "int main(void){void*p=malloc(-@@N@@);if(p)free(p);return 0;}", N=n)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){void*p=calloc(@@N@@,-@@D@@);if(p)free(p);return 0;}",
            N=n, D=d)
    elif shape == 2:
        body = STD + tpl(
            "int main(void){int*p=malloc(0);if(!p)return 2;p[0]=@@V@@;free(p);return 0;}",
            V=v)
    elif shape == 3:
        body = STD + tpl(
            "int main(void){void*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void*q=realloc(p,-@@D@@);if(q)free(q);else free(p);return 0;}", N=n, D=d)
    else:
        body = STD + tpl(
            "int main(void){int*p=calloc(0,sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;free(p);return 0;}", V=v)
    return f"neg_zero_{i}", body


def gen_trailing(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "struct S@@I@@{int n;int a[@@N@@];};\n"
        "int main(void){struct S@@I@@*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p->a[@@LAST@@]=@@V@@;free(p);return 0;}", I=i, N=n, LAST=n - 1, V=v)
    return f"trailing_{i}", body


def gen_fam_bad(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "struct S@@I@@{int n;int a[];};\n"
        "int main(void){struct S@@I@@*p=malloc(sizeof(struct S@@I@@));if(!p)return 2;"
        "p->a[@@N@@-1]=@@V@@;free(p);return 0;}", I=i, N=n, V=v)
    return f"fam_bad_{i}", body


def gen_stackheap(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        body = STD + tpl(
            "int main(void){char st[@@N@@];int*p=malloc(sizeof(st));if(!p)return 2;"
            "p[@@LAST@@]=@@V@@;free(p);(void)st;return 0;}", N=n, LAST=n - 1, V=v)
    elif shape == 1:
        body = STD + tpl(
            "int main(void){int st[@@N@@];int*p=malloc(@@N@@-1);if(!p)return 2;"
            "p[@@LAST@@]=@@V@@;free(p);(void)st;return 0;}", N=n, LAST=n - 1, V=v)
    else:
        body = STD + tpl(
            "int main(void){int st[@@N@@];double d=1.0;double**p=malloc(sizeof(st));"
            "if(!p)return 2;p[@@LAST@@]=&d;free(p);(void)st;return 0;}", N=n, LAST=n - 1)
    return f"stackheap_{i}", body


# ---------------------------------------------------------------------------
# Conditional / unverified violation (declared "strict").
# ---------------------------------------------------------------------------
def gen_strict_cond(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "int main(int argc,char**v){(void)v;int*p=malloc(@@N@@*sizeof(int));"
        "if(!p)return 2;p[@@LAST@@]=@@V@@;int x=p[argc>@@N@@?@@N@@:0];"
        "free(p);return x==@@V@@?0:1;}", N=n, LAST=n - 1, V=v)
    return f"strict_cond_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": must pass both modes).
# ---------------------------------------------------------------------------
def gen_ok_malloc(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=p[@@LAST@@];free(p);return x==@@V@@?0:1;}",
        N=n, LAST=n - 1, V=v)
    return f"ok_malloc_{i}", body


def gen_ok_calloc(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=p[@@LAST@@];free(p);return x==@@V@@?0:1;}",
        N=n, LAST=n - 1, V=v)
    return f"ok_calloc_{i}", body


def gen_ok_realloc(i, rng):
    n, v = rng.randint(6, 12), rng.randint(1, 64)
    if i % 2 == 0:
        m = n + rng.randint(1, 6)  # grow: write inside the new extent
        last = m - 1
    else:
        m = n - rng.randint(1, n - 3)  # shrink: honour the new extent
        last = m - 1
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void*t=realloc(p,@@M@@*sizeof(int));if(!t){free(p);return 1;}p=t;"
        "p[@@LAST@@]=@@V@@;int x=p[@@LAST@@];free(p);return x==@@V@@?0:1;}",
        N=n, M=m, LAST=last, V=v)
    return f"ok_realloc_{i}", body


def gen_ok_sizeof(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    if i % 2 == 0:
        body = STD + tpl(
            "int main(void){int x=@@V@@;int**p=malloc(@@N@@*sizeof(*p));if(!p)return 2;"
            "p[@@LAST@@]=&x;int*x2=p[@@LAST@@];free(p);return *x2==@@V@@?0:1;}",
            N=n, LAST=n - 1, V=v)
    else:
        body = STD + tpl(
            "struct S@@I@@{int a,b,c,d;};\n"
            "int main(void){struct S@@I@@*p=malloc(@@N@@*sizeof(*p));if(!p)return 2;"
            "p[@@LAST@@].d=@@V@@;int x=p[@@LAST@@].d;free(p);return x==@@V@@?0:1;}",
            I=i, N=n, LAST=n - 1, V=v)
    return f"ok_sizeof_{i}", body


def gen_ok_trailing(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "struct S@@I@@{int n;int a[@@N@@];};\n"
        "int main(void){struct S@@I@@*p=malloc(sizeof(*p));if(!p)return 2;"
        "p->a[@@LAST@@]=@@V@@;int x=p->a[@@LAST@@];free(p);return x==@@V@@?0:1;}",
        I=i, N=n, LAST=n - 1, V=v)
    return f"ok_trailing_{i}", body


def gen_ok_fam(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "struct S@@I@@{int n;int a[];};\n"
        "int main(void){struct S@@I@@*p=malloc(sizeof(struct S@@I@@)+@@N@@*sizeof(int));"
        "if(!p)return 2;p->a[@@LAST@@]=@@V@@;int x=p->a[@@LAST@@];free(p);"
        "return x==@@V@@?0:1;}", I=i, N=n, LAST=n - 1, V=v)
    return f"ok_fam_{i}", body


def gen_ok_stackheap(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){char st[@@N@@];int*p=malloc(@@N@@*sizeof(*p));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=p[@@LAST@@];free(p);(void)st;return x==@@V@@?0:1;}",
        N=n, LAST=n - 1, V=v)
    return f"ok_stackheap_{i}", body


def gen_ok_loop(i, rng):
    n = rng.randint(5, 14)
    body = STD + tpl(
        "int main(void){int*p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
        "for(int i=0;i<@@N@@;i++)p[i]=i;int x=p[@@LAST@@];free(p);"
        "return x==@@LAST@@?0:1;}", N=n, LAST=n - 1)
    return f"ok_loop_{i}", body


BUILDERS = (
    gen_malloc_short, gen_missing_sz, gen_sizeof_ptr, gen_calloc_wrong,
    gen_realloc_shr, gen_realloc_growo, gen_wrap, gen_neg_zero, gen_trailing,
    gen_fam_bad, gen_stackheap, gen_strict_cond, gen_ok_malloc, gen_ok_calloc,
    gen_ok_realloc, gen_ok_sizeof, gen_ok_trailing, gen_ok_fam,
    gen_ok_stackheap, gen_ok_loop,
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

    print(f"hv_allocsize: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:14} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hv_allocsize_"))
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
