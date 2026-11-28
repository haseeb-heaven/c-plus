#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of interprocedural extent C programs.

Produces 148 distinct small C programs (>=100) that test one core property:
an allocation with a KNOWN BYTE EXTENT is handed to another function, which
then accesses beyond that extent.  The pointer/extent travels through call
chains, returns, parameter shuffles, struct fields, arrays, realloc and
calloc before the offending access, so a checker must propagate allocation
extents *across function boundaries*, not just within one body.

Generation and checking both run in thread pools.  ``pool.map`` preserves
input order and every case draws from ``random.Random(f"{SEED}:{i}")``, so a
run is fully reproducible regardless of ``FUZZ_WORKERS``.  Every program has
a unique case marker plus case-suffixed helper names, so no case is a copy of
another.  All programs are plain C (no C+ syntax).

Families (counts are in FAMILIES):

  pass_direct     known-size malloc passed to a 1-level callee writing p[n]
  chain2/3/4      extent carried through 2/3/4-level call chains
  ret_factory     callee allocates a known extent, returns it, writer uses it
  swap_params     extent through multiple / swapped parameter positions
  struct_field    extent pointer+index stored in / read back from a struct
  array_elem      extent reached through an array of pointers in a callee
  realloc_shrink  callee uses a stale (larger) extent after a shrink realloc
  realloc_grow    callee writes beyond a freshly grown realloc's new extent
  calloc_oob      calloc(count, sizeof(T)) extent violated inside a callee
  strict_cond     argc-conditional / unverified violation          (strict)
  ok_within       safe control: callee stays within the extent     (accept)
  ok_sizeparam    safe control: callee honours its size parameter  (accept)
  ok_unknown      safe control: extent/index not statically known  (accept)
  ok_chain        safe control: size parameter through a call chain(accept)

