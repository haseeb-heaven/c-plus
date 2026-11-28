#!/usr/bin/env python3
"""Declaration-identity / token-heuristic audit regression suite.

Positive cases must build and run under STRICT with exact stdout; negative
cases must be rejected by both check and build with the asserted diagnostic.
Exact commands, exit codes, stdout and stderr are recorded. No skips, no
`|| true`, hard timeouts.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/ast_identity/run_suite.py
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

POS = [
    ("pos_shadowed_array_scope",
     "#include <stdio.h>\n"
     "int main(void){ int a[8]={0}; { int a[4]={0}; a[3]=1; } a[7]=2; "
     "printf(\"%d\\n\",a[7]); return 0; }\n", "2\n"),
]

NEG = [
    ("neg_shadowed_array_inner",
     "#include <stdio.h>\n"
     "int main(void){ int a[8]={0}; { int a[4]={0}; a[9]=1; } "
     "printf(\"%d\\n\",a[0]); return 0; }\n", "outside the bounds"),
    ("neg_typedef_array",
     "#include <stdio.h>\ntypedef int I;\n"
     "int main(void){ I a[4]={0}; a[9]=1; printf(\"%d\\n\",a[9]); "
     "return 0; }\n", "outside the bounds"),
    ("neg_macro_index",
     "#include <stdio.h>\n#define IDX 9\n"
     "int main(void){ int a[4]={0}; a[IDX]=1; printf(\"%d\\n\",a[IDX]); "
     "return 0; }\n", "outside the bounds"),
    ("neg_cast_alias",
     "#include <stdio.h>\n#include <stdlib.h>\n"
     "int main(void){ int *p=(int*)malloc(16); int *q=(int*)(void*)p; "
     "q[5]=1; printf(\"%d\\n\",q[5]); free(p); return 0; }\n",
     "exceeds heap allocation"),
    ("neg_function_pointer_call",
     "#include <stdio.h>\n"
     "static void wr(int *p,int n){ p[n]=1; printf(\"%d\\n\",p[n]); }\n"
     "int main(void){ int a[4]={0}; void (*fp)(int*,int)=wr; fp(a,9); "
     "return 0; }\n", "unverifiable extent"),
    ("neg_conjunctive_guard",
     "#include <stdio.h>\n"
     "static void w(int *p,int i,int ok){ if(i>=4 && ok) return; p[i]=1; "
     "printf(\"%d\\n\",p[i]); }\n"
     "int main(void){ int a[4]={0}; w(a,9,0); return 0; }\n",
     "unverifiable extent"),
    ("neg_realloc_struct_field",
     "#include <stdio.h>\n#include <stdlib.h>\nstruct S{int *p;};\n"
     "int main(void){ struct S s; s.p=malloc(16); s.p=realloc(s.p,64); "
     "s.p[20]=1; printf(\"%d\\n\",s.p[20]); free(s.p); return 0; }\n",
     "realloc"),
    ("neg_template_source",
     "#include <stdio.h>\n"
     "template<class T> void put(T *p,int i){ p[i]=1; }\n"
     "int main(void){ int a[4]={0}; put(a,9); printf(\"%d\\n\",a[9]); "
     "return 0; }\n", "out of bounds"),
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
        print("%-30s %s" % (name, "PASS" if ok else "FAIL"))
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
        print("%-30s %s" % (name, "PASS" if ok else "FAIL"))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("AST identity audit suite\n%s\n%s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-30s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("ast_identity: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
