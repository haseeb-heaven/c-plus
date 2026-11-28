#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator for pointer-object UB (>=100 cases).

Produces distinct, self-contained C translation units covering pointer-object
undefined behaviour.  Every case declares one of the sibling-corpus outcomes:

  reject - proven pointer UB (proven by the constructed program): must fail
           BOTH --default and --strict.  Acceptance in either mode is a hole.
  strict - conditional / implementation-defined pointer rule: MAY pass
           --default, but must fail --strict (acceptance there is a hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Families covered:

  * subtraction across distinct malloc / calloc / stack arrays
  * relational comparison across distinct malloc / calloc / stack arrays
  * one-past-end dereference
  * pointer arithmetic beyond one-past-end
  * null pointer arithmetic
  * pointer forged from an integer
  * tagged pointers (dereferenced / freed without untagging)
  * misaligned casts and dereferences
  * container_of computed from a null member pointer
  * wrong indirection levels (T** where T* expected, and vice versa)
  * incompatible function-pointer casts
  * void* arithmetic
  * strict-aliasing type punning

Safe controls are interleaved into every family: same-array subtraction,
same-array relational comparison, cross-array equality, one-past formation and
one-past comparison, valid last-element access, valid pointer from an object
address, char-alias and union punning, and matching function-pointer calls.

Programs come from deterministic seeded sweeps plus a seeded RNG (FUZZ_SEED);
each program carries a unique marker and the final pass re-rolls parameters
until TARGET distinct bodies exist.  Cases run in parallel (FUZZ_WORKERS) with
a per-check timeout.  Mismatches are saved under
tests/out/core/ag2_ptr/failures/.  This is a differential measurement, not a
proof of safety.

Environment:
  FUZZ_SEED    default 0xA6F2A1
  FUZZ_WORKERS default 8
  CPLUS_BIN    default <root>/build/c+
  AG_TARGET    default 140 (minimum 100)
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
OUT = ROOT / "tests/out/core/ag2_ptr"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA6F2A1"), 0)
TARGET = max(100, int(os.environ.get("AG_TARGET", "140")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = "#include <stdlib.h>\n"
STDDEF = "#include <stdlib.h>\n#include <stddef.h>\n"
STDINT = "#include <stdlib.h>\n#include <stdint.h>\n"
TYPES = ("int", "long", "short", "char")


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 97)


def nlen():
    """Array length (at least 3 so the small-index helpers stay in range)."""
    return RNG.randint(3, 16)


def off():
    return RNG.randint(1, 4)


def addr():
    return 0x1000 + RNG.randint(0, 0x7FFF) * 0x40


def ty():
    return RNG.choice(TYPES)


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = (f"/* ag2_ptr seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


# --- 1. subtraction across distinct malloc / calloc / stack arrays ----------
for _n in range(1, 11):
    _N = nlen()
    if _n % 4 == 0:  # safe control: same-array subtraction
        _k = RNG.randint(0, _N - 1)
        _body = tpl("int main(void){int a[@@N@@]={0};"
                    "int d=(int)(&a[@@K@@]-&a[0]);return d==@@K@@?0:1;}", N=_N, K=_k)
        _expect = "accept"
    else:
        _kind = _n % 3
        if _kind == 0:  # stack arrays
            _body = tpl("int main(void){int a[@@N@@]={0},b[@@N@@]={0};"
                        "return (int)(&a[0]-&b[0]);}", N=_N)
        elif _kind == 1:  # malloc arrays
            _body = STD + tpl("int main(void){int*a=malloc(@@N@@*sizeof(int)),"
                              "*b=malloc(@@N@@*sizeof(int));if(!a||!b)return 2;"
                              "return (int)(a-b);}", N=_N)
        else:  # calloc arrays
            _body = STD + tpl("int main(void){int*a=calloc(@@N@@,sizeof(int)),"
                              "*b=calloc(@@N@@,sizeof(int));if(!a||!b)return 2;"
                              "return (int)(a-b);}", N=_N)
        _expect = "reject"
    add("cross_sub", tpl("sub_@@I@@", I=_n), _body, _expect)

# --- 2. relational comparison across distinct malloc / calloc / stack arrays -
for _n in range(1, 11):
    _N = nlen()
    _op = "<" if _n % 2 else ">"
    if _n % 4 == 0:  # safe control: same-array relational + cross-array equality
        _k = RNG.randint(1, _N - 1)
        _body = STD + tpl("int main(void){int a[@@N@@]={0},b[@@N@@]={0};"
                          "int r=(&a[0]<&a[@@K@@])&&(a!=b);return r?0:1;}", N=_N, K=_k)
        _expect = "accept"
    else:
        _kind = _n % 3
        if _kind == 0:  # stack arrays
            _body = tpl("int main(void){int a[@@N@@]={0},b[@@N@@]={0};"
                        "return &a[0] @@OP@@ &b[0];}", N=_N, OP=_op)
        elif _kind == 1:  # malloc arrays
            _body = STD + tpl("int main(void){int*a=malloc(@@N@@*sizeof(int)),"
                              "*b=malloc(@@N@@*sizeof(int));if(!a||!b)return 2;"
                              "return a @@OP@@ b;}", N=_N, OP=_op)
        else:  # calloc arrays
            _body = STD + tpl("int main(void){int*a=calloc(@@N@@,sizeof(int)),"
                              "*b=calloc(@@N@@,sizeof(int));if(!a||!b)return 2;"
                              "return a @@OP@@ b;}", N=_N, OP=_op)
        _expect = "reject"
    add("cross_rel", tpl("rel_@@I@@", I=_n), _body, _expect)

# --- 3. one-past-end dereference ---------------------------------------------
for _n in range(1, 11):
    _N = nlen()
    _V = val()
    if _n % 4 == 0:  # safe control: valid last element
        _body = tpl("int main(void){int a[@@N@@]={0};a[@@K@@]=@@V@@;"
                    "return a[@@K@@]==@@V@@?0:1;}", N=_N, K=_N - 1, V=_V)
        _expect = "accept"
    elif _n % 4 == 1:  # conditional: index comes from an unknown argc
        _body = tpl("int main(int argc,char**v){(void)v;int a[@@N@@]={0};"
                    "return a[argc];}", N=_N)
        _expect = "strict"
    elif _n % 4 == 2:
        _body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@;return *p;}", N=_N)
        _expect = "reject"
    else:
        _body = tpl("int main(void){int a[@@N@@]={0};return a[@@N@@];}", N=_N)
        _expect = "reject"
    add("one_past_deref", tpl("onepast_@@I@@", I=_n), _body, _expect)

# --- 4. pointer arithmetic beyond one-past-end -------------------------------
for _n in range(1, 11):
    _N = nlen()
    _K = off()
    if _n % 3 == 0:  # safe control: one-past formation and comparison
        _body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@;"
                    "return p>a?0:1;}", N=_N)
        _expect = "accept"
    elif _n % 3 == 1:
        _body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@+@@K@@;"
                    "(void)p;return 0;}", N=_N, K=_K)
        _expect = "reject"
    else:
        _body = tpl("int main(void){int a[@@N@@]={0};int*p=a+@@N@@+@@K@@;"
                    "return *p;}", N=_N, K=_K)
        _expect = "reject"
    add("past_end_arith", tpl("pastend_@@I@@", I=_n), _body, _expect)

