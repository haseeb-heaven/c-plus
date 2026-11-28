#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator for NULL passed to libc (>=100 cases).

Produces distinct, self-contained, VALID C translation units that exercise a
null pointer passed to a C standard-library function *after assignment*: the
null value is first stored in a variable (literal 0, NULL macro, cast, function
return, struct field, array element) and that variable is then handed to libc.

Every case declares one of two outcome classes:

  reject - undefined behaviour (a null pointer reaches a libc function that
           requires a valid pointer).  A correct checker must fail BOTH
           --default and --strict.  Acceptance in either mode is a hole.
  accept - safe control (non-null pointer, guarded call, or free(NULL), which
           the C standard defines as a no-op).  A correct checker must pass
           BOTH modes; rejection here is a false positive.

FAMILIES (sink -> outcome):

  * memcpy_null     - memcpy with src=NULL or dst=NULL                 (reject)
  * memmove_null    - memmove with src=NULL or dst=NULL                (reject)
  * memset_null     - memset with s=NULL                              (reject)
  * strcpy_null     - strcpy with src=NULL or dst=NULL                 (reject)
  * strlen_null     - strlen with s=NULL                              (reject)
  * strcmp_null     - strcmp with s1=NULL or s2=NULL                   (reject)
  * strcat_null     - strcat with src=NULL or dst=NULL                 (reject)
  * printf_s_null   - printf("%s", NULL)                              (reject)
  * fopen_null      - fopen(NULL, mode)                               (reject)
  * fclose_null     - fclose(NULL)                                    (reject)
  * zero_len_null   - memcpy/memmove/memset with n=0 and a null ptr    (reject)
                      (the C standard still requires valid pointers, so this
                       is UB even though the length is zero)
  * null_source     - null obtained via function return / struct field /
                      array element, then passed to a libc sink       (reject)
  * safe_nonnull    - valid controls: real, non-null pointers         (accept)
  * safe_guarded    - valid controls: null pointer guarded by `if(p)` (accept)
  * safe_free_null  - valid controls: free(NULL) through all sources  (accept)

Programs come from a deterministic seeded sweep; the marker plus seeded buffer
sizes, values, identifiers and null-source forms make every program distinct.
Cases run in parallel (FUZZ_WORKERS) with a per-check timeout.  Mismatches and
their combined diagnostics are saved under tests/out/core/nl_libc/failures/.
This is a differential measurement, not a proof of safety, and does not modify
the compiler.

Environment:
  FUZZ_SEED    default 0x4E4C4C42  ("NLLB")
  FUZZ_WORKERS default 8
  CPLUS_BIN    default <root>/build/c+
  NL_TARGET    default 140 (minimum 100)
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
OUT = ROOT / "tests/out/core/nl_libc"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4E4C4C42"), 0)
TARGET = max(100, int(os.environ.get("NL_TARGET", "140")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # normalized program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = ("#include <stdio.h>\n"
       "#include <stdlib.h>\n"
       "#include <string.h>\n"
       "#include <stddef.h>\n")

FAMILIES = (
    "memcpy_null", "memmove_null", "memset_null", "strcpy_null",
    "strlen_null", "strcmp_null", "strcat_null", "printf_s_null",
    "fopen_null", "fclose_null", "zero_len_null", "null_source",
    "safe_nonnull", "safe_guarded", "safe_free_null",
)

# All the syntactic ways a null value is first ASSIGNED to a pointer variable.
PRODUCERS = ("zero", "null", "cast", "voidcast", "func", "struct", "array")
INDIRECT = ("func", "struct", "array")


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "accept"), (name, expect)
    normalized = ("/* nl_libc seed=0x%X case=%s family=%s */\n"
                  % (SEED, name, family)) + body.strip() + "\n"
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


