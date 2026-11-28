#!/usr/bin/env python3
"""Semantic-adversarial compiler suite for issue #29 (compiler portion).

Self-contained: the manifest below is the single source of truth for fixture
sources and exact expectations.  `--write-fixtures` materialises cases/*.c;
normal runs verify that the on-disk fixtures still match the manifest.

Two assertion kinds only (no skips, no WARN_ONLY, no `|| true`):
  reject: c+ check --strict must exit non-zero with an exact diagnostic substring.
  run:    c+ build --strict must succeed AND the executable must print exact
          stdout and exit with the exact expected code.
Every process has a hard wall-clock timeout; a timeout is a FAIL.
"""
import datetime
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OUT = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(OUT, "cases")
RESULTS = os.path.join(OUT, "results")
LOGDIR = os.path.join(RESULTS, "logs")
BINDIR = os.path.join(RESULTS, "bin")
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
CHECK_TIMEOUT = 30
BUILD_TIMEOUT = 90
RUN_TIMEOUT = 15
STRICT = ["--strict", "--sanitize=none"]

# (id, group, source, expectation, expected value)
# expectation "reject" -> expected value is a diagnostic substring.
# expectation "run"    -> expected value is (exact stdout, exact exit code).
CASES = [
    # ---- casts -------------------------------------------------------------
    ("cast_index_long", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[(int)(long)9]); return 0; }
''', "reject", "constant array index 9"),
    ("cast_index_safe", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[(int)(long)2]); return 0; }
''', "run", ("3\n", 0)),
    ("cast_size_t_negative", "casts", r'''
#include <stdio.h>
#include <stddef.h>
int main(void){ int a[4]={1,2,3,4}; size_t i=(size_t)-1; printf("%d\n", a[i]); return 0; }
''', "reject", "negative array index"),
    ("cast_alias_ptr", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=(int*)a; printf("%d\n", p[9]); return 0; }
''', "reject", "via pointer 'p'"),
    ("cast_alias_safe", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=(int*)a; printf("%d\n", p[2]); return 0; }
''', "run", ("3\n", 0)),
    ("byte_cast_oob", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; char *b=(char*)a; b[36]=7; printf("%d\n", a[0]); return 0; }
''', "reject", "index 36 reaches past array 'a'"),
    ("byte_cast_safe", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; char *b=(char*)a; printf("%d\n", (int)b[4]); return 0; }
''', "run", ("2\n", 0)),
    ("void_cast_launder", "casts", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; void *v=a; int *p=(int*)v + 9; *p=7; printf("%d\n", *p); return 0; }
''', "reject", "via pointer 'p'"),
    ("typedef_alias", "casts", r'''
#include <stdio.h>
typedef int *IP;
int main(void){ int a[4]={1,2,3,4}; IP p=a; printf("%d\n", p[9]); return 0; }
''', "reject", "via pointer 'p'"),
    ("typedef_alias_safe", "casts", r'''
#include <stdio.h>
typedef int *IP;
int main(void){ int a[4]={1,2,3,4}; IP p=a; printf("%d\n", p[2]); return 0; }
''', "run", ("3\n", 0)),

    # ---- macros (the lexer sees replacement text, not expansion structure) -
    ("macro_literal_index", "macros", r'''
#include <stdio.h>
#define NINE 9
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[NINE]); return 0; }
''', "reject", "constant array index 9"),
    ("macro_literal_index_safe", "macros", r'''
#include <stdio.h>
#define TWO 2
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[TWO]); return 0; }
''', "run", ("3\n", 0)),
    ("macro_subscript_body", "macros", r'''
#include <stdio.h>
#define AT(x,i) x[i]
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", AT(a,9)); return 0; }
''', "reject", "constant array index 9"),
    ("macro_extent", "macros", r'''
#include <stdio.h>
#define N 4
int main(void){ int a[N]={1,2,3,4}; printf("%d\n", a[9]); return 0; }
''', "reject", "constant array index 9"),
    ("macro_double_expand", "macros", r'''
#include <stdio.h>
#define OFFSET 9
#define A a[OFFSET]
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", A); return 0; }
''', "reject", "constant array index 9"),
    ("macro_ptr_offset", "macros", r'''
#include <stdio.h>
#define OFF 9
int main(void){ int a[4]={1,2,3,4}; int *p=a+OFF; printf("%d\n", p[0]); return 0; }
''', "reject", "pointer arithmetic outside array"),
    ("macro_cast_launder", "macros", r'''
