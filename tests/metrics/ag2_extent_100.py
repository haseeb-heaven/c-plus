#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of extent-propagation C programs.

Produces 174 distinct small C programs (>=100) from per-case independent seeded
RNGs.  Generation and checking both run in thread pools; ``pool.map`` preserves
input order and every case draws from ``random.Random(f"{SEED}:{i}")``, so a run
is fully reproducible regardless of ``FUZZ_WORKERS``.  Every program carries a
unique case marker, and allocation sizes / indices / values vary per case, so
no case is a copy of another.  All programs are plain C (no C+ syntax).

This corpus is the *extent-propagation* companion to ``ag_heap_100.py``: where
that corpus measures single-function heap bounds, this one forces the allocation
extent to travel through aliases and helpers before the out-of-bounds access.

Families (case counts are listed in ``FAMILIES``):

  chain2          extent through a 2-level call chain, leaf writes p[n]
  chain3          extent through a 3-level call chain
  chain4          extent through a 4-level call chain
  ret             extent through a function return (heap factory)
  swap            extent through multiple / swapped parameter positions
  structfield     extent stored in, and read back from, a struct field
  arrayelem       extent reached through an array of pointers
  realloc_shrink  stale (larger) extent assumed after a shrinking realloc
  realloc_grow    extent assumed beyond a freshly grown realloc
  calloc          calloc(count, sizeof(T)) extent violation
  sizeof_bad      allocation in bytes with the wrong element sizeof
  str_nop1        string copy into an allocation missing the terminating +1
  fam             flexible array member indexed past its allocation
  undersized      allocation smaller than the struct being written
  underflow       negative index below the allocation base
  loop            off-by-one loop bound over heap storage
  strict_cond     argc-conditional / unverified extent violation   (strict)
  ok_within       safe control: access within the extent           (accept)
  ok_sizeparam    safe control: helper honours its size parameter  (accept)
  ok_unknown      safe control: extent/index not provable, in range(accept)
  ok_chain        safe control: size parameter through a call chain(accept)

