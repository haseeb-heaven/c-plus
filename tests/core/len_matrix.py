#!/usr/bin/env python3
"""Focused .len semantics matrix across every pointer/array origin."""
import pathlib, subprocess, sys, os
ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/len_matrix"
OUT.mkdir(parents=True, exist_ok=True)

C = '#include "cplus/cplus.h"\n#include <stdlib.h>\n#include <stdio.h>\n#include "stdlib/string.hp"\n'
# (name, body, expect) expect: accept / reject
CASES = [
 ("static_1", "int a[1]={0}; return a.len;", "accept"),
 ("static_7", "int a[7]={0}; return a.len;", "accept"),
 ("static_char", "char s[32]={0}; return s.len;", "accept"),
 ("static_multidim", "int a[2][3]={{0}}; return a.len;", "accept"),
 ("dynamic_const", "int *p=cplus_new_array(int,5); return p.len;", "accept"),
 ("dynamic_runtime", "int n=3; int *p=cplus_new_array(int,n); return p.len;", "accept"),
 ("alias_direct", "int *p=cplus_new_array(int,4); int *q=p; return q.len;", "accept"),
 ("alias_chain", "int *p=cplus_new_array(int,4); int *q=p; int *r=q; return r.len;", "accept"),
 ("offset_one", "int *p=cplus_new_array(int,4); return (p+1).len;", "reject"),
 ("subscript", "int *p=cplus_new_array(int,4); return p[0].len;", "reject"),
 ("realloc_ok", "int *p=cplus_new_array(int,4); void *t=realloc(p,8*sizeof(int)); if(t)p=t; return p.len;", "accept"),
 ("realloc_same", "int *p=cplus_new_array(int,4); int n=p.len; return n;", "accept"),
 ("after_free", "int *p=cplus_new_array(int,4); cplus_release(p); return p.len;", "reject"),
 ("stack_array", "int a[4]={0}; return a.len;", "accept"),
 ("stack_ptr_to_array", "int a[4]={0}; int *p=a; return p.len;", "reject"),
 ("external_malloc", "int *p=malloc(4*sizeof(int)); return p.len;", "reject"),
 ("external_calloc", "int *p=calloc(4,sizeof(int)); return p.len;", "reject"),
 ("unknown_param", "int f(int *p){return p.len;} return 0;", "reject"),
 ("zero_size", "int *p=cplus_new_array(int,0); return p.len;", "accept"),
 ("alloc_fail_check", "int *p=cplus_new_array(int,4); if(!p)return 0; return p.len;", "accept"),
 ("null_ptr", "int *p=0; return p.len;", "reject"),
 ("string_literal", "char *s=\"abc\"; return s.len;", "reject"),
 ("method_len", "String s=String(\"abc\"); return s.len();", "accept"),
 ("struct_field", "struct S{int *p;}; struct S s; s.p=cplus_new_array(int,2); return s.p.len;", "reject"),
 ("global_array", "static int g[9]; int main(void){return g.len;}", "accept"),
]

def run(c):
    name, body, expect = c
    src = C + ("int main(void){" + body + "}" if "int main" not in body else body) + "\n"
    p = OUT / f"{name}.c"
    p.write_text(src)
    r = subprocess.run([CC, "check", str(p)], capture_output=True, text=True)
    rejected = r.returncode != 0
    diag = (r.stderr + r.stdout).strip().splitlines()
    want_reject = expect == "reject"
    ok = rejected == want_reject
    return name, expect, "reject" if rejected else "accept", ok, diag[0] if diag else ""

def main():
    fails = 0
    for name, expect, got, ok, diag in map(run, CASES):
        print(f"{'ok' if ok else 'FAIL':4} {name:22} want={expect:6} got={got:6} {diag[:90]}")
        if not ok: fails += 1
    print(f"len matrix: {len(CASES)} cases, {fails} failures")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
