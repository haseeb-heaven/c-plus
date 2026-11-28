#!/usr/bin/env python3
"""Regression suite for GitHub issue #172 (qualified pointer borrows).

`RaiiLowering::tryBorrowDeclaration()` used to accept only the rigid token
shape `T *p = &owner;`.  Qualified/multi-token spellings such as
`const T *p`, `struct T *p` and `T * const p` were silently ignored, so a
borrow of a live resource could be used after `cplus_move(owner)` without any
diagnostic.

Every negative case MUST be rejected with the asserted use-after-move
diagnostic; every positive case MUST build, run and produce the asserted
stdout.  Commands, exit codes and outputs are recorded verbatim.  No case is
skipped and no failure is suppressed.  Hard timeouts throughout.

Run:  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_issue_172/run_suite.py
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
CHECK_TIMEOUT = 60

PRELUDE = """#include "cplus/cplus.h"
#include <stdio.h>
resource struct Res { int id; };
void Res_drop(Res *r){ if(r->id){ printf("D%d\\n", r->id); r->id=0; } }
"""

NEG_OWNER = "borrow 'p' of 'owner' is used after that resource was moved"
NEG_MANAGED = "borrow 'p' of 'm' is used after that resource was moved"

# The use of `p` after cplus_move(owner) is the observable bypass: an
# unqualified `Res *p = &owner;` is already diagnosed, so the only variable
# between cases is the spelling of the borrow declaration.
NEG_BODY = """int main(void){{
  Res owner = {{7}};
  {decl}
  Res moved = cplus_move(owner);
  (void)moved;
  printf("%d\\n", p->id);
  return 0;
}}
"""

POS_BODY = """int main(void){{
  Res owner = {{7}};
  {decl}
  printf("P%d\\n", p->id);
  return 0;
}}
"""

MANAGED_NEG = """#include "cplus/cplus.h"
#include <stdio.h>
int main(void){
  managed int *m = cplus_new(int);
  *m = 9;
  const int *p = m;
  managed int *n = cplus_move(m);
  (void)n;
  printf("%d\\n", *p);
  return 0;
}
"""

# A qualified non-pointer declaration must NOT enter the borrow map: `copy`
# is a value, not an alias, and may be read after `owner` is moved.
POS_NON_POINTER = PRELUDE + """int main(void){
  Res owner = {7};
  const Res copy = owner;
  Res moved = cplus_move(owner);
  (void)moved;
  printf("%d\\n", copy.id);
  return 0;
}
"""


def neg(decl):
    return PRELUDE + NEG_BODY.format(decl=decl)


def pos(decl):
    return PRELUDE + POS_BODY.format(decl=decl)


CASES = [
    # ---- negatives: each spelling must now be tracked as a borrow ----------
    dict(name="neg_const_qualified", expect="reject", diag=NEG_OWNER,
         source=neg("const Res *p = &owner;")),
    dict(name="neg_struct_specifier", expect="reject", diag=NEG_OWNER,
         source=neg("struct Res *p = &owner;")),
    dict(name="neg_pointer_const", expect="reject", diag=NEG_OWNER,
         source=neg("Res * const p = &owner;")),
    dict(name="neg_volatile_qualified", expect="reject", diag=NEG_OWNER,
         source=neg("volatile Res *p = &owner;")),
    dict(name="neg_const_struct", expect="reject", diag=NEG_OWNER,
         source=neg("const struct Res *p = &owner;")),
    dict(name="neg_trailing_qualifier", expect="reject", diag=NEG_OWNER,
         source=neg("Res const *p = &owner;")),
    dict(name="neg_pointer_volatile", expect="reject", diag=NEG_OWNER,
         source=neg("Res * volatile p = &owner;")),
    dict(name="neg_managed_const_borrow", expect="reject", diag=NEG_MANAGED,
         source=MANAGED_NEG),

    # ---- positives: valid qualified borrows must still compile and run -----
    dict(name="pos_unqualified_control", expect="accept", out="P7\nD7\n",
         source=pos("Res *p = &owner;")),
    dict(name="pos_const_qualified", expect="accept", out="P7\nD7\n",
         source=pos("const Res *p = &owner;")),
    dict(name="pos_struct_specifier", expect="accept", out="P7\nD7\n",
         source=pos("struct Res *p = &owner;")),
    dict(name="pos_pointer_const", expect="accept", out="P7\nD7\n",
         source=pos("Res * const p = &owner;")),
    dict(name="pos_volatile_qualified", expect="accept", out="P7\nD7\n",
         source=pos("volatile Res *p = &owner;")),
    dict(name="pos_const_struct", expect="accept", out="P7\nD7\n",
         source=pos("const struct Res *p = &owner;")),
    dict(name="pos_non_pointer_const_copy", expect="accept", out="7\nD7\n",
         source=POS_NON_POINTER),
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


def record(rows, fails, name, ok, detail, steps):
    row = {"case": name, "status": "PASS" if ok else "FAIL",
           "detail": detail, "steps": steps}
    rows.append(row)
    if not ok:
        fails.append(name)
    print("%-28s %s" % (name, row["status"]))
    return ok


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
        steps = []
        if case["expect"] == "reject":
            check = run([BIN, "check", "--strict", "--sanitize=none", path],
                        CHECK_TIMEOUT)
            steps.append(check)
            ok = (check["rc"] not in (0, None) and not check["timed_out"] and
                  case["diag"] in check["stderr"])
            build = run([BIN, "build", "--strict", "--sanitize=none", path,
                         "-o", exe], BUILD_TIMEOUT)
            steps.append(build)
            ok = ok and build["rc"] not in (0, None) and not build["timed_out"]
            detail = "check_rc=%s build_rc=%s diag=%s" % (
                check["rc"], build["rc"], case["diag"] in check["stderr"])
        else:
            build = run([BIN, "build", "--strict", "--sanitize=none", path,
                         "-o", exe], BUILD_TIMEOUT)
            steps.append(build)
            ok = build["rc"] == 0 and not build["timed_out"]
            if ok:
                result = run([exe], RUN_TIMEOUT)
                steps.append(result)
                ok = (result["rc"] == 0 and not result["timed_out"] and
                      result["stdout"] == case["out"])
            detail = "build_rc=%s expected_stdout=%r" % (build["rc"],
                                                         case["out"])
        record(rows, fails, name, ok, detail, steps)

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("RAII qualified-borrow regression suite (issue #172)\n%s\n"
                "c+ %s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-28s %s  %s\n" % (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:300],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("raii_issue_172: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
