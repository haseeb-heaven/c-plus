#!/usr/bin/env python3
"""Regression suite for GitHub issues #228, #229 and #230 (RaiiLowering).

Each case is compiled with `c+ build --default --sanitize=none`, executed, and
checked against exact stdout.  Cases with a `text` entry also assert that a
specific rewrite survived in the lowered C; this catches edits that used to be
silently dropped by an overlapping whole-span return replacement.

Commands, exit codes and outputs are recorded verbatim.  No case is skipped.

Run:  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_issues_228_230/run_suite.py
"""
import datetime
import json
import os
import platform
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASES_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

BUILD_TIMEOUT = 60
RUN_TIMEOUT = 10

PRELUDE = """#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r){ if (r->id) { printf("D%d\\n", r->id); r->id = 0; } }
static R R_new(int id){ R r = { id }; return r; }
"""

# --- issue #228: loop frames -------------------------------------------------
ISSUE228_INNER_WHILE = PRELUDE + """int main(void){
  int cond = 1;
  do {
    R outer = R_new(1);
    while (cond) {
      R inner = R_new(2);
      break;
    }
    printf("use %d\\n", outer.id);
  } while (0);
  return 0;
}
"""

ISSUE228_NESTED_DO = PRELUDE + """int main(void){
  int cond = 1;
  do {
    R outer = R_new(1);
    do {
      R middle = R_new(2);
      while (cond) {
        R inner = R_new(3);
        break;
      }
      printf("mid %d\\n", middle.id);
    } while (0);
    printf("out %d\\n", outer.id);
  } while (0);
  return 0;
}
"""

ISSUE228_FOR_HEADER_BRACE = PRELUDE + """struct S { int x; };
int main(void){
  for (struct S s = {1}; s.x < 4; ++s.x) {
    R r = R_new(s.x);
    printf("I%d\\n", s.x);
    if (s.x == 2) break;
  }
  printf("done\\n");
  return 0;
}
"""

# --- issue #229: by-value ownership -----------------------------------------
ISSUE229_EARLY_RETURN = PRELUDE + """static void consume(R r){ if (r.id) return; printf("C%d\\n", r.id); }
int main(void){ R a = R_new(7); consume(a); printf("after\\n"); return 0; }
"""

ISSUE229_CROSS_FUNCTION = PRELUDE + """static void consume(R r){ (void)r; }
static void f(void){ R r = R_new(5); printf("f\\n"); }
static void g(void){ R r = R_new(6); consume(r); printf("g\\n"); }
int main(void){ f(); g(); printf("main\\n"); return 0; }
"""

ISSUE229_CONDITIONAL = PRELUDE + """static void consume(R r){ printf("C%d\\n", r.id); }
int main(void){ R r = R_new(9); if (0) consume(r); printf("skip\\n"); return 0; }
"""

# --- issue #230: return-expression edits ------------------------------------
ISSUE230_EXPLICIT_DROP = PRELUDE + """int take(void){ R a = R_new(4); return (R_drop(&a), 7); }
int main(void){ printf("%d\\n", take()); return 0; }
"""

ISSUE230_ARRAY_RETURN = PRELUDE + """static int buf[4] = {1,2,3,4};
static int *get(void){ R r = R_new(8); return buf; }
int main(void){ printf("%d\\n", get()[2]); return 0; }
"""

# `store` prints the region-owned value so the case has observable stdout, and
# returns 0 so a successful run also has rc == 0.  The alloc stays inside the
# return expression: the whole point is that the #230 rewrite of `return` must
# not clobber the cplus_region_alloc -> cplus_region_take edit.
ISSUE230_REGION_RETURN = """#include <stdio.h>
static int store(int *p){ *p = 7; printf("%d\\n", *p); return 0; }
int main(void){ region r { return store((int *)cplus_region_alloc(r, sizeof(int))); } }
"""

# Non-idempotent destructor: it prints on every call.  This makes a duplicate
# scope-exit drop observable, unlike the idempotent R_drop above.
NONIDEM_PRELUDE = """#include <stdio.h>
resource struct N { int id; };
void N_drop(N *n){ printf("D%d\\n", n->id); }
static N N_new(int id){ N n = { id }; return n; }
"""

