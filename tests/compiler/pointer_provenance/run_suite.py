#!/usr/bin/env python3
"""Pointer-provenance adversarial suite (allocation identity, issue: audit).

Negatives must be rejected by both `check` and `build` in STRICT; positives
must build and run with exact stdout. Exact commands, exit codes, stdout and
stderr are recorded. Hard timeouts; no `|| true`, skips or suppressed failures.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/pointer_provenance/run_suite.py
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
CASE_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASE_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

PRELUDE = '#include <stdio.h>\n#include "cplus/cplus.h"\n'
STD = "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n"

NEG = [
    ("neg_struct_field", PRELUDE + "struct S{int *p;};\n"
     "int main(void){ struct S s; s.p=cplus_new_array(int,4); s.p[5]=1; "
     "printf(\"%d\\n\",s.p[5]); cplus_release(s.p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_nested_struct_field", PRELUDE + "struct I{int *p;}; struct O{struct I i;};\n"
     "int main(void){ struct O o; int *p=cplus_new_array(int,4); o.i.p=p; "
     "o.i.p[5]=1; printf(\"%d\\n\",o.i.p[5]); cplus_release(p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_field_plus_arith", PRELUDE + "struct S{int *p;};\n"
     "int main(void){ struct S s; s.p=cplus_new_array(int,4); int *q=s.p+2; "
     "q[3]=1; printf(\"%d\\n\",q[3]); cplus_release(s.p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_union_integer_pun", STD + "union U{int *p; uintptr_t u;};\n"
     "int main(void){ union U x; x.u=(uintptr_t)malloc(16); x.p[5]=1; "
     "printf(\"%d\\n\",x.p[5]); free(x.p); return 0; }\n",
     "provenance lost"),
    ("neg_union_integer_var_pun", STD + "union U{int *p; uintptr_t u;};\n"
     "int main(void){ int *p=malloc(16); union U x; x.u=(uintptr_t)p; x.p[5]=1; "
     "printf(\"%d\\n\",x.p[5]); free(p); return 0; }\n",
     "provenance lost"),
    ("neg_conditional_alias", PRELUDE +
     "int main(void){ int *p=cplus_new_array(int,4); int f=0; int *q=f?p:p; "
     "q[5]=1; printf(\"%d\\n\",q[5]); cplus_release(p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_conditional_cast_alias", PRELUDE +
     "int main(void){ int *p=cplus_new_array(int,4); int f=1; "
     "int *q=f?(int*)(void*)p:(int*)(void*)p; q[5]=1; printf(\"%d\\n\",q[5]); "
     "cplus_release(p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_integer_roundtrip", STD +
     "int main(void){ int *p=malloc(16); uintptr_t u=(uintptr_t)p; int *q=(int*)u; "
     "q[5]=1; printf(\"%d\\n\",q[5]); free(p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_helper_cast_return", PRELUDE +
     "static int *identity(int *p){ return (int*)(void*)p; }\n"
     "int main(void){ int *p=cplus_new_array(int,4); int *q=identity(p); "
     "q[5]=1; printf(\"%d\\n\",q[5]); cplus_release(p); return 0; }\n",
     "exceeds heap allocation"),
]

POS = [
    ("pos_struct_field_in_bounds", PRELUDE + "struct S{int *p;};\n"
     "int main(void){ struct S s; s.p=cplus_new_array(int,4); s.p[3]=7; "
     "printf(\"%d\\n\",s.p[3]); cplus_release(s.p); return 0; }\n", "7\n"),
    ("pos_conditional_in_bounds", PRELUDE +
     "int main(void){ int *p=cplus_new_array(int,4); int f=0; int *q=f?p:p; "
     "q[3]=7; printf(\"%d\\n\",q[3]); cplus_release(p); return 0; }\n", "7\n"),
    ("pos_integer_roundtrip_in_bounds", STD +
     "int main(void){ int *p=malloc(16); uintptr_t u=(uintptr_t)p; int *q=(int*)u; "
     "q[3]=7; printf(\"%d\\n\",q[3]); free(p); return 0; }\n", "7\n"),
    ("pos_helper_in_bounds", PRELUDE +
     "static int *identity(int *p){ return (int*)(void*)p; }\n"
     "int main(void){ int *p=cplus_new_array(int,4); int *q=identity(p); "
     "q[3]=7; printf(\"%d\\n\",q[3]); cplus_release(p); return 0; }\n", "7\n"),
    ("pos_malloc_cast_in_bounds", STD +
     "int main(void){ int *p=(int*)malloc(16); p[3]=7; printf(\"%d\\n\",p[3]); "
     "free(p); return 0; }\n", "7\n"),
]


def run(argv, timeout=90):
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
    for name, src, diag in NEG:
        path = os.path.join(CASE_DIR, name + ".c")
        with open(path, "w") as f:
            f.write(src)
        exe = os.path.join(OUT, name + ".bin")
        if os.path.exists(exe):
            os.unlink(exe)
        c = run([BIN, "check", "--strict", "--sanitize=none", path])
        b = run([BIN, "build", "--strict", "--sanitize=none", path, "-o", exe])
        ok = (c["rc"] not in (0, None) and diag in c["stderr"] and
              b["rc"] not in (0, None) and not os.path.exists(exe))
        rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                     "expected": diag, "steps": [c, b]})
        if not ok:
            fails.append(name)
        print("%-32s %s" % (name, "PASS" if ok else "FAIL"))
    for name, src, expected in POS:
        path = os.path.join(CASE_DIR, name + ".c")
        with open(path, "w") as f:
            f.write(src)
        exe = os.path.join(OUT, name + ".bin")
        b = run([BIN, "build", "--strict", "--sanitize=none", path, "-o", exe])
        steps = [b]
        ok = b["rc"] == 0 and os.path.exists(exe)
        if ok:
            r = run([exe])
            steps.append(r)
            ok = r["rc"] == 0 and r["stdout"] == expected
        rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                     "expected": expected, "steps": steps})
        if not ok:
            fails.append(name)
        print("%-32s %s" % (name, "PASS" if ok else "FAIL"))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Pointer-provenance adversarial suite\n%s\n%s\n\n" %
                (payload["host"], BIN))
        for r in rows:
            f.write("%-32s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("pointer_provenance: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
