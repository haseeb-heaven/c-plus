#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of conditional use-after-free gaps (112).

This is the "rev_branch" corpus: small, self-contained, syntactically valid C
programs that target path-sensitive use-after-free *gaps* -- a pointer is
released on only one control-flow path (if branch, switch case, loop iteration,
nested join, setjmp/longjmp edge, goto edge, or conditional alias) and then
dereferenced after the paths merge.  Safe controls guard every path (free +
null + guard, or early return) and must be accepted.

Every program is built from a per-case independent seeded RNG:
``random.Random(f"{SEED}:{i}")``.  Generation and verification both run in
thread pools; ``pool.map`` preserves input order, so a run is fully
reproducible regardless of ``FUZZ_WORKERS``.  A unique marker comment plus
seeded size / value / condition parameters make every generated program
byte-distinct -- no case is shared with another corpus.

Families (each targets one of the requested categories):

  if_merge      free in an if-branch, deref after the merge          (reject/strict)
  switch_free   free inside a switch case, deref after the switch    (reject/strict)
  loop_free     conditional free inside a loop, deref after the loop (reject/strict)
  nested_join   free in nested branches, deref after the outer join  (reject/strict)
  setjmp_uaf    free on a longjmp edge, deref after setjmp returns   (reject/strict)
  goto_skip     free on a goto edge, deref at the merge label        (reject/strict)
  alias_uaf     free through one alias, deref via the stale alias    (reject/strict)
  safe_guarded  all paths guarded (free+null+guard / early return)   (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject   - proven conditional UAF (constant-taken release path): must fail
             BOTH modes.
  strict   - runtime-dependent (argc-driven) release path: MAY pass --default,
             but must fail --strict (accepting it in STRICT is a safety hole).
  accept   - valid control: must pass BOTH modes (rejection is a false positive).

Expectations describe CORRECT C semantics, not the current checker's behaviour.
A case the checker misses is a false negative (unsafe accepted); a safe case it
rejects is a false positive.  Programs rejected only for a non-safety reason
are flagged separately as malformed generator cases.

Mismatches are printed and the generated source plus full diagnostics are saved
under tests/out/core/rev_branch/failures/.  This is a differential measurement,
not a proof of safety.

Environment:
  FUZZ_SEED    default 0x7EAF
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
OUT = ROOT / "tests/out/core/rev_branch"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x7EAF"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name -> number of cases (14 each, 112 total).
FAMILIES = (
    ("if_merge", 14),
    ("switch_free", 14),
    ("loop_free", 14),
    ("nested_join", 14),
    ("setjmp_uaf", 14),
    ("goto_skip", 14),
    ("alias_uaf", 14),
    ("safe_guarded", 14),
)
TOTAL = sum(count for _name, count in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
SJ = "#include <setjmp.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* rev_branch {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def mkmain(needs_argc, inner):
    """Wrap an inner main body with the right (argc-driven or plain) signature."""
    sig = "int main(int argc,char **argv){(void)argv;" if needs_argc else "int main(void){"
    return sig + inner + "}\n"


def cond_spec(shape, rng):
    """Return (condition expression, expectation, needs_argc, seeded value).

    shape 0 uses a constant-true condition: the release path is provably taken,
    so the UAF is unconditional and a correct checker must reject in BOTH modes
    ("reject").  Shapes 1..3 use an argc-driven condition: the release path is
    runtime-dependent, so accepting in --strict is the gap ("strict").
    """
    k = rng.randint(1, 9)
    if shape == 0:
        return "1", "reject", False, k
    if shape == 1:
        return "argc>0", "strict", True, k
    if shape == 2:
        return f"argc!={k}", "strict", True, k
    return f"argc=={k}", "strict", True, k


# ---------------------------------------------------------------------------
# 1. free in an if-branch, then dereference after the merge.
# ---------------------------------------------------------------------------
def gen_if_merge(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    v2 = rng.randint(1, 30)
    shape = i % 4
    cond, expect, argc, _k = cond_spec(shape, rng)
    name = f"if_merge_{shape}_{i}"
    inner = tpl(
        "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
        "if(@@C@@) free(p);"
        "return *p+@@V2@@;", N=n, V=v, V2=v2, C=cond)
    return name, marked(name, STD + mkmain(argc, inner)), expect


# ---------------------------------------------------------------------------
# 2. free inside a switch case, then dereference after the switch.
# ---------------------------------------------------------------------------
def gen_switch_free(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    v2 = rng.randint(1, 30)
    k1 = rng.randint(1, 3)
    k2 = k1 + rng.randint(1, 3)
    shape = i % 4
    cond, expect, argc, _k = cond_spec(shape, rng)
    if shape == 0:  # constant selector hits the freeing case -> proven
        sel, k1 = "1", 1
        k2 = k1 + 1
        argc = False
    else:
        sel = "argc"
    name = f"switch_free_{shape}_{i}"
    inner = tpl(
        "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
        "switch(@@S@@){case @@K1@@: free(p);break;"
        "case @@K2@@: *p=@@V2@@;break;default:break;}"
        "return *p;", N=n, V=v, V2=v2, S=sel, K1=k1, K2=k2)
    return name, marked(name, STD + mkmain(argc, inner)), expect


# ---------------------------------------------------------------------------
# 3. conditional free inside a loop, then dereference after the loop.
# ---------------------------------------------------------------------------
def gen_loop_free(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    loops = rng.randint(3, 9)
    kk = rng.randint(0, loops - 1)
    shape = i % 4
    guard, expect, argc, _k = cond_spec(shape, rng)
    name = f"loop_free_{shape}_{i}"
    inner = tpl(
        "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
        "for(int j=0;j<@@L@@;j++){if(@@C@@ && j==@@KK@@) free(p);}"
        "return *p+1;", N=n, V=v, L=loops, C=guard, KK=kk)
    return name, marked(name, STD + mkmain(argc, inner)), expect


# ---------------------------------------------------------------------------
# 4. free in nested branches, then dereference after the outer join.
# ---------------------------------------------------------------------------
def gen_nested_join(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    v2 = rng.randint(1, 30)
    v3 = rng.randint(1, 30)
    shape = i % 4
    c1, expect, argc, k = cond_spec(shape, rng)
    if shape == 0:
        c2 = "1"
    else:
        c2 = f"argc!={k + 1}"
    name = f"nested_join_{shape}_{i}"
    inner = tpl(
        "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
        "if(@@C1@@){if(@@C2@@){free(p);}else{*p=@@V2@@;}}else{*p=@@V3@@;}"
        "return *p;", N=n, V=v, V2=v2, V3=v3, C1=c1, C2=c2)
    return name, marked(name, STD + mkmain(argc, inner)), expect


# ---------------------------------------------------------------------------
# 5. free on a setjmp/longjmp edge, then dereference after setjmp returns.
# ---------------------------------------------------------------------------
def gen_setjmp_uaf(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 4
    cond, expect, argc, _k = cond_spec(shape, rng)
    jb = f"JB_{i}"
    name = f"setjmp_uaf_{shape}_{i}"
    inner = tpl(
        "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
        "if(setjmp(@@JB@@)==0){if(@@C@@) longjmp(@@JB@@,1);}"
        "else{free(p);}"
        "return *p;", N=n, V=v, JB=jb, C=cond)
    body = STD + SJ + f"static jmp_buf {jb};\n" + mkmain(argc, inner)
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# 6. free on a goto edge, then dereference at the merge label.
# ---------------------------------------------------------------------------
def gen_goto_skip(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    v2 = rng.randint(1, 30)
    loops = rng.randint(3, 8)
    shape = i % 4
    cond, expect, argc, _k = cond_spec(shape, rng)
    lf = f"Lf_{i}"
    lu = f"Lu_{i}"
    lb = f"Lb_{i}"
    name = f"goto_skip_{shape}_{i}"
    if shape == 3:  # back-edge goto variant
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int j=0;@@LB@@: if(j<@@L@@){j++;if(@@C@@) goto @@LB@@;}"
            "if(@@C@@) free(p);"
            "return *p;", N=n, V=v, LB=lb, L=loops, C=cond)
    else:  # forward goto that jumps over the free's guard to the merge label
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(@@C@@) goto @@LF@@;"
            "*p=@@V2@@;"
            "goto @@LU@@;"
            "@@LF@@: free(p);"
            "@@LU@@: return *p;", N=n, V=v, V2=v2, C=cond, LF=lf, LU=lu)
    return name, marked(name, STD + mkmain(argc, inner)), expect


# ---------------------------------------------------------------------------
# 7. free through one alias, then dereference via the stale alias.
# ---------------------------------------------------------------------------
def gen_alias_uaf(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    shape = i % 4
    cond, expect, argc, _k = cond_spec(shape, rng)
    name = f"alias_uaf_{shape}_{i}"
    if shape % 2 == 0:
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int *q=p;int **r=&p;"
            "if(@@C@@){free(q);}"
            "return *p + *q;", N=n, V=v, C=cond)
    else:
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int *q=p;int **r=&q;"
            "if(@@C@@){free(*r);}"
            "return *q + *p;", N=n, V=v, C=cond)
    return name, marked(name, STD + mkmain(argc, inner)), expect


# ---------------------------------------------------------------------------
# 8. Safe controls: every path is guarded (free + null + guard / early return).
# ---------------------------------------------------------------------------
def gen_safe_guarded(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    v2 = rng.randint(1, 30)
    k = rng.randint(1, 6)
    loops = rng.randint(3, 8)
    kk = rng.randint(0, loops - 1)
    shape = i % 7
    lj = f"Lj_{i}"
    lf = f"Lf_{i}"
    lu = f"Lu_{i}"
    jb = f"JB_{i}"
    name = f"safe_guarded_{shape}_{i}"
    if shape == 0:  # if-branch, freed pointer nulled then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(@@K@@>0){free(p);p=0;}"
            "return p?*p:0;", N=n, V=v, K=k)
        body = STD + mkmain(False, inner)
    elif shape == 1:  # switch case, nulled then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int s=@@K@@;"
            "switch(s){case 1:free(p);p=0;break;default:break;}"
            "return p?*p:0;", N=n, V=v, K=k)
        body = STD + mkmain(False, inner)
    elif shape == 2:  # loop conditional free, nulled then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "for(int j=0;j<@@L@@;j++){if(j==@@KK@@){free(p);p=0;}}"
            "return p?*p:0;", N=n, V=v, L=loops, KK=kk)
        body = STD + mkmain(False, inner)
    elif shape == 3:  # nested branches, nulled then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(@@K@@>0){if(@@K2@@>0){free(p);p=0;}}"
            "return p?*p:0;", N=n, V=v, K=k, K2=k + 1)
        body = STD + mkmain(False, inner)
    elif shape == 4:  # setjmp/longjmp, nulled on the longjmp edge then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(setjmp(@@JB@@)==0){longjmp(@@JB@@,1);}"
            "else{free(p);p=0;}"
            "return p?*p:0;", N=n, V=v, JB=jb)
        body = STD + SJ + f"static jmp_buf {jb};\n" + mkmain(False, inner)
    elif shape == 5:  # goto edge, nulled at the merge label then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(@@K@@>0) goto @@LF@@;"
            "*p=@@V2@@;"
            "goto @@LU@@;"
            "@@LF@@: free(p);p=0;"
            "@@LU@@: return p?*p:0;", N=n, V=v, V2=v2, K=k, LF=lf, LU=lu)
        body = STD + mkmain(False, inner)
    else:  # conditional alias, both aliases nulled then guarded
        inner = tpl(
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "int *q=p;"
            "if(@@K@@>0){free(q);q=0;p=0;}"
            "return (p&&q)?*p:0;", N=n, V=v, K=k)
        body = STD + mkmain(False, inner)
    # setjmp/longjmp and goto across a live allocation are outside the
    # declared safe subset; STRICT is allowed to fail closed on them.
    expected = "strict" if shape in (4, 5) else "accept"
    return name, marked(name, body), expected


