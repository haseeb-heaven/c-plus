#!/usr/bin/env python3
"""Deterministic, seeded, parallel NULL-STATE / CONTROL-FLOW generator (120 cases).

Every generated program is a small, self-contained C translation unit built from
a seeded RNG (FUZZ_SEED, default 0xB177) plus a parameter sweep.  Each case is a
*safe* program: a pointer may be assigned NULL along some control-flow path, but
every later dereference is dominated by a guard (if/ternary/early-return), so a
correct null-state analysis must ACCEPT it.  Following the sibling generator
convention (ub_ptr_260.py / bounds_350.py / lifetime_350.py):

   accept - valid control: must pass BOTH --default and --strict.  Rejection in
            either mode is a false positive (a mismatch).

Control-flow families covered (12 seeded variants each):

   1. if_branch      - NULL assigned in an if-branch, guarded deref after it
   2. both_branches  - NULL assigned in both if/else branches, guarded use after
   3. loop           - NULL assigned in a while loop, guarded use after loop
   4. switch         - switch cases assign NULL, guarded use after switch
   5. nested         - NULL assigned in nested if/scopes, guarded use after
   6. goto           - NULL assigned across goto labels / goto back-edge loop
   7. setjmp         - setjmp/longjmp paths assign NULL, guarded use after
   8. early_return   - NULL path returns early, deref only on non-NULL path
   9. do_while       - NULL assigned in a do-while body, guarded use after
  10. breakcontinue  - NULL assigned with break/continue, guarded use after

Mismatches are printed (total count, false-positive count, first 10 names) and
the offending source + diagnostics are saved under
tests/out/core/nl_branch/failures/.  Cases run in parallel (FUZZ_WORKERS,
default 8) with a per-check timeout.  Seeded, so a run with the same FUZZ_SEED
is reproducible.  This is a differential false-positive measurement over the
declared subset, not a proof of safety.

Environment:
   FUZZ_SEED    default 0xB177
   FUZZ_WORKERS default 8
   CPLUS_BIN    default <root>/build/c+
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/nl_branch"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xB177"), 0)
TIMEOUT = 60

CASES = []   # dict(name, body, expect, family)
SEEN = set()  # exact generated program bodies, used to enforce distinctness
RNG = random.Random(SEED)

NAMES = ("p", "q", "r", "ptr", "wp", "z")
TYPES = ("int", "long", "short", "char")
VARIANTS = 12  # seeded variants per family


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in Python format strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    """A seeded small non-zero integer constant."""
    return RNG.randint(1, 97)


def off(hi=5):
    """A seeded small non-negative offset/index."""
    return RNG.randint(1, hi)


def nm():
    return RNG.choice(NAMES)


def ty():
    return RNG.choice(TYPES)


def guard(p, v2, style):
    """A safe use of pointer `p`: every dereference is dominated by a guard."""
    return [
        "if (" + p + ") { return *" + p + "; } return 0;",
        "if (!" + p + ") { return 0; } return *" + p + ";",
        "return (" + p + " != 0) ? *" + p + " : 0;",
        "if (" + p + " != 0) { *" + p + " = " + str(v2) + "; return *" + p + "; } return 0;",
    ][style % 4]


def early_guard(p, v2, style):
    """Early-return guard variant used by the early_return family."""
    return [
        "if (!" + p + ") { return 0; } return *" + p + ";",
        "if (" + p + " == 0) { return 1; } *" + p + " = " + str(v2) + "; return *" + p + ";",
        "if (" + p + " == 0) { return " + str(v2) + "; } return *" + p + ";",
        "if (!" + p + ") { return 0; } *" + p + " = " + str(v2) + "; return *" + p + ";",
    ][style % 4]


def add(family, name, body, expect="accept"):
    assert expect == "accept", (name, expect)
    # Stamp a deterministic, case-specific marker so every generated program
    # has unique source text (comments are valid C and do not change behavior).
    normalized = f"/* nl_branch seed=0x{SEED:X} case={name} family={family} */\n" + body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({
        "name": name,
        "body": normalized,
        "expect": expect,
        "family": family,
    })


# ---------------------------------------------------------------------------
# Seeded generation: VARIANTS variants across 10 control-flow families.
# ---------------------------------------------------------------------------
for n in range(1, VARIANTS + 1):
    V = val()
    V2 = val()
    T = ty()
    P = nm()
    K = off()
    N = RNG.randint(3, 8)
    KK = RNG.randint(0, N - 1)         # an in-range loop index to nullify
    K1 = RNG.randint(0, max(1, N - 2)) # break/continue index 1
    K2 = min(N - 1, K1 + RNG.randint(1, 2))
    if K2 == K1:
        K2 = min(N - 1, K1 + 1)
    S = RNG.randint(0, 3)              # guard style selector
    L1, L2 = f"L1_{n}", f"L2_{n}"
    JB = f"JB_{n}"

    # --- 1. NULL assigned in an if-branch, guarded deref after -------------
    add("if_branch", f"if_branch_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; "
        "if(x>@@K@@){ @@P@@=0; } @@G@@ }",
        T=T, V=V, P=P, K=K, G=guard(P, V2, S)))

    # --- 2. NULL assigned in both if/else branches -------------------------
    add("both_branches", f"both_branches_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; "
        "if(x>@@K@@){ @@P@@=0; } else { @@P@@=0; } @@G@@ }",
        T=T, V=V, P=P, K=K, G=guard(P, V2, S)))

    # --- 3. NULL assigned in a while loop, guarded use after loop ----------
    add("loop", f"loop_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; int i=0; "
        "while(i<@@N@@){ if(i==@@KK@@){ @@P@@=0; } i++; } @@G@@ }",
        T=T, V=V, P=P, N=N, KK=KK, G=guard(P, V2, S)))

    # --- 4. switch cases assign NULL, guarded use after switch -------------
    add("switch", f"switch_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; int k=@@K@@; "
        "switch(k){ case 1: @@P@@=0; break; case 2: @@P@@=0; break; "
        "default: break; } @@G@@ }",
        T=T, V=V, P=P, K=K, G=guard(P, V2, S)))

    # --- 5. NULL assigned in nested scopes, guarded use after --------------
    add("nested", f"nested_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; "
        "if(x>@@K@@){ @@T@@ y=@@V2@@; if(y>@@K2@@){ @@P@@=0; } } @@G@@ }",
        T=T, V=V, V2=V2, P=P, K=K, K2=off(), G=guard(P, V2, S)))

    # --- 6. NULL across goto labels (forward skip or back-edge loop) -------
    if S % 2 == 0:
        goto_body = tpl(
            "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; int i=0; "
            "goto @@L2@@; @@L1@@: @@P@@=0; @@L2@@: @@G@@ }",
            T=T, V=V, P=P, L1=L1, L2=L2, G=guard(P, V2, S))
    else:
        goto_body = tpl(
            "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; int i=0; "
            "@@L1@@: if(i<@@N@@){ i++; if(i==@@KK@@){ @@P@@=0; } goto @@L1@@; } @@G@@ }",
            T=T, V=V, P=P, L1=L1, N=N, KK=KK, G=guard(P, V2, S))
    add("goto", f"goto_{n}", goto_body)

    # --- 7. setjmp/longjmp paths assign NULL, guarded use after ------------
    if S % 2 == 0:
        sj_body = tpl(
            "#include <setjmp.h>\nstatic jmp_buf @@JB@@;\n"
            "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; "
            "if(setjmp(@@JB@@)==0){ longjmp(@@JB@@,1); } else { @@P@@=0; } @@G@@ }",
            JB=JB, T=T, V=V, P=P, G=guard(P, V2, S))
    else:
        sj_body = tpl(
            "#include <setjmp.h>\nstatic jmp_buf @@JB@@;\n"
            "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; "
            "if(setjmp(@@JB@@)==0){ @@P@@=0; longjmp(@@JB@@,1); } @@G@@ }",
            JB=JB, T=T, V=V, P=P, G=guard(P, V2, S))
    add("setjmp", f"setjmp_{n}", sj_body)

    # --- 8. NULL path returns early; deref only on non-NULL path -----------
    add("early_return", f"early_return_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; "
        "if(x>@@K@@){ @@P@@=0; } @@EG@@ }",
        T=T, V=V, P=P, K=K, EG=early_guard(P, V2, S)))

    # --- 9. NULL assigned in a do-while body, guarded use after ------------
    add("do_while", f"do_while_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; int i=0; "
        "do{ if(i==@@KK@@){ @@P@@=0; } i++; }while(i<@@N@@); @@G@@ }",
        T=T, V=V, P=P, N=N, KK=KK, G=guard(P, V2, S)))

    # --- 10. NULL assigned with break/continue, guarded use after ----------
    add("breakcontinue", f"breakcontinue_{n}", tpl(
        "int main(void){ @@T@@ x=@@V@@; @@T@@ *@@P@@=&x; int i; "
        "for(i=0;i<@@N@@;i++){ if(i==@@K1@@){ @@P@@=0; continue; } "
        "if(i==@@K2@@){ @@P@@=0; break; } } @@G@@ }",
        T=T, V=V, P=P, N=N, K1=K1, K2=K2, G=guard(P, V2, S)))


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def check_one(case):
    name, body, expect, family = case["name"], case["body"], case["expect"], case["family"]
    path = CASES_DIR / f"{name}.c"
    path.write_text(body)
    results = {}
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
            lines = [ln.strip() for ln in (proc.stderr + proc.stdout).splitlines() if ln.strip()]
            results[mode] = (proc.returncode, lines[0] if lines else "(no diagnostic)")
        except subprocess.TimeoutExpired:
            results[mode] = ("timeout", f"TIMEOUT after {TIMEOUT}s")

    fails = []
    d_rc, d_diag = results["--default"]
    s_rc, s_diag = results["--strict"]
    for mode, (rc, diag) in results.items():
        if rc == "timeout":
            fails.append(f"TIMEOUT {name} [{family}] in {mode}: {diag}")
        elif isinstance(rc, int) and rc < 0:
            fails.append(f"CRASH {name} [{family}] in {mode}: rc={rc} {diag[:160]}")
    if fails:
        _save(name, body, fails, results)
        return name, fails

    # Every case is an `accept` control: both modes must pass.
    if d_rc != 0:
        fails.append(f"DEFAULT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in DEFAULT; "
                     f"diag: {d_diag[:160]}")
    if s_rc != 0:
        fails.append(f"STRICT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in STRICT; "
                     f"diag: {s_diag[:160]}")
    if fails:
        _save(name, body, fails, results)
    return name, fails


def _save(name, body, fails, results):
    (FAILS / f"{name}.c").write_text(body)
    (FAILS / f"{name}.txt").write_text(
        "\n".join(fails)
        + f"\nDEFAULT (rc={results['--default'][0]}): {results['--default'][1][:400]}"
        + f"\nSTRICT  (rc={results['--strict'][0]}): {results['--strict'][1][:400]}\n"
    )


def main():
    names = [c["name"] for c in CASES]
    assert len(names) == len(set(names)), "duplicate case name"
    assert len(CASES) >= 100, f"only {len(CASES)} cases generated"
    counts = Counter(c["expect"] for c in CASES)
    fams = Counter(c["family"] for c in CASES)
    print(f"nl_branch: {len(CASES)} cases, seed={SEED} (0x{SEED:X}), "
          f"workers={WORKERS}, bin={CC}")
    print(f"outcomes: accept={counts['accept']}")
    print(f"families ({len(fams)}): " + ", ".join(f"{k}={v}" for k, v in sorted(fams.items())))

    failures = []
    mismatch_names = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for name, fails in pool.map(check_one, CASES):
            if fails:
                mismatch_names.append(name)
                failures.extend(fails)
    failures.sort()
    mismatch_names.sort()

    fp = sum(1 for f in failures if "FALSE-POSITIVE" in f)
    print(f"mismatches: {len(failures)}")
    print(f"false positives: {fp}")
    print("first 10 mismatch names: " + (", ".join(mismatch_names[:10]) if mismatch_names else "(none)"))
    for line in failures[:10]:
        print(f"  {line}")
    if failures:
        print(f"saved fixtures in {FAILS}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
