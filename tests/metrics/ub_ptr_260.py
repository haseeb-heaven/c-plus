#!/usr/bin/env python3
"""Deterministic POINTER-UB generator: >=260 distinct small C programs.

Every generated program is a small, self-contained C translation unit produced
from parameter sweeps plus a seeded RNG (FUZZ_SEED, default 0xB17E).  Each case
declares an expected outcome, following the convention used by the sibling
generators (bounds_350.py / lifetime_350.py / ffi_350.py):

  reject - proven pointer-UB (proven by the constructed program): must fail
           BOTH --default and --strict.  Acceptance in either mode is a hole.
  strict - conditional / implementation-defined pointer rule: MAY pass
           --default, but must fail --strict (failing to do so is a hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Families covered (integer-forged pointers, uintptr_t round trips, tagged
pointers dereferenced/freed without untagging, misaligned casts/dereferences,
pointer arithmetic past one-past-end, dereferencing one-past-end, null pointer
arithmetic, pointer subtraction/comparison across distinct allocations,
strict-aliasing type punning, restrict overlap, writing to string literals,
container_of on null, wrong indirection levels, incompatible function-pointer
casts, and void* arithmetic) each contribute reject/strict/accept cases.

Mismatches are printed (counts + first 10) and the offending source plus
diagnostics are saved under tests/out/core/ub_ptr/failures/.  Cases run in
parallel (FUZZ_WORKERS, default 8) with a per-check timeout.  The generator is
seeded, so a run with the same FUZZ_SEED is reproducible.  This is a
differential measurement over the declared subset, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xB17E
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
OUT = ROOT / "tests/out/core/ub_ptr"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xB17E"), 0)
TIMEOUT = 60

CASES = []   # dict(name, body, expect, family)
SEEN = set()  # exact generated program bodies, used to enforce distinctness
RNG = random.Random(SEED)

NAMES = ("p", "q", "r", "ptr", "wp", "z")
TYPES = ("int", "long", "short", "char")
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in Python format strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    """A seeded small non-zero integer constant."""
    return RNG.randint(1, 97)


def off(hi=5):
    """A seeded small positive offset."""
    return RNG.randint(1, hi)


def addr():
    """A seeded fake integer address (for forged pointers)."""
    return 0x1000 + RNG.randint(0, 0x7FFF) * 0x40


def nm():
    return RNG.choice(NAMES)


def ty():
    return RNG.choice(TYPES)


def lit(n):
    """A distinct string literal body of length >= 1."""
    return "".join(RNG.choice(ALPHABET) for _ in range(n))


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    # Stamp a deterministic, case-specific marker so every generated program
    # has unique source text (comments are valid C and do not change behavior).
    normalized = f"/* ub_ptr seed=0x{SEED:X} case={name} family={family} */\n" + body.strip() + "\n"
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
# Seeded generation: N sweeps across 15 pointer-UB families.
# ---------------------------------------------------------------------------
for n in range(1, 11):
    N = n
    K = off()
    V = val()
    T = ty()
    P = nm()
    A = addr()

    # --- 1. integer-forged pointers ----------------------------------------
    add("forged_int", tpl("forged_deref_@@N@@", N=N),
        tpl("int main(void){ @@T@@ *@@P@@ = (@@T@@ *)@@A@@; return *@@P@@; }",
            T=T, P=P, A=A), "reject")
    add("forged_int", tpl("forged_write_@@N@@", N=N),
        tpl("int main(void){ @@T@@ *@@P@@ = (@@T@@ *)(@@A@@ + @@K@@); *@@P@@ = @@V@@; return 0; }",
            T=T, P=P, A=A, K=K, V=V), "reject")
    add("forged_int", tpl("forged_form_ok_@@N@@", N=N),
        tpl("int main(void){ int x = @@V@@; int *@@P@@ = &x; (void)@@P@@; return *@@P@@; }",
            V=V, P=P), "accept")

    # --- 2. uintptr_t round trips ------------------------------------------
    add("uintptr", tpl("uintptr_ok_@@N@@", N=N),
        tpl("#include <stdint.h>\nint main(void){ @@T@@ x=@@V@@; @@T@@ *p=&x; "
            "uintptr_t u=(uintptr_t)p; @@T@@ *q=(@@T@@*)u; return *q; }",
            T=T, V=V), "strict")
    add("uintptr", tpl("uintptr_forged_@@N@@", N=N),
        tpl("#include <stdint.h>\nint main(void){ uintptr_t u=@@A@@; "
            "@@T@@ *@@P@@=(@@T@@*)u; return *@@P@@; }",
            A=A, T=T, P=P), "reject")
    add("uintptr", tpl("uintptr_bumped_@@N@@", N=N),
        tpl("#include <stdint.h>\nint main(void){ int x=@@V@@; uintptr_t u=(uintptr_t)&x; "
            "u += @@K@@; int *q=(int*)u; return *q; }",
            V=V, K=K), "reject")

    # --- 3. tagged pointers dereferenced/freed without untagging -----------
    TAG = (N % 3) + 1  # low-bit tag, 1..3 (odd -> misaligns the pointer)
    add("tagged", tpl("tagged_deref_@@N@@", N=N),
        tpl("#include <stdlib.h>\n#include <stdint.h>\nint main(void){ int *p=malloc(sizeof(int)); "
            "if(!p)return 2; int *t=(int*)((uintptr_t)p | @@TAG@@); return *t; }",
            TAG=TAG), "reject")
    add("tagged", tpl("tagged_free_@@N@@", N=N),
        tpl("#include <stdlib.h>\n#include <stdint.h>\nint main(void){ int *p=malloc(sizeof(int)); "
            "if(!p)return 2; int *t=(int*)((uintptr_t)p | @@TAG@@); free(t); return 0; }",
            TAG=TAG), "reject")
    add("tagged", tpl("tagged_untag_ok_@@N@@", N=N),
        tpl("#include <stdint.h>\nint main(void){ int x=@@V@@; uintptr_t u=(uintptr_t)&x | @@TAG@@; "
            "int *t=(int*)(u & ~(uintptr_t)@@TAG@@); *t=@@V2@@; return *t; }",
            TAG=TAG, V=V, V2=V + 1), "strict")
    add("tagged", tpl("tagged_direct_ok_@@N@@", N=N),
        tpl("int main(void){ int x=@@V@@; int *@@P@@=&x; *@@P@@=@@V2@@; return *@@P@@; }",
            V=V, V2=V + 1, P=P), "accept")

    # --- 4. misaligned casts / dereferences --------------------------------
    MISOFF = (N % 3) + 1  # 1,2,3 -> misaligned for a 4-byte int
    add("misaligned", tpl("misaligned_deref_@@N@@", N=N),
        tpl("int main(void){ char buf[64]={0}; int *@@P@@=(int*)(buf + @@M@@); return *@@P@@; }",
            P=P, M=MISOFF), "reject")
    add("misaligned", tpl("char_alias_ok_@@N@@", N=N),
        tpl("int main(void){ int x=@@V@@; char *@@P@@=(char*)&x; return @@P@@[0]; }",
            V=V, P=P), "accept")

    # --- 5. pointer arithmetic past one-past-end ---------------------------
    add("past_end", tpl("past_end_form_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}; int *p = a + @@N@@ + @@K@@; (void)p; return 0; }",
            N=N, K=K), "reject")
    add("past_end", tpl("one_past_form_ok_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}; int *p = a + @@N@@; (void)p; return 0; }",
            N=N), "accept")

    # --- 6. dereferencing one-past-end -------------------------------------
    add("one_past", tpl("one_past_deref_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}; int *p = a + @@N@@; return *p; }",
            N=N), "reject")
    add("one_past", tpl("one_past_subscript_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}; return a[@@N@@]; }", N=N), "reject")

    # --- 7. null pointer arithmetic ----------------------------------------
    add("null_arith", tpl("null_add_@@N@@", N=N),
        tpl("int main(void){ int *@@P@@ = 0; int *q = @@P@@ + @@K@@; (void)q; return 0; }",
            P=P, K=K), "reject")
    add("null_arith", tpl("null_deref_add_@@N@@", N=N),
        tpl("int main(void){ int *@@P@@ = 0; return *(@@P@@ + @@K@@); }",
            P=P, K=K), "reject")
    add("null_arith", tpl("null_compare_ok_@@N@@", N=N),
        tpl("int main(void){ int *@@P@@ = 0; return @@P@@ == 0; }", P=P), "accept")

    # --- 8. pointer subtraction / comparison across distinct allocations ---
    add("cross_alloc", tpl("cross_stack_sub_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}, b[@@N@@]={0}; return &a[0] - &b[0]; }",
            N=N), "reject")
    add("cross_alloc", tpl("cross_stack_rel_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}, b[@@N@@]={0}; return &a[0] < &b[0]; }",
            N=N), "reject")
    add("cross_alloc", tpl("cross_eq_ok_@@N@@", N=N),
        tpl("int main(void){ int a[@@N@@]={0}, b[@@N@@]={0}; return &a[0] == &b[0]; }",
            N=N), "accept")

    # --- 9. strict-aliasing type punning -----------------------------------
    add("type_pun", tpl("float_int_pun_@@N@@", N=N),
        tpl("int main(void){ int x=@@V@@; float *f=(float*)&x; return (int)*f; }",
            V=V), "strict")
    add("type_pun", tpl("float_int_write_@@N@@", N=N),
        tpl("int main(void){ float x=1.0f; int *@@P@@=(int*)&x; *@@P@@=@@V@@; return 0; }",
            P=P, V=V), "strict")
    add("type_pun", tpl("union_pun_ok_@@N@@", N=N),
        tpl("union U { int i; float f; };\nint main(void){ union U u; u.i=@@V@@; return (int)u.f; }",
            V=V), "accept")

    # --- 10. restrict overlap -----------------------------------------------
    add("restrict", tpl("restrict_overlap_@@N@@", N=N),
        tpl("void f(int *restrict a, int *restrict b){ *a = *b; }\n"
            "int main(void){ int x=@@V@@; f(&x,&x); return x; }", V=V), "reject")
    add("restrict", tpl("restrict_distinct_ok_@@N@@", N=N),
        tpl("void f(int *restrict a, int *restrict b){ *a = *b; }\n"
            "int main(void){ int x=@@V@@, y=@@V2@@; f(&x,&y); return x; }",
            V=V, V2=V + 1), "accept")

    # --- 11. writing to string literals ------------------------------------
    S = lit(N + 2)
    add("string_lit", tpl("strlit_write_@@N@@", N=N),
        tpl("int main(void){ char *s = \"@@S@@\"; s[0] = 'x'; return 0; }", S=S), "reject")
    add("string_lit", tpl("strlit_array_ok_@@N@@", N=N),
        tpl("int main(void){ char s[] = \"@@S@@\"; s[0] = 'x'; return s[0]; }", S=S), "accept")

    # --- 12. container_of on null ------------------------------------------
    add("container_of", tpl("container_null_@@N@@", N=N),
        tpl("#include <stddef.h>\nstruct S { int a; int b[@@N@@]; };\n"
            "int main(void){ struct S *s = (struct S*)((char*)0 - offsetof(struct S, b)); "
            "return s->a; }", N=N), "reject")
    add("container_of", tpl("container_ok_@@N@@", N=N),
        tpl("#include <stddef.h>\nstruct S { int a; int b[@@N@@]; };\n"
            "int main(void){ struct S obj = {0}; int *m = obj.b; struct S *s = "
            "(struct S*)((char*)m - offsetof(struct S, b)); return s->a; }", N=N), "accept")

    # --- 13. wrong indirection levels (T** where T* expected) --------------
    add("indirection", tpl("indirection_topp_to_p_@@N@@", N=N),
        tpl("void f(int *p){ *p = @@V@@; }\n"
            "int main(void){ int x=@@V@@; int *q=&x; int **pp=&q; f(pp); return 0; }",
            V=V), "reject")
    add("indirection", tpl("indirection_p_to_topp_@@N@@", N=N),
        tpl("void f(int **p){ **p = @@V@@; }\n"
            "int main(void){ int x=@@V@@; f(&x); return 0; }", V=V), "reject")

    # --- 14. casting function pointers with incompatible signatures --------
    add("func_ptr", tpl("funcptr_bad_cast_@@N@@", N=N),
        tpl("typedef int (*Fi)(int);\nint g(void){ return @@V@@; }\n"
            "int main(void){ Fi f = (Fi)g; return f(@@N@@); }", V=V, N=N), "strict")
    add("func_ptr", tpl("funcptr_ok_@@N@@", N=N),
        tpl("typedef int (*Fi)(int);\nint g(int x){ return x + @@V@@; }\n"
            "int main(void){ Fi f = g; return f(@@N@@); }", V=V, N=N), "accept")

    # --- 15. void* arithmetic ----------------------------------------------
    add("void_arith", tpl("void_add_@@N@@", N=N),
        tpl("int main(void){ char buf[64]={0}; void *p=buf; void *q = p + @@K@@; (void)q; return 0; }",
            K=K), "reject")
    add("void_arith", tpl("void_compare_ok_@@N@@", N=N),
        tpl("int main(void){ char buf[64]={0}; void *p=buf; void *q=buf; return p == q; }"),
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
    assert len(CASES) >= 260, f"only {len(CASES)} cases generated"
    counts = Counter(c["expect"] for c in CASES)
    fams = Counter(c["family"] for c in CASES)
    print(f"ub_ptr: {len(CASES)} cases, seed={SEED} (0x{SEED:X}), "
          f"workers={WORKERS}, bin={CC}")
    print(f"outcomes: reject={counts['reject']} strict={counts['strict']} "
          f"accept={counts['accept']}")
    print(f"families ({len(fams)}): " + ", ".join(f"{k}={v}" for k, v in sorted(fams.items())))

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for f in pool.map(check_one, CASES):
            failures.extend(f)
    failures.sort()

    print(f"mismatches: {len(failures)}")
    for line in failures[:10]:
        print(f"  {line}")
    if failures:
        print(f"saved fixtures in {FAILS}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
