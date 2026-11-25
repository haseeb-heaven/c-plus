#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator for null/init/unsigned UB (>=100 cases).

Produces distinct, self-contained, valid C translation units covering
nullability, initialization and unsigned-size safety:

  * null dereference (read / write / through a function argument)
  * null subscript (p[0] and p[offset])
  * null struct member access (read / write)
  * NULL passed to libc routines that dereference it (strlen / memcpy /
    printf %s / strcmp)
  * unchecked malloc / fopen results used without a null check
  * uninitialized scalar locals and uninitialized pointers
  * uninitialized flat / nested struct fields
  * uninitialized heap reads (int / char)
  * goto jumping over an initialization
  * setjmp / longjmp clobbering non-volatile locals
  * ``size_t`` / ``unsigned`` compared ``< 0`` in a guard before a bad op
  * unsigned underflow used as an index or a size (``n - 1`` / ``n -= k``)
  * a negative int converted to ``size_t`` and used as a length / index
  * scope escapes (block / switch / compound-literal / VLA-backward-goto)
  * returning the address of a local array / struct
  * scalar / array use-after-scope
  * safe controls (null checks, all-branch init, correct ``== 0`` and
    ``x < 0`` unsigned guards, checked malloc / fopen)

Every case declares one of the sibling-corpus outcomes:

  reject - proven nullability / initialization / unsigned error: must fail
           BOTH --default and --strict.  Acceptance in either mode is a hole.
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (acceptance there is a hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Programs come from deterministic seeded parameter sweeps plus a seeded RNG
(FUZZ_SEED); the final seeded pass re-rolls parameters until TARGET distinct
bodies exist.  Cases run in parallel (FUZZ_WORKERS) with a per-check timeout.
Mismatches are saved under tests/out/core/ag2_init/failures/.  This is a
differential measurement, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xA61C7
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
OUT = ROOT / "tests/out/core/ag2_init"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA61C7"), 0)
TARGET = max(100, int(os.environ.get("AG_TARGET", "140")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

NS = range(1, 7)  # deterministic parameter sweep; seeded values fill the slots
TYPES = ("int", "short", "long", "char", "unsigned")
NAMES = ("p", "q", "arr", "buf", "data", "tbl")
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

STD = "#include <stdlib.h>\n"
STR = "#include <string.h>\n"
IO = "#include <stdio.h>\n"
SJ = "#include <setjmp.h>\n"
UINT = "#include <stdint.h>\n"
STDDEF = "#include <stddef.h>\n"  # size_t


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def val():
    return RNG.randint(1, 97)


def off():
    return RNG.randint(1, 4)


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = (f"/* ag2_init seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


# --- 1. null_deref: null pointer read / write / via function argument --------
for n in NS:
    v = val()
    add("null_deref", tpl("null_read_@@N@@", N=n),
        tpl("int main(void){int *p=0;int pad[@@N@@];(void)pad;return *p+@@V@@;}",
            N=n, V=v), "reject")
    add("null_deref", tpl("null_write_@@N@@", N=n),
        tpl("int main(void){int *p=0;int pad[@@N@@];(void)pad;*p=@@V@@;return 0;}",
            N=n, V=v), "reject")
    add("null_deref", tpl("null_via_func_@@N@@", N=n),
        tpl("void f(int *p){*p=@@V@@;}\nint main(void){f(0);return 0;}", V=v),
        "reject")

# --- 2. null_subscript: null [0] and [offset] -------------------------------
for n in NS:
    v = val()
    add("null_subscript", tpl("null_sub_zero_@@N@@", N=n),
        tpl("int main(void){int *p=0;return p[0]+@@V@@;}", V=v), "reject")
    add("null_subscript", tpl("null_sub_off_@@N@@", N=n),
        tpl("int main(void){int *p=0;return p[@@N@@]+@@V@@;}", N=n, V=v),
        "reject")

# --- 3. null_member: null struct pointer ->field read / write ----------------
for n in NS:
    v = val()
    add("null_member", tpl("null_member_read_@@N@@", N=n),
        tpl("struct S{int a,b;};int main(void){struct S *p=0;return p->a+@@V@@;}",
            V=v), "reject")
    add("null_member", tpl("null_member_write_@@N@@", N=n),
        tpl("struct S{int a,b;};int main(void){struct S *p=0;p->b=@@V@@;return 0;}",
            V=v), "reject")

# --- 4. null_libc: NULL passed to libc routines that dereference it ----------
for n in NS:
    v = val()
    add("null_libc", tpl("null_strlen_@@N@@", N=n),
        STR + tpl("int main(void){char *p=0;return (int)strlen(p)+@@V@@;}", V=v),
        "reject")
    add("null_libc", tpl("null_memcpy_@@N@@", N=n),
        STR + tpl("int main(void){char d[@@N@@+8];char s[@@N@@+8]={0};"
                  "memcpy(d,0,@@N@@);return 0;}", N=n), "reject")
    add("null_libc", tpl("null_printf_s_@@N@@", N=n),
        IO + tpl("int main(void){char *p=0;printf(\"%s\",p);return @@V@@;}", V=v),
        "reject")
    add("null_libc", tpl("null_strcmp_@@N@@", N=n),
        STR + 'int main(void){char *p=0;return strcmp(p,"abc");}', "reject")

# --- 5. unchecked_alloc: malloc / fopen results used with no null check -----
for n in NS:
    v = val()
    add("unchecked_alloc", tpl("malloc_unchecked_@@N@@", N=n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));p[0]=@@V@@;"
                  "free(p);return 0;}", N=n, V=v), "strict")
    add("unchecked_alloc", tpl("fopen_unchecked_@@N@@", N=n),
        IO + tpl('int main(void){FILE *f=fopen("/no/such/ag2_init_@@N@@","r");'
                 'fprintf(f,"x");return 0;}', N=n), "strict")

# --- 6. uninit_local: uninitialized scalar locals read ----------------------
for n in NS:
    v = val()
    add("uninit_local", tpl("uninit_scalar_@@N@@", N=n),
        tpl("int main(void){int x;int pad[@@N@@];(void)pad;return x+@@V@@;}",
            N=n, V=v), "reject")
    add("uninit_local", tpl("uninit_multi_@@N@@", N=n),
        tpl("int main(void){int x,y;y=@@V@@;int pad[@@N@@];(void)pad;return x+y;}",
            N=n, V=v), "reject")

# --- 7. uninit_ptr: uninitialized pointer dereferenced ----------------------
for n in NS:
    v = val()
    add("uninit_ptr", tpl("uninit_ptr_read_@@N@@", N=n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;return *p+@@V@@;}",
            N=n, V=v), "reject")
    add("uninit_ptr", tpl("uninit_ptr_write_@@N@@", N=n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;*p=@@V@@;return 0;}",
            N=n, V=v), "reject")

