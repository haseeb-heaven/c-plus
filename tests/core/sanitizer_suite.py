#!/usr/bin/env python3
"""Runtime sanitizer suite: configurable ASan/UBSan/combined/none.

Every negative case must exit non-zero AND print the expected sanitizer
diagnostic. A non-zero exit code alone is never accepted as proof.
"""
import glob
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
SRC = ROOT / "tests/safety/sanitizer"
OUT = ROOT / "tests/out/core/sanitizer"
OUT.mkdir(parents=True, exist_ok=True)


def sdk_path():
    try:
        r = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True,
                           text=True, timeout=20)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    except FileNotFoundError:
        pass
    candidates = glob.glob("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
    return candidates[0] if candidates else ""


PROBE = OUT / "probe.c"
PROBE.write_text("#include <stdlib.h>\n"
                 "int main(void){int*p=malloc(4);if(!p)return 1;"
                 "volatile int i=4; p[i]=1; free(p); return 0;}\n")


def asan_works(clang, extra):
    exe = OUT / "probe.bin"
    env = dict(os.environ)
    env["CPLUS_CLANG"] = clang
    b = subprocess.run([CC, "build", "--sanitize=address", str(PROBE),
                        "-o", str(exe), "--"] + extra,
                       capture_output=True, text=True, env=env, timeout=180)
    if b.returncode != 0 or not exe.exists():
        return False
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    out = r.stderr + r.stdout
    # A broken runtime prints "AddressSanitizer: CHECK failed" without a
    # real diagnostic; that is not a working backend.
    return "ERROR: AddressSanitizer:" in out and "CHECK failed" not in out


def choose_backend():
    sdk = sdk_path()
    candidates = []
    env_clang = os.environ.get("CPLUS_CLANG")
    if env_clang:
        candidates.append((env_clang, []))
    for clang in ["/usr/bin/clang", "/opt/homebrew/opt/llvm/bin/clang"]:
        if not pathlib.Path(clang).exists():
            continue
        candidates.append((clang, []))
        if sdk and "homebrew" in clang:
            candidates.append((clang, ["-isysroot", sdk]))
    for clang, extra in candidates:
        if asan_works(clang, extra):
            return clang, extra
    return (env_clang or "/usr/bin/clang"), []


CLANG, EXTRA = choose_backend()

CASES = [
    ("heap_overflow", "address", "AddressSanitizer: heap-buffer-overflow"),
    ("stack_overflow", "address", "AddressSanitizer: stack-buffer-overflow"),
    ("use_after_free", "address", "AddressSanitizer: heap-use-after-free"),
    ("double_free", "address", "AddressSanitizer: attempting double-free"),
    ("flex_array_overflow", "address", "AddressSanitizer: heap-buffer-overflow"),
    ("ubsan_overflow", "undefined", "runtime error: signed integer overflow"),
]

results = []
base_env = dict(os.environ)
base_env["CPLUS_CLANG"] = CLANG
base_env["ASAN_OPTIONS"] = "detect_leaks=0"
base_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"


def build_run(src, mode, name, verbose=False):
    exe = OUT / f"{name}.bin"
    if exe.exists():
        exe.unlink()
    cmd = [CC, "build", f"--sanitize={mode}"]
    if verbose:
        cmd.append("--verbose")
    cmd += [str(src), "-o", str(exe)]
    if EXTRA:
        cmd += ["--"] + EXTRA
    b = subprocess.run(cmd, capture_output=True, text=True, env=base_env,
                       timeout=240)
    if b.returncode != 0 or not exe.exists():
        return None, b.stderr + b.stdout
    r = subprocess.run([str(exe)], capture_output=True, text=True,
                       env=base_env, timeout=60)
    return r, b.stderr + b.stdout


for name, mode, expected in CASES:
    r, build_out = build_run(SRC / f"{name}.c", mode, name)
    if r is None:
        results.append((name, "negative", False, expected,
                        "build failed: " + build_out.strip().splitlines()[-1][:100]))
        continue
    diag = r.stderr + r.stdout
    ok = r.returncode != 0 and expected in diag
    results.append((name, "negative", ok, expected,
                    next((l for l in diag.splitlines() if expected in l),
                         f"exit {r.returncode}")[:120]))

# Safe program under each configuration must exit cleanly with no diagnostic.
for mode in ["none", "address", "undefined", "address,undefined"]:
    name = "clean_" + mode.replace(",", "_")
    r, build_out = build_run(SRC / "clean.c", mode, name)
    if r is None:
        results.append((name, "positive", False, "exit 0",
                        "build failed: " + build_out.strip().splitlines()[-1][:100]))
        continue
    diag = r.stderr + r.stdout
    bad = ("AddressSanitizer" in diag or "runtime error:" in diag)
    ok = r.returncode == 0 and not bad
    results.append((name, "positive", ok, "exit 0", f"exit {r.returncode}"))

# Flag plumbing: both compile and link commands must carry the flags.
def linked_symbols(name):
    exe = OUT / f"{name}.bin"
    n = subprocess.run(["nm", "-g", str(exe)], capture_output=True,
                       text=True, timeout=30)
    return n.stdout + n.stderr


none_syms = linked_symbols("clean_none")
ok = "__asan_init" not in none_syms and "__ubsan_handle" not in none_syms
results.append(("none_has_no_runtimes", "config", ok, "no sanitizer runtime",
                "absent" if ok else "present"))

both_syms = linked_symbols("clean_address_undefined")
ok = "__asan_init" in both_syms and "__ubsan_handle" in both_syms
results.append(("combined_links_both", "config", ok,
                "ASan and UBSan runtimes linked",
                f"asan={'__asan_init' in both_syms} "
                f"ubsan={'__ubsan_handle' in both_syms}"))

_, verbose_both = build_run(SRC / "clean.c", "address,undefined",
                            "flag_both", verbose=True)
ok = "-fsanitize=address" in verbose_both and \
     "-fsanitize=undefined" in verbose_both
results.append(("combined_compile_flags", "config", ok,
                "both sanitizer flags at compile",
                f"asan={'-fsanitize=address' in verbose_both} "
                f"ubsan={'-fsanitize=undefined' in verbose_both}"))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"sanitizer suite: {passed}/{len(results)} passed "
         f"(backend: {CLANG})"]
for name, kind, ok, expected, got in results:
    if not ok:
        lines.append(f"  FAIL {kind:8} {name:26} expect={expected!r} got={got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(lines[1:]))
sys.exit(0 if passed == len(results) else 1)
