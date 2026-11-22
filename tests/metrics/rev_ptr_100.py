#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of provenance/cast-gap C programs.

Produces 124 distinct small C programs (>=100) from per-case independent seeded
RNGs.  Generation and checking both run in thread pools; ``pool.map`` preserves
input order and every case draws from ``random.Random(f"{SEED}:{i}")``, so a run
is fully reproducible regardless of ``FUZZ_WORKERS``.  Every program carries a
unique case marker plus varied sizes/indices/values, so no case is a copy of
another.  All programs are plain C (no C+ extension syntax).

This is the "reverse" pointer corpus: it concentrates on provenance-preserving
casts and round trips that are VALID C and therefore must be ACCEPTED.  A
rejection here is a false positive (a checker gap), not a caught bug.  The few
genuinely undefined families are included as negative controls with the
correct reject expectation, and two implementation-defined cast families are
declared strict.

Families (declared outcome in parentheses):

  cast_preserve  arbitrary casts that keep provenance (T*->void*->char*->T*)   (accept)
  uintptr_rt     uintptr_t round trip back to the SAME pointer                (accept)
  tagged_untag   tag bits masked/untagged back to the original pointer        (accept)
  void_access    cast a valid object pointer to void* then back and access    (accept)
  interior       interior pointer arithmetic inside one object                (accept)
  container_of   valid container_of (member address minus offsetof)           (accept)
  safe_control   safe control: plain in-bounds pointer/array/malloc use       (accept)
  past_end       pointer arithmetic past one-past-the-end (UB)                (reject)
  cross_obj      subtraction / comparison across distinct objects (UB)        (reject)
  strict_pun     strict-aliasing type punning via incompatible pointer cast    (strict)
  strict_funptr  incompatible function-pointer cast and call                  (strict)

Each case declares one expected outcome:

  reject - proven object/provenance violation: must fail BOTH --default and
           --strict.  Acceptance in either mode is a hole (miss).
  strict - conditional / implementation-defined cast rule: MAY pass --default,
           but must fail --strict.
  accept - valid control (in particular, every valid round trip): must pass
           BOTH modes.  Rejection is a false positive and is reported as such.

Mismatches are printed and the generated source plus full diagnostics are saved
under tests/out/core/rev_ptr/failures/.  This is a differential measurement of
the checker over the declared subset, not a proof of pointer safety.

Environment:
  FUZZ_SEED    default 0xB17E
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
OUT = ROOT / "tests/out/core/rev_ptr"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xB17E"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

STAGE = None

STD = "#include <stdlib.h>\n"
SZ = "#include <stdint.h>\n"
OFF = "#include <stddef.h>\n"
SZH = "#include <stdlib.h>\n#include <stdint.h>\n"
SZOFF = "#include <stdint.h>\n#include <stddef.h>\n"
STDH = "#include <stdlib.h>\n#include <stddef.h>\n"


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* rev_ptr {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


# ---------------------------------------------------------------------------
# Valid, provenance-preserving casts (declared "accept").
# ---------------------------------------------------------------------------
def gen_cast_preserve(i, rng):
    n = rng.randint(4, 14)
    m = rng.randint(2, 5)
    k = rng.randrange(n)
    j = rng.randrange(m)
    v = rng.randint(1, 64)
    v2 = rng.randint(65, 128)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int a[@@N@@]={0};a[@@K@@]=@@V@@;"
                   "void*p=a;int*q=(int*)p;return q[@@K@@];}",
                   N=n, K=k, V=v)
    elif shape == 1:
        body = tpl("int main(void){int x=@@V@@;int*p=&x;char*c=(char*)p;"
                   "void*v=(void*)c;int*q=(int*)v;return *q;}", V=v)
    elif shape == 2:
        body = tpl("int main(void){int x=@@V@@;int*p=&x;const int*cp=p;"
                   "int*q=(int*)cp;*q=@@V2@@;return *q;}", V=v, V2=v2)
    else:
        body = tpl("int main(void){int a[@@N@@][@@M@@]={0};"
                   "a[@@K@@][@@J@@]=@@V@@;int(*row)[@@M@@]=a;"
                   "void*p=row;int(*r)[@@M@@]=(int(*)[@@M@@])p;"
                   "return r[@@K@@][@@J@@];}", N=n, M=m, K=k, J=j, V=v)
    return f"cast_preserve_{i}", body