# --- 8. uninit_field: uninitialized flat / nested struct fields read --------
for n in NS:
    v = val()
    add("uninit_field", tpl("field_flat_@@N@@", N=n),
        tpl("struct S{int a,b;};int main(void){struct S s;return s.a+@@V@@;}",
            V=v), "strict")
    add("uninit_field", tpl("field_nested_@@N@@", N=n),
        tpl("struct I{int v;};struct O{struct I in;};\n"
            "int main(void){struct O o;return o.in.v+@@V@@;}", V=v), "strict")

# --- 9. uninit_heap: uninitialized heap reads (with a null check) -----------
for n in NS:
    v = val()
    add("uninit_heap", tpl("heap_int_@@N@@", N=n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                  "return p[0]+@@V@@;}", N=n, V=v), "strict")
    add("uninit_heap", tpl("heap_char_@@N@@", N=n),
        STD + tpl("int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
                  "return *(unsigned char*)p+@@V@@;}", N=n, V=v), "strict")

# --- 10. goto_bypass: goto jumping over an initialization -------------------
for n in NS:
    v = val()
    add("goto_bypass", tpl("goto_linear_@@N@@", N=n),
        tpl("int main(void){int x;goto done;x=@@V@@;done:return x+@@N@@;}",
            N=n, V=v), "reject")
    add("goto_bypass", tpl("goto_branch_@@N@@", N=n),
        tpl("int main(int argc,char **v){(void)v;int x;if(argc>1)goto skip;"
            "x=@@V@@;skip:return x+@@N@@;}", N=n, V=v), "reject")

