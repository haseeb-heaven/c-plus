#!/usr/bin/env python3
"""Regression suite for the 15 runtime-crash scenarios (issue review).

A case passes if EITHER:
  * C+ rejects it at compile time with the expected static diagnostic, OR
  * the selected sanitizer prints the expected specific report when run.
A fatal signal by itself is never accepted.
"""
import glob
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
SRC = ROOT / "tests/safety/runtime_crashes"
OUT = ROOT / "tests/out/core/runtime_crashes"
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


# Backend detection: keep the explicit override, otherwise find a backend
# where ASan actually produces a diagnostic (Apple clang 16's ASan aborts).
SDK = sdk_path()
PROBE = OUT / "probe.c"
PROBE.write_text("#include <stdlib.h>\n"
                 "int main(void){int*p=malloc(4);if(!p)return 1;"
                 "volatile int i=4;p[i]=1;free(p);return 0;}\n")


def asan_works(clang, extra):
    env = dict(os.environ)
    env["CPLUS_CLANG"] = clang
    exe = OUT / "probe.bin"
    b = subprocess.run([CC, "build", "--sanitize=address", str(PROBE),
                        "-o", str(exe), "--"] + extra, capture_output=True,
                       text=True, env=env, timeout=180)
    if b.returncode != 0 or not exe.exists():
        return False
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    out = r.stderr + r.stdout
    return "ERROR: AddressSanitizer:" in out and "CHECK failed" not in out


def choose_backend():
    env_clang = os.environ.get("CPLUS_CLANG")
    candidates = []
    if env_clang and pathlib.Path(env_clang).exists():
        candidates.append((env_clang, []))
    for c in ["/usr/bin/clang", "/opt/homebrew/opt/llvm/bin/clang"]:
        if not pathlib.Path(c).exists():
            continue
        candidates.append((c, []))
        if SDK and "homebrew" in c:
            candidates.append((c, ["-isysroot", SDK]))
    for clang, extra in candidates:
        if asan_works(clang, extra):
            return clang, extra
    return (env_clang or "/usr/bin/clang"), []


CLANG, EXTRA = choose_backend()

ENV = dict(os.environ)
ENV["CPLUS_CLANG"] = CLANG
ENV["ASAN_OPTIONS"] = "detect_leaks=0"
ENV["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

# name -> (input, sanitizer, expected report, legacy mode)
PLAN = {
    # rejected at compile time by C+ static analysis
    "rc02": ("-50", None, "leaks on this return path", False),
    "rc03": ("2305843009213693951", None, "error:", False),
    "rc06": ("0 1", None, "error:", False),
    "rc09": ("10000000", None, "[CPLUS-OWNER]", False),
    "rc10": ("8", None, "[CPLUS-BOUNDS]", False),
    "rc12": ("abcdefghijklmnopqrstuvwxyz", None, "error:", False),
    # runtime-dependent: detected by a sanitizer
    "rc01": ("4294967297", "address",
             "ERROR: AddressSanitizer: heap-buffer-overflow", False),
    "rc04": ("1048576", "address",
             "ERROR: AddressSanitizer: heap-use-after-free", False),
    "rc05": ("65536 65537", "address",
             "ERROR: AddressSanitizer: SEGV", False),
    "rc07": ("3\n3\n0", "address",
             "ERROR: AddressSanitizer: attempting double-free", False),
    "rc08": ("2", "address",
             "ERROR: AddressSanitizer: heap-use-after-free", False),
    "rc11": ("18446744073709551600", "integer",
             "runtime error: unsigned integer overflow", False),
    "rc13": ("99999", "none", "C+ error: index", False),
    "rc14": ("5000000", "address",
             "ERROR: AddressSanitizer: BUS", False),
    "rc15": ("1", "thread",
             "WARNING: ThreadSanitizer: data race", True),
}

results = []
for path in sorted(SRC.glob("rc*.c")):
    name = path.stem
    stdin_data, san, expected, legacy = PLAN[name]
    chk = subprocess.run([CC, "check", "--strict", str(path)],
                         capture_output=True, text=True, env=ENV, timeout=120)
    static_diag = chk.stderr + chk.stdout
    if chk.returncode != 0 and expected in static_diag:
        results.append((name, "static", True, expected,
                        next((l for l in static_diag.splitlines()
                              if expected in l), "")[:110]))
        continue
    if san is None:
        results.append((name, "static", False, expected,
                        static_diag.strip().splitlines()[-1][:110]))
        continue
    exe = OUT / f"{name}.bin"
    cmd = [CC, "build"]
    if legacy:
        cmd.append("--legacy-c")
    cmd += [f"--sanitize={san}", str(path), "-o", str(exe)]
    if EXTRA:
        cmd += ["--"] + EXTRA
    b = subprocess.run(cmd, capture_output=True, text=True, env=ENV,
                       timeout=240)
    if b.returncode != 0 or not exe.exists():
        results.append((name, "runtime", False, expected,
                        "build failed: " +
                        (b.stderr + b.stdout).strip().splitlines()[-1][:100]))
        continue
    try:
        r = subprocess.run([str(exe)], input=stdin_data + "\n",
                           capture_output=True, text=True, env=ENV,
                           timeout=30)
        diag = r.stderr + r.stdout
    except subprocess.TimeoutExpired:
        diag, r = "", None
    ok = r is not None and r.returncode != 0 and expected in diag
    results.append((name, "runtime", ok, expected,
                    next((l for l in diag.splitlines() if expected in l),
                         f"exit {r.returncode if r else 'timeout'}")[:110]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"runtime-crashes suite: {passed}/{len(results)} passed "
         f"(backend {CLANG})"]
for name, kind, ok, expected, got in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {name:5} {kind:7} "
                 f"expect={expected!r} got={got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines[1:] if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
