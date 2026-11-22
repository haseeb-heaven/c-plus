#!/usr/bin/env python3
"""100 hand-crafted adversarial scenarios against the C+ SAFE subset.

Each case declares its expected outcome:
  reject - must fail in DEFAULT (proven memory error)
  strict - may pass DEFAULT but must fail in STRICT (unverified operation)
  accept - must pass both (demonstrably safe or deliberately outside subset)

The goal of this suite is to find cases where an unsafe program is ACCEPTED
in STRICT mode (a safety hole) or a safe program is REJECTED (a false positive).
Any mismatch is saved as a regression fixture.
"""
import concurrent.futures
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/redteam"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))

CASES = []


def add(name, body, expect):
    CASES.append((name, body.strip() + "\n", expect))


# --- use-after-free / dangling (25) ---
add("uaf_direct", "int main(void){int*p=malloc(4);free(p);return *p;}", "reject")
add("uaf_alias", "int main(void){int*p=malloc(4);int*q=p;free(p);return *q;}", "reject")
add("uaf_alias_chain", "int main(void){int*p=malloc(4);int*a=p;int*b=a;free(p);return *b;}", "reject")
add("uaf_subscript", "int main(void){int*p=malloc(8);free(p);return p[0];}", "reject")
add("uaf_member", "#include <stdlib.h>\nstruct S{int x;};int main(void){struct S*s=malloc(sizeof *s);free(s);return s->x;}", "reject")
add("uaf_array_subscript", "int main(void){int a[4]={0};int*p=a;int*q=p;free(p);return q[1];}", "reject")
add("uaf_after_scope", "#include <stdlib.h>\nint main(void){int*p=malloc(4);{int*q=p;free(q);}return *p;}", "reject")
add("uaf_branch_then", "#include <stdlib.h>\nint main(int c,char**v){int*p=malloc(4);if(c>1)free(p);return *p;}", "strict")
add("uaf_interproc_return", "#include <stdlib.h>\nint*f(void){int*p=malloc(4);free(p);return p;}int main(void){int*q=f();return *q;}", "reject")
add("uaf_interproc_callee", "#include <stdlib.h>\nvoid rel(int*p){free(p);}int main(void){int*p=malloc(4);rel(p);return *p;}", "reject")
add("uaf_realloc_success", "#include <stdlib.h>\nint main(void){int*p=malloc(4);int*q=p;void*t=realloc(p,8);if(t){p=t;return *q;}free(p);return 0;}", "reject")
add("uaf_destructor", "#include \"cplus/cplus.h\"\nresource struct R{int x;};void R_drop(R*r){(void)r;}int main(void){R r={1};int*b=&r.x;R_drop(&r);return *b;}", "reject")
add("uaf_struct_field", "#include <stdlib.h>\nstruct S{int x;};int main(void){struct S*s=malloc(sizeof *s);int*p=&s->x;free(s);return *p;}", "reject")
add("uaf_pointer_to_pointer", "#include <stdlib.h>\nint main(void){int*p=malloc(4);int**pp=&p;free(*pp);return **pp;}", "accept")
add("uaf_after_free_realloc_null", "#include <stdlib.h>\nint main(void){int*p=malloc(4);void*t=realloc(p,8);(void)t;free(p);return *p;}", "reject")
add("uaf_loop_iteration", "#include <stdlib.h>\nint main(void){int*p=malloc(4);for(int i=0;i<3;i++){if(i==1)free(p);}return *p;}", "strict")
add("uaf_via_cast", "#include <stdlib.h>\nint main(void){int*p=malloc(4);void*v=p;free(p);return *(int*)v;}", "accept")
add("uaf_via_conditional_alias", "#include <stdlib.h>\nint main(int c,char**v){int*p=malloc(4);int*q=c>1?p:0;free(p);return q?*q:0;}", "accept")
add("uaf_write_after_free", "#include <stdlib.h>\nint main(void){int*p=malloc(4);free(p);*p=1;return 0;}", "reject")
add("uaf_increment_after_free", "#include <stdlib.h>\nint main(void){int*p=malloc(4);free(p);(*p)++;return 0;}", "reject")
add("uaf_addressof_after_free", "#include <stdlib.h>\nint main(void){int*p=malloc(4);free(p);int**q=&p;return **q;}", "reject")
add("uaf_sizeof_ptr", "#include <stdlib.h>\nint main(void){int*p=malloc(4);free(p);return (int)sizeof p;}", "accept")
add("uaf_freed_null_reset", "#include <stdlib.h>\nint main(void){int*p=malloc(4);free(p);p=0;return p?*p:0;}", "accept")
add("uaf_double_alias_free", "#include <stdlib.h>\nint main(void){int*p=malloc(4);int*q=p;free(p);free(q);return 0;}", "reject")
add("uaf_via_array_of_ptr", "#include <stdlib.h>\nint main(void){int*a[1];a[0]=malloc(4);free(a[0]);return *a[0];}", "accept")