def null_source(ctype, tag, producer, var, rng):
    """Return (helpers, stmt) declaring `<ctype> *<var>` assigned a null value.

    `helpers` are extra declarations that must precede main (helper function
    or struct type); `stmt` declares the variable and is placed in main.  The
    assignment happens before the pointer is passed to any libc sink.
    """
    if producer == "zero":
        return "", f"{ctype} *{var} = 0;"
    if producer == "null":
        return "", f"{ctype} *{var} = NULL;"
    if producer == "cast":
        return "", f"{ctype} *{var} = ({ctype} *)0;"
    if producer == "voidcast":
        return "", f"{ctype} *{var} = (void *)0;"
    if producer == "func":
        helpers = f"static {ctype} *nullfn_{tag}(void) {{ return 0; }}\n"
        return helpers, f"{ctype} *{var} = nullfn_{tag}();"
    if producer == "struct":
        helpers = f"struct S_{tag} {{ {ctype} *field; }};\n"
        stmt = (f"struct S_{tag} s_{tag} = {{0}}; "
                f"{ctype} *{var} = s_{tag}.field;")
        return helpers, stmt
    if producer == "array":
        idx = rng.randint(0, 2)
        stmt = (f"{ctype} *arr_{tag}[3] = {{0}}; "
                f"{ctype} *{var} = arr_{tag}[{idx}];")
        return "", stmt
    raise ValueError("unknown producer: " + producer)


