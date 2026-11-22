#!/usr/bin/env python3
"""Function-pointer array bounds: compile-time for constants, runtime for input."""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
SRC = ROOT / "tests/safety/fnptr_bounds"
OUT = ROOT / "tests/out/core/fnptr_bounds"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
if os.environ.get("CPLUS_CLANG"):
    ENV["CPLUS_CLANG"] = os.environ["CPLUS_CLANG"]

results = []
for name, index in [("const_oob_5556", 5556), ("const_oob_3", 3)]:
    p = SRC / f"{name}.c"
    r = subprocess.run([CC, "check", "--strict", str(p)], capture_output=True,
                       text=True, env=ENV, timeout=120)
    diag = r.stderr + r.stdout
    expected = f"[CPLUS-SAFE-BOUNDS] constant array index {index} is outside the bounds of 'dispatch'"
    ok = r.returncode != 0 and expected in diag and \
         "valid indices are 0..2" in diag
    results.append((name, "compile", ok, expected,
                    next((l for l in diag.splitlines() if "error:" in l), "")[:120]))

exe = OUT / "dynamic_oob.bin"
b = subprocess.run([CC, "build", str(SRC / "dynamic_oob.c"), "-o", str(exe)],
                   capture_output=True, text=True, env=ENV, timeout=180)
if b.returncode != 0:
    results.append(("dynamic_oob", "runtime", False, "C+ error: index",
                    "build failed"))
else:
    r = subprocess.run([str(exe)], input="99999\n", capture_output=True,
                       text=True, env=ENV, timeout=30)
    diag = r.stderr + r.stdout
    expected = "C+ error: index 99999 is outside the bounds of 'dispatch' (length 3)"
    ok = r.returncode != 0 and expected in diag
    results.append(("dynamic_oob", "runtime", ok, expected,
                    next((l for l in diag.splitlines() if "C+ error" in l), "")[:120]))

exe = OUT / "safe_indices.bin"
b = subprocess.run([CC, "build", str(SRC / "safe_indices.c"), "-o", str(exe)],
                   capture_output=True, text=True, env=ENV, timeout=180)
if b.returncode != 0:
    results.append(("safe_indices", "safe", False, "exit 0", "build failed"))
else:
    r = subprocess.run([str(exe)], capture_output=True, text=True, env=ENV,
                       timeout=30)
    results.append(("safe_indices", "safe",
                    r.returncode == 0 and "C+ error" not in (r.stderr + r.stdout),
                    "exit 0", f"exit {r.returncode}"))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"fnptr bounds suite: {passed}/{len(results)} passed"]
for name, kind, ok, expected, got in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {kind:7} {name:18} {got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines[1:] if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