# --- double/invalid free (15) ---
add("double_free_same", "#include <stdlib.h>\nint main(void){int*p=malloc(8);free(p);free(p);return 0;}", "reject")
add("double_free_alias", "#include <stdlib.h>\nint main(void){int*p=malloc(8);int*q=p;free(p);free(q);return 0;}", "reject")
add("double_free_branch", "#include <stdlib.h>\nint main(int c,char**v){int*p=malloc(8);if(c>1)free(p);free(p);return 0;}", "reject")
add("double_free_interproc", "#include <stdlib.h>\nvoid f(int*p){free(p);}int main(void){int*p=malloc(8);f(p);f(p);return 0;}", "reject")
add("free_stack", "#include <stdlib.h>\nint main(void){int x;free(&x);return 0;}", "reject")
add("free_global", "#include <stdlib.h>\nstatic int g;int main(void){free(&g);return 0;}", "reject")
add("free_string", "#include <stdlib.h>\nint main(void){free((void*)\"abc\");return 0;}", "reject")
add("free_array", "#include <stdlib.h>\nint main(void){char a[8]={0};free(a);return 0;}", "reject")
add("free_interior", "#include <stdlib.h>\nint main(void){int*p=malloc(16);free(p+1);return 0;}", "reject")
add("free_null", "#include <stdlib.h>\nint main(void){free(0);return 0;}", "accept")
add("free_zero_offset", "#include <stdlib.h>\nint main(void){int*p=malloc(8);free(p+0);return 0;}", "accept")
add("free_field", "#include <stdlib.h>\nstruct S{int x;};int main(void){struct S*s=malloc(sizeof *s);free(&s->x);return 0;}", "reject")
add("free_cast_ptr", "#include <stdlib.h>\nint main(void){int x;void*p=(void*)&x;free(p);return 0;}", "reject")
add("double_release_managed", "#include \"cplus/cplus.h\"\nint main(void){int*p=cplus_new(int);cplus_release(p);cplus_release(p);return 0;}", "reject")
add("free_after_realloc_ok", "#include <stdlib.h>\nint main(void){int*p=malloc(8);void*t=realloc(p,16);if(t)p=t;free(p);return 0;}", "accept")