# --- 5. null pointer arithmetic ----------------------------------------------
for _n in range(1, 11):
    _K = off()
    if _n % 3 == 0:  # safe control: null comparison / check
        _body = tpl("int main(void){int*p=0;return p==0?0:1;}")
        _expect = "accept"
    elif _n % 3 == 1:
        _body = tpl("int main(void){int*p=0;int*q=p+@@K@@;(void)q;return 0;}", K=_K)
        _expect = "reject"
    else:
        _body = tpl("int main(void){int*p=0;return *(p+@@K@@);}", K=_K)
        _expect = "reject"
    add("null_arith", tpl("null_@@I@@", I=_n), _body, _expect)

# --- 6. pointer from integer -------------------------------------------------
for _n in range(1, 11):
    _A = addr()
    _T = ty()
    _V = val()
    if _n % 3 == 0:  # safe control: pointer taken from a real object
        _body = tpl("int main(void){int x=@@V@@;int*p=&x;return *p==@@V@@?0:1;}", V=_V)
        _expect = "accept"
    elif _n % 3 == 1:  # implementation-defined uintptr_t round trip
        _body = "#include <stdint.h>\n" + tpl(
            "int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x;"
            "int*p=(int*)u;return *p==@@V@@?0:1;}", V=_V)
        _expect = "strict"
    else:
        if _n % 2 == 0:
            _body = tpl("int main(void){@@T@@*p=(@@T@@*)@@A@@;return *p;}", T=_T, A=_A)
        else:
            _body = tpl("int main(void){@@T@@*p=(@@T@@*)@@A@@;*p=@@V@@;return 0;}",
                        T=_T, A=_A, V=_V)
        _expect = "reject"
    add("ptr_from_int", tpl("fromint_@@I@@", I=_n), _body, _expect)

