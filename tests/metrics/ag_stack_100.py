#!/usr/bin/env python3
"""Deterministic seeded adversarial generator: >=100 distinct small C programs
covering stack/global array bounds.

Each generated case declares an expected outcome, following the convention
used by tests/core/redteam_100.py and tests/metrics/bounds_350.py:

  reject - proven out-of-bounds / invalid access: must fail ``--default``
           (and, being a proven error, also ``--strict``).
  strict - unverified or outside-the-safe-subset operation: DEFAULT may pass,
           but ``--strict`` must fail.
  accept - valid control: must pass both modes.

A mismatch is either a *hole* (an unsafe program accepted) or a *false
positive* (a safe control rejected).  Mismatches are printed and the offending
source plus both diagnostics are saved under
tests/out/core/ag_stack/failures/.  The generator is seeded, so a run with the
same FUZZ_SEED is reproducible.  This is evidence for the declared subset, not
a proof of arbitrary-C safety.

Families covered:
  constant OOB, negative indices, loop off-by-one (<= and <), idx+K offsets,
  multidimensional arrays, arrays bounded by another array's length, VLA
  indices, pointer arithmetic past one-past-end, dereferencing one-past-end,
  string-literal writes, struct member buffer overflow, plus safe controls.

Environment:
  FUZZ_SEED    default 0xA6_57A (stack)
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
OUT = ROOT / "tests/out/core/ag_stack"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA657A"), 0)
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
# type, identifier and literal contents, so every program is distinct.
# ---------------------------------------------------------------------------
RNG = random.Random(SEED)
TYPES = ("int", "short", "long", "char")
NAMES = ("a", "buf", "arr", "data", "tab")
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
NS = range(2, 10)


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def off(hi=4):
    return RNG.randint(1, hi)


def lit(length):
    return "".join(RNG.choice(ALPHABET) for _ in range(length))


# --- constant OOB (stack and global) ---------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("global_const", f"gconst_oob_{n}",
        f"static {t} g[{n}] = {{0}}; int main(void) {{ return g[{n + k}]; }}", "reject")
    add("global_const", f"gconst_ok_{n}",
        f"static {t} g[{n}] = {{0}}; int main(void) {{ int i = {RNG.randint(0, n - 1)}; return g[i]; }}",
        "accept")
    add("stack_const", f"sconst_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return {v}[{n + k}]; }}", "reject")
    add("stack_const", f"sconst_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return {v}[{n - 1}]; }}", "accept")

# --- negative indices -------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("negative_index", f"neg_lit_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return {v}[-{k}]; }}", "reject")
    add("negative_index", f"neg_var_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = -{k}; return {v}[i]; }}", "reject")
    add("negative_index", f"neg_global_{n}",
        f"static {t} g[{n}] = {{0}}; int main(void) {{ int i = -{k}; return g[i]; }}", "reject")

# --- loop off-by-one (<= and <) --------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("loop_offbyone", f"loop_le_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; for (int i = 0; i <= {n}; i++) {v}[i] = ({t})i; return 0; }}",
        "reject")
    add("loop_offbyone", f"loop_lt_ext_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; for (int i = 0; i < {n + k}; i++) {v}[i] = 0; return 0; }}",
        "reject")
    add("loop_offbyone", f"loop_le_global_{n}",
        f"static {t} g[{n}]; int main(void) {{ for (int i = 0; i <= {n}; i++) g[i] = 0; return 0; }}",
        "reject")
    add("loop_offbyone", f"loop_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; for (int i = 0; i < {n}; i++) {v}[i] = ({t})i; return {v}[{n - 1}]; }}",
        "accept")

# --- idx+K offsets ----------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("idx_plus_k", f"idxK_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = {n - 1}; return {v}[i + {k}]; }}", "reject")
    add("idx_plus_k", f"idxK_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; return {v}[i + 1]; }}", "accept")
    add("idx_plus_k", f"idxK_unknown_{n}",
        f"int f(int i) {{ {t} {v}[{n}] = {{0}}; return {v}[i + {k}]; }} int main(void) {{ return f(0); }}",
        "strict")

# --- multidimensional arrays ------------------------------------------------
for n in NS:
    t, k = ty(), off()
    add("multidim", f"md2_oob_{n}",
        f"int main(void) {{ {t} g[2][{n}] = {{0}}; return g[1][{n + k}]; }}", "reject")
    add("multidim", f"md2_first_{n}",
        f"int main(void) {{ {t} g[3][{n}] = {{0}}; return g[{3 + k}][0]; }}", "reject")
    add("multidim", f"md3_oob_{n}",
        f"int main(void) {{ {t} g[2][3][{n}] = {{{{0}}}}; return g[1][2][{n + k}]; }}", "reject")
    add("multidim", f"md2_ok_{n}",
        f"int main(void) {{ {t} g[2][{n}] = {{0}}; return g[1][{n - 1}]; }}", "accept")

# --- arrays bounded by another array's length ------------------------------
for n in NS:
    t, k = ty(), off()
    add("other_array_len", f"otherlen_oob_{n}",
        f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} small[{n}] = {{0}}; "
        f"for (int i = 0; i < {n + k}; i++) small[i] = ({t})i; return 0; }}", "reject")
    add("other_array_len", f"otherlen_sizeof_{n}",
        f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} small[{n}] = {{0}}; "
        f"size_t m = sizeof(big) / sizeof(*big); for (size_t i = 0; i < m; i++) small[i] = 0; return 0; }}",
        "reject")
    add("other_array_len", f"otherlen_ok_{n}",
        f"int main(void) {{ {t} a[{n}] = {{0}}; {t} b[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) b[i] = a[i]; return b[{n - 1}]; }}", "accept")

# --- VLA indices (VLA is outside the safe subset -> unverified / strict) ---
for n in NS:
    t, k = ty(), off()
    add("vla", f"vla_le_{n}",
        f"int main(int c, char **v) {{ {t} a[c]; for (int i = 0; i <= c; i++) a[i] = 0; (void)v; return 0; }}",
        "strict")
    add("vla", f"vla_oob_{n}",
        f"int main(int c, char **v) {{ {t} a[c]; (void)v; return a[c]; }}", "strict")
    add("vla", f"vla_neg_{n}",
        f"int main(int c, char **v) {{ {t} a[c]; (void)v; return a[-{k}]; }}", "strict")
    add("vla", f"vla_ok_{n}",
        f"int main(int c, char **v) {{ {t} a[c]; for (int i = 0; i < c; i++) a[i] = ({t})i; (void)v; return 0; }}",
        "strict")

# --- pointer arithmetic past one-past-end ----------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("ptr_one_past", f"ptr_past_form_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n + k}; (void)p; return 0; }}", "reject")
    add("ptr_one_past", f"ptr_neg_form_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} - {k}; (void)p; return 0; }}", "reject")
    add("ptr_one_past", f"ptr_onepast_form_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; (void)p; return 0; }}", "accept")
    add("ptr_one_past", f"ptr_inbounds_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n - 1}; return *p; }}", "accept")

# --- dereferencing one-past-end ---------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("deref_one_past", f"deref_onepast_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; return *p; }}", "reject")
    add("deref_one_past", f"deref_past_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n + k}; return *p; }}", "reject")
    add("deref_one_past", f"deref_global_onepast_{n}",
        f"static {t} g[{n}] = {{0}}; int main(void) {{ return *(g + {n}); }}", "reject")

# --- string literal writes --------------------------------------------------
for n in NS:
    k = off()
    s_write = lit(n + k)
    s_mid = lit(n + k + 2)
    s_read = lit(n + k)
    s_arr = lit(n + k)
    add("string_literal", f"strlit_write_{n}",
        f'int main(void) {{ char *p = "{s_write}"; p[0] = 48; return 0; }}', "reject")
    add("string_literal", f"strlit_write_mid_{n}",
        f'int main(void) {{ char *p = "{s_mid}"; p[{k}] = 48; return 0; }}', "reject")
    add("string_literal", f"strlit_read_{n}",
        f'int main(void) {{ const char *p = "{s_read}"; return p[0]; }}', "accept")
    add("string_literal", f"strlit_arr_{n}",
        f'int main(void) {{ char a[] = "{s_arr}"; a[0] = 48; return a[0]; }}', "accept")

# --- struct member buffer overflow -----------------------------------------
for n in NS:
    t, k = ty(), off()
    add("struct_member", f"struct_oob_{n}",
        f"struct S {{ {t} v[{n}]; }}; int main(void) {{ struct S s = {{{{0}}}}; return s.v[{n + k}]; }}",
        "reject")
    add("struct_member", f"struct_nested_oob_{n}",
        f"struct I {{ {t} v[{n}]; }}; struct O {{ struct I i; }}; "
        f"int main(void) {{ struct O o = {{{{ {{0}} }}}}; return o.i.v[{n + k}]; }}", "reject")
    add("struct_member", f"struct_ptr_oob_{n}",
        f"struct S {{ {t} v[{n}]; }}; int main(void) {{ struct S s = {{{{0}}}}; {t} *p = s.v; return p[{n + k}]; }}",
        "reject")
    add("struct_member", f"struct_ok_{n}",
        f"struct S {{ {t} v[{n}]; }}; int main(void) {{ struct S s = {{{{0}}}}; return s.v[{n - 1}]; }}",
        "accept")


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
        elif rc < 0:
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
        + f"\nSTRICT  (rc={results['--strict'][0]}): {results['--strict'][1][:400]}\n"
    )


def main():
    names = [c["name"] for c in CASES]
    assert len(names) == len(set(names)), "duplicate case name"
    assert len(CASES) >= 100, f"only {len(CASES)} cases generated"
    counts = Counter(c["expect"] for c in CASES)
    families = sorted(set(c["family"] for c in CASES))
    print(f"ag_stack: {len(CASES)} cases across {len(families)} families, "
          f"seed={SEED} (0x{SEED:X}), workers={WORKERS}, bin={CC}")
    print(f"outcomes: reject={counts['reject']} strict={counts['strict']} "
          f"accept={counts['accept']}")

    failures = []
    holes = 0
    false_positives = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for f in pool.map(check_one, CASES):
            failures.extend(f)
    failures.sort()
    for line in failures:
        if "FALSE-POSITIVE" in line:
            false_positives += 1
        elif "HOLE" in line:
            holes += 1

    print(f"mismatches: {len(failures)} "
          f"(holes={holes}, false_positives={false_positives}, other={len(failures) - holes - false_positives})")
    for line in failures[:25]:
        print(f"  {line}")
    if len(failures) > 25:
        print(f"  ... and {len(failures) - 25} more")
    if failures:
        print(f"saved fixtures in {FAILS}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
