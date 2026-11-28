#!/usr/bin/env python3
"""Borrow-escape regression suite (GitHub issue #26).

Every negative case MUST be rejected with the asserted diagnostic; every
positive case MUST build and run with the asserted stdout. Commands, exit
codes and outputs are recorded verbatim. Hard timeouts, no `|| true`, no skips.

Run:  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/borrow_escape/run_suite.py
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

PRELUDE = """#include <stdio.h>
#include "cplus/cplus.h"
resource struct File { FILE *h; };
void File_drop(File *f){ if(f->h){fclose(f->h); f->h=NULL;} }
"""
BAD = "borrow 'b' of local 'f' cannot escape the function"


def bad(body, extra=""):
    return PRELUDE + extra + "FILE *bad(void){ File f; f.h=fopen(\"/tmp/cplus_borrow_probe.txt\",\"w\"); FILE *b=f.h; " + body + " }\n" + \
        "int main(void){ (void)bad(); return 0; }\n"


CASES = [
    # ---- negatives: every supported return-expression form -----------------
    dict(name="neg_direct", expect="reject", diag=BAD,
         body="return b;"),
    dict(name="neg_cast_single", expect="reject", diag=BAD,
         body="return (FILE *)b;"),
    dict(name="neg_cast_void", expect="reject", diag=BAD,
         body="return (void *)b;"),
    dict(name="neg_cast_nested", expect="reject", diag=BAD,
         body="return (FILE *)(void *)(const FILE *)b;"),
    dict(name="neg_cast_cv", expect="reject", diag=BAD,
         body="return (FILE *)(volatile void *)(const volatile FILE *)b;"),
    dict(name="neg_parens", expect="reject", diag=BAD,
         body="return (b);"),
    dict(name="neg_typedef", expect="reject", diag=BAD,
         body="return (FP)b;", extra="typedef FILE *FP;\n"),
    dict(name="neg_conditional", expect="reject", diag=BAD,
         body="int flag=0; return flag ? b : b;"),
    dict(name="neg_nested_conditional", expect="reject", diag=BAD,
         body="int x=0,y=0; return x ? b : (y ? b : b);"),
    dict(name="neg_comma", expect="reject", diag=BAD,
         body="int z=0; return (z++, b);"),
    dict(name="neg_helper", expect="reject", diag=BAD,
         body="return ident(b);",
         extra="static FILE *ident(FILE *p){ return p; }\n"),
    dict(name="neg_helper_cast", expect="reject", diag=BAD,
         body="return (FILE *)(void *)ident((FILE *)b);",
         extra="static FILE *ident(FILE *p){ return p; }\n"),
    dict(name="neg_branch", expect="reject", diag=BAD,
         body="FILE *r=b; int c=1; if(c) r=(FILE *)(void *)b; return (FILE *)r;"),
    dict(name="neg_alias_chain", expect="reject", diag=BAD,
         body="FILE *q=b; FILE *r=(FILE *)(void *)q; return r;"),

    # ---- positives: valid returns must still compile and run ---------------
    dict(name="pos_owned_resource", expect="accept", out="",
         source=PRELUDE + "File make(void){ File f; f.h=fopen(\"/tmp/cplus_borrow_probe.txt\",\"w\"); return f; }\n"
                          "int main(void){ File x=make(); (void)x; return 0; }\n"),
    dict(name="pos_heap_pointer", expect="accept", out="",
         source="#include <stdio.h>\nFILE *make(void){ FILE *p=fopen(\"/tmp/cplus_borrow_probe.txt\",\"w\"); return p; }\n"
                "int main(void){ FILE *p=make(); (void)p; return 0; }\n"),
    dict(name="pos_parameter_cast", expect="accept", out="",
         source="#include <stdio.h>\nFILE *pass(FILE *p){ return (FILE *)(void *)p; }\n"
                "int main(void){ FILE *p=fopen(\"/tmp/cplus_borrow_probe.txt\",\"w\"); (void)pass(p); return 0; }\n"),
    dict(name="pos_global_pointer", expect="accept", out="",
         source="#include <stdio.h>\nstatic FILE *g;\nFILE *get(void){ return (FILE *)(void *)g; }\n"
                "int main(void){ (void)get(); return 0; }\n"),
    dict(name="pos_field_of_heap", expect="accept", out="",
         source=PRELUDE + "FILE *get(File *self){ return (FILE *)(void *)self->h; }\n"
                          "int main(void){ File f; f.h=fopen(\"/tmp/cplus_borrow_probe.txt\",\"w\"); (void)get(&f); File_drop(&f); return 0; }\n"),
    dict(name="pos_borrow_used_locally", expect="accept", out="ok\n",
         source=PRELUDE + "int use(void){ File f; f.h=fopen(\"/tmp/cplus_borrow_probe.txt\",\"w\"); FILE *b=f.h; int r=(b!=0); File_drop(&f); return r; }\n"
                          "int main(void){ printf(\"ok\\n\"); return use()?0:1; }\n"),

    # ---- compatibility and optimization variants ---------------------------
    dict(name="compat_legacy_cast", expect="accept", mode="--legacy-c", out="",
         source="#include <stdio.h>\nint *f(void){ int x=1; int *b=&x; return (int*)b; }\n"
                "int main(void){ (void)f(); return 0; }\n"),
    dict(name="neg_release_o2", expect="reject", diag=BAD, mode="--release",
         body="return (FILE *)(void *)b;"),
    dict(name="neg_debug", expect="reject", diag=BAD, mode="--debug",
         body="return (FILE *)(void *)b;"),
    dict(name="neg_ubsan", expect="reject", diag=BAD, sanitize="undefined",
         body="return (FILE *)(void *)b;"),

    # ---- adjacent frame-escape path ----------------------------------------
    dict(name="neg_address_of_local", expect="reject", diag="return-stack-address",
         source="#include <stdio.h>\nint *bad(void){ int local=1; return &local; }\n"
                "int main(void){ (void)bad(); return 0; }\n"),
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
    print("%-26s %s" % (name, row["status"]))
    return ok


def flags(case):
    mode = case.get("mode", "--default")
    san = case.get("sanitize", "none")
    return [mode, "--sanitize=" + san]


def main():
    rows, fails = [], []
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2
    for case in CASES:
        name = case["name"]
        src = case.get("source") or bad(case["body"], case.get("extra", ""))
        path = os.path.join(CASES_DIR, name + ".cp")
        with open(path, "w") as f:
            f.write(src)
        steps = []
        base = [BIN, "build", *flags(case)]
        if case["expect"] == "reject":
            r = run([BIN, "check", *flags(case), path], CHECK_TIMEOUT)
            steps.append(r)
            ok = (r["rc"] not in (0, None) and not r["timed_out"] and
                  case["diag"] in r["stderr"])
            b = run([*base, path, "-o", os.path.join(OUT, name + ".bin")],
                    BUILD_TIMEOUT)
            steps.append(b)
            ok = ok and b["rc"] not in (0, None) and not b["timed_out"]
            detail = "check_rc=%s build_rc=%s diag=%s" % (
                r["rc"], b["rc"], case["diag"] in r["stderr"])
        else:
            b = run([*base, path, "-o", os.path.join(OUT, name + ".bin")],
                    BUILD_TIMEOUT)
            steps.append(b)
            ok = b["rc"] == 0 and not b["timed_out"]
            if ok and case.get("out") is not None:
                exe = os.path.join(OUT, name + ".bin")
                r = run([exe], RUN_TIMEOUT)
                steps.append(r)
                ok = (r["rc"] == 0 and not r["timed_out"] and
                      r["stdout"] == case["out"])
            detail = "build_rc=%s expected_stdout=%r" % (b["rc"], case.get("out"))
        record(rows, fails, name, ok, detail, steps)

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Borrow-escape regression suite (issue #26)\n%s\nc+ %s\n\n" %
                (payload["host"], BIN))
        for r in rows:
            f.write("%-26s %s  %s\n" % (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:300],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("borrow_escape: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
