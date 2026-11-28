#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of realloc-correctness C programs.

Produces 162 distinct small C programs (>=100) from per-case independent seeded
RNGs.  Generation and checking both run in thread pools; ``pool.map`` preserves
input order and every case draws from ``random.Random(f"{SEED}:{i}")``, so a run
is fully reproducible regardless of ``FUZZ_WORKERS``.  No case is copied from
another corpus.  All programs are plain C (no C+ extension syntax).

Families (every family targets a distinct realloc-misuse mechanism):

  loss         raw ``p = realloc(p, n)`` loses the original block on failure
  loss_guard   same raw overwrite plus a useless NULL check afterwards
  alias        stale alias used after a successful realloc moved the block
  orig_fail    on realloc failure, original is written past its old extent
  zero         ``realloc(p, 0)`` followed by a use / release of the original
  neg          negative size argument cast to size_t
  huge         absurd SIZE_MAX-based size argument
  wrap         count*size wraps around into a small allocation
  interior     interior pointer into the block used after a successful realloc
  extent       old (larger) extent assumed after a shrinking realloc
  param        raw pointer loss inside a called function via int ** parameter
  double       release of both the original and the returned pointer
  badptr       realloc applied to stack / global / string-literal storage
  safe         valid controls: temp + NULL check, explicit size check, safe
               helper parameter                                  (accept)
  strict       argc-conditional / unverified size arguments      (strict)

Each case declares one expected outcome:

  reject - proven realloc error: must fail BOTH --default and --strict.
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (a pass in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are saved
under tests/out/core/ag_realloc/failures/.  False positives (safe controls
rejected by the checker) are counted and reported separately.  This is a
differential measurement, not a proof of realloc safety.

Environment:
  FUZZ_SEED    default 0xA110C
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
OUT = ROOT / "tests/out/core/ag_realloc"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA110C"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases, builder, declared outcome.  Case indices map
# deterministically to exactly one family in this order.
FAMILIES = (
    ("loss",       10, "loss",       "reject"),
    ("loss_guard",  8, "loss_guard", "reject"),
    ("alias",      12, "alias",      "strict"),
    ("orig_fail",  10, "orig_fail",  "strict"),
    ("zero",       10, "zero",       "reject"),
    ("neg",        10, "neg",        "reject"),
    ("huge",       10, "huge",       "reject"),
    ("wrap",       10, "wrap",       "reject"),
    ("interior",   12, "interior",   "strict"),
    ("extent",     10, "extent",     "strict"),
    ("param",      10, "param",      "reject"),
    ("double",     10, "double",     "reject"),
    ("badptr",     12, "badptr",     "reject"),
    ("safe",       20, "safe",       "accept"),
    ("strict",      8, "strict",     "strict"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)
STAGE = None

STD = "#include <stdlib.h>\n"
SZH = "#include <stdlib.h>\n#include <stdint.h>\n"
STRH = "#include <stdlib.h>\n#include <string.h>\n"
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag_realloc {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def lit(n):
    """A distinct, non-empty string literal that varies with n."""
    return ALPHABET[:(n % (len(ALPHABET) - 1)) + 1]


# ---------------------------------------------------------------------------
# Unsafe / conditional families.
# ---------------------------------------------------------------------------
def gen_loss(i, rng):
    n = rng.randint(2, 12)
    body = STD + (
        f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
        f"p=realloc(p,({n}+4)*sizeof(int));free(p);return 0;}}"
    )
    return f"loss_{i}", body


def gen_loss_guard(i, rng):
    n = rng.randint(2, 12)
    rc = rng.randint(1, 9)
    body = STD + (
        f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
        f"p=realloc(p,({n}+4)*sizeof(int));if(!p)return {rc};"
        "free(p);return 0;}"
    )
    return f"loss_guard_{i}", body


def gen_alias(i, rng):
    n, v = rng.randint(2, 12), rng.randint(1, 60)
    body = STD + (
        f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
        f"int *q=p;void *t=realloc(p,({n}+8)*sizeof(int));"
        f"if(t){{p=t;p[0]={v};q[0]={v};}}free(p);return 0;}}"
    )
    return f"alias_{i}", body


def gen_orig_fail(i, rng):
    n, v, big = rng.randint(2, 12), rng.randint(1, 60), rng.randint(64, 512)
    body = STD + (
        f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
        f"void *t=realloc(p,{big}*sizeof(int));"
        f"if(!t){{p[{n}]={v};free(p);return 1;}}free(t);return 0;}}"
    )
    return f"orig_fail_{i}", body


def gen_zero(i, rng):
    n, v = rng.randint(2, 12), rng.randint(1, 60)
    kind = i % 3
    if kind == 0:
        body = STD + (
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,0);free(p);(void)t;return 0;}"
        )
    elif kind == 1:
        body = STD + (
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"p=realloc(p,0);if(p)*p={v};free(p);return 0;}}"
        )
    else:
        body = STD + (
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,0);if(t)free(t);free(p);return 0;}"
        )
    return f"zero_{i}", body


def gen_neg(i, rng):
    n = rng.randint(1, 16)
    body = STD + (
        f"int main(void){{int *p=malloc(8);if(!p)return 2;"
        f"long k=-{n};void *t=realloc(p,(size_t)k);"
        "if(t)free(t);else free(p);return 0;}"
    )
    return f"neg_{i}", body


def gen_huge(i, rng):
    n = rng.randint(1, 16)
    body = SZH + (
        f"int main(void){{int *p=malloc(8);if(!p)return 2;"
        f"void *t=realloc(p,SIZE_MAX-{n});"
        "if(t)free(t);else free(p);return 0;}"
    )
    return f"huge_{i}", body