def gen_uintptr_rt(i, rng):
    n = rng.randint(4, 14)
    k = rng.randrange(n)
    v = rng.randint(1, 64)
    v2 = rng.randint(65, 128)
    ty = rng.choice(("int", "long", "short", "unsigned"))
    cast = "int" if ty == "unsigned" else ty
    shape = i % 4
    if shape == 0:
        body = SZ + tpl("int main(void){@@T@@ x=@@V@@;uintptr_t u=(uintptr_t)&x;"
                        "@@T@@*q=(@@T@@*)u;return (int)*q;}", T=ty, V=v)
    elif shape == 1:
        body = SZ + tpl("int main(void){int x=@@V@@;int*p=&x;"
                        "uintptr_t u=(uintptr_t)(void*)p;void*vp=(void*)u;"
                        "int*q=(int*)vp;return *q;}", V=v)
    elif shape == 2:
        body = SZ + tpl("int main(void){int a[@@N@@]={0};a[@@K@@]=@@V@@;"
                        "uintptr_t u=(uintptr_t)&a[@@K@@];"
                        "int*q=(int*)u;*q=@@V2@@;return *q;}",
                        N=n, K=k, V=v, V2=v2)
    else:
        body = SZ + tpl("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;"
                        "uintptr_t u=(uintptr_t)p;@@T@@*q=(@@T@@*)u;"
                        "return (int)(*q-*p+@@K@@);}", T=cast, V=v, K=k)
    return f"uintptr_rt_{i}", body


def gen_tagged_untag(i, rng):
    n = rng.randint(4, 14)
    k = rng.randrange(n)
    v = rng.randint(1, 64)
    tag = (i % 3) + 1  # low 1..2 bits; int/int[] objects are 4-byte aligned
    shape = i % 3
    if shape == 0:
        body = SZ + tpl("int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x|@@TAG@@;"
                        "int*t=(int*)(u&~(uintptr_t)@@TAG@@);return *t;}",
                        V=v, TAG=tag)
    elif shape == 1:
        body = SZH + tpl("int main(void){int*p=malloc(sizeof(int));if(!p)return 2;"
                         "*p=@@V@@;uintptr_t u=(uintptr_t)p|@@TAG@@;"
                         "int*t=(int*)(u&~(uintptr_t)@@TAG@@);int r=*t;free(p);return r;}",
                         V=v, TAG=tag)
    else:
        body = SZ + tpl("int main(void){int a[@@N@@]={0};a[@@K@@]=@@V@@;"
                        "uintptr_t u=(uintptr_t)&a[@@K@@]|@@TAG@@;"
                        "int*t=(int*)(u&~(uintptr_t)@@TAG@@);*t=@@V2@@;return *t;}",
                        N=n, K=k, V=v, V2=v + 1, TAG=tag)
    return f"tagged_untag_{i}", body


def gen_void_access(i, rng):
    n = rng.randint(4, 14)
    k = rng.randrange(n)
    v = rng.randint(1, 64)
    v2 = rng.randint(65, 128)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int x=@@V@@;void*p=&x;*(int*)p=@@V2@@;"
                   "return *(int*)p;}", V=v, V2=v2)
    elif shape == 1:
        body = tpl("int main(void){int a[@@N@@]={0};void*p=a;int*q=(int*)p;"
                   "q[@@K@@]=@@V@@;return q[@@K@@];}", N=n, K=k, V=v)
    elif shape == 2:
        body = tpl("struct S@@I@@{int a;int b;};\n"
                   "int main(void){struct S@@I@@ s;s.a=@@V@@;void*p=&s;"
                   "struct S@@I@@*q=(struct S@@I@@*)p;q->b=@@V2@@;return q->a;}",
                   I=i, V=v, V2=v2)
    else:
        body = tpl("int main(void){char b[@@N@@]={0};void*p=b;char*q=(char*)p;"
                   "q[@@K@@]=@@V@@;return q[@@K@@];}", N=n, K=k, V=v)
    return f"void_access_{i}", body