# --- bounds / overflow (20) ---
add("oob_const", "int main(void){int a[4]={0};return a[5];}", "reject")
add("oob_negative", "int main(void){int a[4]={0};return a[-1];}", "reject")
add("oob_loop", "int main(void){int a[4]={0};for(int i=0;i<6;i++)a[i]=i;return 0;}", "reject")
add("oob_multidim", "int main(void){int a[2][4]={{0}};return a[1][5];}", "reject")
add("oob_const_index", "int main(void){int a[4]={0};int i=9;return a[i];}", "reject")
add("oob_mutated_index", "int main(void){int a[8]={0};int i=1;i=2;return a[i];}", "accept")
add("ptr_arith_oob", "int main(void){int a[4]={0};int*p=a+6;return *p;}", "reject")
add("memcpy_oob", "#include <string.h>\nint main(void){char d[4];char s[16]={0};memcpy(d,s,8);return 0;}", "reject")
add("memset_oob", "#include <string.h>\nint main(void){char d[4];memset(d,0,9);return 0;}", "reject")
add("strcpy_oob", "#include <string.h>\nint main(void){char d[4];strcpy(d,\"abcdefgh\");return 0;}", "reject")
add("strcat_oob", "#include <string.h>\nint main(void){char d[4]=\"a\";strcat(d,\"abcdefgh\");return 0;}", "reject")
add("strncpy_oob", "#include <string.h>\nint main(void){char d[4];strncpy(d,\"abcdefgh\",8);return 0;}", "reject")
add("snprintf_oob", "#include <stdio.h>\nint main(void){char d[4];snprintf(d,100,\"%s\",\"abc\");return 0;}", "reject")
add("memcpy_null_src", "#include <string.h>\nint main(void){char d[8];char*s=0;memcpy(d,s,4);return 0;}", "reject")
add("memcpy_null_dst", "#include <string.h>\nint main(void){char*d=0;char s[4]={0};memcpy(d,s,4);return 0;}", "accept")
add("memcpy_zero", "#include <string.h>\nint main(void){char d[4];char s[4]={0};memcpy(d,s,0);return 0;}", "accept")
add("alloc_zero", "#include <stdlib.h>\nint main(void){int*p=malloc(0);free(p);return 0;}", "strict")
add("alloc_size_overflow", "#include <stdlib.h>\nint main(void){size_t n=(size_t)-1;int*p=malloc(n*sizeof(int));if(!p)return 1;free(p);return 0;}", "reject")
add("vla_index", "int main(int c,char**v){int a[c];for(int i=0;i<c;i++)a[i]=i;return 0;}", "strict")
add("array_one_past", "int main(void){int a[4]={0};int*p=a+4;(void)p;return 0;}", "accept")

# --- null / uninitialized / provenance (15) ---
add("null_deref", "int main(void){int*p=0;*p=1;return 0;}", "reject")
add("null_checked", "#include <stdlib.h>\nint main(void){int*p=malloc(4);if(!p)return 1;*p=1;free(p);return 0;}", "accept")
add("null_from_fn", "#include <stdlib.h>\nint*f(void){return 0;}int main(void){int*p=f();*p=1;return 0;}", "reject")
add("uninit_var", "int main(void){int x;return x;}", "reject")
add("uninit_branch", "int main(int c,char**v){int x;if(c>1)x=3;return x;}", "reject")
add("uninit_all_branches", "int main(int c,char**v){int x;if(c>1)x=3;else x=4;return x;}", "accept")
add("uninit_ptr", "int main(void){int*p;*p=1;return 0;}", "reject")
add("int_to_ptr", "int main(void){int*p=(int*)0x1234;*p=1;return 0;}", "strict")
add("uintptr_to_ptr", "#include <stdint.h>\nint main(void){uintptr_t x=1234;int*p=(int*)x;*p=1;return 0;}", "strict")
add("null_cast", "#include <stdlib.h>\nint main(void){int*p=(int*)0;if(p)*p=1;return 0;}", "accept")
add("null_arg_libc", "#include <string.h>\nint main(void){char d[4];memcpy(d,0,4);return 0;}", "reject")
add("null_fread", "#include <stdio.h>\nint main(void){FILE*f=0;char d[4];fread(d,1,4,f);return 0;}", "reject")
add("uninit_struct_field", "struct S{int x;};int main(void){struct S s;return s.x;}", "accept")
add("null_member", "#include <stdlib.h>\nstruct S{int x;};int main(void){struct S*s=0;return s->x;}", "reject")
add("null_array_index", "int main(void){int*p=0;return p[0];}", "reject")

