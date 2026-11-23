#!/usr/bin/env python3
"""Compile-time arithmetic safety: STRICT rejects, DEFAULT warns, no UBSan needed."""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/arithmetic"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
ENV["CPLUS_CLANG"] = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")

CASES = [
("int_max_add", "reject", "signed integer overflow in addition", """
#include <limits.h>
int main(void){int v=INT_MAX;return v+1;}
"""),
("int_min_sub", "reject", "signed integer overflow in subtraction", """
#include <limits.h>
int main(void){int v=INT_MIN;return v-1;}
"""),
("mul_overflow", "reject", "signed integer overflow in multiplication", """
int main(void){int a=100000;return a*a;}
"""),
("runtime_div_zero", "reject", "division by zero cannot be excluded", """
int divide(int n,int d){return n/d;}
int main(int argc,char**argv){(void)argv;return divide(100,argc-1);}
"""),
("const_div_zero", "reject", "division by zero", """
int main(void){int z=0;return 1/z;}
"""),
("const_mod_zero", "reject", "remainder by zero", """
int main(void){int z=0;return 1%z;}
"""),
("safe_boundary", "accept", "", """
#include <limits.h>
int main(void){int v=INT_MAX-1;int r=v+1;return r==INT_MAX?0:1;}
"""),
("safe_div", "accept", "", """
int main(void){return 100/7;}
"""),
("guarded_div", "accept", "", """
int div(int n,int d){if(d)return n/d;return 0;}
int main(void){return div(100,4)==25?0:1;}
"""),
("unsigned_wrap", "accept", "", """
#include <limits.h>
int main(void){unsigned v=UINT_MAX;unsigned r=v+1;return r==0?0:1;}
"""),
("unsigned_mul", "accept", "", """
#include <limits.h>
int main(void){unsigned a=65536u;unsigned r=a*a;return r==0?0:1;}
"""),
("helper_add_overflow", "reject", "signed integer overflow in call to 'safe_add'", """
#include <limits.h>
static int safe_add(int left,int right){return left+right;}
int main(void){volatile int a=INT_MAX;volatile int b=1;return safe_add(a,b);}
"""),
("helper_sub_overflow", "reject", "signed integer overflow in call to 'safe_subtract'", """
#include <limits.h>
static int safe_subtract(int left,int right){return left-right;}
int main(void){volatile int a=INT_MIN;volatile int b=1;return safe_subtract(a,b);}
"""),
("helper_mul_overflow", "reject", "signed integer overflow in call to 'safe_multiply'", """
#include <limits.h>
static int safe_multiply(int left,int right){return left*right;}
int main(void){volatile int a=INT_MAX;volatile int b=2;return safe_multiply(a,b);}
"""),
("helper_safe_add", "accept", "", """
#include <limits.h>
static int safe_add(int left,int right){return left+right;}
int main(void){volatile int a=INT_MAX-1;volatile int b=1;int r=safe_add(a,b);return r==INT_MAX?0:1;}
"""),
]

results = []
for name, want, diag, src in CASES:
    p = OUT / f"{name}.c"
    p.write_text(src)
    strict = subprocess.run([CC, "check", "--strict", str(p)],
                            capture_output=True, text=True, env=ENV, timeout=120)
    sout = strict.stderr + strict.stdout
    if want == "reject":
        ok = strict.returncode != 0 and diag in sout
    else:
        ok = strict.returncode == 0 and "error:" not in sout
    # build --sanitize=none must mirror the check verdict
    exe = OUT / f"{name}.bin"
    cmd = [CC, "build", "--strict", "--sanitize=none", str(p), "-o", str(exe)]
    b = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    if want == "reject":
        build_ok = b.returncode != 0 and diag in (b.stderr + b.stdout)
    else:
        build_ok = b.returncode == 0 and exe.exists()
    # DEFAULT mode must at least report the violation as a warning.
    dflt = subprocess.run([CC, "check", "--default", str(p)],
                          capture_output=True, text=True, env=ENV, timeout=120)
    dout = dflt.stderr + dflt.stdout
    default_ok = (diag in dout) if want == "reject" else ("error:" not in dout)
    ok = ok and build_ok and default_ok
    results.append((name, want, ok, diag,
                    next((l for l in sout.splitlines() if "error:" in l),
                         "")[:95]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"arithmetic suite: {passed}/{len(results)} passed "
         f"(check --strict, build --strict --sanitize=none, default)"]
for name, want, ok, diag, got in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {name:20} {want:6} {got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
