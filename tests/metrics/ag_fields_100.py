#!/usr/bin/env python3
"""Deterministic SEEDED adversarial generator for struct/container field ownership.

Produces >=100 distinct small C programs (default target 140) covering field
ownership and field bounds:

  * pointer field allocated, freed, then used (deref / indexed read / write);
  * struct copied by value, then double-freed or used after the original free;
  * array-of-struct shared pointer aliases (alias used, or double-free);
  * nested struct: outer freed while an inner pointer field / element is used;
  * struct field buffer indexed past its declared extent;
  * undersized allocation for a trailing field array, then OOB access;
  * flexible array member written past the actual allocation;
  * container_of computed from a null member pointer;
  * shallow copy of an SSO-style struct whose pointer aliases its own buffer;
  * struct member address handed to a function as an out-parameter, then
    double-freed / used after the callee frees it.

Safe controls (independent fields, exactly-once free, in-extent access, real
container_of base) are included as ``accept`` cases.

Every case declares one of the usual sibling-corpus outcomes:

  reject - proven ownership/bounds error: must fail BOTH modes.
  strict - conditional / unverified error: may pass --default, must fail
           --strict.
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Programs come from deterministic parameter sweeps plus a seeded RNG
(FUZZ_SEED); the final seeded pass re-rolls parameters until each new body is
distinct from every earlier one.  Cases run in parallel (FUZZ_WORKERS) with a
per-check timeout.  Mismatches are saved under
tests/out/core/ag_fields/failures/.  This is a differential measurement, not a
proof of safety.

Environment:
  FUZZ_SEED    default 0xA6F14C
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
OUT = ROOT / "tests/out/core/ag_fields"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA6F14C"), 0)
TARGET = max(100, int(os.environ.get("AG_TARGET", "140")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = "#include <stdlib.h>\n"
STDDEF = "#include <stdlib.h>\n#include <stddef.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 50)


def jump(n):
    return RNG.randint(1, 4)


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


# --- 1. pointer field freed, then used --------------------------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _k = RNG.randint(0, _n - 1)
    _v = val()
    if _i % 3 == 0:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;"
            "*s->p=@@V@@;free(s->p);return *s->p;}", N=_n, V=_v)
    elif _i % 3 == 1:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;"
            "free(s->p);return s->p[@@K@@];}", N=_n, K=_k)
    else:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;"
            "free(s->p);s->p[@@K@@]=@@V@@;return 0;}", N=_n, K=_k, V=_v)
    add("ptr_field_free_use", tpl("ptr_use_@@I@@", I=_i), _body, "reject")

# --- 2. struct copied by value, then double-freed / used --------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _v = val()
    if _i % 4 == 0:  # safe control: deep copy, exactly-once free per allocation
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S a;a.p=malloc(@@N@@*sizeof(int));"
            "if(!a.p)return 2;*a.p=@@V@@;struct S b;b.p=malloc(@@N@@*sizeof(int));"
            "if(!b.p)return 2;*b.p=*a.p;int r=*a.p+*b.p;free(a.p);free(b.p);"
            "return r==@@R@@?0:1;}", N=_n, V=_v, R=2 * _v)
        _expect = "accept"
    elif _i % 2 == 0:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S a;a.p=malloc(@@N@@*sizeof(int));"
            "if(!a.p)return 2;struct S b=a;free(a.p);return *b.p;}", N=_n)
        _expect = "reject"
    else:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S a;a.p=malloc(@@N@@*sizeof(int));"
            "if(!a.p)return 2;struct S b=a;free(a.p);free(b.p);return 0;}", N=_n)
        _expect = "reject"
    add("struct_copy_df", tpl("copy_df_@@I@@", I=_i), _body, _expect)

# --- 3. array-of-struct shared pointer aliases ------------------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    if _i % 4 == 0:  # safe control: independent fields, exactly-once free
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S a[2];"
            "a[0].p=malloc(@@N@@*sizeof(int));a[1].p=malloc((@@N@@+1)*sizeof(int));"
            "if(!a[0].p||!a[1].p)return 2;a[0].p[0]=1;a[1].p[0]=2;"
            "int r=*a[0].p+*a[1].p;free(a[0].p);free(a[1].p);return r==3?0:1;}", N=_n)
        _expect = "accept"
    elif _i % 3 == 0:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S a[2];"
            "a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;"
            "free(a[0].p);free(a[1].p);return 0;}", N=_n)
        _expect = "reject"
    else:
        _body = STD + tpl(
            "struct S{int*p;};int main(void){struct S a[2];"
            "a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;"
            "free(a[0].p);return *a[1].p;}", N=_n)
        _expect = "reject"
    add("arr_struct_shared_alias", tpl("arr_alias_@@I@@", I=_i), _body, _expect)

# --- 4. nested struct: outer freed, inner field still used ------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _k = RNG.randint(0, _n - 1)
    if _i % 3 == 0:
        _body = STD + tpl(
            "struct I{int v[@@N@@];};struct O{struct I in;};"
            "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
            "int*p=&o->in.v[@@K@@];free(o);return *p;}", N=_n, K=_k)
    elif _i % 3 == 1:
        _body = STD + tpl(
            "struct I{int*p;};struct O{struct I in;};"
            "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
            "o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p)return 2;"
            "free(o);return *o->in.p;}", N=_n)
    else:
        _body = STD + tpl(
            "struct I{int*p;};struct O{struct I in;};"
            "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
            "o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p)return 2;"
            "free(o->in.p);free(o);return *o->in.p;}", N=_n)
    add("nested_outer_free_inner", tpl("nested_@@I@@", I=_i), _body, "reject")

# --- 5. struct field buffer bounds -----------------------------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _k = RNG.randint(0, _n - 1)
    _v = val()
    if _i % 4 == 0:  # safe control: in-extent field writes and read
        _body = STD + tpl(
            "struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;for(int i=0;i<@@N@@;i++)s->buf[i]=@@V@@;"
            "int r=s->buf[@@K@@];free(s);return r==@@V@@?0:1;}", N=_n, K=_k, V=_v)
        _expect = "accept"
    elif _i % 4 == 1:
        _body = STD + tpl(
            "struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;return s->buf[@@I2@@];}", N=_n, I2=_n + jump(_n))
        _expect = "reject"
    elif _i % 4 == 2:
        _body = STD + tpl(
            "struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;int i=@@N@@+@@J@@;return s->buf[i];}", N=_n, J=jump(_n))
        _expect = "reject"
    else:
        _body = tpl(
            "struct S{int buf[@@N@@];};int main(int argc,char**v){(void)v;"
            "struct S s={{0}};return s.buf[argc+@@J@@];}", N=_n, J=jump(_n))
        _expect = "strict"
    add("field_buf_bounds", tpl("buf_bound_@@I@@", I=_i), _body, _expect)

# --- 6. undersized allocation for a trailing field array --------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _m = RNG.randint(1, _n)  # fewer ints than the struct needs
    _k = RNG.randint(0, _n - 1)
    _v = val()
    if _i % 4 == 0:  # safe control: full struct allocation, exactly-once free
        _body = STD + tpl(
            "struct S{int lo[@@N@@];int hi[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);"
            "if(!s)return 2;s->hi[@@K@@]=@@V@@;int r=s->hi[@@K@@];free(s);"
            "return r==@@V@@?0:1;}", N=_n, K=_k, V=_v)
        _expect = "accept"
    elif _i % 2 == 0:
        _body = STD + tpl(
            "struct S{int lo[@@N@@];int hi[@@N@@];};int main(void){struct S*s=malloc(@@M@@*sizeof(int));"
            "if(!s)return 2;s->hi[@@K@@]=@@V@@;free(s);return 0;}", N=_n, M=_m, K=_k, V=_v)
        _expect = "reject"
    else:
        _body = STD + tpl(
            "struct S{int lo[@@N@@];int hi[@@N@@];};int main(void){struct S*s=malloc(@@M@@*sizeof(int));"
            "if(!s)return 2;int r=s->hi[@@K@@];free(s);return r;}", N=_n, M=_m, K=_k)
        _expect = "reject"
    add("small_trailing_array", tpl("small_arr_@@I@@", I=_i), _body, _expect)

# --- 7. flexible array member past extent -----------------------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _v = val()
    if _i % 4 == 0:  # safe control: writes stay inside the tail allocation
        _body = STD + tpl(
            "struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));"
            "if(!s)return 2;s->n=@@N@@;for(int i=0;i<@@N@@;i++)s->data[i]=@@V@@;"
            "int r=s->data[@@K@@];free(s);return r==@@V@@?0:1;}", N=_n, K=_n - 1, V=_v)
        _expect = "accept"
    elif _i % 4 == 1:
        _body = STD + tpl(
            "struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));"
            "if(!s)return 2;s->n=@@N@@;s->data[@@I2@@]=@@V@@;free(s);return 0;}", N=_n, I2=_n + jump(_n), V=_v)
        _expect = "reject"
    elif _i % 4 == 2:
        _body = STD + tpl(
            "struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));"
            "if(!s)return 2;s->n=@@N@@;int r=s->data[@@I2@@];free(s);return r;}", N=_n, I2=_n + jump(_n))
        _expect = "reject"
    else:
        _body = STD + tpl(
            "struct S{int n;int data[];};int main(int argc,char**v){(void)v;"
            "struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));if(!s)return 2;"
            "s->n=@@N@@;s->data[argc]=@@V@@;free(s);return 0;}", N=_n, V=_v)
        _expect = "strict"
    add("fam_past_extent", tpl("fam_@@I@@", I=_i), _body, _expect)

# --- 8. container_of computed from a null member pointer --------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _v = val()
    if _i % 3 == 0:  # safe control: real enclosing object
        _body = STDDEF + tpl(
            "struct S{int a;int b[@@N@@];};int main(void){struct S o={0};int*m=o.b;"
            "struct S*s=(struct S*)((char*)m-offsetof(struct S,b));return s->a;}", N=_n)
        _expect = "accept"
    elif _i % 2 == 0:
        _body = STDDEF + tpl(
            "struct S{int a;int b[@@N@@];};int main(void){struct S*s="
            "(struct S*)((char*)0-offsetof(struct S,b));s->a=@@V@@;return 0;}", N=_n, V=_v)
        _expect = "reject"
    else:
        _body = STDDEF + tpl(
            "struct S{int a;int b[@@N@@];};int main(void){struct S*s="
            "(struct S*)((char*)0-offsetof(struct S,b));return s->a;}", N=_n)
        _expect = "reject"
    add("container_of_null", tpl("container_@@I@@", I=_i), _body, _expect)

# --- 9. shallow copy of an SSO-style struct with a self-pointer -------------
for _i in range(1, 13):
    _n = RNG.randint(4, 16)
    if _i % 4 == 0:  # safe control: independent fields, exactly-once free
        _body = STD + tpl(
            "struct S{char*p;char*q;};int main(void){struct S a;"
            "a.p=malloc(@@N@@);a.q=malloc(@@N@@+1);if(!a.p||!a.q)return 2;"
            "a.p[0]='x';a.q[0]='y';int r=a.p[0]+a.q[0];free(a.p);free(a.q);"
            "return r=='x'+'y'?0:1;}", N=_n)
        _expect = "accept"
    elif _i % 4 == 1:
        _body = STD + tpl(
            "struct S{char buf[@@N@@];char*p;};int main(void){struct S a;"
            "a.p=a.buf;a.p[0]='x';struct S b=a;free(b.p);return a.buf[0];}", N=_n)
        _expect = "reject"
    elif _i % 4 == 2:
        _body = STD + tpl(
            "struct S{char buf[@@N@@];char*p;};int main(void){struct S a;"
            "a.p=a.buf;a.p[0]='x';struct S b=a;b.p[0]='y';free(a.p);return b.p[0];}", N=_n)
        _expect = "reject"
    else:  # valid shallow alias read with no free
        _body = STD + tpl(
            "struct S{char buf[@@N@@];char*p;};int main(void){struct S a;"
            "a.p=a.buf;a.p[0]='x';struct S b=a;return b.p[0]=='x'?0:1;}", N=_n)
        _expect = "accept"
    add("sso_selfptr_shallow", tpl("sso_@@I@@", I=_i), _body, _expect)

# --- 10. struct member address passed as an out-parameter -------------------
for _i in range(1, 13):
    _n = RNG.randint(2, 12)
    _v = val()
    if _i % 4 == 0:  # safe control: out-param allocation freed exactly once
        _body = STD + tpl(
            "static void fill@@I@@(int**o){*o=malloc(@@N@@*sizeof(int));}"
            "struct S{int*p;};int main(void){struct S s={0};fill@@I@@(&s.p);"
            "if(!s.p)return 2;*s.p=@@V@@;int r=*s.p;free(s.p);return r==@@V@@?0:1;}",
            N=_n, V=_v, I=_i)
        _expect = "accept"
    elif _i % 4 == 1:
        _body = STD + tpl(
            "static void fill@@I@@(int**o){*o=malloc(@@N@@*sizeof(int));}"
            "struct S{int*p;};int main(void){struct S s={0};fill@@I@@(&s.p);"
            "if(!s.p)return 2;free(s.p);free(s.p);return 0;}", N=_n, I=_i)
        _expect = "reject"
    elif _i % 4 == 2:
        _body = STD + tpl(
            "static void drop@@I@@(int**o){free(*o);}struct S{int*p;};"
            "int main(void){struct S s;s.p=malloc(@@N@@*sizeof(int));if(!s.p)return 2;"
            "drop@@I@@(&s.p);return *s.p;}", N=_n, I=_i)
        _expect = "reject"
    else:
        _body = STD + tpl(
            "static void maybe@@I@@(int**o,int c){if(c)*o=malloc(@@N@@*sizeof(int));}"
            "struct S{int*p;};int main(int argc,char**v){(void)v;struct S s={0};"
            "maybe@@I@@(&s.p,argc);return s.p?*s.p:0;}", N=_n, I=_i)
        _expect = "strict"
    add("member_outparam", tpl("outparam_@@I@@", I=_i), _body, _expect)


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll until TARGET distinct programs exist.
# ---------------------------------------------------------------------------
def seeded_extra(i):
    while True:
        _n = RNG.randint(2, 16)
        _k = RNG.randint(0, _n - 1)
        _v = val()
        _j = jump(_n)
        _kind = i % 10
        if _kind == 0:
            return ("mut_ptr_field", tpl("m_ptr_@@I@@", I=i), STD + tpl(
                "struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;"
                "s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;s->p[0]=@@V@@;"
                "free(s->p);return s->p[@@K@@];}", N=_n, K=_k, V=_v), "reject")
        if _kind == 1:
            return ("mut_copy_df", tpl("m_copy_@@I@@", I=i), STD + tpl(
                "struct S{int*p;};int main(void){struct S a;a.p=malloc(@@N@@*sizeof(int));"
                "if(!a.p)return 2;struct S b=a;free(a.p);free(b.p);return 0;}", N=_n), "reject")
        if _kind == 2:
            return ("mut_arr_alias", tpl("m_arr_@@I@@", I=i), STD + tpl(
                "struct S{int*p;};int main(void){struct S a[2];a[0].p=a[1].p="
                "malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;free(a[0].p);return *a[1].p;}",
                N=_n), "reject")
        if _kind == 3:
            return ("mut_nested", tpl("m_nest_@@I@@", I=i), STD + tpl(
                "struct I{int v[@@N@@];};struct O{struct I in;};int main(void){"
                "struct O*o=malloc(sizeof*o);if(!o)return 2;int*p=&o->in.v[@@K@@];"
                "free(o);return *p;}", N=_n, K=_k), "reject")
        if _kind == 4:
            return ("mut_field_buf", tpl("m_buf_@@I@@", I=i), STD + tpl(
                "struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);"
                "if(!s)return 2;s->buf[0]=@@V@@;return s->buf[@@I2@@];}",
                N=_n, I2=_n + _j, V=_v), "reject")
        if _kind == 5:
            return ("mut_small_arr", tpl("m_small_@@I@@", I=i), STD + tpl(
                "struct S{int lo[@@N@@];int hi[@@N@@];};int main(void){struct S*s="
                "malloc(@@M@@*sizeof(int));if(!s)return 2;s->hi[@@K@@]=@@V@@;free(s);return 0;}",
                N=_n, M=RNG.randint(1, _n), K=_k, V=_v), "reject")
        if _kind == 6:
            return ("mut_fam", tpl("m_fam_@@I@@", I=i), STD + tpl(
                "struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));"
                "if(!s)return 2;s->n=@@N@@;s->data[@@I2@@]=@@V@@;free(s);return 0;}",
                N=_n, I2=_n + _j, V=_v), "reject")
        if _kind == 7:
            return ("mut_container", tpl("m_cont_@@I@@", I=i), STDDEF + tpl(
                "struct S{int a;int b[@@N@@];};int main(void){struct S*s="
                "(struct S*)((char*)0-offsetof(struct S,b));return s->a;}", N=_n), "reject")
        if _kind == 8:
            return ("mut_sso", tpl("m_sso_@@I@@", I=i), STD + tpl(
                "struct S{char buf[@@N@@];char*p;};int main(void){struct S a;"
                "a.p=a.buf;a.p[0]='x';struct S b=a;free(b.p);return a.buf[0];}", N=_n), "reject")
        return ("mut_outparam", tpl("m_out_@@I@@", I=i), STD + tpl(
            "static void fill(int**o){*o=malloc(@@N@@*sizeof(int));}struct S{int*p;};"
            "int main(void){struct S s={0};fill(&s.p);if(!s.p)return 2;free(s.p);free(s.p);return 0;}",
            N=_n), "reject")


_i = 0
while len(CASES) < TARGET:
    _family, _name, _body, _expect = seeded_extra(_i)
    add(_family, _name, _body, _expect)
    _i += 1
    if _i > TARGET * 50:
        raise RuntimeError("could not reach TARGET distinct cases")


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
    print("ag_fields: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
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
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