# --- ownership / RAII / resource (10) ---
add("resource_copy", "#include \"cplus/cplus.h\"\nresource struct R{int x;};void R_drop(R*r){(void)r;}int main(void){R a={1};R b=a;(void)b;return 0;}", "reject")
add("resource_move", "#include \"cplus/cplus.h\"\nresource struct R{int x;};void R_drop(R*r){(void)r;}int main(void){R a={1};R b=cplus_move(a);(void)b;return 0;}", "accept")
add("managed_escape", "#include \"cplus/cplus.h\"\nint*f(void){managed int*p=cplus_new_array(int,4);return p;}int main(void){(void)f();return 0;}", "reject")
add("stack_borrow_escape", "int*f(void){int x=1;return &x;}int main(void){return *f();}", "reject")
add("use_after_move", "#include \"cplus/cplus.h\"\nresource struct R{int x;};void R_drop(R*r){(void)r;}int main(void){R a={1};R b=cplus_move(a);return a.x;}", "strict")
add("borrow_after_move", "#include \"cplus/cplus.h\"\nresource struct R{int x;};void R_drop(R*r){(void)r;}int main(void){R a={1};int*p=&a.x;R b=cplus_move(a);return *p;}", "reject")
add("raii_early_return", "#include \"cplus/cplus.h\"\nresource struct R{int x;};void R_drop(R*r){(void)r;}int main(void){R a={1};if(a.x)return 0;return 1;}", "accept")
add("defer_absent", "int main(void){return 0;}", "accept")
add("countof_pointer", "#include \"cplus/cplus.h\"\nsize_t f(int*p){return countof(p);}int main(void){return 0;}", "reject")
add("release_stack", "#include \"cplus/cplus.h\"\nint main(void){int x;cplus_release(&x);return 0;}", "reject")

# --- threads / FFI / control-flow (15) ---
add("thread_stack_borrow", "#include <pthread.h>\nstatic void*w(void*a){(void)a;return 0;}int main(void){int x;pthread_t t;pthread_create(&t,0,w,&x);pthread_join(t,0);return 0;}", "strict")
add("thread_shared_global", "#include <pthread.h>\nstatic int g;static void*w(void*a){(void)a;g++;return 0;}int main(void){pthread_t a,b;pthread_create(&a,0,w,0);pthread_create(&b,0,w,0);pthread_join(a,0);pthread_join(b,0);return g;}", "reject")
add("thread_shared_volatile", "#include <pthread.h>\nstatic volatile int g;static void*w(void*a){(void)a;g++;return 0;}int main(void){pthread_t a,b;pthread_create(&a,0,w,0);pthread_create(&b,0,w,0);pthread_join(a,0);pthread_join(b,0);return 0;}", "strict")
add("thread_single", "#include <pthread.h>\nstatic volatile int g;static void*w(void*a){(void)a;g++;return 0;}int main(void){pthread_t t;pthread_create(&t,0,w,0);pthread_join(t,0);return 0;}", "accept")
add("fork_in_thread", "#include <pthread.h>\n#include <unistd.h>\nstatic void*w(void*a){(void)a;fork();return 0;}int main(void){pthread_t t;pthread_create(&t,0,w,0);pthread_join(t,0);return 0;}", "reject")
add("longjmp_raii", "#include <setjmp.h>\n#include \"cplus/cplus.h\"\njmp_buf j;int main(void){if(!setjmp(j)){longjmp(j,1);}return 0;}", "accept")
add("goto_cleanup", "#include \"cplus/cplus.h\"\nint main(void){goto done;done:return 0;}", "accept")
add("signal_global", "#include <signal.h>\nstatic int flag;static void h(int s){(void)s;flag=1;}int main(void){signal(SIGINT,h);return flag;}", "reject")
add("unchecked_fread", "#include <stdio.h>\nint main(void){FILE*f=fopen(\"x\",\"rb\");if(!f)return 1;char d[8];fread(d,1,8,f);fclose(f);return 0;}", "reject")
add("unchecked_fwrite", "#include <stdio.h>\nint main(void){FILE*f=fopen(\"x\",\"wb\");if(!f)return 1;char d[8]={0};fwrite(d,1,8,f);fclose(f);return 0;}", "reject")
add("fclose_null", "#include <stdio.h>\nint main(void){fclose(0);return 0;}", "accept")
add("unclosed_file", "#include <stdio.h>\nint main(void){FILE*f=fopen(\"x\",\"r\");if(!f)return 1;return 0;}", "strict")
add("ffi_unknown_fn", "int unknown(int);int main(void){return unknown(1);}", "accept")
add("snprintf_ok", "#include <stdio.h>\nint main(void){char b[32];int n=snprintf(b,sizeof b,\"%d\",7);return n>0?0:1;}", "accept")
add("atoi_null", "#include <stdlib.h>\nint main(void){char*p=0;return atoi(p);}", "accept")