def seeded_case(fam, i):
    """Build one deterministic (family, name, body, expect) tuple."""
    name = "%s_%03d" % (fam, i)
    n = RNG.randint(4, 16)
    k = RNG.randint(1, n - 1)
    v = RNG.randint(1, 120)
    c = RNG.randint(0, 255)
    tag = "n%d" % i
    prod = PRODUCERS[i % len(PRODUCERS)]
    helpers = ""
    comment = ""

    # ---- null passed to memcpy -------------------------------------------
    if fam == "memcpy_null":
        if i % 2 == 0:  # null src, valid dst
            helpers, src = null_source("char", tag, prod, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nmemcpy(dst, src, {k});\nreturn 0;"
        else:            # null dst, valid src
            helpers, dst = null_source("char", tag, prod, "dst", RNG)
            stmts = f"char src[{n}] = {{0}};\n{dst}\nmemcpy(dst, src, {k});\nreturn 0;"
        expect = "reject"

    # ---- null passed to memmove ------------------------------------------
    elif fam == "memmove_null":
        if i % 2 == 0:
            helpers, src = null_source("char", tag, prod, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nmemmove(dst, src, {k});\nreturn 0;"
        else:
            helpers, dst = null_source("char", tag, prod, "dst", RNG)
            stmts = f"char src[{n}] = {{0}};\n{dst}\nmemmove(dst, src, {k});\nreturn 0;"
        expect = "reject"

    # ---- null passed to memset -------------------------------------------
    elif fam == "memset_null":
        helpers, p = null_source("char", tag, prod, "p", RNG)
        stmts = f"{p}\nmemset(p, {c}, {k});\nreturn 0;"
        expect = "reject"

    # ---- null passed to strcpy -------------------------------------------
    elif fam == "strcpy_null":
        if i % 2 == 0:  # null src
            helpers, src = null_source("char", tag, prod, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nstrcpy(dst, src);\nreturn 0;"
        else:            # null dst
            helpers, dst = null_source("char", tag, prod, "dst", RNG)
            stmts = f'char src[] = "abc{v % 7}";\n{dst}\nstrcpy(dst, src);\nreturn 0;'
        expect = "reject"

    # ---- null passed to strlen -------------------------------------------
    elif fam == "strlen_null":
        helpers, p = null_source("char", tag, prod, "p", RNG)
        stmts = f"{p}\nreturn (int)strlen(p);"
        expect = "reject"

    # ---- null passed to strcmp -------------------------------------------
    elif fam == "strcmp_null":
        if i % 2 == 0:  # null second argument
            helpers, b = null_source("char", tag, prod, "b", RNG)
            stmts = f'char a[] = "x";\n{b}\nreturn strcmp(a, b);'
        else:            # null first argument
            helpers, a = null_source("char", tag, prod, "a", RNG)
            stmts = f'char b[] = "y";\n{a}\nreturn strcmp(a, b);'
        expect = "reject"

    # ---- null passed to strcat -------------------------------------------
    elif fam == "strcat_null":
        if i % 2 == 0:  # null src
            helpers, src = null_source("char", tag, prod, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nstrcat(dst, src);\nreturn 0;"
        else:            # null dst
            helpers, dst = null_source("char", tag, prod, "dst", RNG)
            stmts = f'char src[] = "z";\n{dst}\nstrcat(dst, src);\nreturn 0;'
        expect = "reject"

    # ---- printf("%s", NULL) ----------------------------------------------
    elif fam == "printf_s_null":
        helpers, p = null_source("char", tag, prod, "p", RNG)
        stmts = f'{p}\nprintf("%s", p);\nreturn 0;'
        expect = "reject"

    # ---- fopen(NULL, mode) ------------------------------------------------
    elif fam == "fopen_null":
        helpers, path = null_source("char", tag, prod, "path", RNG)
        stmts = (f'{path}\nFILE *f = fopen(path, "r");\n'
                 'if (f) fclose(f);\nreturn 0;')
        expect = "reject"

    # ---- fclose(NULL) -----------------------------------------------------
    elif fam == "fclose_null":
        helpers, f = null_source("FILE", tag, prod, "f", RNG)
        stmts = f"{f}\nfclose(f);\nreturn 0;"
        expect = "reject"

    # ---- zero length with null (UB per C standard) -----------------------
    elif fam == "zero_len_null":
        comment = ("/* C standard: n==0 still requires valid pointers; "
                   "a null pointer is UB here */\n")
        which = i % 3
        if which == 0:
            helpers, src = null_source("char", tag, prod, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nmemcpy(dst, src, 0);\nreturn 0;"
        elif which == 1:
            helpers, src = null_source("char", tag, prod, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nmemmove(dst, src, 0);\nreturn 0;"
        else:
            helpers, p = null_source("char", tag, prod, "p", RNG)
            stmts = f"{p}\nmemset(p, {c}, 0);\nreturn 0;"
        expect = "reject"

    # ---- null obtained indirectly (func return / struct field / array) ---
    elif fam == "null_source":
        indirect = INDIRECT[i % len(INDIRECT)]
        which = i % 4
        if which == 0:
            helpers, p = null_source("char", tag, indirect, "p", RNG)
            stmts = f"{p}\nreturn (int)strlen(p);"
        elif which == 1:
            helpers, src = null_source("char", tag, indirect, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nstrcpy(dst, src);\nreturn 0;"
        elif which == 2:
            helpers, b = null_source("char", tag, indirect, "b", RNG)
            stmts = f'char a[] = "x";\n{b}\nreturn strcmp(a, b);'
        else:
            helpers, src = null_source("char", tag, indirect, "src", RNG)
            stmts = f"char dst[{n}] = {{0}};\n{src}\nmemcpy(dst, src, {k});\nreturn 0;"
        expect = "reject"

    # ---- safe control: real, non-null pointers ---------------------------
    elif fam == "safe_nonnull":
        which = i % 9
        if which == 0:
            stmts = f"char a[{n}]={{0}}, b[{n}]={{0}};\nmemcpy(a,b,{k});\nreturn 0;"
        elif which == 1:
            stmts = f"char a[{n}]={{0}}, b[{n}]={{0}};\nmemmove(a,b,{k});\nreturn 0;"
        elif which == 2:
            stmts = f"char a[{n}]={{0}};\nmemset(a,{c},{k});\nreturn 0;"
        elif which == 3:
            stmts = f'char a[{n}]={{0}}; char b[]="ab";\nstrcpy(a,b);\nreturn 0;'
        elif which == 4:
            stmts = 'char a[] = "abc";\nreturn (int)strlen(a);'
        elif which == 5:
            stmts = 'char a[] = "a", b[] = "b";\nreturn strcmp(a,b);'
        elif which == 6:
            stmts = f'char a[{n}]={{0}}; char b[]="x";\nstrcat(a,b);\nreturn 0;'
        elif which == 7:
            stmts = 'char *p = "ok";\nprintf("%s", p);\nreturn 0;'
        else:
            stmts = 'FILE *f = fopen("nl_libc_input.txt", "r");\nif (f) fclose(f);\nreturn 0;'
        expect = "accept"

    # ---- safe control: null pointer guarded by if(p) ---------------------
    elif fam == "safe_guarded":
        which = i % 9
        if which == 0:
            stmts = f"char a[{n}]={{0}}; char *p=0; if(p) memcpy(a,p,{k}); return 0;"
        elif which == 1:
            stmts = f"char a[{n}]={{0}}; char *p=0; if(p) memmove(a,p,{k}); return 0;"
        elif which == 2:
            stmts = f"char *p=0; if(p) memset(p,{c},{k}); return 0;"
        elif which == 3:
            stmts = f'char a[{n}]={{0}}; char *p=0; if(p) strcpy(a,p); return 0;'
        elif which == 4:
            stmts = "char *p=0; if(p) return (int)strlen(p); return 0;"
        elif which == 5:
            stmts = 'char a[]="x"; char *p=0; if(p) return strcmp(a,p); return 0;'
        elif which == 6:
            stmts = f'char a[{n}]={{0}}; char *p=0; if(p) strcat(a,p); return 0;'
        elif which == 7:
            stmts = 'char *p=0; if(p) printf("%s", p); return 0;'
        else:
            stmts = 'char *p=0; if(p){FILE *f=fopen(p,"r"); if(f) fclose(f);} return 0;'
        expect = "accept"

    # ---- safe control: free(NULL) through every null-source form ---------
    elif fam == "safe_free_null":
        helpers, p = null_source("void", tag, prod, "p", RNG)
        stmts = f"{p}\nfree(p);\nreturn 0;"
        expect = "accept"

    else:
        raise ValueError("unknown family: " + fam)

    body = STD + helpers + comment + "int main(void) {\n" + stmts + "\n}\n"
    return (fam, name, body, expect)


# --- Generate a deterministic seeded sweep, enough to reach TARGET. ---------
_PER_FAMILY = max(7, TARGET // len(FAMILIES) + 1)
for _fi, _fam in enumerate(FAMILIES):
    for _j in range(_PER_FAMILY):
        add(*seeded_case(_fam, _fi * _PER_FAMILY + _j))

# --- Fill to TARGET with additional seeded variants if needed. --------------
_extra = 0
while len(CASES) < TARGET:
    add(*seeded_case("extra_%d" % (_extra % len(FAMILIES)), 10000 + _extra))
    _extra += 1
    if _extra > TARGET * 50:
        raise RuntimeError("could not reach TARGET distinct cases")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def run_case(case):
    family, name, body, expect = case
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT)
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc)
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        diag = (proc.stderr + "\n" + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    false_positive = False
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
    else:  # accept
        false_positive = d_rej or s_rej
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    result = {
        "name": name, "family": family, "expect": expect,
        "reasons": reasons, "false_positive": false_positive,
        "d_rej": d_rej, "s_rej": s_rej, "modes": modes,
    }
    if not reasons:
        return result

    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "kind: %s" % ("false_positive" if expect == "accept" else "hole"),
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        first_diag(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        first_diag(modes["--strict"][1]),
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
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    result["detail"] = (
        "%s [%s] expect=%s reasons=%s | DEFAULT rejected=%s: %s | "
        "STRICT rejected=%s: %s" % (
            name, family, expect, ";".join(reasons),
            d_rej, first_diag(modes["--default"][1]),
            s_rej, first_diag(modes["--strict"][1])))
    return result


def main():
    bodies = [b for _, _, b, _ in CASES]
    unique = len(set(bodies))
    counts = Counter(e for _, _, _, e in CASES)
    families = Counter(f for f, _, _, _ in CASES)
    print("nl_libc: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d accept=%d"
          % (counts["reject"], counts["accept"]))
    print("families (%d): %s" % (len(families),
          ", ".join("%s=%d" % kv for kv in sorted(families.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(run_case, CASES))

    mismatched = [r for r in results if r["reasons"]]
    mismatch_names = sorted(r["name"] for r in mismatched)
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    false_negatives = sum(1 for r in results if r["expect"] == "reject" and r["reasons"])
    hard = sum(1 for r in mismatched
               if any("TIMEOUT" in x or "OS ERROR" in x or "CRASH" in x
                      for x in r["reasons"]))
    by_family = Counter(r["family"] for r in mismatched)

    print("results: %d cases, %d mismatches (%d safety holes, %d false positives, %d hard errors)"
          % (len(CASES), len(mismatched), false_negatives, len(false_positives), hard))
    if by_family:
        print("mismatches by family: " + " ".join(
            "%s=%d" % kv for kv in sorted(by_family.items())))
    print("false positives: %d" % len(false_positives))
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print("  ... %d more" % (len(false_positives) - 20))
    print("first 10 mismatch names:")
    for nm in mismatch_names[:10]:
        print("  " + nm)
    if len(mismatch_names) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatch_names) - 10))
    if mismatched:
        print("saved %d mismatch fixtures in %s" % (len(mismatched), FAILS))
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        "FAIL" if (mismatched or unique != len(CASES)) else "PASS",
        len(CASES), len(mismatched), len(false_positives)))
    return 1 if mismatched or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