CASES = [
    dict(name="raii_goto_same_block", expect="A\nB\nD1\n", no_werror=True,
         source=NONIDEM_PRELUDE + "int main(void){ N n=N_new(1); printf(\"A\\n\"); goto L; L: printf(\"B\\n\"); return 0; }\n"),
    dict(name="raii_goto_leaves_block", expect="A\nD2\nB\n", no_werror=True,
         source=NONIDEM_PRELUDE + "int main(void){ { N n=N_new(2); printf(\"A\\n\"); goto L; } L: printf(\"B\\n\"); return 0; }\n"),
    dict(name="issue228_inner_while_break", expect="D2\nuse 1\nD1\n",
         source=ISSUE228_INNER_WHILE),
    dict(name="issue228_nested_do_break", expect="D3\nmid 2\nD2\nout 1\nD1\n",
         source=ISSUE228_NESTED_DO),
    dict(name="issue228_for_header_initializer",
         expect="I1\nD1\nI2\nD2\ndone\n", source=ISSUE228_FOR_HEADER_BRACE),
    dict(name="issue229_early_return_param", expect="D7\nafter\n",
         source=ISSUE229_EARLY_RETURN,
         text=["(void)cplus_zero(&a, sizeof(a))"]),
    # The call site in g is the transfer point: r is zeroed there, so g's own
    # exit drop is a no-op and the callee's by-value copy emits D6 *before*
    # "g" is printed.  The old forward scan suppressed the wrong function's
    # drop instead of zeroing at the call.
    dict(name="issue229_cross_function", expect="f\nD5\nD6\ng\nmain\n",
         source=ISSUE229_CROSS_FUNCTION,
         text=["(void)cplus_zero(&r, sizeof(r))"]),
    dict(name="issue229_untaken_branch", expect="skip\nD9\n",
         source=ISSUE229_CONDITIONAL,
         text=["(void)cplus_zero(&r, sizeof(r))"]),
    dict(name="issue230_explicit_drop_in_return", expect="D4\n7\n",
         source=ISSUE230_EXPLICIT_DROP,
         text=["__auto_type __cplus_ret",
               "(void)cplus_zero(&a, sizeof(a))"]),
    dict(name="issue230_array_return", expect="D8\n3\n",
         source=ISSUE230_ARRAY_RETURN,
         text=["__auto_type __cplus_ret"],
         absent=["__typeof__(buf) __cplus_ret"]),
    dict(name="issue230_region_alloc_in_return", expect="7\n",
         source=ISSUE230_REGION_RETURN,
         text=["cplus_region_take(&r"], absent=["cplus_region_alloc"]),
]


def run(argv, timeout):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def main():
    rows, fails = [], []
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2
    for case in CASES:
        name = case["name"]
        path = os.path.join(CASES_DIR, name + ".cp")
        with open(path, "w") as f:
            f.write(case["source"])
        exe = os.path.join(OUT, name + ".bin")
        lowered = os.path.join(OUT, name + ".c")
        steps = []

        build_args = [BIN, "build", "--default", "--sanitize=none", path,
                      "--emit-c", lowered, "-o", exe]
        if case.get("no_werror"):
            build_args.insert(2, "--no-werror")
        build = run(build_args, BUILD_TIMEOUT)
        steps.append(build)
        ok = build["rc"] == 0 and not build["timed_out"] and os.path.exists(exe)

        detail = "build_rc=%s expected_stdout=%r" % (build["rc"],
                                                     case["expect"])
        if ok:
            result = run([exe], RUN_TIMEOUT)
            steps.append(result)
            ok = (result["rc"] == 0 and not result["timed_out"] and
                  result["stdout"] == case["expect"])
            detail += " stdout=%r" % result["stdout"]

        if ok and (case.get("text") or case.get("absent")):
            with open(lowered, "r", errors="replace") as f:
                text = f.read()
            missing = [s for s in case.get("text", []) if s not in text]
            present = [s for s in case.get("absent", []) if s in text]
            ok = not missing and not present
            detail += " text_missing=%r text_unexpected=%r" % (missing, present)

        rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                     "detail": detail, "steps": steps})
        if not ok:
            fails.append(name)
        print("%-32s %s" % (name, rows[-1]["status"]))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("RaiiLowering issues #228/#229/#230 regression suite\n%s\n"
                "c+ %s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-32s %s  %s\n" % (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:400],
                         s["stderr"][:600]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("raii_issues_228_230: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