Each case declares one expected outcome:

  reject - proven extent violation: must fail BOTH --default and --strict.
           Acceptance in either mode is a safety hole (a "miss").
  strict - conditional / unverified violation: MAY pass --default, but must
           fail --strict (acceptance in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are saved
under tests/out/core/ag2_extent/failures/.  False positives (safe controls
rejected by the checker) are counted and reported separately.  This is a
differential measurement of the checker, not a proof of heap safety.

Environment:
  FUZZ_SEED    default 0x5E7E
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
OUT = ROOT / "tests/out/core/ag2_extent"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x5E7E"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# family, number of cases, builder, declared outcome.  Case indices map
# deterministically to exactly one family in this order.
FAMILIES = (
    ("chain2",        10, "chain2",        "reject"),
    ("chain3",         8, "chain3",        "reject"),
    ("chain4",         8, "chain4",        "reject"),
    ("ret",           10, "ret",           "reject"),
    ("swap",          10, "swap",          "reject"),
    ("structfield",    9, "structfield",   "reject"),
    ("arrayelem",      9, "arrayelem",     "reject"),
    ("realloc_shrink", 8, "realloc_shrink", "reject"),
    ("realloc_grow",   8, "realloc_grow",  "reject"),
    ("calloc",         9, "calloc",        "reject"),
    ("sizeof_bad",     9, "sizeof_bad",    "reject"),
    ("str_nop1",       8, "str_nop1",      "reject"),
    ("fam",            8, "fam",           "reject"),
    ("undersized",     8, "undersized",    "reject"),
    ("underflow",      8, "underflow",     "reject"),
    ("loop",           8, "loop",          "reject"),
    ("strict_cond",    8, "strict_cond",   "strict"),
    ("ok_within",      8, "ok_within",     "accept"),
    ("ok_sizeparam",   8, "ok_sizeparam",  "accept"),
    ("ok_unknown",     6, "ok_unknown",    "accept"),
    ("ok_chain",       6, "ok_chain",      "accept"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)
STAGE = None

STD = "#include <stdlib.h>\n"
STRH = "#include <stdlib.h>\n#include <string.h>\n"
FAMH = "#include <stdlib.h>\nstruct S{int n;int a[];};\n"
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag2_extent {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def lit(n):
    """A distinct, non-empty string literal that varies with n."""
    return ALPHABET[:(n % (len(ALPHABET) - 1)) + 1]


# ---------------------------------------------------------------------------
# Extent-propagation families (declared "reject": proven violations).
# ---------------------------------------------------------------------------
def gen_chain2(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static int leaf@@I@@(int*p,int i){return p[i];}\n"
        "static int mid@@I@@(int*p,int n){return leaf@@I@@(p,n);}\n"
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int x=mid@@I@@(p,@@N@@);free(p);return x;}", I=i, N=n)
    return f"chain2_{i}", body


def gen_chain3(i, rng):
    n = rng.randint(3, 12)
    body = STD + tpl(
        "static int l3@@I@@(int*p,int i){return p[i];}\n"
        "static int l2@@I@@(int*p,int i){return l3@@I@@(p,i);}\n"
        "static int l1@@I@@(int*p,int i){return l2@@I@@(p,i);}\n"
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int x=l1@@I@@(p,@@N@@);free(p);return x;}", I=i, N=n)
    return f"chain3_{i}", body


def gen_chain4(i, rng):
    n = rng.randint(3, 12)
    body = STD + tpl(
        "static int l4@@I@@(int*p,int i){return p[i];}\n"
        "static int l3@@I@@(int*p,int i){return l4@@I@@(p,i);}\n"
        "static int l2@@I@@(int*p,int i){return l3@@I@@(p,i);}\n"
        "static int l1@@I@@(int*p,int i){return l2@@I@@(p,i);}\n"
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int x=l1@@I@@(p,@@N@@);free(p);return x;}", I=i, N=n)
    return f"chain4_{i}", body


def gen_ret(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static int*mk@@I@@(void){int*p=malloc(@@N@@*sizeof(int));return p;}\n"
        "int main(void){int*p=mk@@I@@();if(!p)return 2;p[@@N@@]=@@V@@;"
        "free(p);return 0;}", I=i, N=n, V=v)
    return f"ret_{i}", body


def gen_swap(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    shape = i % 3
    if shape == 0:  # index is the first of three parameters
        helper = tpl(
            "static int f@@I@@(int i,int*p,int junk){(void)junk;return p[i];}\n"
            "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int x=f@@I@@(@@N@@,p,@@V@@);free(p);return x;}", I=i, N=n, V=v)
    elif shape == 1:  # pointer first, index last (swapped)
        helper = tpl(
            "static int f@@I@@(int*p,int a,int i){(void)a;return p[i];}\n"
            "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int x=f@@I@@(p,@@V@@,@@N@@);free(p);return x;}", I=i, N=n, V=v)
    else:  # size and value swapped around the pointer
        helper = tpl(
            "static int f@@I@@(int n,int*p){return p[n];}\n"
            "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int x=f@@I@@(@@N@@,p);free(p);return x;}", I=i, N=n)
    return f"swap_{i}", STD + helper


def gen_structfield(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "struct B@@I@@{int*p;int n;};\n"
        "int main(void){struct B@@I@@ b;b.n=@@N@@;"
        "b.p=malloc(b.n*sizeof(int));if(!b.p)return 2;"
        "b.p[b.n]=@@V@@;free(b.p);return 0;}", I=i, N=n, V=v)
    return f"structfield_{i}", body


def gen_arrayelem(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    k = i % 3
    body = STD + tpl(
        "int main(void){int*a[3];a[@@K@@]=malloc(@@N@@*sizeof(int));"
        "if(!a[@@K@@])return 2;a[@@K@@][@@N@@]=@@V@@;"
        "free(a[@@K@@]);return 0;}", K=k, N=n, V=v)
    return f"arrayelem_{i}", body


def gen_realloc_shrink(i, rng):
    n = rng.randint(5, 14)
    m = n - rng.randint(1, n - 2)  # strictly smaller, still >= 2
    v = rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void*t=realloc(p,@@M@@*sizeof(int));if(!t)return 1;p=t;"
        "p[@@LAST@@]=@@V@@;free(p);return 0;}",
        N=n, M=m, LAST=n - 1, V=v)
    return f"realloc_shrink_{i}", body


def gen_realloc_grow(i, rng):
    n = rng.randint(3, 12)
    m = n + rng.randint(1, 8)
    v = rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void*t=realloc(p,@@M@@*sizeof(int));if(!t)return 1;p=t;"
        "p[@@M@@]=@@V@@;free(p);return 0;}", N=n, M=m, V=v)
    return f"realloc_grow_{i}", body


def gen_calloc(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    if i % 3 == 0:
        body = STD + tpl(
            "int main(void){int*p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
            "p[@@N@@]=@@V@@;free(p);return 0;}", N=n, V=v)
    elif i % 3 == 1:
        body = STD + tpl(
            "int main(void){char*p=calloc(@@N@@,sizeof(char));if(!p)return 2;"
            "p[@@N@@]='x';free(p);return 0;}", N=n)
    else:
        body = STD + tpl(
            "int main(void){int*p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
            "p[@@N@@+1]=@@V@@;free(p);return 0;}", N=n, V=v)
    return f"calloc_{i}", body


def gen_sizeof_bad(i, rng):
    n, v = rng.randint(4, 12), rng.randint(1, 64)
    ty = "char" if i % 2 == 0 else "short"
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(@@TY@@));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;free(p);return 0;}", N=n, TY=ty, LAST=n - 1, V=v)
    return f"sizeof_bad_{i}", body


def gen_str_nop1(i, rng):
    n = rng.randint(3, 16)
    s = lit(n)
    body = STRH + tpl(
        "int main(void){const char*s=\"@@S@@\";"
        "char*d=malloc(strlen(s));if(!d)return 2;"
        "strcpy(d,s);free(d);return 0;}", S=s)
    return f"str_nop1_{i}", body


def gen_fam(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = FAMH + tpl(
        "int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));"
        "if(!s)return 2;s->a[@@N@@]=@@V@@;free(s);return 0;}", N=n, V=v)
    return f"fam_{i}", body


def gen_undersized(i, rng):
    nf = rng.randint(2, 4)
    fields = "".join(f"int f{k};" for k in range(nf))
    last = f"f{nf - 1}"
    alloc = nf - 1  # one int short
    v = rng.randint(1, 64)
    body = STD + tpl(
        "struct P@@I@@{@@FIELDS@@};\n"
        "int main(void){struct P@@I@@*p=malloc(@@A@@*sizeof(int));"
        "if(!p)return 2;p->@@LAST@@=@@V@@;free(p);return 0;}",
        I=i, FIELDS=fields, A=alloc, LAST=last, V=v)
    return f"undersized_{i}", body


def gen_underflow(i, rng):
    n, off, v = rng.randint(3, 12), rng.randint(1, 4), rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[-@@O@@]=@@V@@;free(p);return 0;}", N=n, O=off, V=v)
    return f"underflow_{i}", body


def gen_loop(i, rng):
    n = rng.randint(3, 12)
    if i % 2 == 0:
        loop = tpl("for(int i=0;i<=@@N@@;i++)p[i]=i;", N=n)
    else:
        loop = tpl("for(int i=@@N@@-1;i>=0;i--)p[i+1]=i;", N=n)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "@@LOOP@@free(p);return 0;}", N=n, LOOP=loop)
    return f"loop_{i}", body


# ---------------------------------------------------------------------------
# Conditional / unverified violation (declared "strict").
# ---------------------------------------------------------------------------
def gen_strict_cond(i, rng):
    n = rng.randint(3, 12)
    body = STD + tpl(
        "static int f@@I@@(int*p,int i){return p[i];}\n"
        "int main(int argc,char**v){(void)v;int*p=malloc(@@N@@*sizeof(int));"
        "if(!p)return 2;int x=f@@I@@(p,argc>@@N@@?@@N@@:0);"
        "free(p);return x;}", I=i, N=n)
    return f"strict_cond_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": must pass both modes).
