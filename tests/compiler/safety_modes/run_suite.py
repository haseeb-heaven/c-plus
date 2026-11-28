#!/usr/bin/env python3
"""--safety=fast / --safety=strict regression.

Fast mode keeps compile-time checks, data-stack lowering and UBSan but must
emit no pointer-to-extent side-table calls. Strict mode must keep them. Both
modes must build and run the same program with identical observable output.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/safety_modes/run_suite.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
ENV = dict(os.environ)
ENV.pop("SDKROOT", None)
ENV.pop("CPLUS_CLANG", None)
SOURCE = ('#include <stdio.h>\n#include <stdlib.h>\n'
          'int main(void){ int *p=malloc(4*sizeof(int)); p[0]=3; '
          'printf("V=%d\\n",p[0]); free(p); return 0; }\n')
SIDE_TABLE = ("cplus_register_allocation", "cplus_free_registered",
              "cplus_check_bounds")


def run(argv, timeout=120):
    p = subprocess.run(argv, env=ENV, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout)
    return p.returncode, p.stdout.decode(errors="replace")


def main():
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2
    results = []
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "mode.c")
        with open(src, "w") as f:
            f.write(SOURCE)
        for mode, want_side_table in (("fast", False), ("strict", True)):
            lowered = os.path.join(d, mode + ".c")
            exe = os.path.join(d, mode + ".bin")
            rc, out = run([BIN, "build", "--safety=" + mode,
                           "--sanitize=none", src, "--emit-c", lowered,
                           "-o", exe])
            ok = rc == 0 and os.path.exists(lowered)
            detail = "build_rc=%s %s" % (rc, out[:300])
            if ok:
                with open(lowered) as f:
                    text = f.read()
                calls = sum(text.count(sym) for sym in SIDE_TABLE)
                ok = (calls == 0) if not want_side_table else (calls > 0)
                detail += " side_table_calls=%d" % calls
            if ok:
                rc, out = run([exe])
                ok = rc == 0 and out == "V=3\n"
                detail += " run_rc=%s stdout=%r" % (rc, out)
            print("%-8s %s" % (mode, "PASS" if ok else "FAIL"))
            if not ok:
                print("  " + detail)
            results.append(ok)
    if all(results):
        print("safety_modes: %d cases, %d passed, 0 failed" %
              (len(results), len(results)))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
