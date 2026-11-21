#!/usr/bin/env python3
"""Deterministic FFI / THREAD / PROVENANCE generator for C+ (440 small C cases).

Generates deterministic, seeded programs across eleven domains and checks each
one with ``c+ check`` in BOTH modes:

  * ``--default``  - proven errors must be rejected; unverified ops may warn.
  * ``--strict``   - unverified operations must be rejected (fail closed).

Domains (40 cases each):

  pth_stack      pthread_create stack borrows (pointer passed to a thread)
  pth_global     shared globals mutated by one vs. multiple thread entries
  file_io        fopen/fclose, double close, use-after-close, valid controls
  io_check       unchecked I/O returns (fread/fopen/scanf) vs. checked I/O
  state_libc     stateful libc (strtok/asctime/gmtime/localtime) reuse
  null_libc      null arguments to libc (memcpy/strlen/strcmp)
  forged_ptr     integer-forged pointers and arbitrary integer casts
  uptr_roundtrip uintptr_t round trips (safe compares vs. forged derefs)
  ljmp_cleanup   setjmp/longjmp cleanup bypass and clean longjmp controls
  signal_handler async-signal-unsafe handler bodies vs. sig_atomic_t flags
  goto_cleanup   goto bypass of initialisation/free and proper single-exit

Every case declares an expected outcome:

  reject - must fail DEFAULT (proven memory error); must also fail STRICT.
  strict - may pass DEFAULT, but must fail STRICT (unverified operation).
  accept - valid control; must pass BOTH modes.

Mismatches (safety holes and false positives) are printed and the offending
source plus expected outcome is saved under tests/out/core/ffi350/failures/.

Generation is seeded (FUZZ_SEED, default 0xF31) and execution uses a thread
pool (FUZZ_WORKERS, default 8) with a 60s per-invocation timeout (FUZZ_TIMEOUT).
CPLUS_BIN overrides the compiler binary. This is evidence for the declared
sound subset, not a proof of arbitrary-C safety.
"""
import collections
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/ffi350"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xF31"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))
N_VALUES = range(2, 10)  # eight parameter values per template

CATEGORIES = (
    "pth_stack", "pth_global", "file_io", "io_check", "state_libc", "null_libc",
    "forged_ptr", "uptr_roundtrip", "ljmp_cleanup", "signal_handler",
    "goto_cleanup",
)

CASES = []  # (name, body, expect, category)
NAMES = set()
STAGE = None  # temp directory holding one staged .c file per case


def add(name, body, expect, category):
    """Register one generated case; the name comment guarantees distinctness."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    assert category in CATEGORIES, (name, category)
    assert name not in NAMES, name
    NAMES.add(name)
    text = f"/* ffi350 {category} {name} */\n" + body.strip() + "\n"
    CASES.append((name, text, expect, category))


# ---------------------------------------------------------------------------
# 1. pthread_create stack borrows: stack addresses escaping into a thread.
# ---------------------------------------------------------------------------
def build_pth_stack(rng):
    c = "pth_stack"
    for n in N_VALUES:
        add(f"pth_stack_write_{n}", f"""#include <pthread.h>
static void *worker(void *arg) {{ int *p=(int*)arg; *p={n}; return 0; }}
int main(void) {{ int local=0; pthread_t t; pthread_create(&t,0,worker,&local); pthread_join(t,0); return local=={n}?0:1; }}""",
            "strict", c)
        add(f"pth_stack_read_{n}", f"""#include <pthread.h>
static void *worker(void *arg) {{ int *p=(int*)arg; int v=*p; (void)v; return 0; }}
int main(void) {{ int local={n}; pthread_t t; pthread_create(&t,0,worker,&local); pthread_join(t,0); return 0; }}""",
            "strict", c)
        add(f"pth_stack_array_{n}", f"""#include <pthread.h>
