#!/usr/bin/env python3
"""hw_loop_100.py -- deterministic, seeded, parallel loop-bound gap corpus.

Generates >=100 distinct, syntactically valid C programs that target loop-bound
gaps, then runs each one through ``c+ check`` in both safety modes.  Every case
carries a hand-verified expectation derived from the C semantics of the program
(not from the checker's current behaviour):

  reject - the program performs a provably out-of-bounds access on some
           iteration of the loop (or an explicit one-past dereference), so both
           ``--default`` and ``--strict`` must reject it.
  strict - the bound / offset comes from an unverified parameter or provenance,
           so DEFAULT may pass but ``--strict`` must fail closed.
  accept - every access in every iteration is within the declared array; both
           modes must accept it (safe control).

A mismatch is either a *hole* (an unsafe/unverified program was accepted) or a
*false positive* (a safe control was rejected).  Mismatches are printed and the
offending source plus both diagnostics are saved under
``tests/out/core/hw_loop/failures/``.  The generator is seeded, so a run with
the same FUZZ_SEED is reproducible.  This is evidence for the declared subset,
not a proof of arbitrary-C safety.

Families covered (the requested loop-bounds sweep):
  while_le_arith   - while with <= and arithmetic index (i, i+K)
  do_while_oob     - do-while bodies that run one iteration too far
  stride           - counted loops with stride > 1 (<= bound and i+K offsets)
  ptr_induction    - pointer induction variables, including p++ past the end
  one_past         - one-past pointers formed/dereferenced inside loops
  param_bound      - parameter-dependent loop bounds
  unverified_off   - unverified index offsets from parameters
plus a safe control for every family with correct bounds for every iteration.

Environment:
  FUZZ_SEED    default 0x48573031 ("HW01")
  FUZZ_WORKERS default 8
  FUZZ_TIMEOUT default 60 (seconds per check)
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
OUT = ROOT / "tests/out/core/hw_loop"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x48573031"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

CASES = []  # dict(name, body, expect, family, why)


def add(family, name, body, expect, why):
    # A deterministic per-case comment guarantees every program body is distinct.
    CASES.append({
        "name": name,
        "body": f"/* hw-case: {name} */\n" + body.strip() + "\n",
        "expect": expect,
        "family": family,
        "why": why,
    })


# ---------------------------------------------------------------------------
# Seeded generation helpers.  Parameter sweeps vary n plus a seeded element
# type, identifier, offset and stride so every generated program is distinct
# while its expected outcome stays hand-verifiable.
# ---------------------------------------------------------------------------
RNG = random.Random(SEED)
TYPES = ("int", "short", "long", "char", "unsigned int", "unsigned char")
NAMES = ("a", "buf", "arr", "data", "tab", "v")
NS = range(2, 10)


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def off(hi=4):
    return RNG.randint(1, hi)


def off_lt(n, hi=4):
    """Offset guaranteed < n, for in-bounds i+K controls."""
    return RNG.randint(1, min(hi, n - 1))


def stride():
    return RNG.randint(2, 4)


# --- while with <= and arithmetic index ------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("while_le_arith", f"hw_while_le_direct_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i <= {n}) {{ {v}[i] = ({t})i; i++; }} return 0; }}",
        "reject", f"while condition admits i == {n}, one past the array")
    add("while_le_arith", f"hw_while_le_arith_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i <= {n}) {{ {v}[i + {k}] = ({t})i; i++; }} return 0; }}",
        "reject", f"i+{k} reaches {n + k} >= {n} inside the loop")
    add("while_le_arith", f"hw_while_le_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i <= {n - 1}) {{ {v}[i] = ({t})i; i++; }} "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i in [0,{n - 1}] at every access")
    add("while_le_arith", f"hw_while_arith_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i <= {n - 1 - k}) {{ {v}[i + {k}] = ({t})i; i++; }} "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i+{k} in [{k},{n - 1}] for every iteration")

# --- do-while one iteration too far ----------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("do_while_oob", f"hw_do_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i] = ({t})i; i++; }} while (i <= {n}); return 0; }}",
        "reject", f"do-while body runs again for i == {n}")
    add("do_while_oob", f"hw_do_arith_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i + {k}] = ({t})i; i++; }} while (i < {n}); return 0; }}",
        "reject", f"last body iteration reaches i+{k} == {n - 1 + k}")
    add("do_while_oob", f"hw_do_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i] = ({t})i; i++; }} while (i < {n}); "
        f"return {v}[{n - 1}]; }}", "accept",
        f"last body execution has i == {n - 1}")

# --- counted loops with stride > 1 -----------------------------------------
for n in NS:
    t, v, s = ty(), nm(), stride()
    last = ((n - 1) // s) * s          # last index visited by i < n, i += s
    k = (n - last) + RNG.randint(1, 2)  # guarantees last + k >= n
    add("stride", f"hw_stride_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n + s}; i += {s}) {v}[i] = ({t})i; return 0; }}",
        "reject", f"with stride {s} the loop reaches an index >= {n + 1}")
    add("stride", f"hw_stride_arith_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i + {k}] = ({t})i; return 0; }}",
        "reject", f"last visited index {last} plus {k} is {last + k} >= {n}")
    add("stride", f"hw_stride_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i] = ({t})i; "
        f"return {v}[{last}]; }}", "accept",
        f"last visited index is {last} < {n} with stride {s}")

# --- pointer induction variables (including p++ past the end) --------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("ptr_induction", f"hw_ptr_past_end_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i <= {n}; i++) {{ *p = ({t})i; p++; }} return 0; }}",
        "reject", f"p is dereferenced {n + 1} times over an {n}-element buffer")
    add("ptr_induction", f"hw_ptr_end_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (; p <= {v} + {n}; p++) *p = ({t})0; return 0; }}",
        "reject", f"dereferences p at {v} + {n}, one past the buffer")
    add("ptr_induction", f"hw_ptr_inc_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i < {n}; i++) {{ *p = ({t})i; p++; }} "
        f"return {v}[{n - 1}]; }}", "accept",
        f"p is dereferenced exactly {n} times, then advanced one past legally")
    add("ptr_induction", f"hw_ptr_end_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (; p < {v} + {n}; p++) *p = ({t})1; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"pointer stays in [{v}, {v} + {n})")

# --- one-past dereferences formed inside loops ------------------------------
for n in NS:
    t, v = ty(), nm()
    add("one_past", f"hw_onepast_deref_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {{ {t} *p = {v} + i; *p = ({t})i; }} "
        f"return 0; }}", "reject",
        f"at i == {n} the formed pointer is one past and is dereferenced")
    add("one_past", f"hw_onepast_form_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {{ {t} *p = {v} + i; (void)p; }} "
        f"return 0; }}", "accept",
        f"forming {v} + {n} is legal; it is never dereferenced")
    add("one_past", f"hw_onepast_direct_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; "
        f"*p = ({t})1; return 0; }}", "reject",
        f"direct one-past dereference of {v}")
    add("one_past", f"hw_onepast_inloop_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {{ {t} *p = {v} + i; *p = ({t})i; }} "
        f"return {v}[{n - 1}]; }}", "accept",
        f"formed pointers stay within [{v}, {v} + {n - 1}]")

# --- parameter-dependent bounds (unverified outside the safe subset) -------
for n in NS:
    t, v = ty(), nm()
    add("param_bound", f"hw_param_ptr_strict_{n}",
        f"int f({t} *p, int len) {{ for (int i = 0; i < len; i++) "
        f"p[i] = ({t})i; return 0; }} "
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return f({v}, {n}); }}",
        "strict", "callee cannot prove the pointer extent from len alone")
    add("param_bound", f"hw_param_local_strict_{n}",
        f"int f(int len) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < len; i++) {v}[i] = ({t})i; return 0; }} "
        f"int main(void) {{ return f({n}); }}", "strict",
        "bound is a function parameter, not a compile-time extent")
    add("param_bound", f"hw_param_clamp_ok_{n}",
        f"int f(int len) {{ {t} {v}[{n}] = {{0}}; if (len > {n}) len = {n}; "
        f"for (int i = 0; i < len; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }} int main(void) {{ return f({n - 1}); }}",
        "accept", f"bound is clamped to at most {n} before the loop")

# --- unverified index offsets from parameters ------------------------------
for n in NS:
    t, v = ty(), nm()
    add("unverified_off", f"hw_off_param_strict_{n}",
        f"int f({t} *p, int off) {{ return p[off]; }} "
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return f({v}, 0); }}",
        "strict", "index offset is an unproven function parameter")
    add("unverified_off", f"hw_off_loop_strict_{n}",
        f"int f({t} *p, int len, int off) {{ "
        f"for (int i = 0; i < len; i++) p[i + off] = ({t})i; return 0; }} "
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return f({v}, {n}, 1); }}",
        "strict", "both the bound and the offset are unverified parameters")
    add("unverified_off", f"hw_off_const_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int off = 1; "
        f"for (int i = 0; i < {n - 1}; i++) {v}[i + off] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"constant offset 1 with bound {n - 1} keeps i+1 in [1,{n - 1}]")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def check_one(case):
    name, body = case["name"], case["body"]
    expect, family, why = case["expect"], case["family"], case["why"]
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
        _save(name, body, why, fails, results)
        return fails

    d_fail, s_fail = d_rc != 0, s_rc != 0
    if expect == "reject":
        if not d_fail:
            fails.append(f"DEFAULT-HOLE {name} [{family}]: expected reject, accepted in DEFAULT; "
                         f"why: {why}; diag: {d_diag[:160]}")
        if not s_fail:
            fails.append(f"STRICT-HOLE {name} [{family}]: expected reject, accepted in STRICT; "
                         f"why: {why}; diag: {s_diag[:160]}")
    elif expect == "strict":
        if not s_fail:
            fails.append(f"STRICT-HOLE {name} [{family}]: expected strict failure, accepted in STRICT; "
                         f"why: {why}; diag: {s_diag[:160]}")
    elif expect == "accept":
        if d_fail:
            fails.append(f"DEFAULT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in DEFAULT; "
                         f"why: {why}; diag: {d_diag[:160]}")
        if s_fail:
            fails.append(f"STRICT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in STRICT; "
                         f"why: {why}; diag: {s_diag[:160]}")
    if fails:
        _save(name, body, why, fails, results)
    return fails


def _save(name, body, why, fails, results):
    (FAILS / f"{name}.c").write_text(body)
    (FAILS / f"{name}.txt").write_text(
        f"expectation rationale: {why}\n"
        + "\n".join(fails)
        + f"\nDEFAULT (rc={results['--default'][0]}): {results['--default'][1][:400]}"
        + f"\nSTRICT  (rc={results['--strict'][0]}): {results['--strict'][1][:400]}\n")


def main():
    names = [c["name"] for c in CASES]
    bodies = [c["body"] for c in CASES]
    assert len(names) == len(set(names)), "duplicate case name"
    assert len(bodies) == len(set(bodies)), "duplicate program body"
    assert len(CASES) >= 100, f"only {len(CASES)} cases generated"

    # Start from a clean failure directory so the report reflects only this run.
    for old in list(FAILS.glob("*.c")) + list(FAILS.glob("*.txt")):
        old.unlink()

    counts = Counter(c["expect"] for c in CASES)
    families = Counter(c["family"] for c in CASES)
    print(f"hw_loop: {len(CASES)} generated cases ({len(set(bodies))} distinct), "
          f"seed={SEED} (0x{SEED:X}), workers={WORKERS}, bin={CC}")
    print(f"expected outcomes: reject={counts['reject']} strict={counts['strict']} "
          f"accept={counts['accept']}")
    print(f"families ({len(families)}): " +
          ", ".join(f"{k}={v}" for k, v in sorted(families.items())))
    print("first 10 names: " + ", ".join(names[:10]))

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