#include <stdio.h>
#include <stdint.h>
#define LAUNDER(x) ((int*)(uintptr_t)(x) + 9)
int main(void){ int a[4]={1,2,3,4}; int *p=LAUNDER(a); printf("%d\n", p[0]); return 0; }
''', "reject", "unverifiable provenance"),
    ("macro_multi_statement", "macros", r'''
#include <stdio.h>
#define TWICE(x) do { (x); (x); } while (0)
int main(void){ int n=1; TWICE(n++); printf("%d\n", n); return 0; }
''', "reject", "arithmetic in a macro expansion cannot be instrumented"),

    # ---- aliases -----------------------------------------------------------
    ("alias_direct", "aliases", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("%d\n", p[9]); return 0; }
''', "reject", "via pointer 'p'"),
    ("alias_one_past_deref", "aliases", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=a+4; printf("%d\n", *p); return 0; }
''', "reject", "one past the end"),
    ("alias_chain", "aliases", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=a; int *q=p; printf("%d\n", q[9]); return 0; }
''', "reject", "via pointer 'q'"),
    ("alias_reassigned_safe", "aliases", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int b[16]={0}; int *p=a; p=b; p[9]=7; printf("%d\n", p[9]); return 0; }
''', "run", ("7\n", 0)),
    ("alias_struct_field", "aliases", r'''
#include <stdio.h>
struct S { int *p; };
int main(void){ int a[4]={1,2,3,4}; struct S s; s.p=a; printf("%d\n", s.p[9]); return 0; }
''', "reject", "out-of-bounds access"),
    ("alias_struct_field_safe", "aliases", r'''
#include <stdio.h>
struct S { int *p; };
int main(void){ int a[4]={1,2,3,4}; struct S s; s.p=a; printf("%d\n", s.p[2]); return 0; }
''', "run", ("3\n", 0)),
    ("alias_row_pointer", "aliases", r'''
#include <stdio.h>
int main(void){ int a[2][4]={{1,2,3,4},{5,6,7,8}}; int (*r)[4]=a; printf("%d\n", r[9][0]); return 0; }
''', "reject", "via pointer 'r'"),
    ("alias_row_pointer_safe", "aliases", r'''
#include <stdio.h>
int main(void){ int a[2][4]={{1,2,3,4},{5,6,7,8}}; int (*r)[4]=a; printf("%d\n", r[1][2]); return 0; }
''', "run", ("7\n", 0)),
    ("alias_pointer_to_pointer", "aliases", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=a; int **pp=&p; printf("%d\n", (*pp)[9]); return 0; }
''', "reject", "via pointer 'pp'"),
    ("alias_paren_pointer", "aliases", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=(a); printf("%d\n", (p)[9]); return 0; }
''', "reject", "array index 9"),

    # ---- shadowed declarations / nested scopes ----------------------------
    ("scope_inner_shadow_safe", "scopes", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; { int a[64]={0}; a[9]=7; printf("%d\n", a[9]); } printf("%d\n", a[0]); return 0; }
''', "run", ("7\n1\n", 0)),
    ("scope_outer_after_inner", "scopes", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; { int a[64]={0}; a[9]=7; } printf("%d\n", a[9]); return 0; }
''', "reject", "constant array index 9"),
    ("scope_param_shadows_global", "scopes", r'''
#include <stdio.h>
static int a[4]={1,2,3,4};
static void f(int *a){ printf("%d\n", a[9]); }
int main(void){ int b[64]={0}; f(b); return 0; }
''', "run", ("0\n", 0)),
    ("scope_param_bigger_array", "scopes", r'''
#include <stdio.h>
static void f(int *p){ printf("%d\n", p[9]); }
int main(void){ int big[16]={0}; big[9]=42; f(big); return 0; }
''', "run", ("42\n", 0)),
    ("scope_param_name_like_global", "scopes", r'''
#include <stdio.h>
static int a[4]={1,2,3,4};
static void f(int *a){ printf("%d\n", a[3]); }
int main(void){ int b[8]={0}; b[3]=42; f(b); return 0; }
''', "run", ("42\n", 0)),
    ("scope_local_shadows_global_safe", "scopes", r'''
#include <stdio.h>
static int a[4]={1,2,3,4};
static void f(void){ int a[8]={0}; a[7]=42; printf("%d\n", a[7]); }
int main(void){ f(); printf("%d\n", a[0]); return 0; }
''', "run", ("42\n1\n", 0)),
    ("scope_nested_oob", "scopes", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; { { printf("%d\n", a[12]); } } return 0; }
''', "reject", "constant array index 12"),
    ("scope_scalar_shadow_safe", "scopes", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; { int a=7; (void)a; } printf("%d\n", a[3]); return 0; }
