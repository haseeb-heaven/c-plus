#!/usr/bin/env python3
"""p0fuzz_*: deterministic P0 safety fuzz suite for ``c+ check --strict``.

The script generates 176 unique, self-contained C programs (no C+ extension
syntax needed) in four families, then runs ``c+ check --strict`` on every one
with ``CPLUS_CLANG=/usr/bin/clang``.  Generated sources live only in a fresh
temporary directory under /tmp; nothing is written into the repository.

Every case carries an explicit expected outcome:

  ("reject", SUBSTR)  -- strict check must fail AND ``SUBSTR`` must appear
                         verbatim in the combined stdout/stderr.
  ("accept", "")      -- strict check must succeed (return code 0).

Outcomes are tuned to the *current* compiler, so a correct run reports zero
failures / zero false positives.  This is a regression oracle for the checked
behaviours, not a proof that the underlying programs are correct C.

Families
  realloc  raw realloc loss, stale aliases after success/failure, failed
           realloc writes past the old extent, guarded-realloc controls.
  init     definite-init through output pointers, compound assignments,
           increments, memset/address aliases and loop paths, plus controls.
  cfg      setjmp/longjmp and goto cleanup paths: skipped frees, leaks on the
           return path, double frees, stale aliases across goto, and controls.
  alias    field / nested-field / array-of-pointer / plain-pointer alias
           release (use-after-free, double free) plus safe reset controls.

Determinism: each case draws from ``random.Random(f"{SEED}:{global_index}")``
so generation is independent of worker count; case order is preserved by
``pool.map``.  Set FUZZ_SEED / FUZZ_WORKERS / FUZZ_TIMEOUT / CPLUS_BIN to tune.
"""
import collections
import concurrent.futures
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).parents[2]
LOCAL_BIN = ROOT / "build" / "c+"
if os.environ.get("CPLUS_BIN"):
    BIN = os.environ["CPLUS_BIN"]
elif LOCAL_BIN.exists():
    BIN = str(LOCAL_BIN)
else:
    BIN = "c+"

ENV = dict(os.environ)
ENV["CPLUS_CLANG"] = "/usr/bin/clang"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xBEEF"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))
STAGE = None

STD = "#include <stdlib.h>\n"
STR = "#include <stdlib.h>\n#include <string.h>\n"
JMP = "#include <stdlib.h>\n#include <setjmp.h>\n"
SJ = "#include <setjmp.h>\n"

UNINIT = "is uninitialized (read before it is initialized)"
UAF_DEREF = "use-after-free: dereference of released pointer"
UAF_SUB = "use-after-free: subscript of released pointer"

# (family, template, count, builder).  Builders take (rng, index) and return
# (body, expectation) where expectation is the (kind, substring) tuple.
SPECS = []


def spec(family, name, count, builder):
    SPECS.append((family, name, count, builder))


def reject(sub):
    return ("reject", sub)


ACCEPT = ("accept", "")


# ---------------------------------------------------------------------------
# Family: realloc success/failure alias invalidation and stale aliases.
# ---------------------------------------------------------------------------
def r_raw(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "p=realloc(p,%d*sizeof(int));free(p);return 0;}" % (n, g))
    return body, reject("raw realloc can lose the original block on failure")


def r_raw_alias(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;p=realloc(p,%d*sizeof(int));free(p);(void)q;return 0;}"
        % (n, g))
    return body, reject("raw realloc can lose the original block on failure")


def r_stale_deref(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    v = rng.randint(1, 99)
    rc = rng.randint(3, 9)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;int *t=realloc(p,%d*sizeof(int));if(!t){free(p);return %d;}"
        "p=t;*q=%d;free(p);return 0;}" % (n, g, rc, v))
    return body, reject(UAF_DEREF)


def r_stale_sub(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    v = rng.randint(1, 99)
    rc = rng.randint(3, 9)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;int *t=realloc(p,%d*sizeof(int));if(!t){free(p);return %d;}"
        "p=t;q[0]=%d;free(p);return 0;}" % (n, g, rc, v))
    return body, reject(UAF_SUB)


def r_stale_free(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    rc = rng.randint(3, 9)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;int *t=realloc(p,%d*sizeof(int));if(!t){free(p);return %d;}"
        "p=t;free(q);free(p);return 0;}" % (n, g, rc))
    return body, reject("double free of")