# --- 7. tagged pointers ------------------------------------------------------
for _n in range(1, 11):
    _TAG = (_n % 3) + 1
    _V = val()
    if _n % 3 == 0:  # implementation-defined untag round trip
        _body = STDINT + tpl(
            "int main(void){int x=@@V@@;uintptr_t u=(uintptr_t)&x|@@TAG@@;"
            "int*p=(int*)(u&~(uintptr_t)@@TAG@@);*p=@@V2@@;return *p==@@V2@@?0:1;}",
            V=_V, V2=_V + 1, TAG=_TAG)
        _expect = "strict"
    elif _n % 3 == 1:  # safe control: untagged pointer
        _body = tpl("int main(void){int x=@@V@@;int*p=&x;*p=@@V2@@;"
                    "return *p==@@V2@@?0:1;}", V=_V, V2=_V + 1)
        _expect = "accept"
    else:
        if _n % 2 == 0:
            _body = STDINT + tpl(
                "int main(void){int*p=malloc(sizeof(int));if(!p)return 2;"
                "int*t=(int*)((uintptr_t)p|@@TAG@@);return *t;}", TAG=_TAG)
        else:
            _body = STDINT + tpl(
                "int main(void){int*p=malloc(sizeof(int));if(!p)return 2;"
                "int*t=(int*)((uintptr_t)p|@@TAG@@);free(t);return 0;}", TAG=_TAG)
        _expect = "reject"
    add("tagged_ptr", tpl("tagged_@@I@@", I=_n), _body, _expect)

# --- 8. misaligned casts / dereferences --------------------------------------
for _n in range(1, 11):
    _M = (_n % 3) + 1  # 1..3 bytes off an int boundary
    _V = val()
    if _n % 3 == 0:  # safe control: character alias of any object is allowed
        _body = tpl("int main(void){int x=@@V@@;char*c=(char*)&x;return c[0];}", V=_V)
        _expect = "accept"
    elif _n % 3 == 1:
        _body = tpl("int main(void){char buf[64]={0};int*p=(int*)(buf+@@M@@);"
                    "return *p;}", M=_M)
        _expect = "reject"
    else:
        _body = tpl("int main(void){char buf[64]={0};int*p=(int*)(buf+@@M@@);"
                    "*p=@@V@@;return 0;}", M=_M, V=_V)
        _expect = "reject"
    add("misaligned", tpl("misalign_@@I@@", I=_n), _body, _expect)

# --- 9. container_of computed from a null member pointer ---------------------
for _n in range(1, 11):
    _N = nlen()
    _V = val()
    if _n % 3 == 0:  # safe control: real enclosing object
        _body = STDDEF + tpl(
            "struct S{int a;int b[@@N@@];};int main(void){struct S o={0};"
            "int*m=o.b;struct S*s=(struct S*)((char*)m-offsetof(struct S,b));"
            "return s->a;}", N=_N)
        _expect = "accept"
    elif _n % 3 == 1:
        _body = STDDEF + tpl(
            "struct S{int a;int b[@@N@@];};int main(void){struct S*s="
            "(struct S*)((char*)0-offsetof(struct S,b));s->a=@@V@@;return 0;}",
            N=_N, V=_V)
        _expect = "reject"
    else:
        _body = STDDEF + tpl(
            "struct S{int a;int b[@@N@@];};int main(void){struct S*s="
            "(struct S*)((char*)0-offsetof(struct S,b));return s->a;}", N=_N)
        _expect = "reject"
    add("container_of_null", tpl("container_@@I@@", I=_n), _body, _expect)