static void *worker(void *arg) {{ int *p=(int*)arg; for(int i=0;i<{n};i++) p[i]=i; return 0; }}
int main(void) {{ int buf[{n}]={{0}}; pthread_t t; pthread_create(&t,0,worker,buf); pthread_join(t,0); return buf[0]; }}""",
            "strict", c)
        add(f"pth_heap_arg_{n}", f"""#include <pthread.h>
#include <stdlib.h>
static void *worker(void *arg) {{ int *p=(int*)arg; *p={n}; return 0; }}
int main(void) {{ int *h=malloc(sizeof(int)); if(!h)return 2; pthread_t t; pthread_create(&t,0,worker,h); pthread_join(t,0); int v=*h; free(h); return v=={n}?0:1; }}""",
            "strict", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.75:
            add(f"mut_pth_stack_borrow_{i}", f"""#include <pthread.h>
static void *worker(void *arg) {{ int *p=(int*)arg; *p=*p+{n}; return 0; }}
int main(void) {{ int local=1; pthread_t t; pthread_create(&t,0,worker,&local); pthread_join(t,0); return local; }}""",
                "strict", c)
        else:
            add(f"mut_pth_stack_local_{i}", f"""#include <pthread.h>
static void *worker(void *arg) {{ (void)arg; int x={n}; x++; (void)x; return 0; }}
int main(void) {{ pthread_t t; pthread_create(&t,0,worker,0); pthread_join(t,0); return 0; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 2. shared globals across threads: plain vs volatile vs mutex vs single writer.
# ---------------------------------------------------------------------------
def build_pth_global(rng):
    c = "pth_global"
    for n in N_VALUES:
        add(f"pth_plain_two_{n}", f"""#include <pthread.h>
static int g;
static void *worker(void *arg) {{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void) {{ pthread_t a,b; pthread_create(&a,0,worker,0); pthread_create(&b,0,worker,0); pthread_join(a,0); pthread_join(b,0); return g; }}""",
            "reject", c)
        add(f"pth_volatile_two_{n}", f"""#include <pthread.h>
static volatile int g;
static void *worker(void *arg) {{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void) {{ pthread_t a,b; pthread_create(&a,0,worker,0); pthread_create(&b,0,worker,0); pthread_join(a,0); pthread_join(b,0); return 0; }}""",
            "strict", c)
        add(f"pth_mutex_two_{n}", f"""#include <pthread.h>
static int g; static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;
static void *worker(void *arg) {{ (void)arg; pthread_mutex_lock(&m); for(int i=0;i<{n};i++) g++; pthread_mutex_unlock(&m); return 0; }}
int main(void) {{ pthread_t a,b; pthread_create(&a,0,worker,0); pthread_create(&b,0,worker,0); pthread_join(a,0); pthread_join(b,0); return 0; }}""",
            "accept", c)
        add(f"pth_single_writer_{n}", f"""#include <pthread.h>
static int g;
static void *worker(void *arg) {{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void) {{ pthread_t t; pthread_create(&t,0,worker,0); pthread_join(t,0); return g; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        two = rng.random() < 0.65
        vol = rng.random() < 0.5
        qty = 2 if two else 1
        creates = " ".join(f"pthread_create(&t[{k}],0,worker,0);" for k in range(qty))
        joins = " ".join(f"pthread_join(t[{k}],0);" for k in range(qty))
        v = "volatile " if vol else ""
        expect = ("strict" if vol else "reject") if two else "accept"
        add(f"mut_pth_global_{i}", f"""#include <pthread.h>
static {v}int g;
static void *worker(void *arg) {{ (void)arg; for(int i=0;i<{n};i++) g++; return 0; }}
int main(void) {{ pthread_t t[{qty}]; {creates} {joins} return 0; }}""",
            expect, c)


# ---------------------------------------------------------------------------
# 3. FILE streams: use-after-close, double close, null close, valid control.
# ---------------------------------------------------------------------------
def build_file_io(rng):
    c = "file_io"
    for n in N_VALUES:
        add(f"file_uaf_close_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_{n}.txt","w"); if(!f)return 2; fclose(f); fprintf(f,"x{n}"); return 0; }}""",
            "reject", c)
        add(f"file_double_close_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_{n}.txt","w"); if(!f)return 2; fclose(f); fclose(f); return 0; }}""",
            "reject", c)
        add(f"file_close_null_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=0; if({n}>0) {{ fclose(f); }} return 0; }}""",
            "reject", c)
        add(f"file_ok_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_{n}.txt","w"); if(!f)return 2; if(fputs("x",f)==EOF){{fclose(f);return 1;}} if(fclose(f)!=0)return 1; return 0; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.6:
            add(f"mut_file_bad_{i}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_mut_{i}.txt","w"); if(!f)return 2; fclose(f); fputc('a'+{n%7},f); return 0; }}""",
                "reject", c)
        else:
            add(f"mut_file_ok_{i}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_mut_{i}.txt","w"); if(!f)return 2; if(fputc('a'+{n%7},f)==EOF){{fclose(f);return 1;}} return fclose(f)==0?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 4. unchecked I/O returns: fread/fopen/scanf results ignored before use.
# ---------------------------------------------------------------------------
def build_io_check(rng):
    c = "io_check"
    for n in N_VALUES:
        add(f"io_fread_unchecked_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_in_{n}.bin","rb"); if(!f)return 2; char b[{n}]; fread(b,1,sizeof b,f); fclose(f); return b[0]; }}""",
            "reject", c)
        add(f"io_fopen_unchecked_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_{n}.txt","w"); fclose(f); return {n}-{n}; }}""",
            "reject", c)
        add(f"io_scanf_unchecked_{n}", f"""#include <stdio.h>
