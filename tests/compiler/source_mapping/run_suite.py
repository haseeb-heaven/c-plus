#!/usr/bin/env python3
"""End-to-end source-mapping regression.

Verifies that a deliberate syntax error and a deliberate runtime out-of-bounds
access placed on line 42 of a source file are reported at that source line,
and that injected RAII cleanup is preceded by a #line directive.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/source_mapping/run_suite.py
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
ENV = dict(os.environ)
ENV.pop("SDKROOT", None)
ENV.pop("CPLUS_CLANG", None)


def run(argv, timeout=180):
    try:
        p = subprocess.run(argv, env=ENV, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode(errors="replace")
    except subprocess.TimeoutExpired as e:
        return None, (e.stdout or b"").decode(errors="replace")


def syntax_case(d):
    src = os.path.join(d, "syntax.cp")
    with open(src, "w") as f:
        for n in range(1, 42):
            f.write("int filler_%d;\n" % n)
        f.write("int broken = ;\n")
    rc, out = run([BIN, "build", "--default", "--sanitize=none", src,
                   "-o", os.path.join(d, "syntax.bin")])
    ok = rc != 0 and re.search(r"syntax\.cp:42:", out) is not None
    return ok, "rc=%s %s" % (rc, out[:400])


def ubsan_case(d):
    src = os.path.join(d, "oob.cp")
    with open(src, "w") as f:
        f.write("#include <stdio.h>\nstatic int f(int idx){\n"
                "  int a[4] = {1,2,3,4};\n")
        for _ in range(4, 42):
            f.write("  ;\n")
        f.write("  return a[idx];\n}\n"
                "int main(int argc, char **argv){ (void)argv; "
                "return f(argc + 8); }\n")
    exe = os.path.join(d, "oob.bin")
    rc, out = run([BIN, "build", "--legacy-c", "--sanitize=undefined",
                   "--no-werror", src, "-o", exe])
    if rc != 0 or not os.path.exists(exe):
        return False, "build_rc=%s %s" % (rc, out[:400])
    rc, out = run([exe])
    ok = (rc != 0 and "runtime error" in out and
          re.search(r"oob\.cp:42:", out) is not None)
    return ok, "run_rc=%s %s" % (rc, out[:400])


def injection_case(d):
    src = os.path.join(d, "raii.cp")
    lowered = os.path.join(d, "raii.c")
    with open(src, "w") as f:
        f.write('#include <stdio.h>\n'
                'resource struct R { int id; };\n'
                'void R_drop(R *r){ printf("D%d\\n", r->id); r->id = 0; }\n'
                'int main(void){ R a = {1}; { R b = {2}; } return a.id; }\n')
    rc, out = run([BIN, "build", "--default", "--sanitize=none", src,
                   "--emit-c", lowered, "-o", os.path.join(d, "raii.bin")])
    if rc != 0 or not os.path.exists(lowered):
        return False, "build_rc=%s %s" % (rc, out[:400])
    with open(lowered) as f:
        lines = f.read().splitlines()
    missing = []
    for n, line in enumerate(lines):
        if "R_drop(&" in line or "cplus_zero(&" in line:
            if n == 0 or not lines[n - 1].startswith("#line "):
                missing.append(n + 1)
    ok = not missing
    return ok, "unmapped_injections=%s" % missing


def main():
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2
    cases = [("syntax_line_42", syntax_case),
             ("ubsan_line_42", ubsan_case),
             ("injected_cleanup_mapping", injection_case)]
    fails = []
    with tempfile.TemporaryDirectory() as d:
        for name, fn in cases:
            sub = os.path.join(d, name)
            os.makedirs(sub)
            ok, detail = fn(sub)
            print("%-28s %s" % (name, "PASS" if ok else "FAIL"))
            if not ok:
                print("  " + detail)
                fails.append(name)
    if fails:
        print("source_mapping: %d case(s) failed" % len(fails))
        return 1
    print("source_mapping: %d cases, %d passed, 0 failed" %
          (len(cases), len(cases)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
