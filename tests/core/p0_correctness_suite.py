#!/usr/bin/env python3
"""Isolated P0 regressions for issue #21 task 2/3/4.

Each case is minimal and asserts an exact diagnostic substring in STRICT.
"""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/p0_correctness"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
if os.environ.get("CPLUS_CLANG"):
    ENV["CPLUS_CLANG"] = os.environ["CPLUS_CLANG"]

CASES = [
("shadowed_string_literal_name_ok", "accept", "", """
#include <stdio.h>
#include <string.h>
int main(void){
  const char *request = "GET / HTTP/1.0";
  if (strlen(request) < 2) return 1;
  {
    char request[64] = "local";
    char *nl = strchr(request, 10);
    if (nl) *nl = 0;
    printf("%s %s", request, request);
  }
  return 0;
}
"""),
("null_outparam_safe", "accept", "", """
#include <stdio.h>
#include <stdlib.h>
static void out(int **q){ *q = malloc(4); }
int main(void){ int *p=0; out(&p); if(!p)return 1; *p=1; printf("%d",*p); free(p); return 0; }
"""),
("address_of_non_output_not_init", "reject", "uninitialized", """
#include <stdio.h>
static void sink(int **q){ (void)q; }
int main(void){ int *p; sink(&p); printf("%p",(void*)p); return 0; }
"""),
("safe_alias_read", "accept", "", """
#include <stdlib.h>
int main(void){ int *p=malloc(8); if(!p)return 1; int *q=p; *p=1; *q=2; free(p); return 0; }
"""),
("alias_uaf", "reject", "use-after-free", """
#include <stdlib.h>
int main(void){ int *p=malloc(8); if(!p)return 1; int *q=p; free(p); return *q; }
"""),
("alias_double_free", "reject", "double free", """
#include <stdlib.h>
int main(void){ int *p=malloc(8); if(!p)return 1; int *q=p; free(p); free(q); return 0; }
"""),
("realloc_checked_ok", "accept", "", """
#include <stdlib.h>
int main(void){ int *p=malloc(8); if(!p)return 1; int *t=realloc(p,32); if(!t){free(p);return 1;} p=t; p[0]=1; free(p); return 0; }
"""),
("realloc_success_alias_uaf", "reject", "use-after-free", """
#include <stdlib.h>
int main(void){ int *p=malloc(8); if(!p)return 1; int *q=p; int *t=realloc(p,32); if(!t)return 1; return *q; }
"""),
("goto_bypasses_cleanup", "reject", "bypasses cleanup of live allocation", """
#include <stdlib.h>
int main(void){ int *p=malloc(8); if(!p)return 1; if(p) goto done; free(p); done: return 0; }
"""),
("setjmp_live_cleanup_violation", "reject", "bypasses cleanup of live allocation", """
#include <setjmp.h>
#include <stdlib.h>
static jmp_buf J;
static void work(void){ char *t=malloc(256); longjmp(J,1); free(t); }
int main(void){ if(setjmp(J)==0) work(); return 0; }
"""),
("setjmp_resume_path_safe", "accept", "", """
#include <setjmp.h>
#include <stdlib.h>
static jmp_buf J;
int main(void){ int *p=malloc(8); if(!p)return 1; if(setjmp(J)==0) longjmp(J,1); else free(p); return 0; }
"""),
]

results = []
for name, want, diag, src in CASES:
    p = OUT / f"{name}.c"
    p.write_text(src)
    r = subprocess.run([CC, "check", "--strict", str(p)], capture_output=True,
                       text=True, env=ENV, timeout=120)
    out = r.stderr + r.stdout
    if want == "accept":
        ok = r.returncode == 0 and "error:" not in out
    else:
        ok = r.returncode != 0 and diag in out
    results.append((name, want, ok, diag,
                    next((l for l in out.splitlines() if "error:" in l), "")[:110]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"P0 correctness suite: {passed}/{len(results)} passed"]
for name, want, ok, diag, got in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {name:34} expect={want:6} {got}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
