#!/usr/bin/env python3
"""RAII cleanup / control-flow regression suite.

Negative cases must be rejected by `c+ check --strict`; positive cases must
build, run and produce exact destructor-counter stdout. Fixtures use Clang-declaration
identity through the resource-owner lowering; no case is skipped and no
failure is suppressed. Hard timeouts throughout.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_cleanup/run_suite.py
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

PRE = ('#include "cplus/cplus.h"\n#include <stdio.h>\n'
       'resource struct R { int id; };\n'
       'void R_drop(R *r){ if(r->id){ printf("D%d ",r->id); r->id=0; } }\n')

NEG = [
    ("neg_multi_declarator",
     PRE + "int main(void){ R a={1}, b={2}; printf(\"BODY\\n\"); return 0; }\n",
     "declare one owning resource per statement"),
    ("neg_goto_out_of_scope",
     PRE + "int main(void){ { R x={1}; goto L; } L: printf(\"OUT\\n\"); return 0; }\n",
     "goto' bypasses C+ cleanup"),
    ("neg_goto_into_scope",
     PRE + "int main(void){ int c=1; if(c) goto L; { R x={1}; L: printf(\"IN\\n\"); } return 0; }\n",
     "goto' bypasses C+ cleanup"),
    ("neg_setjmp_assignment",
     PRE + "#include <setjmp.h>\nstatic jmp_buf jb;\n"
     "int main(void){ int j=setjmp(jb); if(j==0){ R x={1}; longjmp(jb,1); } "
     "printf(\"L\\n\"); return 0; }\n",
     "setjmp' bypasses C+ deterministic cleanup"),
    ("neg_longjmp_from_callback",
     PRE + "#include <setjmp.h>\nstatic jmp_buf jb;\n"
     "static void escape(void){ longjmp(jb,1); }\n"
     "int main(void){ if(setjmp(jb)==0){ R x={1}; escape(); } "
     "printf(\"A\\n\"); return 0; }\n",
     "longjmp' bypasses C+ deterministic cleanup"),
]

POS = [
    ("pos_shadow_order",
     PRE + "int main(void){ R x={1}; { R x={2}; } printf(\"END\\n\"); return 0; }\n",
     "D2 END\nD1 "),
    ("pos_loop_cleanup",
     PRE + "int main(void){ for(int i=1;i<=3;i++){ R x={i}; } "
     "printf(\"END\\n\"); return 0; }\n", "D1 D2 D3 END\n"),
    ("pos_switch_break",
     PRE + "int main(void){ int c=1; switch(c){ case 1: { R x={1}; break; } "
     "case 2: { R y={2}; break; } } printf(\"END\\n\"); return 0; }\n",
     "D1 END\n"),
    ("pos_return_order",
     PRE + "int main(void){ R a={1}; { R b={2}; return 0; } }\n", "D2 D1 "),
    ("pos_move_transfer",
     PRE + "int main(void){ R a={1}; R b=cplus_move(a); printf(\"END\\n\"); "
     "return 0; }\n", "END\nD1 "),
    ("pos_continue_cleanup",
     PRE + "int main(void){ for(int i=1;i<=2;i++){ R x={i}; if(i==1) continue; "
     "printf(\"B%d \",i); } return 0; }\n", "D1 B2 D2 "),
    ("pos_explicit_helper_drop",
     PRE + "static void drop2(R *a,R *b){ R_drop(a); R_drop(b); }\n"
     "int main(void){ R x={1}; R y={2}; drop2(&x,&y); "
     "printf(\"END\\n\"); return 0; }\n", "D1 D2 END\n"),
    ("pos_separate_declarations",
     PRE + "int main(void){ R a={1}; R b={2}; printf(\"END\\n\"); return 0; }\n",
     "END\nD2 D1 "),
]

# Control-flow fuzz: varying loop bound, branch shape and nesting depth.
FUZZ = []
for n in (1, 2, 4, 8, 16):
    # One owner per statement: multiple declarators are intentionally rejected
    # by STRICT and are covered by neg_multi_declarator.
    body = " ".join("R x%d={%d};" % (i + 1, i + 1) for i in range(n))
    expected = " ".join("D%d" % (i + 1) for i in reversed(range(n))) + " END\n"
    FUZZ.append(("fuzz_chain_%02d" % n,
                 PRE + "int main(void){ { %s } printf(\"END\\n\"); return 0; }\n" % body,
                 expected))
for n in (1, 3, 7):
    FUZZ.append(("fuzz_branch_%02d" % n,
                 PRE + "int main(void){ int c=1; for(int i=1;i<=%d;i++){ "
                 "R x={i}; if(c) { } else { } } printf(\"END\\n\"); return 0; }\n" % n,
                 " ".join("D%d" % i for i in range(1, n + 1)) + " END\n"))


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
        path = os.path.join(CASE_DIR, name + ".cp")
        with open(path, "w") as f:
            f.write(src)
        step = run([BIN, "check", "--strict", "--sanitize=none", path])
        ok = step["rc"] not in (0, None) and diag in step["stderr"]
        rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                     "expected": diag, "steps": [step]})
        if not ok:
            fails.append(name)
        print("%-30s %s" % (name, "PASS" if ok else "FAIL"))
    for name, src, expected in POS + FUZZ:
        path = os.path.join(CASE_DIR, name + ".cp")
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

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("RAII cleanup regression suite\n%s\n%s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-30s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("raii_cleanup: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