# ---------------------------------------------------------------------------
def gen_ok_within(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=p[@@LAST@@];free(p);"
        "return x==@@V@@?0:1;}", N=n, LAST=n - 1, V=v)
    return f"ok_within_{i}", body


def gen_ok_sizeparam(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static int f@@I@@(int*p,int n){return p[n-1];}\n"
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=f@@I@@(p,@@N@@);free(p);"
        "return x==@@V@@?0:1;}", I=i, N=n, LAST=n - 1, V=v)
    return f"ok_sizeparam_{i}", body


def gen_ok_unknown(i, rng):
    n = rng.randint(3, 12)
    body = STD + tpl(
        "static int f@@I@@(int*p,int i){return p[i];}\n"
        "int main(int argc,char**v){(void)v;int*p=malloc(@@N@@*sizeof(int));"
        "if(!p)return 2;int x=f@@I@@(p,argc%@@N@@);free(p);return x;}",
        I=i, N=n)
    return f"ok_unknown_{i}", body


def gen_ok_chain(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static int l2@@I@@(int*p,int n){return p[n-1];}\n"
        "static int l1@@I@@(int*p,int n){return l2@@I@@(p,n);}\n"
        "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@LAST@@]=@@V@@;int x=l1@@I@@(p,@@N@@);free(p);"
        "return x==@@V@@?0:1;}", I=i, N=n, LAST=n - 1, V=v)
    return f"ok_chain_{i}", body


BUILDERS = (
    gen_chain2, gen_chain3, gen_chain4, gen_ret, gen_swap, gen_structfield,
    gen_arrayelem, gen_realloc_shrink, gen_realloc_grow, gen_calloc,
    gen_sizeof_bad, gen_str_nop1, gen_fam, gen_undersized, gen_underflow,
    gen_loop, gen_strict_cond, gen_ok_within, gen_ok_sizeparam,
    gen_ok_unknown, gen_ok_chain,
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

    print(f"ag2_extent: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:16} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag2_extent_"))
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

    for r in mismatches[:12]:
        print(f"  {r['family']}/{r['name']} [expect={r['expect']}] "
              f"{'; '.join(r['reasons'])} | default tags={r['tags']['--default']} "
              f"strict tags={r['tags']['--strict']}")
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