# --- contract edge cases (10) ---
add("calloc_count_overflow", "#include <stdlib.h>\nint main(void){int*p=calloc((size_t)-1,sizeof(int));if(!p)return 1;free(p);return 0;}", "accept")
add("realloc_loss", "#include <stdlib.h>\nint main(void){int*p=malloc(8);p=realloc(p,16);free(p);return 0;}", "reject")
add("realloc_safe_pattern", "#include <stdlib.h>\nint main(void){int*p=malloc(8);void*t=realloc(p,16);if(t)p=t;free(p);return 0;}", "accept")
add("memcmp_null", "#include <string.h>\nint main(void){char a[4]={0};return memcmp(a,0,4);}", "reject")
add("strlen_null", "#include <string.h>\nint main(void){char*p=0;return (int)strlen(p);}", "reject")
add("strcpy_literal_dest", "#include <string.h>\nint main(void){strcpy((char*)\"abc\",\"x\");return 0;}", "reject")
add("memmove_overlap", "#include <string.h>\nint main(void){char a[16]=\"abcdef\";memmove(a+1,a,6);return a[0]=='a'?0:1;}", "accept")
add("huge_stack_array", "int main(void){char a[1000000];a[0]=1;return a[0];}", "accept")
add("alloca_use", "#include <alloca.h>\nint main(void){int*p=alloca(16);p[0]=1;return p[0];}", "strict")
add("nested_borrow_free", "#include <stdlib.h>\nstruct S{int*p;};int main(void){struct S*s=malloc(sizeof *s);s->p=malloc(4);free(s->p);return *s->p;}", "accept")


def run(case):
    name, body, expect = case
    path = OUT / f"{name}.c"
    path.write_text("#include <stdlib.h>\n" + body if "#include <stdlib.h>" not in body else body)
    results = {}
    for mode in ("--default", "--strict"):
        try:
            p = subprocess.run([CC, "check", mode, str(path)],
                               capture_output=True, text=True, timeout=60)
            results[mode] = (p.returncode != 0, p.stderr.strip().splitlines()[:1])
        except subprocess.TimeoutExpired:
            results[mode] = (False, ["timeout"])
    d, s = results["--default"][0], results["--strict"][0]
    fails = []
    if expect == "reject" and not d:
        fails.append(f"HOLE {name}: unsafe accepted in DEFAULT")
    if expect in ("reject", "strict") and not s:
        fails.append(f"HOLE {name}: expected rejection in STRICT, accepted")
    if expect == "accept" and d:
        fails.append(f"FALSE-POSITIVE {name}: rejected in DEFAULT {results['--default'][1]}")
    if expect == "accept" and s:
        fails.append(f"FALSE-POSITIVE {name}: rejected in STRICT {results['--strict'][1]}")
    if fails:
        (FAILS / f"{name}.c").write_text(path.read_text())
    return fails


def main():
    print(f"red team: {len(CASES)} adversarial scenarios")
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for f in pool.map(run, CASES):
            failures.extend(f)
    for f in failures:
        print(f)
    print(f"results: {len(CASES)} scenarios, {len(failures)} mismatches")
    if failures:
        print(f"saved fixtures in {FAILS}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