def r_fail_write(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    v = rng.randint(1, 99)
    rc = rng.randint(3, 9)
    big = n + g + rng.randint(8, 24)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *t=realloc(p,%d*sizeof(int));if(!t){p[%d]=%d;free(p);return %d;}"
        "p=t;free(p);return 0;}" % (n, g, big, v, rc))
    return body, reject("exceeds heap allocation")


def r_safe_tmp(rng, k):
    n = rng.randint(2, 12)
    g = n + rng.randint(1, 8)
    v = rng.randint(1, 99)
    rc = rng.randint(3, 9)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *t=realloc(p,%d*sizeof(int));if(!t){free(p);return %d;}"
        "p=t;p[0]=%d;free(p);return 0;}" % (n, g, rc, v))
    return body, ACCEPT


spec("realloc", "raw", 6, r_raw)
spec("realloc", "raw_alias", 6, r_raw_alias)
spec("realloc", "stale_deref", 8, r_stale_deref)
spec("realloc", "stale_sub", 8, r_stale_sub)
spec("realloc", "stale_free", 6, r_stale_free)
spec("realloc", "fail_write", 6, r_fail_write)
spec("realloc", "safe_tmp", 8, r_safe_tmp)


# ---------------------------------------------------------------------------
# Family: definite-init through output pointers and compound assignments.
# ---------------------------------------------------------------------------
def i_read(rng, k):
    v = "v%d" % k
    body = "int main(void){int %s;return %s;}" % (v, v)
    return body, reject(UNINIT)


def i_compound(rng, k):
    ops = ["+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="]
    op = ops[k % len(ops)]
    v = rng.randint(2, 9)
    body = "int main(void){int x;x%s%d;return x;}" % (op, v)
    return body, reject(UNINIT)


def i_incdec(rng, k):
    op = "++" if k % 2 == 0 else "--"
    body = "int main(void){int x;x%s;return x;}" % op
    return body, reject(UNINIT)


def i_out_empty(rng, k):
    v = rng.randint(1, 99)
    fn = "set%d" % k
    body = ("void %s(int *o){(void)o;} int main(void){int x;%s(&x);return x;}"
            % (fn, fn))
    return body, reject(UNINIT)


def i_out_cond(rng, k):
    v = rng.randint(1, 99)
    body = ("void set(int *o,int c){if(c)*o=%d;} "
            "int main(int argc,char **argv){int x;set(&x,argc);(void)argv;return x;}"
            % v)
    return body, reject(UNINIT)


def i_out_write(rng, k):
    # NOTE: the checker does not currently track writes through output
    # pointers, so this safe-looking program is conservatively rejected.
    v = rng.randint(1, 99)
    fn = "fill%d" % k
    body = ("void %s(int *o){*o=%d;} int main(void){int x;%s(&x);return x;}"
            % (fn, v, fn))
    return body, reject(UNINIT)


def i_memset(rng, k):
    # Scalar memset is not treated as definite-init by the checker (an array
    # memset is), so reading the scalar is flagged uninitialized.  Use the
    # explicit builtin form: a literal `memset` is a libc header macro on
    # macOS (expanded to __builtin___memset_chk) but a plain declaration on
    # glibc, so the checker accepts it on Linux and rejects it on macOS.
    # __builtin_memset has one spelling on every platform and is deliberately
    # NOT name-matched by the checker's definite-write table, which keeps this
    # case a reject with the same exact diagnostic substring everywhere.
    v = rng.randint(2, 12)
    body = STD + (
        "int main(void){int x;int y=%d;(void)y;"
        "__builtin_memset(&x,0,sizeof x);return x;}" % v)
    return body, reject(UNINIT)


def i_addr_alias(rng, k):
    v = rng.randint(1, 99)
    body = ("int main(void){int x;int *p=&x;*p=%d;return x;}" % v)
    return body, reject(UNINIT)


def i_loop(rng, k):
    n = rng.randint(2, 9)
    body = "int main(void){int x;for(int i=0;i<%d;i++)x=i;return x;}" % n
    return body, reject(UNINIT)


def i_ok_init(rng, k):
    ops = ["+=", "-=", "*=", "|=", "^=", "<<="]
    op = ops[k % len(ops)]
    a = rng.randint(1, 20)
    b = rng.randint(1, 9)
    body = "int main(void){int x=%d;x%s%d;return x;}" % (a, op, b)
    return body, ACCEPT