''', "run", ("4\n", 0)),

    # ---- conditional expressions ------------------------------------------
    ("cond_negative_branch", "conditionals", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int f=0; printf("%d\n", a[f?-1:0]); return 0; }
''', "reject", "constant array index -1"),
    ("cond_phi_variable", "conditionals", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int f=1; int i=f?-1:2; printf("%d\n", a[i]); return 0; }
''', "reject", "chosen by conditional expression"),
    ("cond_parenthesized_oob", "conditionals", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int f=0; printf("%d\n", a[(f?3:20)]); return 0; }
''', "reject", "out-of-bounds"),
    ("cond_nested_oob", "conditionals", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int f=0,g=0; printf("%d\n", a[f?0:(g?1:99)]); return 0; }
''', "reject", "out-of-bounds"),
    ("cond_constant_safe", "conditionals", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[0?9:1]); return 0; }
''', "run", ("2\n", 0)),
    ("cond_both_branches_safe", "conditionals", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int f=0; printf("%d\n", a[f?1:2]); return 0; }
''', "run", ("3\n", 0)),

    # ---- comma and parenthesized expressions ------------------------------
    ("expr_paren_literal", "expressions", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[((9))]); return 0; }
''', "reject", "constant array index 9"),
    ("expr_arith_literal", "expressions", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[1+8]); return 0; }
''', "reject", "constant array index 9"),
    ("expr_comma_safe", "expressions", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[((void)0,(void)9,1)]); return 0; }
''', "run", ("2\n", 0)),
    ("expr_comma_oob", "expressions", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", a[(1,9)]); return 0; }
''', "reject", "constant array index 9"),
    ("expr_comma_call_args", "expressions", r'''
#include <stdio.h>
static int f(int x,int y){ return x+y; }
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", f(a[1],a[9])); return 0; }
''', "reject", "constant array index 9"),
    ("expr_paren_lvalue_safe", "expressions", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", (a)[1]); return 0; }
''', "run", ("2\n", 0)),

    # ---- integer-laundered pointers ---------------------------------------
    ("launder_uintptr_add", "laundering", r'''
#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=(uintptr_t)a + 9*sizeof(int); int *p=(int*)u; *p=7; printf("%d\n", *p); return 0; }
''', "reject", "pointer arithmetic more than one past"),
    ("launder_tag_or", "laundering", r'''
#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=((uintptr_t)a)|1; int *p=(int*)(u & ~(uintptr_t)1); p[9]=7; printf("%d\n", p[9]); return 0; }
''', "reject", "unverifiable provenance"),
    ("launder_add_variable", "laundering", r'''
#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=(uintptr_t)a; int k=9; u += (uintptr_t)k*sizeof(int); int *p=(int*)u; p[0]=7; printf("%d\n", p[0]); return 0; }
''', "reject", "unverifiable provenance"),
    ("launder_xor", "laundering", r'''
#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=(uintptr_t)a ^ 8; u ^= 8; int *p=(int*)u; p[9]=7; printf("%d\n", p[9]); return 0; }
''', "reject", "out-of-bounds access"),
    ("launder_union", "laundering", r'''
#include <stdio.h>
#include <stdint.h>
union U { uintptr_t u; int *p; };
int main(void){ int a[4]={1,2,3,4}; union U x; x.u=(uintptr_t)a + 9*sizeof(int); printf("%d\n", x.p[0]); return 0; }
''', "reject", "pointer arithmetic more than one past"),
    ("launder_long_cast", "laundering", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; long u=(long)(void*)a; int *p=(int*)(u + 9L*4L); p[0]=7; printf("%d\n", p[0]); return 0; }
''', "reject", "unverifiable provenance"),

    # ---- loop / data-flow facts the token pass approximates ---------------
    ("flow_for_loop_oob", "dataflow", r'''
#include <stdio.h>
int main(void){ int a[4]={0}; for(int i=0;i<10;i++) a[i]=1; printf("%d\n", a[0]); return 0; }
''', "reject", "constant array index 9"),
    ("flow_while_loop_oob", "dataflow", r'''
#include <stdio.h>
int main(void){ int a[4]={0}; int i=0; while(i<10) a[i++]=1; printf("%d\n", a[0]); return 0; }
''', "reject", "out-of-bounds"),
    ("flow_volatile_index_safe", "dataflow", r'''
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; volatile int v=2; int i=v; printf("%d\n", a[i]); return 0; }
''', "run", ("3\n", 0)),
    ("flow_helper_index_safe", "dataflow", r'''
