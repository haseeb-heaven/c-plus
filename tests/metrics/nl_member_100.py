#!/usr/bin/env python3
"""Deterministic NULL-USE-FORM generator: >=100 distinct small C programs.

Every generated program is a small, self-contained C translation unit produced
from a seeded sweep over null-pointer USE FORMS, the different ways a pointer
can be set to null, and guarded safe controls.  It follows the convention of
the sibling generators (bounds_350.py / ub_ptr_260.py / ag2_*_100.py):

  reject - proven null-pointer use after assignment to null: must fail BOTH
           --default and --strict.  Acceptance in either mode is a hole.
  accept - valid control (a real guard, a comparison, or a pointer to a live
           object): must pass BOTH modes (rejection is a false positive).

The pointer is always assigned null first (via NULL, literal 0, a cast, a
late assignment, a void* cast, or 0x0) and then used through one of these
forms:

  write        *p = x
  read         x = *p
  sub0         p[0]
  subi         p[i]
  arrow        p->field
  add_deref    (p + 1) then deref
  incderef     ++p then deref
  sub0len      p[0].len
  memcpy       memcpy into p
  addrsub0     &p[0]
  deref_field  (*p).field
  compound     *p += 1
  funarg       function call with *p as argument

Safe controls guard the same forms (if (p) ..., if (!p) return ..., short
circuit &&, null comparisons, no-deref, and pointers to live objects).

Mismatches are printed (counts + first 10) and the offending source plus
diagnostics are saved under tests/out/core/nl_member/failures/.  Cases run in
parallel (FUZZ_WORKERS, default 8) with a per-check timeout.  The generator is
seeded (FUZZ_SEED, default 0xB17E), so a run with the same seed is reproducible.
This is a differential measurement over the declared subset, not a proof of
safety.

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
OUT = ROOT / "tests/out/core/nl_member"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xB17E"), 0)
TIMEOUT = 60

CASES = []    # dict(name, body, expect, family)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

INCLUDES = "#include <stddef.h>\n#include <string.h>\n"

NAMES = ("p", "q", "r", "ptr", "wp", "z")
NULL_STYLES = ("NULL", "zero", "cast", "assign", "voidcast", "hex")
UNSAFE_FORMS = (
    "write", "read", "sub0", "subi", "arrow", "add_deref", "incderef",
    "sub0len", "memcpy", "addrsub0", "deref_field", "compound", "funarg",
)
SAFE_FORMS = (
    "guard_write", "guard_read", "guard_early", "guard_sub0", "guard_arrow",
    "guard_memcpy", "valid_write", "null_compare", "no_deref", "shortcircuit",
    "guard_deref_field", "guard_compound", "valid_arrow",
)
STRUCT_FORMS = ("arrow", "sub0len", "deref_field")


def val():
    """A seeded small non-zero integer constant."""
    return RNG.randint(1, 97)


def idx():
    """A seeded small positive index/offset."""
    return RNG.randint(1, 5)


def nm():
    return RNG.choice(NAMES)


def ty():
    return RNG.choice(("int", "long", "short", "char"))


def decl_null(ctype, pname, style):
    """Assign null to *pname using one of the requested styles."""
    if style == "NULL":
        return f"{ctype} *{pname} = NULL;"
    if style == "zero":
        return f"{ctype} *{pname} = 0;"
    if style == "cast":
        return f"{ctype} *{pname} = ({ctype} *)0;"
    if style == "assign":
        return f"{ctype} *{pname}; {pname} = NULL;"
    if style == "voidcast":
        return f"{ctype} *{pname} = (void *)0;"
    if style == "hex":
        return f"{ctype} *{pname} = 0x0;"
    raise ValueError(style)


def wrap(prefix, stmts):
    return INCLUDES + prefix + "int main(void) {\n" + stmts + "\n}\n"


def unsafe_program(form, style):
    """Build a program that assigns null and then uses the pointer in *form*."""
    V, V2, I, K = val(), val(), idx(), idx()
    P, T = nm(), ty()
    prefix = ""
    if form in STRUCT_FORMS:
        PT = "struct S"
        if form == "sub0len":
            prefix = "struct S { int len; int data[4]; };\n"
        else:
            prefix = "struct S { int f; int g; };\n"
    else:
        PT = T
    D = decl_null(PT, P, style)

    if form == "write":
        stmts = f"{D}\n*{P} = {V};\nreturn 0;"
    elif form == "read":
        stmts = f"{D}\nint x = *{P};\nreturn x;"
    elif form == "sub0":
        stmts = f"{D}\nreturn {P}[0];"
    elif form == "subi":
        stmts = f"{D}\nint i = {I};\nreturn {P}[i];"
    elif form == "arrow":
        stmts = f"{D}\nreturn {P}->f;"
    elif form == "add_deref":
        stmts = f"{D}\nreturn *({P} + {K});"
    elif form == "incderef":
        stmts = f"{D}\n++{P};\nreturn *{P};"
    elif form == "sub0len":
        stmts = f"{D}\nreturn {P}[0].len;"
    elif form == "memcpy":
        stmts = f"{D}\nint src = {V};\nmemcpy({P}, &src, sizeof src);\nreturn 0;"
    elif form == "addrsub0":
        stmts = f"{D}\n{T} *q = &{P}[0];\n(void)q;\nreturn 0;"
    elif form == "deref_field":
        stmts = f"{D}\nreturn (*{P}).f;"
    elif form == "compound":
        stmts = f"{D}\n*{P} += {K};\nreturn 0;"
    elif form == "funarg":
        prefix += f"int sink(int x) {{ return x; }}\n"
        stmts = f"{D}\nreturn sink(*{P});"
    else:
        raise ValueError(form)
    return wrap(prefix, stmts)


def safe_program(form, variant):
    """Build a guarded / valid control program for the same use forms."""
    V, V2, R, K = val(), val(), val(), idx()
    P, T = nm(), ty()
    prefix = ""
    if form in ("guard_arrow", "guard_deref_field", "valid_arrow"):
        prefix = "struct S { int f; int g; };\n"
        PT = "struct S"
    else:
        PT = T
    style = "zero" if variant % 2 == 0 else "NULL"
    D = decl_null(PT, P, style)

    if form == "guard_write":
        stmts = f"{D}\nif({P}) *{P} = {V};\nreturn 0;"
    elif form == "guard_read":
        stmts = f"{D}\nif({P} != NULL) {{ int x = *{P}; (void)x; }}\nreturn 0;"
    elif form == "guard_early":
        stmts = f"{D}\nif(!{P}) return {R};\nreturn *{P};"
    elif form == "guard_sub0":
        stmts = f"{D}\nif({P}) return {P}[0];\nreturn 0;"
    elif form == "guard_arrow":
        stmts = f"{D}\nif({P}) return {P}->f;\nreturn 0;"
    elif form == "guard_memcpy":
        stmts = f"{D}\nint src = {V};\nif({P}) memcpy({P}, &src, sizeof src);\nreturn 0;"
    elif form == "valid_write":
        stmts = f"{T} x = {V}; {T} *{P} = &x; *{P} = {V2}; return x;"
    elif form == "null_compare":
        stmts = f"{D}\nreturn {P} == NULL;"
    elif form == "no_deref":
        stmts = f"{D}\n(void){P};\nreturn 0;"
    elif form == "shortcircuit":
        stmts = f"{D}\nreturn {P} && *{P};"
    elif form == "guard_deref_field":
        stmts = f"{D}\nif({P}) return (*{P}).f;\nreturn 0;"
    elif form == "guard_compound":
        stmts = f"{D}\nif({P}) *{P} += {K};\nreturn 0;"
    elif form == "valid_arrow":
        stmts = f"{PT} obj = {{ {V} }}; {PT} *{P} = &obj; return {P}->f;"
    else:
        raise ValueError(form)
    return wrap(prefix, stmts)


def add(family, name, body, expect):
    assert expect in ("reject", "accept"), (name, expect)
    # Unique, case-specific, valid-C marker: every program has unique source.
    normalized = f"/* nl_member seed=0x{SEED:X} case={name} family={family} */\n" + body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({"name": name, "body": normalized, "expect": expect, "family": family})


# ---------------------------------------------------------------------------
# Seeded generation: 6 sweeps x 13 unsafe forms (each form sees all 6 null
# styles) + 2 sweeps x 13 guarded controls = 104 distinct programs.
# ---------------------------------------------------------------------------
for n in range(1, 7):
    for fi, form in enumerate(UNSAFE_FORMS):
        style = NULL_STYLES[(n + fi) % len(NULL_STYLES)]
        add(form, f"nl_{form}_{style}_{n}", unsafe_program(form, style), "reject")

for n in range(1, 3):
    for form in SAFE_FORMS:
        add(f"safe_{form}", f"safe_{form}_{n}", safe_program(form, n), "accept")


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
    fams = Counter(c["family"] for c in CASES)
    print(f"nl_member: {len(CASES)} cases, seed={SEED} (0x{SEED:X}), "
          f"workers={WORKERS}, bin={CC}")
    print(f"outcomes: reject={counts['reject']} accept={counts['accept']}")
    print(f"families ({len(fams)}): " + ", ".join(f"{k}={v}" for k, v in sorted(fams.items())))

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for f in pool.map(check_one, CASES):
            failures.extend(f)
    failures.sort()

    false_positives = [f for f in failures if "FALSE-POSITIVE" in f]
    print(f"mismatches: {len(failures)}")
    print(f"false positives: {len(false_positives)}")
    print("first 10 mismatch names:")
    for line in failures[:10]:
        print(f"  {line}")
    if failures:
        print(f"saved fixtures in {FAILS}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