int main(void) {{ int x; scanf("%d",&x); if({n}>0) return x; return 0; }}""",
            "reject", c)
        add(f"io_checked_{n}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_in_{n}.bin","rb"); if(!f)return 2; char b[{n}]={{0}}; size_t k=fread(b,1,sizeof b,f); int rc=(k>0)?0:1; if(fclose(f)!=0)return 1; return rc; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.6:
            add(f"mut_io_bad_{i}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_mut_{i}.bin","rb"); if(!f)return 2; char b[{n}]; fread(b,1,{n},f); fclose(f); return b[0]; }}""",
                "reject", c)
        else:
            add(f"mut_io_ok_{i}", f"""#include <stdio.h>
int main(void) {{ FILE *f=fopen("ffi350_mut_{i}.bin","rb"); if(!f)return 2; char b[{n}]={{0}}; size_t k=fread(b,1,{n},f); int rc=(k=={n})?0:1; fclose(f); return rc; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 5. stateful libc: strtok/asctime/gmtime/localtime static-state reuse.
# ---------------------------------------------------------------------------
def build_state_libc(rng):
    c = "state_libc"
    for n in N_VALUES:
        add(f"state_strtok_{n}", f"""#include <string.h>
int main(void) {{ char a[64]="x y z"; char b[64]="m n o"; int k=0; for(int i=0;i<{n};i++) {{ char *p=strtok(a," "); char *q=strtok(b," "); if(p&&q)k++; }} (void)k; return 0; }}""",
            "strict", c)
        add(f"state_gmtime_{n}", f"""#include <time.h>
int main(void) {{ time_t t={n}; struct tm *a=gmtime(&t); struct tm *b=gmtime(&t); (void)b; return a->tm_year==0?0:1; }}""",
            "strict", c)
        add(f"state_localtime_{n}", f"""#include <time.h>
int main(void) {{ time_t t={n}; struct tm *a=localtime(&t); struct tm *b=localtime(&t); (void)b; return a->tm_year==0?0:1; }}""",
            "strict", c)
        add(f"state_asctime_{n}", f"""#include <time.h>
#include <stdio.h>
int main(void) {{ time_t t={n}; struct tm *tm=gmtime(&t); char *a=asctime(tm); char *b=asctime(tm); printf("%s",b); (void)a; return 0; }}""",
            "strict", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.65:
            add(f"mut_state_reuse_{i}", f"""#include <time.h>
int main(void) {{ time_t t={n}; struct tm *a=localtime(&t); struct tm *b=gmtime(&t); (void)b; return a->tm_mon==0?0:1; }}""",
                "strict", c)
        else:
            add(f"mut_state_ok_{i}", f"""#include <time.h>
int main(void) {{ time_t t={n}; struct tm *a=gmtime(&t); int y=a->tm_year; struct tm *b=gmtime(&t); (void)b; return y==0?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 6. null arguments to libc: memcpy/strlen/strcmp with null pointers.
# ---------------------------------------------------------------------------
def build_null_libc(rng):
    c = "null_libc"
    for n in N_VALUES:
        add(f"null_memcpy_{n}", f"""#include <string.h>
int main(void) {{ char d[{n}]; char *s=0; memcpy(d,s,{n}); return 0; }}""",
            "reject", c)
        add(f"null_strlen_{n}", f"""#include <string.h>
int main(void) {{ char *s=0; if({n}>0) return (int)strlen(s); return 0; }}""",
            "reject", c)
        add(f"null_strcmp_{n}", f"""#include <string.h>
int main(void) {{ char *s=0; int c=strcmp(s,"x"); return (c!=0 && {n}>0)?0:1; }}""",
            "reject", c)
        add(f"null_ok_{n}", f"""#include <string.h>
int main(void) {{ char s[16]="abc{n}"; char d[16]; size_t k=strlen(s); if(k>=sizeof d)return 1; memcpy(d,s,k+1); return d[0]=='a'?0:1; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.7:
            add(f"mut_null_bad_{i}", f"""#include <string.h>
int main(void) {{ char d[{n}]; char *s=0; memset(d,0,sizeof d); memcpy(d,s,{n}); return 0; }}""",
                "reject", c)
        else:
            add(f"mut_null_ok_{i}", f"""#include <string.h>
int main(void) {{ char s[{n}]={{0}}; char d[{n}]={{0}}; memcpy(d,s,{n}); return d[0]==0?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 7. integer-forged pointers: arbitrary integer -> pointer conversions.
# ---------------------------------------------------------------------------
def build_forged_ptr(rng):
    c = "forged_ptr"
    for n in N_VALUES:
        add(f"forged_intlit_{n}", f"""int main(void) {{ int *p=(int*)(0x1000+{n}); *p=1; return 0; }}""",
            "strict", c)
        add(f"forged_uptr_{n}", f"""#include <stdint.h>
int main(void) {{ uintptr_t x=0x2000+{n}; int *p=(int*)x; *p=1; return 0; }}""",
            "strict", c)
        add(f"forged_arith_{n}", f"""#include <stdint.h>
int main(void) {{ uintptr_t x=0x3000+{n}; int *p=(int*)(x+4); *p=1; return 0; }}""",
            "strict", c)
        add(f"forged_noderef_{n}", f"""#include <stdint.h>
int main(void) {{ uintptr_t x=0x4000+{n}; int *p=(int*)x; (void)p; return {n}-{n}; }}""",
            "strict", c)

    for i in range(8):
        addr = rng.randint(0x1000, 0x8000)
        if rng.random() < 0.7:
            add(f"mut_forged_deref_{i}", f"""#include <stdint.h>
int main(void) {{ uintptr_t x={addr}; int *p=(int*)x; *p={i+1}; return *p; }}""",
                "strict", c)
        else:
            add(f"mut_forged_noderef_{i}", f"""#include <stdint.h>
int main(void) {{ uintptr_t x={addr}; int *p=(int*)x; (void)p; return 0; }}""",
                "strict", c)


# ---------------------------------------------------------------------------
# 8. uintptr_t round trips: safe compares vs. forged pointer reconstruction.
# ---------------------------------------------------------------------------
def build_uptr_roundtrip(rng):
    c = "uptr_roundtrip"
    for n in N_VALUES:
        add(f"uptr_roundtrip_deref_{n}", f"""#include <stdint.h>
#include <stdlib.h>
int main(void) {{ int *p=malloc(sizeof(int)); if(!p)return 2; *p={n}; uintptr_t u=(uintptr_t)p; int *q=(int*)u; int v=*q; free(p); return v=={n}?0:1; }}""",
            "strict", c)
        add(f"uptr_roundtrip_offset_{n}", f"""#include <stdint.h>
#include <stdlib.h>
int main(void) {{ int *p=malloc(4*sizeof(int)); if(!p)return 2; uintptr_t u=(uintptr_t)p; int *q=(int*)(u+sizeof(int)); *q={n}; free(p); return 0; }}""",
            "strict", c)
        add(f"uptr_roundtrip_store_{n}", f"""#include <stdint.h>
#include <stdlib.h>
static uintptr_t g;
int main(void) {{ int *p=malloc(sizeof(int)); if(!p)return 2; *p={n}; g=(uintptr_t)p; int *q=(int*)g; int v=*q; free(p); return v=={n}?0:1; }}""",
            "strict", c)
        add(f"uptr_cmp_ok_{n}", f"""#include <stdint.h>
#include <stdlib.h>
int main(void) {{ int *p=malloc(sizeof(int)); if(!p)return 2; uintptr_t a=(uintptr_t)p; uintptr_t b=(uintptr_t)(p+0); int ok=(a==b); free(p); return ok?0:1; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.7:
            add(f"mut_uptr_forged_{i}", f"""#include <stdint.h>
#include <stdlib.h>
int main(void) {{ int *p=malloc(2*sizeof(int)); if(!p)return 2; uintptr_t u=(uintptr_t)p; int *q=(int*)(u+{i%2}); *q={n}; free(p); return 0; }}""",
                "strict", c)
        else:
            add(f"mut_uptr_cmp_{i}", f"""#include <stdint.h>
#include <stdlib.h>
int main(void) {{ int *p=malloc(sizeof(int)); if(!p)return 2; uintptr_t u=(uintptr_t)p; uintptr_t v=u+{n}; int ok=(v>u); free(p); return ok?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 9. setjmp/longjmp cleanup: skipped free/clobber vs. clean longjmp controls.
# ---------------------------------------------------------------------------
def build_ljmp_cleanup(rng):
    c = "ljmp_cleanup"
    for n in N_VALUES:
        add(f"ljmp_skip_free_{n}", f"""#include <setjmp.h>
#include <stdlib.h>
static jmp_buf env;
static void deep(void) {{ int *t=malloc({n}); longjmp(env,1); free(t); }}
int main(void) {{ if(setjmp(env)==0) {{ deep(); }} return 0; }}""",
            "strict", c)
        add(f"ljmp_clobber_{n}", f"""#include <setjmp.h>
static jmp_buf env;
static void deep(void) {{ longjmp(env,1); }}
int main(void) {{ int x={n}; if(setjmp(env)==0) {{ x={n}+1; deep(); }} return x=={n}?0:1; }}""",
            "strict", c)
        add(f"ljmp_nested_{n}", f"""#include <setjmp.h>
#include <stdlib.h>
static jmp_buf outer, inner;
static void mid(void) {{ int *t=malloc({n}); if(setjmp(inner)==0) {{ longjmp(outer,1); }} free(t); }}
int main(void) {{ if(setjmp(outer)==0) {{ mid(); }} return 0; }}""",
            "strict", c)
        add(f"ljmp_ok_{n}", f"""#include <setjmp.h>
static jmp_buf env;
static void deep(void) {{ longjmp(env,{n}); }}
int main(void) {{ int r=setjmp(env); if(r==0) {{ deep(); }} return r=={n}?0:1; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.65:
            add(f"mut_ljmp_skip_{i}", f"""#include <setjmp.h>
#include <stdlib.h>
static jmp_buf env;
static void deep(void) {{ char *t=malloc({n}); longjmp(env,1); free(t); }}
int main(void) {{ if(setjmp(env)==0) {{ deep(); }} return 0; }}""",
                "strict", c)
        else:
            add(f"mut_ljmp_ok_{i}", f"""#include <setjmp.h>
static jmp_buf env;
static void deep(void) {{ longjmp(env,{n}); }}
int main(void) {{ int r=setjmp(env); if(r==0) {{ deep(); }} return r=={n}?0:1; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 10. signal handlers: async-signal-unsafe work vs. sig_atomic_t flags.
# ---------------------------------------------------------------------------
def build_signal_handler(rng):
    c = "signal_handler"
    for n in N_VALUES:
        add(f"sig_malloc_{n}", f"""#include <signal.h>
#include <stdlib.h>
static void h(int s) {{ (void)s; void *p=malloc({n}); free(p); }}
int main(void) {{ signal(SIGINT,h); return 0; }}""",
            "reject", c)
        add(f"sig_free_global_{n}", f"""#include <signal.h>
#include <stdlib.h>
static int *g;
static void h(int s) {{ (void)s; free(g); g=0; }}
int main(void) {{ g=malloc({n}*sizeof(int)); if(!g)return 2; signal(SIGINT,h); free(g); g=0; return 0; }}""",
            "reject", c)
        add(f"sig_global_plain_{n}", f"""#include <signal.h>
static int g;
static void h(int s) {{ (void)s; g={n}; }}
int main(void) {{ signal(SIGINT,h); return g=={n}?0:1; }}""",
            "reject", c)
        add(f"sig_flag_ok_{n}", f"""#include <signal.h>
static volatile sig_atomic_t flag;
static void h(int s) {{ (void)s; flag=1; }}
int main(void) {{ signal(SIGINT,h); if(flag=={n}) return 1; return 0; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        if rng.random() < 0.65:
            add(f"mut_sig_unsafe_{i}", f"""#include <signal.h>
#include <stdlib.h>
static void h(int s) {{ (void)s; void *p=malloc({n}); free(p); }}
int main(void) {{ signal(SIGTERM,h); return 0; }}""",
                "reject", c)
        else:
            add(f"mut_sig_flag_{i}", f"""#include <signal.h>
static volatile sig_atomic_t flag;
static void h(int s) {{ (void)s; flag={n}; }}
int main(void) {{ signal(SIGTERM,h); return (int)flag=={n}?1:0; }}""",
                "accept", c)


# ---------------------------------------------------------------------------
# 11. goto cleanup bypass: skipped init/free/UAF vs. proper single exit.
# ---------------------------------------------------------------------------
def build_goto_cleanup(rng):
    c = "goto_cleanup"
    for n in N_VALUES:
        add(f"goto_bypass_init_{n}", f"""int main(void) {{ goto skip; int *p=0; skip: if(p!=0) {{ *p={n}; }} return 0; }}""",
            "reject", c)
        add(f"goto_bypass_free_{n}", f"""#include <stdlib.h>
int main(void) {{ int *p=malloc({n}); if(!p)return 2; goto done; free(p); done: return 0; }}""",
            "strict", c)
        add(f"goto_uaf_{n}", f"""#include <stdlib.h>
int main(void) {{ int *p=malloc({n}); if(!p)return 2; free(p); goto use; use: return *p; }}""",
            "reject", c)
        add(f"goto_cleanup_ok_{n}", f"""#include <stdlib.h>
int main(void) {{ int rc=1; int *p=malloc({n}); if(!p)return 2; if(rc) {{ goto done; }} rc=0; done: free(p); return rc; }}""",
            "accept", c)

    for i in range(8):
        n = rng.randint(2, 16)
        pick = rng.random()
        if pick < 0.35:
            add(f"mut_goto_init_{i}", f"""int main(void) {{ goto end; int *p=0; end: if(p) {{ *p={n}; }} return 0; }}""",
                "reject", c)
        elif pick < 0.65:
            add(f"mut_goto_free_{i}", f"""#include <stdlib.h>
int main(void) {{ int *p=malloc({n}); if(!p)return 2; goto out; free(p); out: return 0; }}""",
                "strict", c)
        else:
            add(f"mut_goto_ok_{i}", f"""#include <stdlib.h>
int main(void) {{ int rc=1; int *p=malloc({n}); if(!p)return 2; if(rc) goto done; rc=0; done: free(p); return rc; }}""",
                "accept", c)


BUILDERS = (
    build_pth_stack, build_pth_global, build_file_io, build_io_check,
    build_state_libc, build_null_libc, build_forged_ptr, build_uptr_roundtrip,
    build_ljmp_cleanup, build_signal_handler, build_goto_cleanup,
)


# ---------------------------------------------------------------------------
# Execution: check every case in DEFAULT and STRICT and classify mismatches.
# ---------------------------------------------------------------------------
def check_case(case):
    name, body, expect, category = case
    path = STAGE / f"{name}.c"
    path.write_text(body)
    results = {}
    for mode in ("--default", "--strict"):
        try:
            p = subprocess.run([CC, "check", mode, str(path)],
                               capture_output=True, text=True, timeout=TIMEOUT)
            lines = (p.stderr + p.stdout).strip().splitlines()
            diag = lines[0].strip() if lines else "(no diagnostic)"
            results[mode] = (p.returncode != 0, diag[:200], p.returncode < 0)
        except subprocess.TimeoutExpired:
            results[mode] = (False, "TIMEOUT", False)

    d_fail, d_diag, d_crash = results["--default"]
    s_fail, s_diag, s_crash = results["--strict"]
    fails = []
    if d_crash:
        fails.append(f"CRASH[{category}] {name}: compiler signal in DEFAULT")
    if s_crash:
        fails.append(f"CRASH[{category}] {name}: compiler signal in STRICT")
    if expect == "reject":
        if not d_fail:
            fails.append(f"HOLE[{category}] {name}: accepted in DEFAULT; diag={d_diag}")
        if not s_fail:
            fails.append(f"HOLE[{category}] {name}: accepted in STRICT; diag={s_diag}")
    elif expect == "strict":
        if not s_fail:
            fails.append(f"HOLE[{category}] {name}: accepted in STRICT (expected strict); diag={s_diag}")
    else:  # accept
        if d_fail:
            fails.append(f"FALSE-POSITIVE[{category}] {name}: rejected in DEFAULT; diag={d_diag}")
        if s_fail:
            fails.append(f"FALSE-POSITIVE[{category}] {name}: rejected in STRICT; diag={s_diag}")

    if fails:
        (FAILS / f"{name}.c").write_text(body)
        (FAILS / f"{name}.expect.txt").write_text(
            f"expect={expect}\ncategory={category}\n"
            f"default_failed={d_fail}\nstrict_failed={s_fail}\n"
            + "\n".join(fails) + "\n")
    return fails


def main():
    global STAGE
    rng = random.Random(SEED)
    for builder in BUILDERS:
        builder(rng)

    assert len(CASES) >= 350, len(CASES)
    assert len({c[1] for c in CASES}) == len(CASES), "generated programs are not distinct"

    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ffi350_"))

    outcomes = collections.Counter(c[2] for c in CASES)
    counts = collections.Counter(c[3] for c in CASES)
    print(f"ffi350: {len(CASES)} cases, seed={SEED}, workers={WORKERS}, binary={CC}")
    for cat in CATEGORIES:
        print(f"  {cat}: {counts[cat]}")
    print(f"  outcomes: reject={outcomes['reject']}, strict={outcomes['strict']}, "
          f"accept={outcomes['accept']}")

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for fails in pool.map(check_case, CASES):
            failures.extend(fails)

    print(f"mismatch count: {len(failures)}")
    for f in failures[:10]:
        print(f"  {f}")
    if len(failures) > 10:
        print(f"  ... and {len(failures) - 10} more")
    if failures:
        print(f"saved fixtures in {FAILS}")
    print(f"FINAL: {'FAIL' if failures else 'PASS'} "
          f"({len(CASES)} cases, {len(failures)} mismatches)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
