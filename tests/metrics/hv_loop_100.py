#!/usr/bin/env python3
"""hv_loop_100.py -- deterministic, seeded, parallel loop-bound corpus.

Generates >=100 distinct, syntactically valid C programs that exercise loop
bounds, then runs each one through ``c+ check`` in both safety modes.  Every
case carries a hand-verified expectation derived from the C semantics of the
program (not from the checker's current behaviour):

  reject - the program performs a provably out-of-bounds access on some
           iteration of the loop (or an explicit one-past dereference), so
           both ``--default`` and ``--strict`` must reject it.
  strict - the bound comes from an unverified parameter/provenance, so
           DEFAULT may pass but ``--strict`` must fail closed.
  accept - every access in every iteration is within the declared array;
           both modes must accept it (safe control).

A mismatch is either a *hole* (an unsafe/unverified program was accepted) or a
*false positive* (a safe control was rejected).  Mismatches are printed and the
offending source plus both diagnostics are saved under
``tests/out/core/hv_loop/failures/``.  The generator is seeded, so a run with
the same FUZZ_SEED is reproducible.  This is evidence for the declared subset,
not a proof of arbitrary-C safety.

Families covered (all requested by the loop-bounds sweep):
  counted loops with idx+K / idx-K, <= off-by-one bounds, bounds taken from
  sizeof()/length of a *different* array, while/do-while, stride>1, pointer
  induction, one-past dereferences formed inside loops, multidimensional
  arrays, nested loops, unsigned counters, plus safe controls with correct
  bounds for every iteration.

Environment:
  FUZZ_SEED    default 0x48563130 ("HV10")
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
OUT = ROOT / "tests/out/core/hv_loop"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x48563130"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

CASES = []  # dict(name, body, expect, family, why)


def add(family, name, body, expect, why):
    CASES.append({
        "name": name,
        "body": body.strip() + "\n",
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


# --- counted loops: idx+K ---------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("idx_plus_k", f"hv_idxK_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {v}[i + {k}] = ({t})i; return 0; }}",
        "reject", f"i in [0,{n}) so i+{k} reaches {n - 1 + k} >= {n}")
    add("idx_plus_k", f"hv_idxK_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n - 1 - k}; i++) {v}[i + {k}] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i+{k} in [{k},{n - 1}] for every iteration")
    add("idx_plus_k", f"hv_idxK_unknown_{n}",
        f"int f(int i) {{ {t} {v}[{n}] = {{0}}; return {v}[i + {k}]; }} "
        f"int main(void) {{ return f(0); }}", "strict",
        "callee cannot prove the caller's index is in range")
    add("idx_plus_k", f"hv_idxK_negstart_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = -{k}; i < {n - k}; i++) {v}[i + {k}] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i+{k} in [0,{n - 1}] despite the negative loop start")

# --- counted loops: idx-K ---------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("idx_minus_k", f"hv_idxKneg_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {v}[i - {k}] = ({t})i; return 0; }}",
        "reject", f"first iterations give i-{k} < 0")
    add("idx_minus_k", f"hv_idxKneg_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = {k}; i < {n}; i++) {v}[i - {k}] = ({t})i; "
        f"return {v}[{n - 1 - k}]; }}", "accept",
        f"i-{k} in [0,{n - 1 - k}] for every iteration")
    add("idx_minus_k", f"hv_idxKneg_unknown_{n}",
        f"int f(int i) {{ {t} {v}[{n}] = {{0}}; return {v}[i - {k}]; }} "
        f"int main(void) {{ return f({k}); }}", "strict",
        "callee index is not statically bounded")

# --- <= off-by-one bounds ---------------------------------------------------
for n in NS:
    t, v = ty(), nm()
    add("le_bound", f"hv_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {v}[i] = ({t})i; return 0; }}",
        "reject", f"final iteration i == {n} is one past the array")
    add("le_bound", f"hv_le_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n - 1}; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i in [0,{n - 1}] exactly covers the array")
    add("le_bound", f"hv_le_global_oob_{n}",
        f"static {t} g[{n}] = {{0}}; int main(void) {{ "
        f"for (int i = 0; i <= {n}; i++) g[i] = ({t})i; return 0; }}",
        "reject", f"global g[{n}] accessed at i == {n}")

# --- bounds taken from sizeof() of a *different* array ---------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("sizeof_bound", f"hv_sizeof_other_oob_{n}",
        HDR_SIZE + f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"size_t m = sizeof(big) / sizeof(*big); "
        f"for (size_t i = 0; i < m; i++) {v}[i] = ({t})i; return 0; }}",
        "reject", f"bound is {n + k} but {v} has only {n} elements")
    add("sizeof_bound", f"hv_sizeof_other_ok_{n}",
        HDR_SIZE + f"int main(void) {{ {t} big[{n}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"size_t m = sizeof(big) / sizeof(*big); "
        f"for (size_t i = 0; i < m; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"both arrays have {n} elements, so m == {n}")
    add("sizeof_bound", f"hv_sizeof_direct_oob_{n}",
        HDR_SIZE + f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"for (size_t i = 0; i < sizeof(big) / sizeof(*big); i++) "
        f"{v}[i] = ({t})i; return 0; }}", "reject",
        f"sizeof expression yields {n + k}, larger than {v}")

# --- bounds taken from a saved length of a different array -----------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("len_bound", f"hv_len_other_oob_{n}",
        f"int main(void) {{ {t} big[{n + k}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"int m = {n + k}; for (int i = 0; i < m; i++) {v}[i] = ({t})i; "
        f"return 0; }}", "reject", f"m = {n + k} exceeds the {n}-element buffer")
    add("len_bound", f"hv_len_other_ok_{n}",
        f"int main(void) {{ {t} big[{n}] = {{0}}; {t} {v}[{n}] = {{0}}; "
        f"int m = {n}; for (int i = 0; i < m; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"m = {n} matches the destination buffer")

# --- sizeof with differing element types -----------------------------------
for n in NS:
    t = ty()
    add("sizeof_cross_type", f"hv_sizeof_cross_oob_{n}",
        HDR_SIZE + f"int main(void) {{ int big[{n}] = {{0}}; char v[{n}] = {{0}}; "
        f"size_t m = sizeof(big); "
        f"for (size_t i = 0; i < m; i++) v[i] = (char)i; return 0; }}",
        "reject", f"sizeof(big) is {4 * n} bytes but v has only {n} elements")
    add("sizeof_cross_type", f"hv_sizeof_cross_ok_{n}",
        HDR_SIZE + f"int main(void) {{ int big[{n}] = {{0}}; "
        f"char v[{n} * sizeof(int)] = {{0}}; size_t m = sizeof(big); "
        f"for (size_t i = 0; i < m; i++) v[i] = (char)i; "
        f"return v[m - 1]; }}", "accept",
        f"v is sized in bytes to match sizeof(big) == {4 * n}")

# --- nested loops -----------------------------------------------------------
for n in NS:
    t, k, d = ty(), off(), dims()
    add("nested", f"hv_nested_le_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j <= {n}; j++) "
        f"g[i][j] = ({t})j; return 0; }}", "reject",
        f"inner loop reaches j == {n} in every row")
    add("nested", f"hv_nested_ok_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j] = ({t})j; return g[{d - 1}][{n - 1}]; }}", "accept",
        f"i in [0,{d}), j in [0,{n})")
    add("nested", f"hv_nested_plus_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j + {k}] = ({t})j; return 0; }}", "reject",
        f"j+{k} reaches {n - 1 + k} on the last column")

# --- while loops ------------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("while_loop", f"hv_while_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i <= {n}) {{ {v}[i] = ({t})i; i++; }} return 0; }}",
        "reject", f"while condition admits i == {n}")
    add("while_loop", f"hv_while_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i < {n}) {{ {v}[i] = ({t})i; i++; }} "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i in [0,{n}) at every access")
    add("while_loop", f"hv_while_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"while (i < {n}) {{ {v}[i + {k}] = ({t})i; i++; }} return 0; }}",
        "reject", f"i+{k} reaches {n - 1 + k}")

# --- do-while loops ---------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("do_while", f"hv_do_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i] = ({t})i; i++; }} while (i <= {n}); return 0; }}",
        "reject", f"body runs again for i == {n}")
    add("do_while", f"hv_do_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i] = ({t})i; i++; }} while (i < {n}); "
        f"return {v}[{n - 1}]; }}", "accept",
        f"last body execution has i == {n - 1}")
    add("do_while", f"hv_do_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i + {k}] = ({t})i; i++; }} while (i < {n}); return 0; }}",
        "reject", f"i+{k} reaches {n - 1 + k}")
    add("do_while", f"hv_do_plus_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = 0; "
        f"do {{ {v}[i + {k}] = ({t})i; i++; }} while (i < {n - k}); "
        f"return {v}[{n - 1}]; }}", "accept",
        f"i+{k} in [{k},{n - 1}] across all iterations")

# --- unsigned counters ------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("unsigned_counter", f"hv_unsigned_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (unsigned int i = 0; i <= {n}; i++) {v}[i] = ({t})i; return 0; }}",
        "reject", f"unsigned i takes the value {n} at the last iteration")
    add("unsigned_counter", f"hv_unsigned_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (unsigned int i = 0; i < {n}; i++) {v}[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"unsigned i in [0,{n - 1}]")
    add("unsigned_counter", f"hv_unsigned_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (unsigned int i = 0; i < {n}; i++) {v}[i + {k}] = ({t})i; "
        f"return 0; }}", "reject", f"i+{k} reaches {n - 1 + k}")

# --- stride > 1 counted loops ----------------------------------------------
for n in NS:
    t, v, s = ty(), nm(), stride()
    last = ((n - 1) // s) * s          # last iteration index of i < n, i += s
    slack = n - 1 - last               # unused tail after the last iteration
    k = slack + RNG.randint(1, 2)      # guarantees last + k >= n
    add("stride", f"hv_stride_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i += {s}) {v}[i] = ({t})i; return 0; }}",
        "reject", f"i reaches {n} with stride {s}")
    add("stride", f"hv_stride_lt_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i] = ({t})i; "
        f"return {v}[{last}]; }}", "accept",
        f"last visited index is {last} < {n}")
    add("stride", f"hv_stride_plus_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i + {k}] = ({t})i; "
        f"return 0; }}", "reject",
        f"last index {last} plus {k} is {last + k} >= {n}")
    add("stride", f"hv_stride_plus_ok_{n}",
        f"int main(void) {{ {t} {v}[{n + k}] = {{0}}; "
        f"for (int i = 0; i < {n}; i += {s}) {v}[i + {k}] = ({t})i; "
        f"return {v}[{last + k}]; }}", "accept",
        f"destination has {n + k} elements; max index is {last + k}")

# --- multidimensional arrays in nested loops --------------------------------
for n in NS:
    t, k, d = ty(), off(), dims()
    add("multidim", f"hv_md_inner_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j + {k}] = ({t})j; return 0; }}", "reject",
        f"inner index reaches {n - 1 + k}")
    add("multidim", f"hv_md_outer_oob_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i <= {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j] = ({t})j; return 0; }}", "reject",
        f"outer index reaches {d} on a [{d}] array")
    init3 = "{" * 3 + "0" + "}" * 3  # {{{0}}} without f-string brace escaping
    add("multidim", f"hv_md3_inner_oob_{n}",
        f"int main(void) {{ {t} g[2][{d}][{n}] = {init3}; "
        f"for (int i = 0; i < 2; i++) for (int j = 0; j < {d}; j++) "
        f"for (int q = 0; q < {n}; q++) g[i][j][q + {k}] = ({t})q; "
        f"return 0; }}", "reject",
        f"innermost index reaches {n - 1 + k}")
    add("multidim", f"hv_md_ok_{n}",
        f"int main(void) {{ {t} g[{d}][{n}] = {{{{0}}}}; "
        f"for (int i = 0; i < {d}; i++) for (int j = 0; j < {n}; j++) "
        f"g[i][j] = ({t})j; return g[{d - 1}][{n - 1}]; }}", "accept",
        f"both dimensions fully in range")

# --- pointer induction variables --------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off_lt(n)
    add("ptr_induction", f"hv_ptr_idx_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i < {n}; i++) p[i + {k}] = ({t})i; return 0; }}",
        "reject", f"p[i+{k}] reaches index {n - 1 + k}")
    add("ptr_induction", f"hv_ptr_le_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i <= {n}; i++) p[i] = ({t})i; return 0; }}",
        "reject", f"p[{n}] is one past the buffer")
    add("ptr_induction", f"hv_ptr_inc_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i <= {n}; i++) {{ *p = ({t})i; p++; }} return 0; }}",
        "reject", f"p is advanced {n + 1} times over an {n}-element buffer")
    add("ptr_induction", f"hv_ptr_idx_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (int i = 0; i < {n}; i++) p[i] = ({t})i; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"p visits exactly [{n}] elements")
    add("ptr_induction", f"hv_ptr_end_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (; p <= {v} + {n}; p++) *p = ({t})0; return 0; }}",
        "reject", f"dereferences p at {v} + {n}, one past the end")
    add("ptr_induction", f"hv_ptr_end_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v}; "
        f"for (; p < {v} + {n}; p++) *p = ({t})1; "
        f"return {v}[{n - 1}]; }}", "accept",
        f"pointer stays in [{v}, {v} + {n})")

# --- one-past dereferences formed inside loops ------------------------------
for n in NS:
    t, v = ty(), nm()
    add("one_past_loop", f"hv_onepast_deref_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {{ {t} *p = {v} + i; *p = ({t})i; }} "
        f"return 0; }}", "reject",
        f"at i == {n} the formed pointer is one past and is dereferenced")
    add("one_past_loop", f"hv_onepast_form_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i <= {n}; i++) {{ {t} *p = {v} + i; (void)p; }} "
        f"return 0; }}", "accept",
        f"forming {v} + {n} is legal; it is never dereferenced")
    add("one_past_loop", f"hv_onepast_direct_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; "
        f"*p = 1; return 0; }}", "reject",
        f"direct one-past dereference of {v}")
    add("one_past_loop", f"hv_onepast_inloop_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < {n}; i++) {{ {t} *p = {v} + i; *p = ({t})i; }} "
        f"return {v}[{n - 1}]; }}", "accept",
        f"formed pointers stay in [{v}, {v} + {n - 1}]")

# --- parameter-driven bounds (unverified outside the safe subset) ----------
for n in NS:
    t, v = ty(), nm()
    add("strict_unverified", f"hv_ptr_param_strict_{n}",
        f"int f({t} *p, int len) {{ for (int i = 0; i < len; i++) "
        f"p[i] = ({t})i; return 0; }} "
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return f({v}, {n}); }}",
        "strict", "callee cannot prove the pointer's extent from len alone")
    add("strict_unverified", f"hv_len_param_strict_{n}",
        f"int f(int len) {{ {t} {v}[{n}] = {{0}}; "
        f"for (int i = 0; i < len; i++) {v}[i] = ({t})i; return 0; }} "
        f"int main(void) {{ return f({n}); }}", "strict",
        "bound is a function parameter, not a compile-time extent")


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
    counts = Counter(c["expect"] for c in CASES)
    families = Counter(c["family"] for c in CASES)
    print(f"hv_loop: {len(CASES)} generated cases ({len(set(bodies))} distinct), "
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
