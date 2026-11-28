#!/usr/bin/env python3
"""Deterministic BOUNDS/LIBC generator: >=350 distinct small C programs.

For every generated program we declare an expected outcome, following the
convention used by tests/core/redteam_100.py:

  reject - proven out-of-bounds / overflow: must fail ``--default`` (and,
           being a proven error, also ``--strict``).
  strict - unverified operation: DEFAULT may pass, but ``--strict`` must fail.
  accept - valid control: must pass both modes.

Mismatches are printed and the offending source (plus diagnostics) is saved
under tests/out/core/bounds350/failures/.  The generator is seeded, so a run
with the same FUZZ_SEED is reproducible.  This is evidence for the declared
subset, not a proof of arbitrary-C safety.

Environment:
  FUZZ_SEED    default 0xB0D5
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
OUT = ROOT / "tests/out/core/bounds350"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xB0D5"), 0)
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
# Seeded generation helpers.  Parameter sweeps vary n plus a seeded offset,
# element type, identifier and literal contents, so each program is distinct.
# ---------------------------------------------------------------------------
RNG = random.Random(SEED)
TYPES = ("int", "short", "long", "char")
NAMES = ("a", "buf", "arr", "data", "table")
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
NS = range(2, 11)


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def off(hi=4):
    return RNG.randint(1, hi)


def lit(length):
    return "".join(RNG.choice(ALPHABET) for _ in range(length))


def wrap():
    return RNG.choice(("(size_t)-1", "(size_t)-1 / 2 + 2", "~(size_t)0"))


# --- stack array bounds -----------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("const_index", f"const_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return {v}[{n + k}]; }}", "reject")
    add("const_index", f"const_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = {RNG.randint(0, n - 1)}; return {v}[i]; }}",
        "accept")
    add("negative_index", f"neg_lit_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; return {v}[-{k}]; }}", "reject")
    add("negative_index", f"neg_var_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = -{k}; return {v}[i]; }}", "reject")

# --- loop bounds ------------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("loop_index", f"loop_le_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; for (int i = 0; i <= {n}; i++) {v}[i] = ({t})i; return 0; }}",
        "reject")
    add("loop_index", f"loop_over_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; for (int i = 0; i < {n + k}; i++) {v}[i] = 0; return 0; }}",
        "reject")
    add("loop_index", f"loop_read_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int s = 0; for (int i = 0; i < {n + k}; i++) s += {v}[i]; return s; }}",
        "reject")
    add("loop_index", f"loop_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; for (int i = 0; i < {n}; i++) {v}[i] = ({t})i; return {v}[{n - 1}]; }}",
        "accept")

# --- multidimensional -------------------------------------------------------
for n in NS:
    t, k = ty(), off()
    add("multidim", f"multidim2_oob_{n}",
        f"int main(void) {{ {t} g[2][{n}] = {{0}}; return g[1][{n + k}]; }}", "reject")
    add("multidim", f"multidim2_ok_{n}",
        f"int main(void) {{ {t} g[2][{n}] = {{0}}; return g[1][{n - 1}]; }}", "accept")
    add("multidim", f"multidim3_oob_{n}",
        f"int main(void) {{ {t} g[2][3][{n}] = {{0}}; return g[1][2][{n + k}]; }}", "reject")
    add("multidim", f"multidim_first_oob_{n}",
        f"int main(void) {{ {t} g[3][{n}] = {{0}}; return g[{3 + k}][0]; }}", "reject")

# --- variable indices -------------------------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("variable_index", f"var_const_bad_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = {n + k}; return {v}[i]; }}", "reject")
    add("variable_index", f"var_const_ok_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; int i = {n - 1}; return {v}[i]; }}", "accept")
    add("variable_index", f"var_unknown_{n}",
        f"int f(int i) {{ {t} {v}[{n}] = {{0}}; return {v}[i]; }} int main(void) {{ return f(2); }}",
        "strict")

# --- pointer arithmetic / one-past-end -------------------------------------
for n in NS:
    t, v, k = ty(), nm(), off()
    add("pointer_arith", f"ptr_deref_oob_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n + k}; return *p; }}", "reject")
    add("pointer_arith", f"ptr_far_form_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n + k}; (void)p; return 0; }}",
        "reject")
    add("pointer_arith", f"ptr_one_past_deref_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; return *p; }}", "reject")
    add("pointer_arith", f"ptr_one_past_form_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n}; (void)p; return 0; }}",
        "accept")
    add("pointer_arith", f"ptr_inbounds_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} + {n - 1}; return *p; }}", "accept")
    add("pointer_arith", f"ptr_negative_{n}",
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; {t} *p = {v} - {k}; return *p; }}", "reject")

# --- heap extents / allocation size mistakes --------------------------------
for n in NS:
    t, k = ty(), off()
    add("heap_extent", f"heap_write_oob_{n}",
        f"#include <stdlib.h>\nint main(void) {{ {t} *p = malloc({n} * sizeof({t})); if (!p) return 1; "
        f"p[{n + k}] = ({t})7; free(p); return 0; }}", "reject")
    add("heap_extent", f"heap_write_ok_{n}",
        f"#include <stdlib.h>\nint main(void) {{ {t} *p = malloc({n} * sizeof({t})); if (!p) return 1; "
        f"p[{n - 1}] = ({t})7; free(p); return 0; }}", "accept")
    add("malloc_size", f"malloc_no_sizeof_{n}",
        f"#include <stdlib.h>\nint main(void) {{ int *p = malloc({n}); if (!p) return 1; "
        f"p[{n - 1}] = {n}; free(p); return 0; }}", "reject")
    src = lit(RNG.randint(n + 1, n + 8))
    add("malloc_size", f"malloc_no_plus1_{n}",
        f'#include <stdlib.h>\n#include <string.h>\nint main(void) {{ const char *s = "{src}"; '
        f'char *d = malloc(strlen(s)); strcpy(d, s); free(d); return 0; }}', "reject")
    add("malloc_size", f"malloc_str_ok_{n}",
        f'#include <stdlib.h>\n#include <string.h>\nint main(void) {{ const char *s = "{src}"; '
        f'char *d = malloc(strlen(s) + 1); strcpy(d, s); free(d); return 0; }}', "accept")
    add("heap_extent", f"heap_small_struct_{n}",
        f"#include <stdlib.h>\nstruct Sx {{ int lo[{n}]; int hi[{n}]; }};\n"
        f"int main(void) {{ struct Sx *s = malloc((size_t){n} * sizeof(int)); if (!s) return 1; "
        f"s->hi[0] = 7; free(s); return 0; }}", "reject")
    add("malloc_size", f"malloc_wrap_{n}",
        f"#include <stdlib.h>\nint main(void) {{ size_t m = {wrap()}; {t} *p = malloc(m * sizeof({t})); "
        f"if (!p) return 1; free(p); return 0; }}", "reject")
    add("malloc_size", f"calloc_wrap_{n}",
        f"#include <stdlib.h>\nint main(void) {{ size_t m = {wrap()}; int *p = calloc(m, sizeof(int)); "
        f"if (!p) return 1; free(p); return 0; }}", "reject")
    add("malloc_size", f"realloc_wrap_{n}",
        f"#include <stdlib.h>\nint main(void) {{ int *p = malloc({n}); if (!p) return 1; "
        f"void *t = realloc(p, {wrap()}); free(t); return 0; }}", "strict")

# --- libc copy/fill/format sizes (literal and computed) ---------------------
for n in NS:
    k = off()
    add("libc_memcpy", f"memcpy_lit_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; char s[64] = {{0}}; "
        f"memcpy(d, s, {n + k}); return 0; }}", "reject")
    add("libc_memcpy", f"memcpy_lit_ok_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; char s[64] = {{0}}; "
        f"memcpy(d, s, {n}); return 0; }}", "accept")
    add("libc_memcpy", f"memcpy_comp_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; char s[64] = {{0}}; size_t m = {n + k}; "
        f"memcpy(d, s, m); return 0; }}", "reject")
    add("libc_memmove", f"memmove_lit_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; char s[64] = {{0}}; "
        f"memmove(d, s, {n + k}); return 0; }}", "reject")
    add("libc_memmove", f"memmove_comp_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; char s[64] = {{0}}; size_t m = {n + k}; "
        f"memmove(d, s, m); return 0; }}", "reject")
    add("libc_memset", f"memset_lit_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; memset(d, 0, {n + k}); return 0; }}",
        "reject")
    add("libc_memset", f"memset_comp_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; size_t m = {n + k}; memset(d, 0, m); return 0; }}",
        "reject")
    add("libc_memset", f"memset_ok_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; memset(d, 0, {n}); return d[0]; }}",
        "accept")
    add("libc_memcpy", f"memcpy_overlap_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n + 12}] = \"abcdef\"; memcpy(d + 1, d, {n}); return 0; }}",
        "reject")
    add("libc_memcpy", f"memcpy_null_{n}",
        f"#include <string.h>\nint main(void) {{ char d[{n}]; char *s = 0; memcpy(d, s, {n}); return 0; }}",
        "reject")

    src_long = lit(RNG.randint(n + 2, n + 14))
    add("libc_strcpy", f"strcpy_lit_{n}",
        f'#include <string.h>\nint main(void) {{ char d[{n}]; strcpy(d, "{src_long}"); return 0; }}',
        "reject")
    add("libc_strcpy", f"strcpy_ok_{n}",
        f'#include <string.h>\nint main(void) {{ char d[{n + 16}] = {{0}}; strcpy(d, "abc"); return 0; }}',
        "accept")
    add("libc_strncpy", f"strncpy_lit_{n}",
        f'#include <string.h>\nint main(void) {{ char d[{n}]; strncpy(d, "{src_long}", {n + k}); return 0; }}',
        "reject")
    add("libc_strncpy", f"strncpy_comp_{n}",
        f'#include <string.h>\nint main(void) {{ char d[{n}]; size_t m = {n + k}; '
        f'strncpy(d, "{src_long}", m); return 0; }}', "reject")
    add("libc_strcat", f"strcat_lit_{n}",
        f'#include <string.h>\nint main(void) {{ char d[{n}] = "x"; strcat(d, "{src_long}"); return 0; }}',
        "reject")
    add("libc_strcat", f"strcat_ok_{n}",
        f'#include <string.h>\nint main(void) {{ char d[{n + 16}] = "x"; strcat(d, "ab"); return 0; }}',
        "accept")
    add("libc_snprintf", f"snprintf_lit_{n}",
        f'#include <stdio.h>\nint main(void) {{ char d[{n}]; snprintf(d, {n + k}, "%s", "{src_long}"); return 0; }}',
        "reject")
    add("libc_snprintf", f"snprintf_comp_{n}",
        f'#include <stdio.h>\nint main(void) {{ char d[{n}]; size_t m = {n + k}; '
        f'snprintf(d, m, "%s", "{src_long}"); return 0; }}', "reject")
    add("libc_snprintf", f"snprintf_ok_{n}",
        f'#include <stdio.h>\nint main(void) {{ char d[{n + 16}]; snprintf(d, sizeof(d), "%s", "abc"); return 0; }}',
        "accept")

# --- sizeof(pointer) bugs ---------------------------------------------------
for n in NS:
    t, v = ty(), nm()
    add("sizeof_pointer", f"sizeof_ptr_memset_{n}",
        f"#include <string.h>\nvoid z({t} *p) {{ memset(p, 0, sizeof(p) + {n} * sizeof(*p)); }}\n"
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; z({v}); return 0; }}", "reject")
    add("sizeof_pointer", f"sizeof_ptr_loop_{n}",
        f"#include <string.h>\nvoid z({t} *p) {{ for (size_t i = 0; i < sizeof(p) / sizeof(*p) + {n}; i++) p[i] = 0; }}\n"
        f"int main(void) {{ {t} {v}[{n}] = {{0}}; z({v}); return 0; }}", "reject")

# --- struct member overflows ------------------------------------------------
for n in NS:
    t, k = ty(), off()
    add("struct_member", f"struct_member_oob_{n}",
        f"struct Sx {{ {t} v[{n}]; }}; int main(void) {{ struct Sx s = {{{{0}}}}; return s.v[{n + k}]; }}",
        "reject")
    add("struct_member", f"struct_member_ok_{n}",
        f"struct Sx {{ {t} v[{n}]; }}; int main(void) {{ struct Sx s = {{{{0}}}}; return s.v[{n - 1}]; }}",
        "accept")
    add("struct_member", f"struct_nested_oob_{n}",
        f"struct Ix {{ {t} v[{n}]; }}; struct Ox {{ struct Ix i; }};\n"
        f"int main(void) {{ struct Ox o = {{{{ {{0}} }}}}; return o.i.v[{n + k}]; }}", "reject")


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
    assert len(CASES) >= 350, f"only {len(CASES)} cases generated"
    counts = Counter(c["expect"] for c in CASES)
    print(f"bounds350: {len(CASES)} cases, seed={SEED} (0x{SEED:X}), "
          f"workers={WORKERS}, bin={CC}")
    print(f"outcomes: reject={counts['reject']} strict={counts['strict']} "
          f"accept={counts['accept']}")

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