# --- 10. wrong indirection levels --------------------------------------------
for _n in range(1, 11):
    _V = val()
    if _n % 3 == 0:  # safe control: matching indirection
        _body = tpl("void f(int*p){*p=@@V@@;}int main(void){int x=0;f(&x);"
                    "return x==@@V@@?0:1;}", V=_V)
        _expect = "accept"
    elif _n % 3 == 1:  # T** passed where T* is expected
        _body = tpl("void f(int*p){*p=@@V@@;}int main(void){int x=@@V@@;"
                    "int*q=&x;int**pp=&q;f(pp);return 0;}", V=_V)
        _expect = "reject"
    else:  # T* passed where T** is expected
        _body = tpl("void f(int**p){**p=@@V@@;}int main(void){int x=@@V@@;"
                    "f(&x);return 0;}", V=_V)
        _expect = "reject"
    add("indirection", tpl("indir_@@I@@", I=_n), _body, _expect)

# --- 11. incompatible function-pointer casts ---------------------------------
for _n in range(1, 11):
    _V = val()
    if _n % 3 == 0:  # safe control: matching signature
        _body = tpl("typedef int(*Fi)(int);int g(int x){return x+@@V@@;}"
                    "int main(void){Fi f=g;return f(1)==1+@@V@@?0:1;}", V=_V)
        _expect = "accept"
    else:  # cast to an incompatible signature then call it
        _body = tpl("typedef int(*Fi)(int);int g(void){return @@V@@;}"
                    "int main(void){Fi f=(Fi)g;return f(@@N@@);}", V=_V, N=_n)
        _expect = "strict"
    add("func_ptr_cast", tpl("fnptr_@@I@@", I=_n), _body, _expect)

# --- 12. void* arithmetic ----------------------------------------------------
for _n in range(1, 11):
    _K = off()
    if _n % 3 == 0:  # safe control: void* equality / comparison only
        _body = tpl("int main(void){char buf[64]={0};void*p=buf;void*q=buf;"
                    "return p==q?0:1;}")
        _expect = "accept"
    elif _n % 3 == 1:
        _body = tpl("int main(void){char buf[64]={0};void*p=buf;void*q=p+@@K@@;"
                    "(void)q;return 0;}", K=_K)
        _expect = "reject"
    else:
        _body = tpl("int main(void){char buf[64]={0};void*p=buf;void*q=p-@@K@@;"
                    "(void)q;return 0;}", K=_K)
        _expect = "reject"
    add("void_arith", tpl("void_@@I@@", I=_n), _body, _expect)

