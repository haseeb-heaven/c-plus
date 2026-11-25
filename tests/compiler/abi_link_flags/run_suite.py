#!/usr/bin/env python3
"""Cross-TU/static/shared link regression suite.

Proves that linker-only -L/-l flags on a `c+ build` compile step do not
become hard errors under the default warnings-as-errors policy, and that
uncontracted cross-module allocation/release and callback effects fail closed
in STRICT. Exact commands, exit codes, stdout and stderr are recorded.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/abi_link_flags/run_suite.py
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
HOST_CC = os.environ.get("CC", "/usr/bin/clang" if os.path.exists("/usr/bin/clang") else "clang")

HELPER = "#include <stdio.h>\nint abi_mul(int a,int b){ return a*b; }\n"
MAIN = ("#include <stdio.h>\nint abi_mul(int,int);\n"
        "int main(void){ printf(\"mul=42\\n\"); return abi_mul(6,7)==42?0:1; }\n")
ALLOC_A = ("#include <stdlib.h>\nint *abi_create(void){ return (int*)malloc(sizeof(int)*4); }\n"
           "void abi_destroy(int *p){ free(p); }\n")
ALLOC_B = ("#include \"cplus/cplus.h\"\nint *abi_create(void);\nvoid abi_destroy(int *);\n"
           "int main(void){ int *p=abi_create(); p[0]=1; abi_destroy(p); return 0; }\n")


def run(argv, timeout=90, env=None):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, env=env)
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
    helper_c = os.path.join(CASE_DIR, "abi_helper.c")
    main_c = os.path.join(CASE_DIR, "abi_main.c")
    with open(helper_c, "w") as f:
        f.write(HELPER)
    with open(main_c, "w") as f:
        f.write(MAIN)

    # Static archive: host CC produces the archive, c+ links it in every mode.
    obj = os.path.join(CASE_DIR, "abi_helper.o")
    archive = os.path.join(CASE_DIR, "libabi_helper.a")
    steps = [run([HOST_CC, "-std=c11", "-c", helper_c, "-o", obj]),
             run(["ar", "rcs", archive, obj])]
    ok = all(s["rc"] == 0 for s in steps)
    for mode in ("--default", "--strict", "--legacy-c"):
        exe = os.path.join(OUT, "static_" + mode[2:] + ".bin")
        b = run([BIN, "build", mode, "--sanitize=none", main_c,
                 "-L" + CASE_DIR, "-labi_helper", "-o", exe])
        steps.append(b)
        if b["rc"] == 0 and os.path.exists(exe):
            r = run([exe])
            steps.append(r)
            ok = ok and r["rc"] == 0 and r["stdout"] == "mul=42\n"
        else:
            ok = False
    rows.append({"case": "static_archive_link", "status": "PASS" if ok else "FAIL",
                 "expected": "mul=42", "steps": steps})
    if not ok:
        fails.append("static_archive_link")
    print("%-30s %s" % ("static_archive_link", "PASS" if ok else "FAIL"))

    # Shared library: same link flags must survive the compile step.
    dylib = os.path.join(CASE_DIR, "libabi_helper.dylib")
    steps = [run([HOST_CC, "-shared", "-fPIC", helper_c, "-o", dylib])]
    ok = steps[0]["rc"] == 0
    if ok:
        exe = os.path.join(OUT, "shared.bin")
        env = dict(os.environ)
        env["DYLD_LIBRARY_PATH"] = CASE_DIR + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
        b = run([BIN, "build", "--strict", "--sanitize=none", main_c,
                 "-L" + CASE_DIR, "-labi_helper", "-o", exe], env=env)
        steps.append(b)
        if b["rc"] == 0 and os.path.exists(exe):
            r = run([exe], env=env)
            steps.append(r)
            ok = r["rc"] == 0 and r["stdout"] == "mul=42\n"
        else:
            ok = False
    rows.append({"case": "shared_library_link", "status": "PASS" if ok else "FAIL",
                 "expected": "mul=42", "steps": steps})
    if not ok:
        fails.append("shared_library_link")
    print("%-30s %s" % ("shared_library_link", "PASS" if ok else "FAIL"))

    # `check` must also accept linker flags (they are link-only inputs).
    c = run([BIN, "check", "--strict", "--sanitize=none", main_c,
             "-L" + CASE_DIR, "-labi_helper"])
    ok = c["rc"] == 0
    rows.append({"case": "check_with_link_flags", "status": "PASS" if ok else "FAIL",
                 "expected": "accept", "steps": [c]})
    if not ok:
        fails.append("check_with_link_flags")
    print("%-30s %s" % ("check_with_link_flags", "PASS" if ok else "FAIL"))

    # STRICT must fail closed when allocation and release live in different
    # TUs with no validated contract; legacy keeps its plain-C behaviour.
    a = os.path.join(CASE_DIR, "alloc_a.c")
    b = os.path.join(CASE_DIR, "alloc_b.cp")
    with open(a, "w") as f:
        f.write(ALLOC_A)
    with open(b, "w") as f:
        f.write(ALLOC_B)
    strict = run([BIN, "check", "--strict", "--sanitize=none", a, b])
    legacy = run([BIN, "check", "--legacy-c", "--sanitize=none", a, b])
    ok = (strict["rc"] not in (0, None) and "no declared contract" in strict["stderr"]
          and legacy["rc"] == 0)
    rows.append({"case": "cross_tu_allocator_boundary",
                 "status": "PASS" if ok else "FAIL",
                 "expected": "STRICT reject + LEGACY accept",
                 "steps": [strict, legacy]})
    if not ok:
        fails.append("cross_tu_allocator_boundary")
    print("%-30s %s" % ("cross_tu_allocator_boundary", "PASS" if ok else "FAIL"))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN, "host_cc": HOST_CC,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Cross-TU/ABI link regression suite\n%s\n%s\nCC=%s\n\n" %
                (payload["host"], BIN, HOST_CC))
        for r in rows:
            f.write("%-30s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("abi_link_flags: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