BUILDERS = (
    gen_if_merge,
    gen_switch_free,
    gen_loop_free,
    gen_nested_join,
    gen_setjmp_uaf,
    gen_goto_skip,
    gen_alias_uaf,
    gen_safe_guarded,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body, expect = builder(i, rng)
            assert expect in ("reject", "strict", "accept"), (name, expect)
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
            return s[:240]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def check_case(case):
    """Run one case in both modes; return a mismatch record or None."""
    name, body, expect, family = case
    path = STAGE / f"{name}.c"
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run(
                [CC, "check", mode, str(path)],
                capture_output=True, text=True, timeout=TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            modes[mode] = (False, f"TIMEOUT after {TIMEOUT}s")
            hard.append(f"{mode}: TIMEOUT after {TIMEOUT}s")
            continue
        except OSError as exc:
            modes[mode] = (False, f"OS ERROR: {exc}")
            hard.append(f"{mode}: OS ERROR: {exc}")
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append(f"{mode}: CRASH rc={proc.returncode}")
        if proc.returncode != 0 and "[CPLUS-" not in diag:
            hard.append(f"{mode}: rejected without a CPLUS safety diagnostic "
                        "(possible malformed generated program)")
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT (false negative)")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT (false negative)")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

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
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / f"{name}.diag.txt").write_text("\n".join(log))
    return {
        "name": name,
        "family": family,
        "expect": expect,
        "reasons": reasons,
        "detail": (
            f"{name} [{family}] expect={expect}: {'; '.join(reasons)} | "
            f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
            f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}"
        ),
    }


def main():
    global STAGE
    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the output.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"
    assert unique == TOTAL, "duplicate generated program body"

    print(f"rev_branch: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count in FAMILIES:
        print(f"  {family}: {families[family]}")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="rev_branch_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]

    false_negatives = [m for m in mismatches if m["expect"] != "accept"]
    false_positives = [m for m in mismatches if m["expect"] == "accept"]
    malformed = [m for m in mismatches
                 if any("malformed" in r for r in m["reasons"])]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"(false negatives/unsafe-accepted={len(false_negatives)}, "
          f"false positives/safe-rejected={len(false_positives)}, "
          f"malformed generator cases={len(malformed)})")
    print("first 10 mismatch names:")
    for m in sorted(mismatches, key=lambda m: m["name"])[:10]:
        print(f"  {m['name']}")
    details = sorted(m["detail"] for m in mismatches)
    for line in details[:10]:
        print("  " + line)
    if len(details) > 10:
        print(f"  ... {len(details) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    print(f"FINAL: {'PASS' if not mismatches else 'FAIL'} "
          f"({len(cases)} cases, {len(mismatches)} mismatches, "
          f"{len(false_positives)} false positives)")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
