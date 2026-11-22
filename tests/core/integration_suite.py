#!/usr/bin/env python3
"""End-to-end integration: compiler + runtime + contracts + sanitizer + multi-TU."""
import glob
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
SRC = ROOT / "tests/safety/integration"
OUT = ROOT / "tests/out/core/integration"
OUT.mkdir(parents=True, exist_ok=True)
def sdk_path():
    try:
        r = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True,
                           text=True, timeout=20)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    except FileNotFoundError:
        pass
    c = glob.glob("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
    return c[0] if c else ""


def asan_works(clang, extra):
    env = dict(os.environ)
    env["CPLUS_CLANG"] = clang
    probe = OUT / "probe.c"
    probe.write_text("#include <stdlib.h>\n"
                     "int main(void){int*p=malloc(4);if(!p)return 1;"
                     "volatile int i=4;p[i]=1;free(p);return 0;}\n")
    exe = OUT / "probe.bin"
    b = subprocess.run([CC, "build", "--sanitize=address", str(probe),
                        "-o", str(exe), "--"] + extra, capture_output=True,
                       text=True, env=env, timeout=180)
    if b.returncode != 0 or not exe.exists():
        return False
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    out = r.stderr + r.stdout
    return "ERROR: AddressSanitizer:" in out and "CHECK failed" not in out


CLANG = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")
EXTRA = []
sdk = sdk_path()
for candidate in [CLANG, "/usr/bin/clang", "/opt/homebrew/opt/llvm/bin/clang"]:
    if candidate and pathlib.Path(candidate).exists():
        extra = ["-isysroot", sdk] if sdk and "homebrew" in candidate else []
        if asan_works(candidate, extra):
            CLANG, EXTRA = candidate, extra
            break
ENV = dict(os.environ)
ENV["CPLUS_CLANG"] = CLANG

results = []
for mode in ["none", "address", "undefined", "address,undefined"]:
    exe = OUT / f"integration_{mode.replace(',', '_')}"
    cmd = [CC, "build", f"--sanitize={mode}",
           str(SRC / "int_main.c"), str(SRC / "int_helper.c"),
           "-I" + str(SRC), "-o", str(exe)]
    if EXTRA:
        cmd += ["--"] + EXTRA
    b = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=240)
    if b.returncode != 0 or not exe.exists():
        results.append((mode, "build", False, "build failed",
                        (b.stderr + b.stdout).strip().splitlines()[-1][:100]))
        continue
    r = subprocess.run([str(exe)], capture_output=True, text=True, env=ENV,
                       timeout=60)
    out = r.stderr + r.stdout
    bad = "AddressSanitizer" in out or "runtime error:" in out or "C+ error:" in out
    ok = r.returncode == 0 and "integration ok 36" in out and not bad
    results.append((mode, "run", ok, "integration ok 36, clean",
                    f"rc={r.returncode} {out.strip()[:90]}"))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"integration suite: {passed}/{len(results)} passed"]
for mode, kind, ok, expected, got in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {mode:20} {kind:5} {got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
