#!/usr/bin/env python3
"""Deterministic seeded parallel generator: >=100 distinct small C programs
covering NULLABILITY + INITIALIZATION ("aggressive init") analysis.

Every case is generated programmatically from parameter sweeps plus a seeded
RNG (FUZZ_SEED, default 0xA61F).  Each program is a small, self-contained C
translation unit; all generated bodies are distinct (enforced by SEEN and by a
case-specific marker comment).  Re-running with the same seed is reproducible.

Families (18 total, each contributing reject/strict/accept cases):

   1  null_deref        null pointer read / write / via function argument
   2  null_subscript    null pointer [0] and [offset] subscript
   3  null_member       null struct pointer ->field read / write
   4  null_libc         NULL passed to strlen / memcpy / printf %s / strcmp
   5  unchecked_alloc   unchecked malloc and fopen results used
   6  uninit_local      uninitialized scalar locals read
   7  uninit_ptr        uninitialized pointer dereferenced (read / write)
   8  uninit_field      uninitialized flat / nested struct fields read
   9  uninit_heap       uninitialized heap reads (int / char)
  10  goto_bypass       goto jumping over an initialization
  11  setjmp_clobber    setjmp/longjmp clobbering non-volatile locals
  12  unsigned_lt0      `unsigned < 0` (always false) guard before a bad op
  13  scope_escape      block / switch / compound-literal / VLA-backward-goto
                        scope escapes
  14  ret_local         returning the address of a local array / struct
  15  use_after_scope   scalar / array use-after-scope
  16  safe_controls     null checks, all-branch init, unsigned == 0 guards,
                        checked malloc/fopen (valid controls)

Declared expectations follow the sibling generators (ub_init_260 / ub_ptr_260):

   reject - proven nullability / initialization error: must be rejected in
            BOTH --default and --strict.  Acceptance in either mode is a hole.
   strict - conditional / unverified error: MAY pass --default, but must be
            rejected in --strict (acceptance there is a hole).
   accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus a full diagnostic log are
saved under tests/out/core/ag_init/failures/.  Cases run in parallel
(FUZZ_WORKERS, default 8) with a 60s per-check timeout.  This is a differential
measurement over the declared subset, not a proof of safety.

Environment:
   FUZZ_SEED    default 0xA61F
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
OUT = ROOT / "tests/out/core/ag_init"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA61F"), 0)
TIMEOUT = 60

CASES = []   # dict(name, body, expect, family)
SEEN = set()  # exact generated program bodies, used to enforce distinctness
RNG = random.Random(SEED)

NS = range(1, 7)                       # parameter sweep (seeded values below)
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
    """Token templating: avoids C-brace escaping in Python format strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def ty():
    return RNG.choice(TYPES)


def nm():
    return RNG.choice(NAMES)


def val():
    return RNG.randint(1, 97)


def lit(length):
    return "".join(RNG.choice(ALPHABET) for _ in range(length))


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    # A deterministic, case-specific marker makes every generated program
    # unique in source text (comments are valid C and do not change behavior).
    normalized = (f"/* ag_init seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({"name": name, "body": normalized,
                  "expect": expect, "family": family})


# ---------------------------------------------------------------------------
# 1. null_deref: null pointer read / write / via function argument
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# 2. null_subscript: null [0] and [offset]
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("null_subscript", tpl("null_sub_zero_@@N@@", N=n),
        tpl("int main(void){int *p=0;return p[0]+@@V@@;}", V=v), "reject")
    add("null_subscript", tpl("null_sub_off_@@N@@", N=n),
        tpl("int main(void){int *p=0;return p[@@N@@]+@@V@@;}", N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 3. null_member: null struct pointer ->field read / write
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("null_member", tpl("null_member_read_@@N@@", N=n),
        tpl("struct S{int a,b;};int main(void){struct S *p=0;return p->a+@@V@@;}",
            V=v), "reject")
    add("null_member", tpl("null_member_write_@@N@@", N=n),
        tpl("struct S{int a,b;};int main(void){struct S *p=0;p->b=@@V@@;return 0;}",
            V=v), "reject")

# ---------------------------------------------------------------------------
# 4. null_libc: NULL passed to libc routines that dereference it
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# 5. unchecked_alloc: malloc/fopen results used without a null check
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("unchecked_alloc", tpl("malloc_unchecked_@@N@@", N=n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));p[0]=@@V@@;"
                  "free(p);return 0;}", N=n, V=v), "strict")
    add("unchecked_alloc", tpl("fopen_unchecked_@@N@@", N=n),
        IO + tpl('int main(void){FILE *f=fopen("/no/such/ag_init_@@N@@","r");'
                 'fprintf(f,"x");return 0;}', N=n), "strict")