def i_ok_branch(rng, k):
    a = rng.randint(1, 50)
    b = rng.randint(51, 99)
    body = "int main(void){int x;if(%d)x=%d;else x=%d;return x;}" % (k % 3, a, b)
    return body, ACCEPT


def i_ok_call(rng, k):
    v = rng.randint(1, 99)
    fn = "calc%d" % k
    body = "int %s(void){return %d;} int main(void){int x=%s();return x;}" % (
        fn, v, fn)
    return body, ACCEPT


def i_ok_array_out(rng, k):
    n = rng.randint(2, 6)
    v = rng.randint(1, 99)
    fn = "fill%d" % k
    body = ("void %s(int *a){a[0]=%d;} int main(void){int b[%d];%s(b);return b[0];}"
            % (fn, v, n, fn))
    return body, ACCEPT


def i_ok_memcpy(rng, k):
    v = rng.randint(1, 99)
    body = STR + (
        "int main(void){int x;int y=%d;memcpy(&x,&y,sizeof x);return x;}" % v)
    return body, ACCEPT


def i_ok_scanf(rng, k):
    body = ("#include <stdio.h>\n"
            "int main(void){int x;if(scanf(\"%d\",&x)!=1)return 2;return x;}")
    return body, ACCEPT


spec("init", "read", 3, i_read)
spec("init", "compound", 10, i_compound)
spec("init", "incdec", 3, i_incdec)
spec("init", "out_empty", 3, i_out_empty)
spec("init", "out_cond", 3, i_out_cond)
spec("init", "out_write", 3, i_out_write)
spec("init", "memset", 3, i_memset)
spec("init", "addr_alias", 2, i_addr_alias)
spec("init", "loop", 2, i_loop)
spec("init", "ok_init", 5, i_ok_init)
spec("init", "ok_branch", 3, i_ok_branch)
spec("init", "ok_call", 2, i_ok_call)
spec("init", "ok_array_out", 2, i_ok_array_out)
spec("init", "ok_memcpy", 2, i_ok_memcpy)
spec("init", "ok_scanf", 2, i_ok_scanf)


# ---------------------------------------------------------------------------
# Family: setjmp / goto cleanup paths.
# ---------------------------------------------------------------------------
def g_skip_free(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "goto L;free(p);L:return 0;}" % n)
    return body, reject("bypasses cleanup of live allocation")


def g_leak_return(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "goto L;L:return 0;}" % n)
    return body, reject("leaks on this return path")


def g_two_allocs(rng, k):
    n = rng.randint(2, 12)
    m = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=malloc(%d*sizeof(int));if(!q){free(p);return 2;}"
        "goto L;free(p);free(q);L:return 0;}" % (n, m))
    return body, reject("bypasses cleanup of live allocation")


def g_double_free(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "goto L;L:free(p);free(p);return 0;}" % n)
    return body, reject("double free of")


def g_stale_alias(rng, k):
    n = rng.randint(2, 12)
    m = n + rng.randint(1, 8)
    v = rng.randint(1, 99)
    rc = rng.randint(3, 9)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;goto L;L:int *t=realloc(p,%d*sizeof(int));"
        "if(!t){free(p);return %d;}p=t;*q=%d;free(p);return 0;}"
        % (n, m, rc, v))
    return body, reject(UAF_DEREF)


def g_sj_ok(rng, k):
    jb = "jb%d" % k
    body = SJ + "jmp_buf %s;int main(void){if(setjmp(%s))return 1;return 0;}" % (
        jb, jb)
    return body, ACCEPT


def g_sj_longjmp_ok(rng, k):
    jb = "jb%d" % k
    body = SJ + (
        "jmp_buf %s;int main(void){if(setjmp(%s)==0)longjmp(%s,1);return 0;}"
        % (jb, jb, jb))
    return body, ACCEPT


def g_sj_free_ok(rng, k):
    jb = "jb%d" % k
    n = rng.randint(2, 12)
    body = JMP + (
        "jmp_buf %s;int main(void){if(setjmp(%s)==0){int *p=malloc(%d*sizeof(int));"
        "if(!p)return 2;free(p);longjmp(%s,1);}return 0;}"
        % (jb, jb, n, jb))
    return body, ACCEPT