def gen_interior(i, rng):
    n = rng.randint(6, 16)
    r = rng.randint(2, 5)
    c = rng.randint(2, 5)
    k = rng.randint(1, n - 2)
    r2 = rng.randrange(r)
    c2 = rng.randrange(c)
    j = rng.randint(1, n - 2)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@K@@;*p=@@V@@;"
                   "return p[0];}", N=n, K=k, V=v)
    elif shape == 1:
        body = tpl("int main(void){int a[@@R@@][@@C@@]={0};"
                   "int*p=&a[@@R2@@][@@C2@@];*p=@@V@@;return *p;}",
                   R=r, C=c, R2=r2, C2=c2, V=v)
    elif shape == 2:
        body = STD + tpl("int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                         "int*q=p+@@K@@;q[0]=@@V@@;int x=q[0];free(p);return x;}",
                         N=n, K=k, V=v)
    else:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a;"
                   "for(int z=0;z<@@J@@;z++)p++;*p=@@V@@;return *p;}",
                   N=n, J=j, V=v)
    return f"interior_{i}", body


def gen_container_of(i, rng):
    n = rng.randint(3, 10)
    k = rng.randrange(n)
    v = rng.randint(1, 64)
    v2 = rng.randint(65, 128)
    shape = i % 4
    if shape == 0:
        body = OFF + tpl("struct S@@I@@{int a;int b[@@N@@];};\n"
                         "int main(void){struct S@@I@@ s;s.a=@@V@@;"
                         "struct S@@I@@*p=(struct S@@I@@*)((char*)&s.b"
                         "-offsetof(struct S@@I@@,b));return p->a;}",
                         I=i, N=n, V=v)
    elif shape == 1:
        body = OFF + tpl("struct S@@I@@{int a;int b[@@N@@];};\n"
                         "int main(void){struct S@@I@@ s;s.b[@@K@@]=@@V@@;"
                         "struct S@@I@@*p=(struct S@@I@@*)((char*)&s.b[@@K@@]"
                         "-offsetof(struct S@@I@@,b)-@@K@@*sizeof(int));"
                         "return p->b[@@K@@];}", I=i, N=n, K=k, V=v)
    elif shape == 2:
        body = STDH + tpl("struct S@@I@@{int a;int b[@@N@@];};\n"
                          "int main(void){struct S@@I@@*s=malloc(sizeof *s);if(!s)return 2;"
                          "s->a=@@V@@;s->b[@@K@@]=@@V2@@;"
                          "struct S@@I@@*p=(struct S@@I@@*)((char*)&s->b[@@K@@]"
                          "-offsetof(struct S@@I@@,b)-@@K@@*sizeof(int));"
                          "int r=p->a;free(s);return r;}",
                          I=i, N=n, K=k, V=v, V2=v2)
    else:
        body = OFF + tpl("struct I@@I@@{int x;int y;};"
                         "struct O@@I@@{char tag;struct I@@I@@ in;};\n"
                         "int main(void){struct O@@I@@ o;o.in.y=@@V@@;"
                         "struct O@@I@@*p=(struct O@@I@@*)((char*)&o.in.y"
                         "-offsetof(struct O@@I@@,in)-offsetof(struct I@@I@@,y));"
                         "return p->in.y;}", I=i, V=v)
    return f"container_of_{i}", body


def gen_safe_control(i, rng):
    n = rng.randint(6, 16)
    k = rng.randint(1, n - 2)
    v = rng.randint(1, 64)
    v2 = rng.randint(65, 128)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int x=@@V@@;int*p=&x;*p=@@V2@@;return *p;}",
                   V=v, V2=v2)
    elif shape == 1:
        body = tpl("int main(void){int a[@@N@@]={0};a[@@K@@]=@@V@@;return a[@@K@@];}",
                   N=n, K=k, V=v)
    elif shape == 2:
        body = STD + tpl("int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                         "p[@@K@@]=@@V@@;int x=p[@@K@@];free(p);return x;}",
                         N=n, K=k, V=v)
    else:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a;int*e=a+@@N@@;"
                   "while(p<e){*p=@@V@@;p++;}return a[@@N@@-1];}", N=n, V=v)
    return f"safe_control_{i}", body


# ---------------------------------------------------------------------------
# Genuine object/provenance violations used as negative controls ("reject").
# ---------------------------------------------------------------------------
def gen_past_end(i, rng):
    n = rng.randint(4, 14)
    k = rng.randint(1, 6)
    v = rng.randint(1, 64)
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@+@@K@@;(void)p;return 0;}",
                   N=n, K=k)
    elif shape == 1:
        body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@+@@K@@;*p=@@V@@;return 0;}",
                   N=n, K=k, V=v)
    elif shape == 2:
        body = tpl("int main(void){int a[@@N@@]={0};return a[@@N@@+@@K@@];}", N=n, K=k)
    else:
        body = STD + tpl("int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                         "int*q=p+@@N@@+@@K@@;*q=@@V@@;free(p);return 0;}",
                         N=n, K=k, V=v)
    return f"past_end_{i}", body


