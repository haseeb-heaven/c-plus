#!/usr/bin/env python3
"""Deterministic seeded adversarial generator: >=100 distinct small C programs
covering loop-aware bounds (round 2, loop-focused).

Each generated case declares an expected outcome, following the convention
used by tests/core/redteam_100.py, tests/metrics/bounds_350.py and the
``ag_*_100.py`` generators:

  reject - proven out-of-bounds / invalid access: must fail ``--default``
           (and, being a proven error, also ``--strict``).
  strict - unverified or outside-the-safe-subset operation: DEFAULT may pass,
           but ``--strict`` must fail.
  accept - valid control: must pass both modes (correct bounds for every
           iteration of the loop).

A mismatch is either a *hole* (an unsafe/unverified program accepted) or a
*false positive* (a safe control rejected).  Mismatches are printed and the
offending source plus both diagnostics are saved under
tests/out/core/ag2_loop/failures/.  The generator is seeded, so a run with the
same FUZZ_SEED is reproducible.  This is evidence for the declared subset, not
a proof of arbitrary-C safety.

Families covered:
  idx+K / idx-K inside counted loops, <= off-by-one bounds, bounds expressed
  via sizeof()/length of a *different* array, nested loops, while/do-while,
  unsigned counters, stride>1 loops, multidimensional arrays, pointer
  induction variables, one-past dereferences formed inside loops, and safe
  controls with correct bounds for every loop iteration.

Environment:
  FUZZ_SEED    default 0xA6200
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
OUT = ROOT / "tests/out/core/ag2_loop"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA6200"), 0)
TIMEOUT = 60

CASES = []  # dict(name, body, expect, family)


def add(family, name, body, expect):
    CASES.append({
        "name": name,
        "body": body.strip() + "\n",
        "expect": expect,
        "family": family,
    })


# ---------------------------------------------------------------------------
# Seeded generation helpers.  Parameter sweeps vary n plus a seeded element
# type, identifier, offset and stride, so every program is distinct.
# ---------------------------------------------------------------------------
RNG = random.Random(SEED)
TYPES = ("int", "short", "long", "char", "unsigned int", "unsigned char")
NAMES = ("a", "buf", "arr", "data", "tab", "v")
NS = range(2, 9)
HDR_SIZE = "#include <stddef.h>\n"


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def off(hi=4):
    return RNG.randint(1, hi)


def off_lt(n, hi=4):
    """Offset guaranteed < n, for in-bounds idx+K / idx-K controls."""
    return RNG.randint(1, min(hi, n - 1))


def stride():
    return RNG.randint(2, 3)


def dims():
    return RNG.randint(2, 4)


# --- idx+K inside counted loops ---------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("idx_plus_k", f"idxK_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {v}[i + {k}] = ({t})i; return 0; }}", "reject")
    add("idx_plus_k", f"idxK_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n - 1 - k}; i++) {v}[i + {k}] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")
    add("idx_plus_k", f"idxK_unknown_{n}",
        f"int f(int i) {{ {t} {v}[{n}] = {{0}}; return {v}[i + {k}]; }} "
        f"int main(void) {{ return f(0); }}", "strict")
    add("idx_plus_k", f"idxK_negstart_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = -{k}; i < {n - k}; i++) {v}[i + {k}] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")

# --- idx-K inside counted loops ---------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("idx_minus_k", f"idxKneg_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {v}[i - {k}] = ({t})i; return 0; }}", "reject")
    add("idx_minus_k", f"idxKneg_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = {k}; i < {n}; i++) {v}[i - {k}] = ({t})i; "
        f"return {v}[{n - 1 - k}]; }}", "accept")
    add("idx_minus_k", f"idxKneg_unknown_{n}",
        f"int f(int i) {{ {t} {v}[{n}] = {{0}}; return {v}[i - {k}]; }} "
        f"int main(void) {{ return f({k}); }}", "strict")

# --- <= off-by-one bounds ---------------------------------------------------
for n in NS:
    t, v = ty(), nm()
    add("le_bound", f"le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {v}[i] = ({t})i; return 0; }}", "reject")
    add("le_bound", f"le_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n - 1}; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")
    add("le_bound", f"le_global_oob_{n}",
        f"static {t} g[{n}] = {{0}}; int main(void) {{ "
        f"for (int i = 0; i <= {n}; i++) g[i] = ({t})i; return 0; }}", "reject")

# --- bounds expressed via sizeof()/length of a *different* array ------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("sizeof_bound", f"sizeof_other_oob_{n}",
        HDR_SIZE + f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"size_t m = sizeof(big) / sizeof(*big); "
        f"for (size_t i = 0; i < m; i++) {v}[i] = ({t})i; return 0; }}", "reject")
    add("sizeof_bound", f"sizeof_other_ok_{n}",
        HDR_SIZE + f"int main(void) {{ {t} big[{n}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"size_t m = sizeof(big) / sizeof(*big); "
        f"for (size_t i = 0; i < m; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")
    add("sizeof_bound", f"sizeof_direct_oob_{n}",
        HDR_SIZE + f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"for (size_t i = 0; i < sizeof(big) / sizeof(*big); i++) "
        f"{v}[i] = ({t})i; return 0; }}", "reject")

# --- bounds expressed via a saved length of a different array ---------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("len_bound", f"len_other_oob_{n}",
        f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"int m = {n + k}; for (int i = 0; i < m; i++) {v}[i] = ({t})i; "
        f"return 0; }}", "reject")
    add("len_bound", f"len_other_ok_{n}",
        f"int main(void) {{ {t} big[{n}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"int m = {n}; for (int i = 0; i < m; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")

# --- nested loops -----------------------------------------------------------
for n in NS:
    t, k, d = ty(), off(), dims()
    add("nested", f"nested_le_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j <= {n}; j++) "
        f"g[i][j] = ({t})j; return 0; }}", "reject")
    add("nested", f"nested_ok_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j] = ({t})j; return g[{d - 1}][{n - 1}]; }}", "accept")
    add("nested", f"nested_plus_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j + {k}] = ({t})j; return 0; }}", "reject")

# --- while loops ------------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("while_loop", f"while_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i <= {n}) {{ {v}[i] = ({t})i; i++; }} return 0; }}", "reject")
    add("while_loop", f"while_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i < {n}) {{ {v}[i] = ({t})i; i++; }} "
        f"return {v}[{n - 1}]; }}", "accept")
    add("while_loop", f"while_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i < {n}) {{ {v}[i + {k}] = ({t})i; i++; }} return 0; }}", "reject")

# --- do-while loops ---------------------------------------------------------
for n in NS:
    t, v = ty(), nm()
    add("do_while", f"do_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i] = ({t})i; i++; }} while (i <= {n}); return 0; }}", "reject")
    add("do_while", f"do_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i] = ({t})i; i++; }} while (i < {n}); "
        f"return {v}[{n - 1}]; }}", "accept")

# --- unsigned counters ------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("unsigned_counter", f"unsigned_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (unsigned int i = 0; i <= {n}; i++) {v}[i] = ({t})i; return 0; }}", "reject")
    add("unsigned_counter", f"unsigned_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (unsigned int i = 0; i < {n}; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")
    add("unsigned_counter", f"unsigned_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (unsigned int i = 0; i < {n}; i++) {v}[i + {k}] = ({t})i; "
        f"return 0; }}", "reject")

# --- stride > 1 counted loops -----------------------------------------------
for n in NS:
    t, v, s = ty(), nm(), stride()
    last = ((n - 1) // s) * s          # last iteration index of i < n, i += s
    slack = n - 1 - last               # unused tail after the last iteration
    k = slack + RNG.randint(1, 2)      # guarantees last + k >= n
    add("stride", f"stride_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i += {s}) {v}[i] = ({t})i; return 0; }}", "reject")
    add("stride", f"stride_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i] = ({t})i; "
        f"return {v}[{last}]; }}", "accept")
    add("stride", f"stride_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i + {k}] = ({t})i; "
        f"return 0; }}", "reject")
    add("stride", f"stride_plus_ok_{n}",
        f"int main(void) {{ {t} {v}[{n + k}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i + {k}] = ({t})i; "
        f"return {v}[{last + k}]; }}", "accept")

# --- multidimensional arrays in nested loops --------------------------------
for n in NS:
    t, k, d = ty(), off(), dims()
    add("multidim", f"md_inner_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j + {k}] = ({t})j; return 0; }}", "reject")
    add("multidim", f"md_outer_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i <= {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j] = ({t})j; return 0; }}", "reject")
    init3 = "{" * 3 + "0" + "}" * 3  # {{{0}}} without f-string brace escaping
    add("multidim", f"md3_inner_oob_{n}",
        f"int main(void) {{ {t} g[2][{d}][{n}] = {init3}; "
        f"for (int i = 0; i < 2; i++) for (int j = 0; j < {d}; j++) "
        f"for (int q = 0; q < {n}; q++) g[i][j][q + {k}] = ({t})q; "
        f"return 0; }}", "reject")
    add("multidim", f"md_ok_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j] = ({t})j; return g[{d - 1}][{n - 1}]; }}", "accept")

# --- pointer induction variables --------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("ptr_induction", f"ptr_idx_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i < {n}; i++) p[i + {k}] = ({t})i; return 0; }}", "reject")
    add("ptr_induction", f"ptr_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i <= {n}; i++) p[i] = ({t})i; return 0; }}", "reject")
    add("ptr_induction", f"ptr_inc_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i <= {n}; i++) {{ *p = ({t})i; p++; }} return 0; }}", "reject")
    add("ptr_induction", f"ptr_idx_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i < {n}; i++) p[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept")

# --- one-past dereferences formed inside loops ------------------------------
for n in NS:
    t, v = ty(), nm()
    add("one_past_loop", f"onepast_deref_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {{ {t} *p = {v} + i; *p = ({t})i; }} "
        f"return 0; }}", "reject")
    add("one_past_loop", f"onepast_form_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {{ {t} *p = {v} + i; (void)p; }} "
        f"return 0; }}", "accept")
    add("one_past_loop", f"onepast_direct_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; "
        f"*p = 1; return 0; }}", "reject")
    add("one_past_loop", f"onepast_inloop_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {{ {t} *p = {v} + i; *p = ({t})i; }} "
        f"return {v}[{n - 1}]; }}", "accept")

# --- parameter-driven bounds (unverified outside the safe subset) -----------
for n in NS:
    t, v = ty(), nm()
    add("strict_unverified", f"ptr_param_strict_{n}",
        f"int f({t} *p, int len) {{ for (int i = 0; i < len; i++) "
        f"p[i] = ({t})i; return 0; }} "
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return f({v}, {n}); }}", "strict")
    add("strict_unverified", f"len_param_strict_{n}",
        f"int f(int len) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < len; i++) {v}[i] = ({t})i; return 0; }} "
        f"int main(void) {{ return f({n}); }}", "strict")


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
        return fails

    d_fail, s_fail = d_rc != 0, s_rc != 0
    if expect == "reject":
        if not d_fail:
            fails.append(f"DEFAULT-HOLE {name} [{family}]: expected reject, accepted in DEFAULT; "
                         f"diag: {d_diag[:160]}")
        if not s_fail:
            fails.append(f"STRICT-HOLE {name} [{family}]: expected reject, accepted in STRICT; "
                         f"diag: {s_diag[:160]}")
    elif expect == "strict":
        if not s_fail:
            fails.append(f"STRICT-HOLE {name} [{family}]: expected strict failure, accepted in STRICT; "
                         f"diag: {s_diag[:160]}")
    elif expect == "accept":
        if d_fail:
            fails.append(f"DEFAULT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in DEFAULT; "
                         f"diag: {d_diag[:160]}")
        if s_fail:
            fails.append(f"STRICT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in STRICT; "
                         f"diag: {s_diag[:160]}")
    if fails:
        _save(name, body, fails, results)
    return fails


def _save(name, body, fails, results):
    (FAILS / f"{name}.c").write_text(body)
    (FAILS / f"{name}.txt").write_text(
        "\n".join(fails)
        + f"\nDEFAULT (rc={results['--default'][0]}): {results['--default'][1][:400]}"
        + f"\nSTRICT  (rc={results['--strict'][0]}): {results['--strict'][1][:400]}\n")


def main():
    names = [c["name"] for c in CASES]
    bodies = [c["body"] for c in CASES]
    assert len(names) == len(set(names)), "duplicate case name"
    assert len(bodies) == len(set(bodies)), "duplicate program body"
    assert len(CASES) >= 100, f"only {len(CASES)} cases generated"
    counts = Counter(c["expect"] for c in CASES)
    families = Counter(c["family"] for c in CASES)
    print(f"ag2_loop: {len(CASES)} generated cases ({len(set(bodies))} distinct), "
          f"seed={SEED} (0x{SEED:X}), workers={WORKERS}, bin={CC}")
    print(f"expected outcomes: reject={counts['reject']} strict={counts['strict']} "
          f"accept={counts['accept']}")
    print(f"families ({len(families)}): " +
          ", ".join(f"{k}={v}" for k, v in sorted(families.items())))

    failures = []
    false_positives = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for f in pool.map(check_one, CASES):
            failures.extend(f)
    failures.sort()
    for line in failures:
        if "FALSE-POSITIVE" in line:
            false_positives.append(line)
    holes = sum(1 for line in failures if "HOLE" in line)
    hard = sum(1 for line in failures if "TIMEOUT" in line or "CRASH" in line)
    fp = len(false_positives)

    print(f"results: {len(CASES)} cases, {len(failures)} mismatches "
          f"({holes} holes, {fp} false positives, {hard} hard errors)")
    print(f"false positives: {fp}")
    for line in false_positives[:20]:
        print(f"  {line}")
    for line in failures[:25]:
        print(f"  {line}")
    if len(failures) > 25:
        print(f"  ... and {len(failures) - 25} more")
    if failures:
        print(f"saved {sum(1 for f in FAILS.glob('*.c'))} fixtures in {FAILS}")
    status = "PASS" if not failures else "FAIL"
    print(f"FINAL: {status} ({len(CASES)} cases, {len(failures)} mismatches, {fp} false positives)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