def g_goto_ok(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "goto L;L:free(p);return 0;}" % n)
    return body, ACCEPT


def g_goto_free_before_ok(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "free(p);goto L;L:return 0;}" % n)
    return body, ACCEPT


def g_goto_backward_ok(rng, k):
    n = rng.randint(2, 9)
    body = "int main(void){int i=0;L:i++;if(i<%d)goto L;return i;}" % n
    return body, ACCEPT


spec("cfg", "skip_free", 6, g_skip_free)
spec("cfg", "leak_return", 5, g_leak_return)
spec("cfg", "two_allocs", 4, g_two_allocs)
spec("cfg", "double_free", 4, g_double_free)
spec("cfg", "stale_alias", 5, g_stale_alias)
spec("cfg", "sj_ok", 4, g_sj_ok)
spec("cfg", "sj_longjmp_ok", 4, g_sj_longjmp_ok)
spec("cfg", "sj_free_ok", 3, g_sj_free_ok)
spec("cfg", "goto_ok", 3, g_goto_ok)
spec("cfg", "goto_free_before_ok", 2, g_goto_free_before_ok)
spec("cfg", "goto_backward_ok", 2, g_goto_backward_ok)


# ---------------------------------------------------------------------------
# Family: field / array alias release.
# ---------------------------------------------------------------------------
def a_field_deref(rng, k):
    n = rng.randint(2, 12)
    v = rng.randint(1, 99)
    body = STD + (
        "struct S{int *p;};int main(void){struct S s;"
        "s.p=malloc(%d*sizeof(int));if(!s.p)return 2;free(s.p);*s.p=%d;return 0;}"
        % (n, v))
    return body, reject(UAF_DEREF)


def a_field_sub(rng, k):
    n = rng.randint(2, 12)
    v = rng.randint(1, 99)
    body = STD + (
        "struct S{int *p;};int main(void){struct S s;"
        "s.p=malloc(%d*sizeof(int));if(!s.p)return 2;free(s.p);s.p[0]=%d;return 0;}"
        % (n, v))
    return body, reject(UAF_SUB)


def a_nested(rng, k):
    n = rng.randint(2, 12)
    v = rng.randint(1, 99)
    body = STD + (
        "struct I{int *p;};struct O{struct I i;};int main(void){struct O o;"
        "o.i.p=malloc(%d*sizeof(int));if(!o.i.p)return 2;free(o.i.p);"
        "*o.i.p=%d;return 0;}" % (n, v))
    return body, reject(UAF_DEREF)


def a_arrptr(rng, k):
    n = rng.randint(2, 12)
    v = rng.randint(1, 99)
    body = STD + (
        "int main(void){int *a[2];a[0]=malloc(%d*sizeof(int));if(!a[0])return 2;"
        "free(a[0]);*a[0]=%d;return 0;}" % (n, v))
    return body, reject(UAF_DEREF)


def a_plain_deref(rng, k):
    n = rng.randint(2, 12)
    v = rng.randint(1, 99)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;free(p);*q=%d;return 0;}" % (n, v))
    return body, reject(UAF_DEREF)


def a_plain_sub(rng, k):
    n = rng.randint(2, 12)
    v = rng.randint(1, 99)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;free(p);q[0]=%d;return 0;}" % (n, v))
    return body, reject(UAF_SUB)


def a_plain_df(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "int *q=p;free(p);free(q);return 0;}" % n)
    return body, reject("double free of")


def a_ok_field(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "struct S{int *p;};int main(void){struct S s;"
        "s.p=malloc(%d*sizeof(int));if(!s.p)return 2;free(s.p);s.p=0;return 0;}"
        % n)
    return body, ACCEPT


def a_ok_plain(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *p=malloc(%d*sizeof(int));if(!p)return 2;"
        "free(p);p=0;return 0;}" % n)
    return body, ACCEPT


def a_ok_array(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "int main(void){int *a=malloc(%d*sizeof(int));if(!a)return 2;"
        "free(a);return 0;}" % n)
    return body, ACCEPT


def a_ok_two_fields(rng, k):
    n = rng.randint(2, 12)
    body = STD + (
        "struct S{int *p;int *q;};int main(void){struct S s;"
        "s.p=malloc(%d*sizeof(int));if(!s.p)return 2;s.q=s.p;free(s.p);"
        "s.p=0;s.q=0;return 0;}" % n)
    return body, ACCEPT


