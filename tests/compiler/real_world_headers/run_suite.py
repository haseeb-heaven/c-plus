#!/usr/bin/env python3
"""Real-world header passthrough regressions for the C+ rewriter.

Each case is compiled in legacy-C mode, where the rewriter must copy the user
translation unit byte for byte after one leading #line directive. Heavy
system headers, vendored third-party headers, nested macros, __attribute__ and
`#if 0` prose with unbalanced braces must not trigger syntax or bounds errors.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_world_headers/run_suite.py
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
TIMEOUT = 180

PRELUDE = '#include <stdio.h>\n'

CASES = [
    ("system_socket", PRELUDE + '#include <sys/socket.h>\n'
     'int main(void){ printf("ok\\n"); return 0; }\n', [], "ok\n"),
    ("macro_attribute", PRELUDE + '#define SQ(x) ((x)*(x))\n'
     '#if 0\n  don\'t parse this unbalanced { brace\n#endif\n'
     'static int f(void) __attribute__((unused));\n'
     'static int f(void){ return SQ(2); }\n'
     'int main(void){ printf("R=%d\\n", f()); return 0; }\n', [], "R=4\n"),
    ("vendored_stb", PRELUDE + '#include "stb_image.h"\n'
     'int main(void){ printf("ok\\n"); return 0; }\n',
     ["-I" + os.path.join(ROOT, "third_party", "stb")], "ok\n"),
    ("vendored_yyjson", PRELUDE + '#include "yyjson.h"\n'
     'int main(void){ printf("ok\\n"); return 0; }\n',
     ["-I" + os.path.join(ROOT, "third_party", "yyjson")], "ok\n"),
    ("vendored_miniaudio", PRELUDE + '#include "miniaudio.h"\n'
     'int main(void){ printf("ok\\n"); return 0; }\n',
     ["-I" + os.path.join(ROOT, "third_party", "miniaudio")], "ok\n"),
]


def run(argv):
    try:
        p = subprocess.run(argv, env=ENV, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=TIMEOUT)
        return p.returncode, p.stdout.decode(errors="replace")
    except subprocess.TimeoutExpired as e:
        return None, (e.stdout or b"").decode(errors="replace")


def main():
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2
    failures = []
    with tempfile.TemporaryDirectory() as d:
        for name, source, includes, expect in CASES:
            src = os.path.join(d, name + ".c")
            lowered = os.path.join(d, name + ".lowered.c")
            exe = os.path.join(d, name + ".bin")
            with open(src, "w") as f:
                f.write(source)
            rc, out = run([BIN, "build", "--legacy-c", "--sanitize=none", src,
                           "--emit-c", lowered, "-o", exe] + includes)
            ok = rc == 0 and os.path.exists(lowered)
            detail = "build_rc=%s %s" % (rc, out[:400])
            if ok:
                with open(lowered) as f:
                    emitted = f.read()
                first, sep, rest = emitted.partition("\n")
                ok = (sep == "\n" and first.startswith("#line 1 \"") and
                      first.endswith('"') and rest == source)
                detail += " passthrough=%s" % ok
            if ok:
                rc, out = run([exe])
                ok = rc == 0 and out == expect
                detail += " run_rc=%s stdout=%r" % (rc, out)
            print("%-22s %s" % (name, "PASS" if ok else "FAIL"))
            if not ok:
                failures.append((name, detail))
    if failures:
        print("real_world_headers: %d case(s) failed" % len(failures))
        for name, detail in failures:
            print("  %s: %s" % (name, detail))
        return 1
    print("real_world_headers: %d cases, %d passed, 0 failed" %
          (len(CASES), len(CASES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