# --- 13. strict-aliasing type punning ---------------------------------------
for _n in range(1, 11):
    _V = val()
    if _n % 3 == 0:  # safe control: union / character punning
        _body = tpl("union U{int i;float f;};int main(void){union U u;u.i=@@V@@;"
                    "return (int)u.f;}", V=_V)
        _expect = "accept"
    elif _n % 3 == 1:
        _body = tpl("int main(void){int x=@@V@@;float*f=(float*)&x;"
                    "return (int)*f;}", V=_V)
        _expect = "strict"
    else:
        _body = tpl("int main(void){float x=1.0f;int*p=(int*)&x;*p=@@V@@;"
                    "return 0;}", V=_V)
        _expect = "strict"
    add("strict_alias", tpl("alias_@@I@@", I=_n), _body, _expect)


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll until TARGET distinct programs exist.
# ---------------------------------------------------------------------------
def seeded_extra(i):
    while True:
        _N = nlen()
        _K = off()
        _V = val()
        _A = addr()
        _T = ty()
        _kind = i % 13
        if _kind == 0:
            return ("cross_sub", tpl("m_sub_@@I@@", I=i), tpl(
                "int main(void){int a[@@N@@]={0},b[@@N@@]={0};return (int)(&a[0]-&b[0]);}",
                N=_N), "reject")
        if _kind == 1:
            return ("cross_rel", tpl("m_rel_@@I@@", I=i), STD + tpl(
                "int main(void){int*a=calloc(@@N@@,sizeof(int)),"
                "*b=calloc(@@N@@,sizeof(int));if(!a||!b)return 2;return a<b;}",
                N=_N), "reject")
        if _kind == 2:
            return ("one_past_deref", tpl("m_onepast_@@I@@", I=i), tpl(
                "int main(void){int a[@@N@@]={0};int*p=a+@@N@@;return *p;}",
                N=_N), "reject")
        if _kind == 3:
            return ("past_end_arith", tpl("m_pastend_@@I@@", I=i), tpl(
                "int main(void){int a[@@N@@]={0};int*p=a+@@N@@+@@K@@;(void)p;return 0;}",
                N=_N, K=_K), "reject")
        if _kind == 4:
            return ("null_arith", tpl("m_null_@@I@@", I=i), tpl(
                "int main(void){int*p=0;return *(p+@@K@@);}", K=_K), "reject")
        if _kind == 5:
            return ("ptr_from_int", tpl("m_fromint_@@I@@", I=i), tpl(
                "int main(void){@@T@@*p=(@@T@@*)@@A@@;return *p;}", T=_T, A=_A), "reject")
        if _kind == 6:
            return ("tagged_ptr", tpl("m_tagged_@@I@@", I=i), STDINT + tpl(
                "int main(void){int*p=malloc(sizeof(int));if(!p)return 2;"
                "int*t=(int*)((uintptr_t)p|@@TAG@@);return *t;}",
                TAG=(_N % 3) + 1), "reject")
        if _kind == 7:
            return ("misaligned", tpl("m_misalign_@@I@@", I=i), tpl(
                "int main(void){char buf[64]={0};int*p=(int*)(buf+@@M@@);return *p;}",
                M=(_N % 3) + 1), "reject")
        if _kind == 8:
            return ("container_of_null", tpl("m_container_@@I@@", I=i), STDDEF + tpl(
                "struct S{int a;int b[@@N@@];};int main(void){struct S*s="
                "(struct S*)((char*)0-offsetof(struct S,b));return s->a;}", N=_N), "reject")
        if _kind == 9:
            return ("indirection", tpl("m_indir_@@I@@", I=i), tpl(
                "void f(int**p){**p=@@V@@;}int main(void){int x=@@V@@;f(&x);return 0;}",
                V=_V), "reject")
        if _kind == 10:
            return ("func_ptr_cast", tpl("m_fnptr_@@I@@", I=i), tpl(
                "typedef int(*Fi)(int);int g(void){return @@V@@;}"
                "int main(void){Fi f=(Fi)g;return f(1);}", V=_V), "strict")
        if _kind == 11:
            return ("void_arith", tpl("m_void_@@I@@", I=i), tpl(
                "int main(void){char buf[64]={0};void*p=buf;void*q=p+@@K@@;(void)q;return 0;}",
                K=_K), "reject")
        return ("strict_alias", tpl("m_alias_@@I@@", I=i), tpl(
            "int main(void){int x=@@V@@;float*f=(float*)&x;return (int)*f;}", V=_V),
            "strict")


_i = 0
while len(CASES) < TARGET:
    _family, _name, _body, _expect = seeded_extra(_i)
    add(_family, _name, _body, _expect)
    _i += 1
    if _i > TARGET * 50:
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
        diag = (proc.stderr + proc.stdout).strip()
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
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
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
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        first_diag(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        first_diag(modes["--strict"][1]),
        "",
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
    print("ag2_ptr: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families (%d): %s" % (len(families),
          ", ".join("%s=%d" % kv for kv in sorted(families.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(run_case, CASES))

    mismatches = sorted(r["detail"] for r in results if r["reasons"])
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    false_negatives = sum(1 for r in results if r["expect"] != "accept" and r["reasons"])
    hard = sum(1 for r in results for reason in r["reasons"]
               if "TIMEOUT" in reason or "OS ERROR" in reason or "CRASH" in reason)

    print("results: %d cases, %d mismatches (%d false negatives, %d false positives, %d hard errors)"
          % (len(CASES), len(mismatches), false_negatives, len(false_positives), hard))
    print("false positives: %d" % len(false_positives))
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print("  ... %d more" % (len(false_positives) - 20))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        "FAIL" if (mismatches or unique != len(CASES)) else "PASS",
        len(CASES), len(mismatches), len(false_positives)))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