# --- 11. setjmp_clobber: setjmp / longjmp clobbering non-volatile locals -----
for n in NS:
    v = val()
    add("setjmp_clobber", tpl("setjmp_one_@@N@@", N=n),
        SJ + tpl("jmp_buf j;int main(void){int x=@@V@@;if(!setjmp(j))"
                 "{x+=@@N@@;longjmp(j,1);}return x;}", N=n, V=v), "strict")
    add("setjmp_clobber", tpl("setjmp_two_@@N@@", N=n),
        SJ + tpl("jmp_buf j;int main(void){int x=@@V@@,y=@@V2@@;"
                 "if(!setjmp(j)){x++;y++;longjmp(j,1);}return x+y;}",
                 V=v, V2=v + 1), "strict")

# --- 12. size_lt0_guard: unsigned < 0 guard (always false) before bad op ----
for n in NS:
    add("size_lt0_guard", tpl("lt0_guard_index_@@N@@", N=n),
        UINT + tpl("int main(void){int a[@@N@@]={0};unsigned i=0u-1u;"
                   "if(i<0)return 0;return a[i];}", N=n), "reject")
    add("size_lt0_guard", tpl("lt0_guard_memcpy_@@N@@", N=n),
        STR + STDDEF + UINT + tpl("int main(void){char d[@@N@@+8];char s[@@N@@+8]={0};"
                         "size_t m=(size_t)-1;if(m<0)return 0;memcpy(d,s,m);"
                         "return 0;}", N=n), "reject")

# --- 13. underflow_index: unsigned underflow used as an index or size -------
for n in NS:
    v, k = nm(), off()
    add("underflow_index", tpl("under_idx_sz_@@N@@", N=n),
        STDDEF + UINT + tpl("int main(void){int @@V@@[@@N@@]={0};size_t n=0;"
                            "return @@V@@[n-1];}", V=v, N=n), "reject")
    add("underflow_index", tpl("under_size_memcpy_@@N@@", N=n),
        STR + STDDEF + UINT + tpl("int main(void){char @@V@@[@@N@@+8];char s[64]={0};"
                         "size_t n=0;memcpy(@@V@@,s,n-1);return 0;}",
                         V=v, N=n), "reject")
    add("underflow_index", tpl("under_idx_u32_@@N@@", N=n),
        UINT + tpl("int main(void){int @@V@@[@@N@@]={0};uint32_t n=0;"
                   "n-=@@K@@;return @@V@@[n];}", V=v, N=n, K=k), "reject")

# --- 14. neg_to_size: negative int converted to size_t and used -------------
for n in NS:
    v, k = nm(), off()
    add("neg_to_size", tpl("neg_len_direct_@@N@@", N=n),
        STR + STDDEF + tpl("int main(void){char @@V@@[@@N@@+8];char s[64]={0};"
                  "int x=-@@K@@;memcpy(@@V@@,s,(size_t)x);return 0;}",
                  V=v, N=n, K=k), "reject")
    add("neg_to_size", tpl("neg_len_call_@@N@@", N=n),
        STR + STDDEF + tpl("void cp(size_t m){char d[@@N@@+8];char s[64]={0};"
                  "memcpy(d,s,m);}\nint main(void){cp(-@@K@@);return 0;}",
                  N=n, K=k), "reject")
    add("neg_to_size", tpl("neg_len_indirect_@@N@@", N=n),
        STR + STDDEF + tpl("void cp(size_t m){char d[@@N@@+8];char s[64]={0};"
                  "memcpy(d,s,m);}\nvoid fwd(int x){cp((size_t)x);}\n"
                  "int main(void){fwd(-@@K@@);return 0;}", N=n, K=k), "strict")

# --- 15. scope_escape: block / switch / compound-literal / VLA goto ---------
for n in NS:
    v = val()
    add("scope_escape", tpl("escape_block_@@N@@", N=n),
        tpl("int main(void){int *p;{int a[@@N@@];a[0]=@@V@@;p=&a[0];}"
            "return p[0];}", N=n, V=v), "reject")
    add("scope_escape", tpl("escape_switch_@@N@@", N=n),
        tpl("int main(void){int *p;switch(1){case 1:{int x=@@V@@;p=&x;break;}"
            "default:p=0;break;}return p?*p:0;}", V=v), "reject")
    add("scope_escape", tpl("escape_compound_@@N@@", N=n),
        tpl("int *f(int n){int *p=(int[]){1,2,3};(void)n;return p;}\n"
            "int main(void){int *q=f(@@N@@);return q[0]+@@V@@;}", N=n, V=v),
        "reject")
    add("scope_escape", tpl("escape_vla_@@N@@", N=n),
        tpl("int main(void){int n=@@N@@;int *p=0;int i=0;"
            "top:if(i==0){int a[n];a[0]=@@V@@;p=a;i++;goto top;}return p[0];}",
            N=n, V=v), "reject")