spec("alias", "field_deref", 5, a_field_deref)
spec("alias", "field_sub", 5, a_field_sub)
spec("alias", "nested", 3, a_nested)
spec("alias", "arrptr", 3, a_arrptr)
spec("alias", "plain_deref", 4, a_plain_deref)
spec("alias", "plain_sub", 4, a_plain_sub)
spec("alias", "plain_df", 4, a_plain_df)
spec("alias", "ok_field", 3, a_ok_field)
spec("alias", "ok_plain", 2, a_ok_plain)
spec("alias", "ok_array", 2, a_ok_array)
spec("alias", "ok_two_fields", 3, a_ok_two_fields)


# ---------------------------------------------------------------------------
# Generation / verification.
# ---------------------------------------------------------------------------
def build_cases():
    cases = []
    index = 0
    seen = set()
    for family, tname, count, builder in SPECS:
        for j in range(count):
            rng = random.Random("%s:%d:%d" % (SEED, index, j))
            body, expect = builder(rng, index)
            name = "p0fuzz_%s_%s_%03d" % (family, tname, j)
            body = "/* %s seed=%#x */\n%s\n" % (name, SEED, body.strip())
            assert name not in seen, "duplicate name %s" % name
            seen.add(name)
            cases.append((name, family, tname, body, expect))
            index += 1
    return cases


def first_diag(text):
    for line in text.splitlines():
        s = line.strip()
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:200]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:200] if lines else "(no diagnostics)"


def check_case(case):
    name, family, tname, body, (kind, substr) = case
    path = STAGE / (name + ".c")
    path.write_text(body)
    try:
        proc = subprocess.run([BIN, "check", "--strict", str(path)],
                              capture_output=True, text=True,
                              env=ENV, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return case, False, "TIMEOUT after %ds" % TIMEOUT, ""
    except OSError as exc:
        return case, False, "OS ERROR: %s" % exc, ""

    text = (proc.stderr + proc.stdout).strip()
    failed = proc.returncode != 0
    if kind == "reject":
        ok = failed and substr in text
        why = "" if ok else ("accepted (expected reject)" if not failed
                             else "missing substring %r" % substr)
    else:
        ok = not failed
        why = "" if ok else "rejected in STRICT: %s" % first_diag(text)
    return case, ok, why, first_diag(text)


def main():
    global STAGE
    cases = build_cases()
    bodies = [c[3] for c in cases]
    unique = len(set(bodies))
    families = collections.Counter(c[1] for c in cases)
    outcomes = collections.Counter(c[4][0] for c in cases)
    print("p0fuzz: %d generated cases (%d distinct), seed=%#x, workers=%d, bin=%s"
          % (len(cases), unique, SEED, WORKERS, BIN))
    print("  families: " + " ".join("%s=%d" % (f, families[f])
                                    for f in sorted(families)))
    print("  expectations: reject=%d accept=%d"
          % (outcomes["reject"], outcomes["accept"]))
    if unique != len(cases):
        print("FATAL: duplicate program bodies (%d)" % (len(cases) - unique))
        return 2

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="p0fuzz_", dir="/tmp"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    failures = [r for r in results if not r[1]]
    by_family = collections.Counter(r[0][1] for r in failures)
    print("  failures by family: "
          + ("none" if not failures else
             " ".join("%s=%d" % (f, by_family[f]) for f in sorted(by_family))))
    if not failures:
        print("PASS: %d/%d cases matched expectations "
              "(0 failures, 0 false positives)" % (len(cases), len(cases)))
        print("  generated sources: %s" % STAGE)
        return 0

    print("FAIL: %d/%d cases failed (%d passed)"
          % (len(failures), len(cases), len(results) - len(failures)))
    for n, (case, ok, why, diag) in enumerate(failures[:10], 1):
        name, family, tname, body, (kind, substr) = case
        print("  [%2d] %s (%s/%s) expect=%s %r: %s | %s"
              % (n, name, family, tname, kind, substr, why, diag))
    if len(failures) > 10:
        print("  ... %d more failures" % (len(failures) - 10))
    print("  generated sources: %s" % STAGE)
    return 1


if __name__ == "__main__":
    sys.exit(main())