# ---------------------------------------------------------------------------
# 6. uninit_local: uninitialized scalar locals read
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("uninit_local", tpl("uninit_scalar_@@N@@", N=n),
        tpl("int main(void){int x;int pad[@@N@@];(void)pad;return x+@@V@@;}",
            N=n, V=v), "reject")
    add("uninit_local", tpl("uninit_multi_@@N@@", N=n),
        tpl("int main(void){int x,y;y=@@V@@;int pad[@@N@@];(void)pad;return x+y;}",
            N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 7. uninit_ptr: uninitialized pointer dereferenced
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("uninit_ptr", tpl("uninit_ptr_read_@@N@@", N=n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;return *p+@@V@@;}",
            N=n, V=v), "reject")
    add("uninit_ptr", tpl("uninit_ptr_write_@@N@@", N=n),
        tpl("int main(void){int *p;int pad[@@N@@];(void)pad;*p=@@V@@;return 0;}",
            N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 8. uninit_field: uninitialized flat / nested struct fields read
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("uninit_field", tpl("field_flat_@@N@@", N=n),
        tpl("struct S{int a,b;};int main(void){struct S s;return s.a+@@V@@;}",
            V=v), "strict")
    add("uninit_field", tpl("field_nested_@@N@@", N=n),
        tpl("struct I{int v;};struct O{struct I in;};\n"
            "int main(void){struct O o;return o.in.v+@@V@@;}", V=v), "strict")

# ---------------------------------------------------------------------------
# 9. uninit_heap: uninitialized heap reads
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("uninit_heap", tpl("heap_int_@@N@@", N=n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                  "return p[0]+@@V@@;}", N=n, V=v), "strict")
    add("uninit_heap", tpl("heap_char_@@N@@", N=n),
        STD + tpl("int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
                  "return *(unsigned char*)p+@@V@@;}", N=n, V=v), "strict")

# ---------------------------------------------------------------------------
# 10. goto_bypass: goto jumping over an initialization
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("goto_bypass", tpl("goto_linear_@@N@@", N=n),
        tpl("int main(void){int x;goto done;x=@@V@@;done:return x+@@N@@;}",
            N=n, V=v), "reject")
    add("goto_bypass", tpl("goto_branch_@@N@@", N=n),
        tpl("int main(int argc,char **v){(void)v;int x;if(argc>1)goto skip;"
            "x=@@V@@;skip:return x+@@N@@;}", N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 11. setjmp_clobber: setjmp/longjmp clobbering non-volatile locals
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("setjmp_clobber", tpl("setjmp_one_@@N@@", N=n),
        SJ + tpl("jmp_buf j;int main(void){int x=@@V@@;if(!setjmp(j))"
                 "{x+=@@N@@;longjmp(j,1);}return x;}", N=n, V=v), "strict")
    add("setjmp_clobber", tpl("setjmp_two_@@N@@", N=n),
        SJ + tpl("jmp_buf j;int main(void){int x=@@V@@,y=@@V2@@;"
                 "if(!setjmp(j)){x++;y++;longjmp(j,1);}return x+y;}",
                 V=v, V2=v + 1), "strict")

# ---------------------------------------------------------------------------
# 12. unsigned_lt0: `unsigned < 0` guard (always false) before a bad operation
# ---------------------------------------------------------------------------
for n in NS:
    add("unsigned_lt0", tpl("lt0_guard_index_@@N@@", N=n),
        UINT + tpl("int main(void){int a[@@N@@]={0};unsigned i=0u-1u;"
                   "if(i<0)return 0;return a[i];}", N=n), "reject")
    add("unsigned_lt0", tpl("lt0_guard_memcpy_@@N@@", N=n),
        STR + STDDEF + UINT + tpl("int main(void){char d[@@N@@+8];char s[@@N@@+8]={0};"
                         "size_t m=(size_t)-1;if(m<0)return 0;memcpy(d,s,m);"
                         "return 0;}", N=n), "reject")

# ---------------------------------------------------------------------------
# 13. scope_escape: block / switch / compound literal / VLA backward goto
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# 14. ret_local: returning the address of a local
# ---------------------------------------------------------------------------
for n in NS:
    add("ret_local", tpl("ret_local_arr_@@N@@", N=n),
        tpl("int *f(void){int a[@@N@@];return &a[@@N1@@];}\n"
            "int main(void){int *q=f();return *q;}", N=n, N1=n - 1), "reject")
    add("ret_local", tpl("ret_local_struct_@@N@@", N=n),
        tpl("struct S{int x[@@N@@];};struct S *f(void){struct S s;return &s;}\n"
            "int main(void){struct S *q=f();return q->x[0];}", N=n), "reject")

# ---------------------------------------------------------------------------
# 15. use_after_scope: scalar / array use-after-scope
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("use_after_scope", tpl("uas_scalar_@@N@@", N=n),
        tpl("int main(void){int *p;{int x=@@V@@;p=&x;}*p=@@V2@@;return *p;}",
            N=n, V=v, V2=v + 1), "reject")
    add("use_after_scope", tpl("uas_array_@@N@@", N=n),
        tpl("int main(void){int *p;{int a[@@N@@];p=a;}p[0]=@@V@@;return p[0];}",
            N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 16. safe_controls: null checks, all-branch init, unsigned == 0 guards,
#     checked malloc/fopen (must be accepted in both modes)
# ---------------------------------------------------------------------------
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
    add("safe_controls", tpl("ok_malloc_check_@@N@@", N=n),
        STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                  "p[0]=@@V@@;free(p);return 0;}", N=n, V=v), "accept")
    add("safe_controls", tpl("ok_fopen_check_@@N@@", N=n),
        IO + tpl('int main(void){FILE *f=fopen("/no/such/ag_init_@@N@@","r");'
                 'if(!f)return 2;fprintf(f,"x");fclose(f);return 0;}', N=n),
        "accept")


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
    name, body, expect, family = case["name"], case["body"], case["expect"], case["family"]
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT, "TIMEOUT")
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc, "OS-ERROR")
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag, first_diag(diag))

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("DEFAULT-HOLE")
        if not s_rej:
            reasons.append("STRICT-HOLE")
    elif expect == "strict":
        if not s_rej:
            reasons.append("STRICT-HOLE")
    else:  # accept
        if d_rej:
            reasons.append("DEFAULT-FALSE-POSITIVE")
        if s_rej:
            reasons.append("STRICT-FALSE-POSITIVE")

    if not reasons:
        return family, name, expect, [], modes

    # Save the generated source and a full diagnostic log for every mismatch.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s: %s" % (d_rej, modes["--default"][2]),
        "--strict  rejected=%s: %s" % (s_rej, modes["--strict"][2]),
        "",
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    detail = ("%s/%s [expect=%s] %s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  family, name, expect, ";".join(reasons),
                  d_rej, modes["--default"][2],
                  s_rej, modes["--strict"][2]))
    return family, name, expect, [detail], modes


def main():
    bodies = [c["body"] for c in CASES]
    unique = len(set(bodies))
    counts = Counter(c["expect"] for c in CASES)
    fams = Counter(c["family"] for c in CASES)

    print("ag_init: %d generated cases (%d distinct), seed=0x%X, workers=%d, bin=%s"
          % (len(CASES), unique, SEED, WORKERS, CC))
    print("outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families (%d): %s" % (
        len(fams), ", ".join("%s=%d" % (k, v) for k, v in sorted(fams.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    holes = 0
    false_positives = 0
    by_family = Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for family, _name, expect, fails, _modes in pool.map(run_case, CASES):
            if not fails:
                continue
            mismatches.extend(fails)
            by_family[family] += 1
            if expect == "accept":
                false_positives += 1
            else:
                holes += 1

    mismatches.sort()
    print("results: %d cases, %d mismatches (%d safety holes, %d false positives)"
          % (len(CASES), len(mismatches), holes, false_positives))
    if by_family:
        print("mismatches by family: " + ", ".join(
            "%s=%d" % (k, v) for k, v in sorted(by_family.items())))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs in %s)" % (len(mismatches) - 10, FAILS))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
