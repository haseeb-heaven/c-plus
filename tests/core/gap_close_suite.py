#!/usr/bin/env python3
"""Paired regressions for newly closed pointer/bounds/lifetime gaps.

Every negative case asserts an exact diagnostic substring; every positive case
must compile cleanly. No bare non-zero exit is accepted as evidence.
"""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/gap_close"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
ENV["CPLUS_CLANG"] = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")

CASES = [
("fam_onepast", "reject", "flexible array index 2 exceeds", """
#include <stdlib.h>
struct V { int n; int a[]; };
int main(void){struct V*v=malloc(sizeof *v+8);if(!v)return 1;int*p=&v->a[2];return *p;}
"""),
("fam_inbounds", "accept", "", """
#include <stdlib.h>
struct V { int n; int a[]; };
int main(void){struct V*v=malloc(sizeof *v+8);if(!v)return 1;v->a[0]=1;v->a[1]=2;int r=v->a[1]-2;free(v);return r;}
"""),
("strncpy_src_extent", "reject", "exceeds source of length 4", """
#include <string.h>
int main(void){char src[4]="abc", dst[16];strncpy(dst,src,16);return 0;}
"""),
("strncpy_src_ok", "accept", "", """
#include <string.h>
int main(void){char src[4]="abc", dst[16];strncpy(dst,src,4);return 0;}
"""),
("compound_oob", "reject", "exceeds compound literal of length 2", """
int main(void){int*p=(int[]){1,2};return p[3];}
"""),
("compound_ok", "accept", "", """
int main(void){int*p=(int[]){1,2};return p[1]-2;}
"""),
("onepast_deref", "reject", "one past the end", """
int main(void){int a[4]={0};int*p=a+4;*p=1;return 0;}
"""),
("negative_index", "reject", "negative array index", """
int main(void){int a[4]={0};int*p=a;return p[-1];}
"""),
("array_inbounds", "accept", "", """
int main(void){int a[4]={0};int*p=a;p[3]=1;return p[3]-1;}
"""),
("alias_uaf", "reject", "use-after-free", """
#include <stdlib.h>
int main(void){int*p=malloc(8);if(!p)return 1;int*q=p;free(p);return *q;}
"""),
("alias_safe", "accept", "", """
#include <stdlib.h>
int main(void){int*p=malloc(8);if(!p)return 1;int*q=p;*q=1;free(p);return 0;}
"""),
("field_uaf", "reject", "use-after-free", """
#include <stdlib.h>
struct S{int*p;};
int main(void){struct S s;s.p=malloc(8);if(!s.p)return 1;free(s.p);return *s.p;}
"""),
("field_safe", "accept", "", """
#include <stdlib.h>
struct S{int*p;};
int main(void){struct S s;s.p=malloc(8);if(!s.p)return 1;*s.p=1;free(s.p);return 0;}
"""),
]

results = []
for name, want, diag, src in CASES:
    p = OUT / f"{name}.c"
    p.write_text(src)
    r = subprocess.run([CC, "check", "--strict", str(p)], capture_output=True,
                       text=True, env=ENV, timeout=120)
    out = r.stderr + r.stdout
    ok = (r.returncode != 0 and diag in out) if want == "reject" else \
         (r.returncode == 0 and "error:" not in out)
    results.append((name, want, ok, diag,
                    next((l for l in out.splitlines() if "error:" in l), "")[:100]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"gap-close suite: {passed}/{len(results)} passed"]
for name, want, ok, diag, got in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {name:22} {want:6} {got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
