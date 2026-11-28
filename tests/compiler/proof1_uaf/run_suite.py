#!/usr/bin/env python3
"""Pointer-to-pointer release-effect regression suite (Proof 1).

Negative cases must be rejected by STRICT check and build; positive controls
must build and run with exact stdout. Exact commands, exit codes, stdout and
stderr are recorded. No skips, no `|| true`, hard timeouts.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/proof1_uaf/run_suite.py
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

PRE = "#include <stdio.h>\n#include <stdlib.h>\n"

NEG = [
    ("neg_double_ptr_free",
     PRE + "static void sneak_free(int **pp){ free(*pp); }\n"
     "int main(void){ int *p=(int*)malloc(sizeof(int)); if(!p) return 1; "
     "*p=100; sneak_free(&p); *p=999; "
     "printf(\"[PROOF 1] %d\\n\",*p); return 0; }\n", "use-after-free"),
    ("neg_double_ptr_index_free",
     PRE + "static void sneak_free(int **pp){ free(pp[0]); }\n"
     "int main(void){ int *p=(int*)malloc(sizeof(int)); if(!p) return 1; "
     "sneak_free(&p); free(p); return 0; }\n", "double free"),
]

POS = [
    ("pos_read_through_indirection",
     PRE + "static int read_through(int **pp){ return **pp; }\n"
     "int main(void){ int *p=(int*)malloc(sizeof(int)); if(!p) return 1; "
     "*p=42; printf(\"%d\\n\",read_through(&p)); free(p); return 0; }\n", "42\n"),
    ("pos_write_through_indirection",
     PRE + "static void bump(int **pp){ **pp = **pp + 1; }\n"
     "int main(void){ int *p=(int*)malloc(sizeof(int)); if(!p) return 1; "
     "*p=41; bump(&p); printf(\"%d\\n\",*p); free(p); return 0; }\n", "42\n"),
]


def run(argv, timeout=60):
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
        print("%-28s %s" % (name, "PASS" if ok else "FAIL"))
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
        print("%-28s %s" % (name, "PASS" if ok else "FAIL"))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Pointer-to-pointer UAF suite\n%s\n%s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-28s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:400]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("proof1_uaf: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