#include <stdio.h>
static int idx(void){ return 2; }
int main(void){ int a[4]={1,2,3,4}; int i=idx(); printf("%d\n", a[i]); return 0; }
''', "run", ("3\n", 0)),
]


def fixture_path(cid):
    return os.path.join(CASES_DIR, cid + ".c")


def write_fixtures():
    os.makedirs(CASES_DIR, exist_ok=True)
    for cid, _group, src, _kind, _want in CASES:
        with open(fixture_path(cid), "w") as f:
            f.write(src.lstrip("\n"))


def verify_fixtures():
    missing, stale = [], []
    for cid, _group, src, _kind, _want in CASES:
        p = fixture_path(cid)
        if not os.path.isfile(p):
            missing.append(p)
        else:
            with open(p) as f:
                if f.read() != src.lstrip("\n"):
                    stale.append(p)
    if missing or stale:
        print("fixtures missing/stale: %s %s" % (missing, stale))
        print("run: python3 %s --write-fixtures" % os.path.abspath(__file__))
        return False
    return True


def run_cmd(argv, timeout):
    """Never raises on timeout; returns (rc, stdout, stderr, note)."""
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, cwd=OUT)
        return (p.returncode, p.stdout.decode(errors="replace"),
                p.stderr.decode(errors="replace"), "")
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors="replace") if e.stdout else ""
        err = e.stderr.decode(errors="replace") if e.stderr else ""
        return (-999, out, err, "TIMEOUT after %ss" % timeout)
    except OSError as e:
        return (-998, "", str(e), "OS_ERROR")


def main():
    if "--write-fixtures" in sys.argv:
        write_fixtures()
        print("wrote %d fixtures to %s" % (len(CASES), CASES_DIR))
        return 0
    if not os.path.isfile(BIN):
        print("missing CPLUS_BIN: %s" % BIN)
        return 2
    if not verify_fixtures():
        return 2
    for d in (RESULTS, LOGDIR, BINDIR):
        os.makedirs(d, exist_ok=True)

    rows, failures = [], []
    for cid, group, _src, kind, want in CASES:
        src = fixture_path(cid)
        exe = os.path.join(BINDIR, cid)
        check_cmd = [BIN, "check"] + STRICT + [src]
        build_cmd = [BIN, "build"] + STRICT + [src, "-o", exe]
        row = {
            "id": cid, "group": group, "fixture": src, "expectation": kind,
            "expected": want if kind == "reject" else {"stdout": want[0], "exit": want[1]},
            "commands": {"check": " ".join(check_cmd), "build": " ".join(build_cmd),
                         "run": exe},
        }
        log = ["case %s [%s] expectation=%s" % (cid, group, kind),
               "$ " + " ".join(check_cmd)]
        crc, cout, cerr, cnote = run_cmd(check_cmd, CHECK_TIMEOUT)
        row["check"] = {"rc": crc, "stdout": cout, "stderr": cerr, "note": cnote}
        log.append("rc=%d note=%s\nstdout:\n%s\nstderr:\n%s" % (crc, cnote, cout, cerr))

        if kind == "reject":
            combined = cout + cerr
            ok = (0 < crc < 128) and cnote == "" and want in combined
            row["status"] = "PASS" if ok else "FAIL"
            if not ok:
                reason = ("accepted (rc=0)" if crc == 0 else
                          "diagnostic substring %r absent" % want)
                if cnote:
                    reason = cnote
                # Evidence only: show what an accepted unsafe case actually does.
                if crc == 0:
                    log.append("$ " + " ".join(build_cmd))
                    brc, bout, berr, bnote = run_cmd(build_cmd, BUILD_TIMEOUT)
                    row["build"] = {"rc": brc, "stdout": bout, "stderr": berr,
                                    "note": bnote}
                    log.append("build rc=%d note=%s" % (brc, bnote))
                    if brc == 0:
                        rrc, rout, rerr, rnote = run_cmd([exe], RUN_TIMEOUT)
                        row["run"] = {"rc": rrc, "stdout": rout, "stderr": rerr,
                                      "note": rnote}
                        log.append("$ %s\nrc=%d note=%s\nstdout:\n%s\nstderr:\n%s" %
                                   (exe, rrc, rnote, rout, rerr))
                        reason += "; run rc=%s stdout=%r stderr=%r" % (
                            rrc, rout[:200], rerr[:200])
                failures.append({"id": cid, "fixture": src, "reason": reason,
                                 "log": os.path.join(LOGDIR, cid + ".txt")})
                row["reason"] = reason
        else:
            log.append("$ " + " ".join(build_cmd))
            brc, bout, berr, bnote = run_cmd(build_cmd, BUILD_TIMEOUT)
            row["build"] = {"rc": brc, "stdout": bout, "stderr": berr, "note": bnote}
            log.append("rc=%d note=%s\nstdout:\n%s\nstderr:\n%s" % (brc, bnote, bout, berr))
            reason = None
            if brc != 0:
                reason = "build failed rc=%d %s" % (brc, bnote)
            else:
                rrc, rout, rerr, rnote = run_cmd([exe], RUN_TIMEOUT)
                row["run"] = {"rc": rrc, "stdout": rout, "stderr": rerr, "note": rnote}
                log.append("$ %s\nrc=%d note=%s\nstdout:\n%s\nstderr:\n%s" %
                           (exe, rrc, rnote, rout, rerr))
                if rnote or rout != want[0] or rrc != want[1]:
                    reason = ("run mismatch: rc=%r (want %r) stdout=%r (want %r) note=%s" %
                              (rrc, want[1], rout[:200], want[0], rnote))
            if reason:
                row["status"] = "FAIL"
                row["reason"] = reason
                failures.append({"id": cid, "fixture": src, "reason": reason,
                                 "log": os.path.join(LOGDIR, cid + ".txt")})
            else:
                row["status"] = "PASS"

        with open(os.path.join(LOGDIR, cid + ".txt"), "w") as f:
            f.write("\n".join(log) + "\n")
        print("%-34s %-6s %s" % (cid, row["status"], row.get("reason", "")))
        rows.append(row)

    n = len(rows)
    npass = sum(1 for r in rows if r["status"] == "PASS")
    nfail = n - npass
    counts = {
        "total": n, "pass": npass, "fail": nfail,
        "reject_expectations": sum(1 for r in rows if r["expectation"] == "reject"),
        "run_expectations": sum(1 for r in rows if r["expectation"] == "run"),
        "reject_failures": sum(1 for r in rows if r["expectation"] == "reject" and r["status"] == "FAIL"),
        "run_failures": sum(1 for r in rows if r["expectation"] == "run" and r["status"] == "FAIL"),
    }
    payload = {
        "version": 1,
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "binary": BIN, "root": ROOT,
        "flags": {"check": STRICT, "build": STRICT},
        "timeouts_seconds": {"check": CHECK_TIMEOUT, "build": BUILD_TIMEOUT,
                             "run": RUN_TIMEOUT},
        "counts": counts,
        "failures": failures,
        "results": rows,
    }
    with open(os.path.join(RESULTS, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
    with open(os.path.join(RESULTS, "summary.txt"), "w") as f:
        f.write("Semantic-adversarial suite (issue #29, compiler portion)\n")
        f.write("generated: %s\nbinary: %s\n" % (payload["generated"], BIN))
        f.write("flags: check/build %s ; timeouts check=%ss build=%ss run=%ss\n" %
                (" ".join(STRICT), CHECK_TIMEOUT, BUILD_TIMEOUT, RUN_TIMEOUT))
        f.write("counts: total=%d PASS=%d FAIL=%d reject_expectations=%d "
                "run_expectations=%d reject_failures=%d run_failures=%d\n\n" %
                (counts["total"], counts["pass"], counts["fail"],
                 counts["reject_expectations"], counts["run_expectations"],
                 counts["reject_failures"], counts["run_failures"]))
        for r in rows:
            f.write("%-34s %-4s %-13s %s\n" %
                    (r["id"], r["status"], r["expectation"], r["fixture"]))
        f.write("\nFAILURES (%d)\n" % len(failures))
        for x in failures:
            f.write("  %s\n    fixture: %s\n    reason: %s\n    log: %s\n" %
                    (x["id"], x["fixture"], x["reason"], x["log"]))
        f.write("\ncommands are recorded verbatim per case in summary.json "
                "(row.commands) and results/logs/<id>.txt\n")
    print("\ntotal=%d PASS=%d FAIL=%d (reject=%d/%d fail, run=%d/%d fail)" %
          (n, npass, nfail, counts["reject_failures"],
           counts["reject_expectations"], counts["run_failures"],
           counts["run_expectations"]))
    for x in failures:
        print("FAIL %s: %s" % (x["id"], x["reason"]))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