Each case declares one expected outcome (same convention as the sibling
ag2_extent_100 / fuzz_interproc_300 generators):

  reject - proven extent violation: must fail BOTH --default and --strict;
           acceptance in either mode is a safety hole (a "miss").
  strict - conditional / unverified violation: MAY pass --default, but must
           fail --strict (acceptance in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full both-mode
diagnostics are saved under tests/out/core/hv_interproc/failures/.  False
positives (safe controls rejected by the checker) are counted and reported
separately.  This is a differential measurement of the checker, not a proof
of interprocedural bounds safety.

Environment:
  FUZZ_SEED    default 0x48564C50
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
OUT = ROOT / "tests/out/core/hv_interproc"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x48564C50"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# family, number of cases, builder name, declared outcome.  Case indices map
# deterministically to exactly one family in this order.
FAMILIES = (
    ("pass_direct",    10, "pass_direct",    "reject"),
    ("chain2",         10, "chain2",         "reject"),
    ("chain3",         10, "chain3",         "reject"),
    ("chain4",          8, "chain4",         "reject"),
    ("ret_factory",    10, "ret_factory",    "reject"),
    ("swap_params",    10, "swap_params",    "reject"),
    ("struct_field",   10, "struct_field",   "reject"),
    ("array_elem",     10, "array_elem",     "reject"),
    ("realloc_shrink",  8, "realloc_shrink", "reject"),
    ("realloc_grow",    8, "realloc_grow",   "reject"),
    ("calloc_oob",     10, "calloc_oob",     "reject"),
    ("strict_cond",     8, "strict_cond",    "strict"),
    ("ok_within",      10, "ok_within",      "accept"),
    ("ok_sizeparam",   10, "ok_sizeparam",   "accept"),
    ("ok_unknown",      8, "ok_unknown",     "accept"),
    ("ok_chain",        8, "ok_chain",       "accept"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)
STAGE = None

STD = "#include <stdlib.h>\n"


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hv_interproc {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


# ---------------------------------------------------------------------------
# Proven extent violations (declared "reject").
# ---------------------------------------------------------------------------
def gen_pass_direct(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static void wr@@I@@(int*p){ p[@@N@@]=@@V@@; }\n"
        "int main(void){ int*p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
        "wr@@I@@(p); free(p); return 0; }", I=i, N=n, V=v)
    return f"pass_direct_{i}", body


def _chain(i, rng, depth, family):
    """Build a depth-level chain; the leaf writes p[n], n == element count."""
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    names = [f"l{k}_{i}" for k in range(depth)]
    lines = [f"static void {names[-1]}(int*p){{ p[{n}]={v}; }}"]
    for k in range(depth - 2, -1, -1):
        lines.append(f"static void {names[k]}(int*p){{ {names[k + 1]}(p); }}")
    lines.append(f"int main(void){{ int*p=malloc({n}*sizeof(int)); if(!p)return 2; "
                 f"{names[0]}(p); free(p); return 0; }}")
    return f"{family}_{i}", STD + "\n".join(lines) + "\n"


def gen_chain2(i, rng):
    return _chain(i, rng, 2, "chain2")


def gen_chain3(i, rng):
    return _chain(i, rng, 3, "chain3")


def gen_chain4(i, rng):
    return _chain(i, rng, 4, "chain4")


def gen_ret_factory(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    if i % 2 == 0:  # caller writes beyond the returned extent directly
        head, tail = "", tpl("p[@@N@@]=@@V@@;", N=n, V=v)
    else:  # a second callee writes beyond the returned extent
        head = tpl("static void wr@@I@@(int*p){ p[@@N@@]=@@V@@; }\n",
                   I=i, N=n, V=v)
        tail = f"wr{i}(p);"
    body = STD + head + tpl(
        "static int* mk@@I@@(void){ return malloc(@@N@@*sizeof(int)); }\n"
        "int main(void){ int*p=mk@@I@@(); if(!p)return 2; @@TAIL@@ free(p); return 0; }",
        I=i, N=n, TAIL=tail)
    return f"ret_factory_{i}", body


def gen_swap_params(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    shape = i % 3
    if shape == 0:  # index is the first of three parameters
        helper = tpl(
            "static void f@@I@@(int idx,int*p,int junk){(void)junk;p[@@N@@]=@@V@@;}\n"
            "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "f@@I@@(@@N@@,p,@@V@@);free(p);return 0;}", I=i, N=n, V=v)
    elif shape == 1:  # pointer first, index last (swapped)
        helper = tpl(
            "static void f@@I@@(int*p,int junk,int idx){(void)junk;(void)idx;p[@@N@@]=@@V@@;}\n"
            "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "f@@I@@(p,@@V@@,@@N@@);free(p);return 0;}", I=i, N=n, V=v)
    else:  # two allocations; the smaller one is written via a swapped arg
        helper = tpl(
            "static void f@@I@@(int*big,int*small){(void)big;small[@@N@@]=@@V@@;}\n"
            "int main(void){int*a=malloc((@@N@@+8)*sizeof(int));"
            "int*b=malloc(@@N@@*sizeof(int));if(!a||!b)return 2;"
            "f@@I@@(a,b);free(a);free(b);return 0;}", I=i, N=n, V=v)
    return f"swap_params_{i}", STD + helper


def gen_struct_field(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    st = f"S{i}"
    decl = f"struct {st}{{int *p; int idx;}};\n"
    if i % 2 == 0:  # struct passed by pointer
        helper = tpl(
            "static void f@@I@@(struct @@ST@@ *s){ s->p[s->idx]=@@V@@; }\n"
            "int main(void){ struct @@ST@@ s; s.p=malloc(@@N@@*sizeof(int)); "
            "s.idx=@@N@@; if(!s.p)return 2; f@@I@@(&s); free(s.p); return 0; }",
            I=i, ST=st, N=n, V=v)
    else:  # struct passed by value
        helper = tpl(
            "static void f@@I@@(struct @@ST@@ s){ s.p[s.idx]=@@V@@; }\n"
            "int main(void){ struct @@ST@@ s; s.p=malloc(@@N@@*sizeof(int)); "
            "s.idx=@@N@@; if(!s.p)return 2; f@@I@@(s); free(s.p); return 0; }",
            I=i, ST=st, N=n, V=v)
    return f"struct_field_{i}", STD + decl + helper


def gen_array_elem(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    k = rng.randint(0, 2)
    body = STD + tpl(
        "static void f@@I@@(int **a,int k,int idx){ a[k][idx]=@@V@@; }\n"
        "int main(void){ int *a[3]; a[0]=a[1]=a[2]=0; a[@@K@@]=malloc(@@N@@*sizeof(int)); "
        "if(!a[@@K@@])return 2; f@@I@@(a,@@K@@,@@N@@); free(a[@@K@@]); return 0; }",
        I=i, K=k, N=n, V=v)
    return f"array_elem_{i}", body


def gen_realloc_shrink(i, rng):
    n = rng.randint(5, 14)
    m = rng.randint(2, n - 1)          # strictly smaller extent
    last = rng.randint(m, n - 1)       # index beyond the new extent
    v = rng.randint(1, 64)
    body = STD + tpl(
        "static void wr@@I@@(int*p,int idx){ p[idx]=@@V@@; }\n"
        "int main(void){ int*p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
        "int*q=realloc(p,@@M@@*sizeof(int)); if(!q){free(p);return 1;} p=q; "
        "wr@@I@@(p,@@LAST@@); free(p); return 0; }",
        I=i, N=n, M=m, LAST=last, V=v)
    return f"realloc_shrink_{i}", body


def gen_realloc_grow(i, rng):
    n = rng.randint(3, 12)
    m = n + rng.randint(2, 8)          # strictly larger extent
    idx = m + rng.randint(0, 2)        # beyond the new extent
    v = rng.randint(1, 64)
    body = STD + tpl(
        "static void wr@@I@@(int*p,int idx){ p[idx]=@@V@@; }\n"
        "int main(void){ int*p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
        "int*q=realloc(p,@@M@@*sizeof(int)); if(!q){free(p);return 1;} p=q; "
        "wr@@I@@(p,@@IDX@@); free(p); return 0; }",
        I=i, N=n, M=m, IDX=idx, V=v)
    return f"realloc_grow_{i}", body


def gen_calloc_oob(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    shape = i % 3
    if shape == 0:
        alloc, idx = f"calloc({n},sizeof(int))", n
    elif shape == 1:  # char allocation treated as int storage
        alloc, idx = f"calloc({n},sizeof(char))", n
    else:
        alloc, idx = f"calloc({n},sizeof(int))", n + 1
    body = STD + tpl(
        "static void f@@I@@(int*p,int idx){ p[idx]=@@V@@; }\n"
        "int main(void){ int*p=@@ALLOC@@; if(!p)return 2; f@@I@@(p,@@IDX@@); "
        "free(p); return 0; }", I=i, ALLOC=alloc, IDX=idx, V=v)
    return f"calloc_oob_{i}", body


# ---------------------------------------------------------------------------
# Conditional / unverified violation (declared "strict").
# ---------------------------------------------------------------------------
def gen_strict_cond(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static void wr@@I@@(int*p,int idx){ p[idx]=@@V@@; }\n"
        "int main(int argc,char**v){(void)v; int*p=malloc(@@N@@*sizeof(int)); "
        "if(!p)return 2; wr@@I@@(p,argc>@@N@@?@@N@@:0); free(p); return 0; }",
        I=i, N=n, V=v)
    return f"strict_cond_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": must pass both modes).
# ---------------------------------------------------------------------------
def gen_ok_within(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    last = n - 1
    body = STD + tpl(
        "static int f@@I@@(int*p,int i){ return p[i]; }\n"
        "int main(void){ int*p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
        "p[@@LAST@@]=@@V@@; int x=f@@I@@(p,@@LAST@@); free(p); return x==@@V@@?0:1; }",
        I=i, N=n, LAST=last, V=v)
    return f"ok_within_{i}", body


def gen_ok_sizeparam(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    last = n - 1
    body = STD + tpl(
        "static int f@@I@@(int*p,int n){ return p[n-1]; }\n"
        "int main(void){ int*p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
        "p[@@LAST@@]=@@V@@; int x=f@@I@@(p,@@N@@); free(p); return x==@@V@@?0:1; }",
        I=i, N=n, LAST=last, V=v)
    return f"ok_sizeparam_{i}", body


def gen_ok_unknown(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    body = STD + tpl(
        "static int f@@I@@(int*p,int i){ return p[i]; }\n"
        "int main(int argc,char**v){(void)v; int*p=malloc(@@N@@*sizeof(int)); "
        "if(!p)return 2; p[0]=@@V@@; int x=f@@I@@(p,argc%@@N@@); free(p); return x; }",
        I=i, N=n, V=v)
    return f"ok_unknown_{i}", body


def gen_ok_chain(i, rng):
    n, v = rng.randint(3, 12), rng.randint(1, 64)
    last = n - 1
    body = STD + tpl(
        "static int l2@@I@@(int*p,int n){ return p[n-1]; }\n"
        "static int l1@@I@@(int*p,int n){ return l2@@I@@(p,n); }\n"
        "int main(void){ int*p=malloc(@@N@@*sizeof(int)); if(!p)return 2; "
        "p[@@LAST@@]=@@V@@; int x=l1@@I@@(p,@@N@@); free(p); return x==@@V@@?0:1; }",
        I=i, N=n, LAST=last, V=v)
    return f"ok_chain_{i}", body


BUILDERS = (
    gen_pass_direct, gen_chain2, gen_chain3, gen_chain4, gen_ret_factory,
    gen_swap_params, gen_struct_field, gen_array_elem, gen_realloc_shrink,
    gen_realloc_grow, gen_calloc_oob, gen_strict_cond, gen_ok_within,
    gen_ok_sizeparam, gen_ok_unknown, gen_ok_chain,
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

    print(f"hv_interproc: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:16} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hv_interproc_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    mismatches = [r for r in results if r["reasons"]]
    mismatches.sort(key=lambda r: r["name"])
    false_positives = [r for r in mismatches if r["expect"] == "accept"]
    misses = [r for r in mismatches if r["expect"] in ("reject", "strict")]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"({len(misses)} safety holes / unsafe-accepted, "
          f"{len(false_positives)} false positives)")
    print("false positives (safe rejected): "
          + ("none" if not false_positives else
             ", ".join(f"{r['family']}/{r['name']}" for r in false_positives)))
    by_family = collections.Counter(r["family"] for r in mismatches)
    print("mismatches by family: "
          + ("none" if not by_family else
             " ".join(f"{k}={by_family[k]}" for k in sorted(by_family))))

    print("first 10 mismatch names:")
    for r in mismatches[:10]:
        print(f"  {r['name']} [{r['family']}, expect={r['expect']}] "
              f"{'; '.join(r['reasons'])}")
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