def gen_cross_obj(i, rng):
    n = rng.randint(4, 14)
    m = n + rng.randint(1, 9)
    a = rng.randrange(n)
    b = rng.randrange(m)
    v = rng.randint(1, 64)
    op = rng.choice(("<", ">", "<=", ">="))
    shape = i % 4
    if shape == 0:
        body = tpl("int main(void){int x[@@N@@]={0},y[@@M@@]={0};"
                   "return &x[@@A@@]-&y[@@B@@];}", N=n, M=m, A=a, B=b)
    elif shape == 1:
        body = tpl("int main(void){int x[@@N@@]={0},y[@@M@@]={0};"
                   "return &x[@@A@@] @@OP@@ &y[@@B@@];}",
                   N=n, M=m, A=a, B=b, OP=op)
    elif shape == 2:
        body = STD + tpl("int main(void){int*p=malloc(@@N@@*sizeof(int));"
                         "int*q=malloc(@@M@@*sizeof(int));if(!p||!q)return 2;"
                         "p[0]=@@V@@;q[@@B@@]=@@V@@;int d=(int)(p-q);"
                         "free(p);free(q);return d;}", N=n, M=m, B=b, V=v)
    else:
        body = tpl("int main(void){int x[@@N@@]={0},y[@@M@@]={0};"
                   "return (x+@@N@@) @@OP@@ (y+@@M@@);}", N=n, M=m, OP=op)
    return f"cross_obj_{i}", body


# ---------------------------------------------------------------------------
# Implementation-defined cast behaviour (declared "strict").
# ---------------------------------------------------------------------------
def gen_strict_pun(i, rng):
    v = rng.randint(1, 64)
    v2 = rng.randint(65, 128)
    shape = i % 3
    if shape == 0:
        body = tpl("int main(void){int x=@@V@@;float*f=(float*)&x;return (int)*f;}", V=v)
    elif shape == 1:
        body = tpl("int main(void){float x=1.0f;int*p=(int*)&x;*p=@@V@@;return 0;}", V=v)
    else:
        body = tpl("int main(void){long x=@@V@@;double*p=(double*)&x;"
                   "*p=1.0;return (int)x+@@V2@@;}", V=v, V2=v2)
    return f"strict_pun_{i}", body


def gen_strict_funptr(i, rng):
    v = rng.randint(1, 64)
    n = rng.randint(1, 9)
    shape = i % 3
    if shape == 0:
        body = tpl("typedef int(*Fi)(int);\nint g(void){return @@V@@;}\n"
                   "int main(void){Fi f=(Fi)g;return f(@@N@@);}", V=v, N=n)
    elif shape == 1:
        body = tpl("typedef void(*Fv)(void);\nint g(int x){return x+@@V@@;}\n"
                   "int main(void){Fv f=(Fv)g;f();return 0;}", V=v)
    else:
        body = tpl("typedef int(*Fii)(int,int);\nint g(int x){return x+@@V@@;}\n"
                   "int main(void){Fii f=(Fii)g;return f(@@V@@,@@N@@);}", V=v, N=n)
    return f"strict_funptr_{i}", body


# family, number of cases, builder, declared outcome.  Defined after the
# builders so each name is bound when the table is constructed.
FAMILIES = (
    ("cast_preserve", 12, gen_cast_preserve, "accept"),
    ("uintptr_rt",    12, gen_uintptr_rt,    "accept"),
    ("tagged_untag",  12, gen_tagged_untag,  "accept"),
    ("void_access",   12, gen_void_access,   "accept"),
    ("interior",      12, gen_interior,      "accept"),
    ("container_of",  12, gen_container_of,  "accept"),
    ("safe_control",  12, gen_safe_control,  "accept"),
    ("past_end",      12, gen_past_end,      "reject"),
    ("cross_obj",     12, gen_cross_obj,     "reject"),
    ("strict_pun",     8, gen_strict_pun,    "strict"),
    ("strict_funptr",  8, gen_strict_funptr, "strict"),
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
            reasons.append("valid cast/round-trip rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("valid cast/round-trip rejected in STRICT (false positive)")

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

    print(f"rev_ptr: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:14} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="rev_ptr_"))
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
    print("false positives (valid cast/round-trip rejected): "
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