def gen_wrap(i, rng):
    n, off = rng.randint(2, 16), rng.randint(1, 8)
    body = SZH + (
        f"int main(void){{int *p=malloc(8);if(!p)return 2;"
        f"size_t c=SIZE_MAX/(size_t){n}+{off};"
        f"void *t=realloc(p,c*(size_t){n});"
        "if(t)free(t);else free(p);return 0;}"
    )
    return f"wrap_{i}", body


def gen_interior(i, rng):
    n, off, v = rng.randint(2, 12), rng.randint(1, 6), rng.randint(1, 60)
    body = STD + (
        f"int main(void){{int *p=malloc(({n}+8)*sizeof(int));if(!p)return 2;"
        f"int *q=p+{off};void *t=realloc(p,({n}+32)*sizeof(int));"
        f"if(t){{q[0]={v};free(t);}}return 0;}}"
    )
    return f"interior_{i}", body


def gen_extent(i, rng):
    old, v = rng.randint(8, 24), rng.randint(1, 60)
    half = old // 2  # compute in Python: '//' is a comment token in C
    body = STD + (
        f"int main(void){{int *p=malloc({old}*sizeof(int));if(!p)return 2;"
        f"int v0=p[{old}-1];void *t=realloc(p,{half}*sizeof(int));"
        f"if(t){{p=t;p[{old}-1]={v};}}free(p);return v0==0?0:1;}}"
    )
    return f"extent_{i}", body


def gen_param(i, rng):
    n = rng.randint(2, 12)
    body = STD + (
        f"static void grow_{i}(int **pp){{*pp=realloc(*pp,({n}+8)*sizeof(int));}}\n"
        f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
        f"grow_{i}(&p);free(p);return 0;}}"
    )
    return f"param_{i}", body


def gen_double(i, rng):
    n = rng.randint(2, 12)
    kind = i % 2
    seq = "free(p);free(t);" if kind == 0 else "free(t);free(p);"
    body = STD + (
        f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
        f"void *t=realloc(p,({n}+8)*sizeof(int));if(t){{{seq}}}return 0;}}"
    )
    return f"double_{i}", body


def gen_badptr(i, rng):
    n = rng.randint(2, 12)
    kind = i % 3
    if kind == 0:
        body = STD + (
            f"int main(void){{int a[{n}+2];void *t=realloc(a,64);(void)t;return 0;}}"
        )
    elif kind == 1:
        body = STD + (
            f"static int g[{n}+2];int main(void){{void *t=realloc(g,64);"
            "(void)t;return 0;}"
        )
    else:
        s = lit(n)
        body = STD + (
            f'int main(void){{const char *s="{s}";'
            "void *t=realloc((void*)s,64);(void)t;return 0;}"
        )
    return f"badptr_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (must pass both modes).
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    n, v = rng.randint(2, 12), rng.randint(1, 60)
    shape = i % 4
    if shape == 0:  # temp + NULL check + use of the new extent
        body = STD + (
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"void *t=realloc(p,({n}+8)*sizeof(int));"
            f"if(!t){{free(p);return 1;}}p=t;p[{n}+7]={v};"
            f"int x=p[{n}+7];free(p);return x=={v}?0:1;}}"
        )
    elif shape == 1:  # explicit size check before realloc
        body = SZH + (
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"size_t sz=(size_t)({n}+8)*sizeof(int);"
            "if(sz>SIZE_MAX/2){free(p);return 1;}"
            "void *t=realloc(p,sz);if(!t){free(p);return 1;}"
            f"p=t;p[0]={v};int x=p[0];free(p);return x=={v}?0:1;}}"
        )
    elif shape == 2:  # original freed only on failure; returned block on success
        body = STD + (
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"void *t=realloc(p,({n}+4)*sizeof(int));"
            f"if(!t){{free(p);return 1;}}p=t;p[0]={v};free(p);return 0;}}"
        )
    else:  # safe growth helper through an int ** parameter
        body = STD + (
            f"static int grow_{i}(int **pp){{"
            f"void *t=realloc(*pp,({n}+8)*sizeof(int));"
            "if(!t)return 0;*pp=t;return 1;}\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"if(!grow_{i}(&p)){{free(p);return 1;}}"
            f"p[0]={v};int x=p[0];free(p);return x=={v}?0:1;}}"
        )
    return f"safe_{i}", body


# Conditional / unverified size arguments (must fail --strict).
def gen_strict(i, rng):
    n = rng.randint(1, 16)
    body = STD + (
        f"static void *g_{i}(long k){{void *p=malloc(8);if(!p)return 0;"
        "void *t=realloc(p,(size_t)k);if(t)return t;return p;}\n"
        f"int main(int argc,char **argv){{(void)argv;void *r=g_{i}(argc?-{n}:16);"
        "if(r)free(r);return 0;}"
    )
    return f"strict_{i}", body


BUILDERS = (
    gen_loss, gen_loss_guard, gen_alias, gen_orig_fail, gen_zero,
    gen_neg, gen_huge, gen_wrap, gen_interior, gen_extent, gen_param,
    gen_double, gen_badptr, gen_safe, gen_strict,
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

    print(f"ag_realloc: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:11} {families[family]:3d}  (expect {dict((f, e) for f, c, b, e in FAMILIES)[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag_realloc_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    mismatches = [r for r in results if r["reasons"]]
    mismatches.sort(key=lambda r: r["name"])
    false_positives = [r for r in mismatches if r["expect"] == "accept"]
    misses = [r for r in mismatches if r["expect"] in ("reject", "strict")]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"({len(misses)} misses / unsafe-accepted, {len(false_positives)} false positives)")
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
    return 1 if mismatches or unique != len(cases) else 0


if __name__ == "__main__":
    sys.exit(main())