# --- 16. ret_local: returning the address of a local ------------------------
for n in NS:
    add("ret_local", tpl("ret_local_arr_@@N@@", N=n),
        tpl("int *f(void){int a[@@N@@];return &a[@@N1@@];}\n"
            "int main(void){int *q=f();return *q;}", N=n, N1=n - 1), "reject")
    add("ret_local", tpl("ret_local_struct_@@N@@", N=n),
        tpl("struct S{int x[@@N@@];};struct S *f(void){struct S s;return &s;}\n"
            "int main(void){struct S *q=f();return q->x[0];}", N=n), "reject")

# --- 17. use_after_scope: scalar / array use-after-scope --------------------
for n in NS:
    v = val()
    add("use_after_scope", tpl("uas_scalar_@@N@@", N=n),
        tpl("int main(void){int *p;{int x=@@V@@;p=&x;}*p=@@V2@@;return *p;}",
            V=v, V2=v + 1), "reject")
    add("use_after_scope", tpl("uas_array_@@N@@", N=n),
        tpl("int main(void){int *p;{int a[@@N@@];p=a;}p[0]=@@V@@;return p[0];}",
            N=n, V=v), "reject")

# --- 18. safe_controls: null checks, all-branch init, correct guards --------
for n in NS:
    v = val()
    add("safe_controls", tpl("ok_null_check_@@N@@", N=n),
        tpl("int main(void){int *p=0;if(!p)return 0;return *p+@@V@@;}", V=v),
        "accept")
    add("safe_controls", tpl("ok_null_libc_@@N@@", N=n),
        STR + tpl("int main(void){char *p=0;if(!p)return 0;"
                  "return (int)strlen(p)+@@V@@;}", V=v), "accept")
    add("safe_controls", tpl("ok_all_branch_@@N@@", N=n),
        tpl("int main(int argc,char **v){(void)v;int x;"
            "if(argc>1)x=@@V@@;else x=@@V2@@;return x;}", V=v, V2=v + 1),
        "accept")
    add("safe_controls", tpl("ok_eq0_guard_@@N@@", N=n),
        STDDEF + UINT + tpl("int main(void){int a[@@N@@]={0};size_t i=0;"
                   "if(i==0)return 0;return a[i-1];}", N=n), "accept")
    add("safe_controls", tpl("ok_neg_check_@@N@@", N=n),
        STR + STDDEF + tpl("int main(void){char d[@@N@@+8];char s[@@N@@+8];"
                  "int x=-@@K@@;if(x<0)return 0;if(x>@@N@@)return 0;"
                  "memcpy(d,s,(size_t)x);return 0;}", N=n, K=off()), "accept")
    add("safe_controls", tpl("ok_malloc_check_@@N@@", N=n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                  "p[0]=@@V@@;free(p);return 0;}", N=n, V=v), "accept")
    add("safe_controls", tpl("ok_fopen_check_@@N@@", N=n),
        IO + tpl('int main(void){FILE *f=fopen("/no/such/ag2_init_@@N@@","r");'
                 'if(!f)return 2;fprintf(f,"x");fclose(f);return 0;}', N=n),
        "accept")


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll until TARGET distinct programs exist.
# ---------------------------------------------------------------------------
def seeded_extra(i):
    while True:
        n = RNG.randint(2, 16)
        k = off()
        v = val()
        t = ty()
        a = nm()
        kind = i % 18
        if kind == 0:
            return ("null_deref", tpl("m_null_@@I@@", I=i), tpl(
                "int main(void){int *p=0;return *p+@@V@@;}", V=v), "reject")
        if kind == 1:
            return ("null_subscript", tpl("m_sub_@@I@@", I=i), tpl(
                "int main(void){int *p=0;return p[@@N@@]+@@V@@;}", N=n, V=v),
                "reject")
        if kind == 2:
            return ("null_member", tpl("m_member_@@I@@", I=i), tpl(
                "struct S{int a,b;};int main(void){struct S*p=0;p->b=@@V@@;return 0;}",
                V=v), "reject")
        if kind == 3:
            return ("null_libc", tpl("m_libc_@@I@@", I=i), STR + tpl(
                "int main(void){char *p=0;return (int)strlen(p)+@@V@@;}", V=v),
                "reject")
        if kind == 4:
            return ("unchecked_alloc", tpl("m_alloc_@@I@@", I=i), STD + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));p[0]=@@V@@;free(p);return 0;}",
                N=n, V=v), "strict")
        if kind == 5:
            return ("uninit_local", tpl("m_local_@@I@@", I=i), tpl(
                "int main(void){int x;int pad[@@N@@];(void)pad;return x+@@V@@;}",
                N=n, V=v), "reject")
        if kind == 6:
            return ("uninit_ptr", tpl("m_uptr_@@I@@", I=i), tpl(
                "int main(void){int *p;return *p+@@V@@;}", V=v), "reject")
        if kind == 7:
            return ("uninit_field", tpl("m_field_@@I@@", I=i), tpl(
                "struct S{int a,b;};int main(void){struct S s;return s.a+@@V@@;}",
                V=v), "strict")
        if kind == 8:
            return ("uninit_heap", tpl("m_heap_@@I@@", I=i), STD + tpl(
                "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "return p[0]+@@V@@;}", N=n, V=v), "strict")
        if kind == 9:
            return ("goto_bypass", tpl("m_goto_@@I@@", I=i), tpl(
                "int main(void){int x;goto done;x=@@V@@;done:return x+@@N@@;}",
                N=n, V=v), "reject")
        if kind == 10:
            return ("setjmp_clobber", tpl("m_setjmp_@@I@@", I=i), SJ + tpl(
                "jmp_buf j;int main(void){int x=@@V@@;if(!setjmp(j))"
                "{x+=@@N@@;longjmp(j,1);}return x;}", N=n, V=v), "strict")
        if kind == 11:
            return ("size_lt0_guard", tpl("m_lt0_@@I@@", I=i), UINT + tpl(
                "int main(void){int @@A@@[@@N@@]={0};unsigned i=0u-@@K@@;"
                "if(i<0)return 0;return @@A@@[i];}", A=a, N=n, K=k), "reject")
        if kind == 12:
            return ("underflow_index", tpl("m_under_@@I@@", I=i), STDDEF + UINT + tpl(
                "int main(void){int @@A@@[@@N@@]={0};size_t n=0;return @@A@@[n-1];}",
                A=a, N=n), "reject")
        if kind == 13:
            return ("neg_to_size", tpl("m_neg_@@I@@", I=i), STR + STDDEF + tpl(
                "int main(void){char @@A@@[@@N@@+8];char s[64]={0};int x=-@@K@@;"
                "memcpy(@@A@@,s,(size_t)x);return 0;}", A=a, N=n, K=k), "reject")
        if kind == 14:
            return ("scope_escape", tpl("m_scope_@@I@@", I=i), tpl(
                "int main(void){int *p;{int a[@@N@@];a[0]=@@V@@;p=&a[0];}"
                "return p[0];}", N=n, V=v), "reject")
        if kind == 15:
            return ("ret_local", tpl("m_ret_@@I@@", I=i), tpl(
                "int *f(void){int a[@@N@@];return &a[@@N1@@];}"
                "int main(void){int *q=f();return *q;}", N=n, N1=n - 1),
                "reject")
        if kind == 16:
            return ("use_after_scope", tpl("m_uas_@@I@@", I=i), tpl(
                "int main(void){int *p;{int x=@@V@@;p=&x;}*p=@@V2@@;return *p;}",
                V=v, V2=v + 1), "reject")
        if kind == 17:
            return ("safe_controls", tpl("m_safe_@@I@@", I=i),
                STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));"
                          "if(!p)return 2;p[0]=@@V@@;free(p);return 0;}",
                          N=n, V=v), "accept")


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

    # Save generated source and a full diagnostic log for every mismatch.
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
    print("ag2_init: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
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
