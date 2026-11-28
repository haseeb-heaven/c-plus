#!/usr/bin/env python3
"""Line-mapping regression for lowered C.

Every multi-line block injected by disjoint-bounds instrumentation must be
followed by a #line directive restoring the original file and line, so Clang
diagnostics, runtime traps and debuggers point at the user's source.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/line_mapping/run_suite.py
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

SOURCE = """#include <stdio.h>
int main(void){ int x=3; int *p=&x; printf("LINE=%d\\n",__LINE__); return *p-3; }
"""


def run(argv, timeout=120):
    try:
        p = subprocess.run(argv, env=ENV, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode(errors="replace")
    except subprocess.TimeoutExpired as e:
        return None, (e.stdout or b"").decode(errors="replace")


def main():
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "line.c")
        lowered = os.path.join(d, "line.lowered.c")
        exe = os.path.join(d, "line.bin")
        with open(src, "w") as f:
            f.write(SOURCE)

        detail = []
        rc, out = run([BIN, "build", "--default", "--sanitize=none", src,
                       "--emit-c", lowered, "-o", exe,
                       "--", "-fcplus-disjoint-bounds"])
        ok = rc == 0 and os.path.exists(lowered)
        detail.append("build_rc=%s %s" % (rc, out[:400]))

        if ok:
            with open(lowered) as f:
                text = f.read()
            lines = text.splitlines()
            # The baseline mapping must name the original source, not a
            # generated temporary.
            baseline = '#line 1 "%s"' % src
            start = lines.index(baseline) if baseline in lines else -1
            ok = start >= 0
            detail.append("baseline_index=%d" % start)

            # Injected statements in the user-source region must restore the
            # original path before the next user token. Prelude helpers are
            # intentionally mapped to the synthetic instrumentation filename.
            if start >= 0:
                for n, line in enumerate(lines[start:]):
                    if "cplus_shadow_" not in line or "extern" in line:
                        continue
                    window = "\n".join(lines[start + n + 1:start + n + 5])
                    restored = re.search(r'#line (\d+) "([^"]+)"', window)
                    if not restored or restored.group(2) != src:
                        ok = False
                        detail.append("no restore after line %d: %r" %
                                      (start + n + 1, line))

            rc, out = run([exe])
            ok = ok and rc == 0 and "LINE=2" in out
            detail.append("run_rc=%s stdout=%r" % (rc, out))

        if ok:
            print("line_mapping: 1 case, 1 passed, 0 failed")
            return 0
        print("line_mapping: FAIL")
        for line in detail:
            print("  " + line)
        return 1


if __name__ == "__main__":
    sys.exit(main())
