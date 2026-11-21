#!/usr/bin/env python3
"""Deterministic adversarial fuzz for the C+ SAFE subset.

Generates parameterised positive and negative programs, runs `c+ check`, and
asserts that every negative case is rejected (and, where the checker owns the
diagnostic, that the expected CPLUS-* category is present). Any mismatch is
saved under tests/out/core/adversarial/failures/ for regression triage.

This is evidence for the declared subset, not a proof of arbitrary-C safety.
"""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/adversarial"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

UNSAFE = []
POSITIVE = []


def add_unsafe(name, body, category=None):
    UNSAFE.append((name, body, category))


def add_positive(name, body):
    POSITIVE.append((name, body))


for n in range(2, 10):
    add_unsafe(f"uaf_direct_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); free(p); return *p; }}""", "CPLUS-LIFETIME")
    add_unsafe(f"uaf_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); int *q=p; free(p); return q[0]; }}""", "CPLUS-LIFETIME")
    add_unsafe(f"double_free_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); free(p); free(p); return 0; }}""", "CPLUS-OWNER")
    add_unsafe(f"double_free_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); int *q=p; free(p); free(q); return 0; }}""", "CPLUS-OWNER")
    add_unsafe(f"oob_const_{n}", f"int main(void){{ int a[{n}]={{0}}; return a[{n+1}]; }}", "CPLUS-BOUNDS")
    add_unsafe(f"oob_loop_{n}", f"int main(void){{ int a[{n}]={{0}}; for(int i=0;i<{n+2};i++) a[i]=i; return 0; }}", "CPLUS-BOUNDS")
    add_unsafe(f"oob_multidim_{n}", f"int main(void){{ int g[2][{n}]={{0}}; return g[1][{n+1}]; }}", "CPLUS-BOUNDS")
    add_unsafe(f"ptr_arith_{n}", f"int main(void){{ int a[{n}]={{0}}; int *p=a+{n+2}; return *p; }}", "CPLUS-BOUNDS")
    add_unsafe(f"null_deref_{n}", f"int main(void){{ int *p=0; for(int i=0;i<{n};i++) *p=i; return 0; }}", "CPLUS-LIFETIME")
    add_unsafe(f"uninit_branch_{n}", f"int main(int argc,char**argv){{ int x; if(argc>{n}) x=3; return x; }}", "CPLUS-LIFETIME")
    overflow_str = "abcdefghij"[:max(2, n)]
    add_unsafe(f"strcpy_overflow_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; strcpy(d,\"{overflow_str}\"); return 0; }}", "CPLUS-BOUNDS")
    oob_count = n + 2
    mem_src = max(16, oob_count)
    add_unsafe(f"memcpy_overflow_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; char s[{mem_src}]={{0}}; memcpy(d,s,{oob_count}); return 0; }}", "CPLUS-BOUNDS")
    checked_src = "abcdefghi"[:max(2, n)]
    add_unsafe(f"checked_copy_bad_{n}", f"""#include "cplus/cplus.h"
int main(void){{ char d[{n}]; return cplus_strcpy(d,2,"{checked_src}"); }}""", "CPLUS-LIBC")
    add_unsafe(f"borrow_conflict_{n}", f"""struct S {{ int x[{n}]; }};
int main(void){{ struct S s={{0}}; int *a=&s.x[0]; int *b=&s.x[1]; *a=1; *b=2; return s.x[0]; }}""", "CPLUS-BORROW")
    add_unsafe(f"realloc_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); int *q=p; void *t=realloc(p,{n}*2); if(t){{p=t;return *q;}} free(p); return 0; }}""", "CPLUS-LIFETIME")
    add_unsafe(f"resource_copy_{n}", f"""resource struct R {{ int v[{n}]; }};
void R_drop(R*r){{ if(r->v[0]) r->v[0]=0; }}
int main(void){{ R a={{{{1}}}}; R b=a; (void)b; return 0; }}""", None)
    add_unsafe(f"managed_escape_{n}", f"""#include "cplus/cplus.h"
int *bad(void){{ managed int *p=cplus_new_array(int,{n}); return p; }}
int main(void){{ (void)bad(); return 0; }}""", None)
    add_unsafe(f"stack_return_{n}", f"""int *bad(void){{ int local[{n}]; return &local[0]; }}
int main(void){{ (void)bad(); return 0; }}""", None)
    add_unsafe(f"countof_ptr_{n}", f"""#include "cplus/cplus.h"
size_t f(int *p){{ return countof(p)+{n}; }}
int main(void){{ return 0; }}""", None)

    add_positive(f"malloc_ok_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 1; p[0]={n}; int v=p[0]; free(p); return v=={n}?0:1; }}""")
    add_positive(f"alias_before_free_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); int *q=p; *q=7; int v=*p; free(p); return v==7?0:1; }}""")
    add_positive(f"realloc_safe_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); void *t=realloc(p,{n}*2); if(t) p=t; if(!p)return 1; free(p); return 0; }}""")
    add_positive(f"loop_ok_{n}", f"int main(void){{ int a[{n}]={{0}}; for(int i=0;i<{n};i++) a[i]=i; return a[{n-1}]=={n-1}?0:1; }}")
    add_positive(f"null_check_{n}", f"#include <stdlib.h>\nint main(void){{ int *p=malloc({n}); if(!p)return 1; *p=3; free(p); return 0; }}")
    add_positive(f"init_all_branches_{n}", f"int main(int argc,char**argv){{ int x; if(argc>{n}) x=1; else x=2; return x>0?0:1; }}")
    ok_str = "abc"[:max(1, n-1)]
    add_positive(f"strcpy_ok_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; strcpy(d,\"{ok_str}\"); return d[0]=='{ok_str[0]}'?0:1; }}")
    m = min(4, n)
    add_positive(f"memcpy_ok_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; char s[4]={{'a','b','c',0}}; memcpy(d,s,{m}); return d[0]=='a'?0:1; }}")
    cok = "abc"[:max(1, n-1)]
    add_positive(f"checked_copy_ok_{n}", f"""#include "cplus/cplus.h"
int main(void){{ char d[{n}]; return cplus_strcpy(d,sizeof(d),"{cok}")=={len(cok)}?0:1; }}""")


def run_case(name, body, expect_reject, category, idx):
    path = OUT / f"{name}_{idx}.c"
    path.write_text(body + "\n")
    proc = subprocess.run([CC, "check", str(path)], capture_output=True, text=True)
    diag = proc.stderr + proc.stdout
    rejected = proc.returncode != 0
    if rejected != expect_reject:
        (FAILS / f"{name}_{idx}.c").write_text(body + "\n")
        return f"MISMATCH {name}_{idx}: expected {'reject' if expect_reject else 'accept'}, rc={proc.returncode}\n{diag[:300]}"
    if expect_reject and category and category not in diag:
        (FAILS / f"{name}_{idx}.c").write_text(body + "\n")
        return f"CATEGORY {name}_{idx}: expected {category}\n{diag[:300]}"
    return None


failures = []
for i, (name, body, cat) in enumerate(UNSAFE):
    result = run_case(name, body, True, cat, i)
    if result:
        failures.append(result)
for i, (name, body) in enumerate(POSITIVE):
    result = run_case(name, body, False, None, i)
    if result:
        failures.append(result)

print(f"adversarial fuzz: {len(UNSAFE)} unsafe, {len(POSITIVE)} positive, "
      f"{len(failures)} failures")
for f in failures[:20]:
    print(f)
if failures:
    print(f"saved fixtures in {FAILS}")
    sys.exit(1)
