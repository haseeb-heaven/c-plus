#!/usr/bin/env python3
"""Mass-scale deterministic fuzzing for the C+ SAFE subset.

Generates thousands of parameterised and mutated programs, then verifies them
along several independent dimensions:

  1. STATIC REJECTION  - every unsafe program must be rejected by `c+ check`
                         with the expected CPLUS-* diagnostic category.
  2. STATIC ACCEPTANCE - every positive program must be accepted.
  3. DIFFERENTIAL      - every positive program must also be accepted by the
                         host C compiler (i.e. it is real, unchanged C).
  4. RUNTIME           - a deterministic sample of positive programs is built
                         and executed; exit status must be zero.
  5. ROBUSTNESS        - the compiler must never crash (no signal/abort).

Cases run in parallel.  Any mismatch is saved as a regression fixture under
tests/out/core/mass_fuzz/failures/.  The generator is seeded, so a run is
reproducible.  This is evidence for the declared subset, not a proof of
arbitrary-C safety.
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
CLANG = os.environ.get("CLANG_BIN", "clang")
OUT = ROOT / "tests/out/core/mass_fuzz"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xC0FFEE"), 0)
STRICT = os.environ.get("FUZZ_STRICT", "1") != "0"
MAX_N = int(os.environ.get("FUZZ_MAX_N", "48"))
MUTATIONS = int(os.environ.get("FUZZ_MUTATIONS", "1600"))

CASES = []  # (name, body, expect_reject, category, check_runtime)


def add(name, body, reject, category=None, runtime=False):
    CASES.append((name, body, reject, category, runtime))


# ---------------------------------------------------------------------------
# Dimension 1: wide parameter sweep of known templates.
# ---------------------------------------------------------------------------
for n in range(1, MAX_N + 1):
    add(f"uaf_direct_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p); return *p; }}""",
        True, "CPLUS-LIFETIME")
    add(f"uaf_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; int *q=p; free(p); return q[0]; }}""",
        True, "CPLUS-LIFETIME")
    add(f"uaf_subscript_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p); return p[0]; }}""",
        True, "CPLUS-LIFETIME")
    add(f"double_free_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; free(p); free(p); return 0; }}""",
        True, "CPLUS-OWNER")
    add(f"double_free_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; int *q=p; free(p); free(q); return 0; }}""",
        True, "CPLUS-OWNER")
    add(f"oob_const_{n}", f"int main(void){{ int a[{n}]={{0}}; return a[{n+1}]; }}",
        True, "CPLUS-BOUNDS")
    add(f"oob_loop_{n}", f"int main(void){{ int a[{n}]={{0}}; for(int i=0;i<{n+2};i++) a[i]=i; return 0; }}",
        True, "CPLUS-BOUNDS")
    add(f"oob_multidim_{n}", f"int main(void){{ int g[2][{n}]={{0}}; return g[1][{n+1}]; }}",
        True, "CPLUS-BOUNDS")
    add(f"ptr_arith_{n}", f"int main(void){{ int a[{n}]={{0}}; int *p=a+{n+2}; return *p; }}",
        True, "CPLUS-BOUNDS")
    add(f"null_deref_{n}", f"int main(void){{ int *p=0; for(int i=0;i<{n};i++) *p=i; return 0; }}",
        True, "CPLUS-LIFETIME")
    add(f"uninit_branch_{n}", f"int main(int argc,char**argv){{ int x; if(argc>{n}) x=3; return x; }}",
        True, "CPLUS-LIFETIME")
    alphabet = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    src = alphabet[:n + 2]  # always one byte more than the destination holds
    add(f"strcpy_overflow_{n}", f'#include <string.h>\nint main(void){{ char d[{n}]; strcpy(d,"{src}"); return 0; }}',
        True, "CPLUS-BOUNDS")
    cnt = n + 2
    add(f"memcpy_overflow_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; char s[{max(16,cnt)}]={{0}}; memcpy(d,s,{cnt}); return 0; }}",
        True, "CPLUS-BOUNDS")
    add(f"memset_overflow_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; memset(d,0,{cnt}); return 0; }}",
        True, "CPLUS-BOUNDS")
    add(f"var_index_bad_{n}", f"int main(void){{ int a[{n}]={{0}}; int i={n+1}; return a[i]; }}",
        True, "CPLUS-BOUNDS")
    add(f"strcat_overflow_{n}", f'#include <string.h>\nint main(void){{ char d[{n}]="a"; strcat(d,"{alphabet}"); return 0; }}',
        True, "CPLUS-BOUNDS")
    add(f"strncpy_overflow_{n}", f'#include <string.h>\nint main(void){{ char d[{n}]; strncpy(d,"{alphabet}",{n+2}); return 0; }}',
        True, "CPLUS-BOUNDS")
    add(f"snprintf_overflow_{n}", f'#include <stdio.h>\nint main(void){{ char d[{n}]; snprintf(d,{n+2},"%s","abcdefgh"); return 0; }}',
        True, "CPLUS-BOUNDS")
    add(f"memcpy_null_src_{n}", """#include <string.h>
int main(void) { char d[8]; char *s = 0; memcpy(d, s, 4); return 0; }""",
        True, "CPLUS-LIFETIME")
    csrc = "abcdefghi"[:max(2, n)]
    add(f"checked_copy_bad_{n}", f'#include "cplus/cplus.h"\nint main(void){{ char d[{n}]; return cplus_strcpy(d,2,"{csrc}"); }}',
        True, "CPLUS-LIBC")
    add(f"realloc_alias_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; int *q=p; void *t=realloc(p,{n}*2); if(t){{p=t;return *q;}} free(p); return 0; }}""",
        True, "CPLUS-LIFETIME")
    add(f"resource_copy_{n}", f"""resource struct R {{ int v[{n}]; }};
void R_drop(R*r){{ if(r->v[0]) r->v[0]=0; }}
int main(void){{ R a={{{{1}}}}; R b=a; (void)b; return 0; }}""", True)
    add(f"managed_escape_{n}", f"""#include "cplus/cplus.h"
int *bad(void){{ managed int *p=cplus_new_array(int,{n}); return p; }}
int main(void){{ (void)bad(); return 0; }}""", True)
    add(f"stack_return_{n}", f"""int *bad(void){{ int local[{n}]; return &local[0]; }}
int main(void){{ (void)bad(); return 0; }}""", True)
    add(f"countof_ptr_{n}", f"""#include "cplus/cplus.h"
size_t f(int *p){{ return countof(p)+{n}; }}
int main(void){{ return 0; }}""", True)
    add(f"free_stack_{n}", f"""#include <stdlib.h>
int main(void){{ int local[{n}]={{0}}; int *p=&local[0]; free(p); return 0; }}""",
        True, "CPLUS-OWNER")
    add(f"free_global_{n}", f"""#include <stdlib.h>
static int g[{n}];
int main(void){{ free(&g[0]); return 0; }}""", True, "CPLUS-OWNER")
    add(f"free_array_{n}", f"""#include <stdlib.h>
int main(void){{ char a[{n}]={{0}}; free(a); return 0; }}""", True, "CPLUS-OWNER")
    add(f"free_interior_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 2; free(p+1); return 0; }}""",
        True, "CPLUS-OWNER")
    add(f"free_string_{n}", """#include <stdlib.h>
int main(void) { const char *s = "abcdefghijklmnop"; free((void *)s); return 0; }""",
        True, "CPLUS-OWNER")

    add(f"malloc_ok_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 1; p[0]={n}; int v=p[0]; free(p); return v=={n}?0:1; }}""",
        False, runtime=(n % 8 == 0))
    add(f"alias_before_free_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 1; int *q=p; *q=7; int v=*p; free(p); return v==7?0:1; }}""",
        False, runtime=(n % 8 == 0))
    add(f"realloc_safe_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 1; void *t=realloc(p,{n}*2); if(t) p=t; if(!p)return 1; free(p); return 0; }}""",
        False)
    add(f"loop_ok_{n}", f"int main(void){{ int a[{n}]={{0}}; for(int i=0;i<{n};i++) a[i]=i; return a[{n-1}]=={n-1}?0:1; }}",
        False, runtime=(n % 8 == 0))
    add(f"null_check_{n}", f"#include <stdlib.h>\nint main(void){{ int *p=malloc({n}); if(!p)return 1; *p=3; free(p); return 0; }}",
        False)
    add(f"init_all_branches_{n}", f"int main(int argc,char**argv){{ int x; if(argc>{n}) x=1; else x=2; return x>0?0:1; }}",
        False, runtime=(n % 8 == 0))
    k = min(3, max(0, n - 1))
    ok_str = "abc"[:k]
    check_char = f"d[0]=='{ok_str[0]}'" if k else "1"
    add(f"alias_writes_{n}", f"""struct S {{ int x[{n}]; }};
int main(void){{ struct S s={{0}}; int *a=&s.x[0]; int *b=&s.x[{n-1}]; *a=1; *b=2; return s.x[0]==1?0:1; }}""",
        False, runtime=(n % 8 == 0))
    add(f"strcpy_ok_{n}", f'#include <string.h>\nint main(void){{ char d[{n}]={{0}}; strcpy(d,"{ok_str}"); return {check_char}?0:1; }}',
        False, runtime=(n % 8 == 0))
    m = min(4, n)
    add(f"memcpy_ok_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]; char s[4]={{'a','b','c',0}}; memcpy(d,s,{m}); return d[0]=='a'?0:1; }}",
        False)
    cok = "abc"[:k]
    add(f"checked_copy_ok_{n}", f'#include "cplus/cplus.h"\nint main(void){{ char d[{n}]={{0}}; return cplus_strcpy(d,sizeof(d),"{cok}")=={len(cok)}?0:1; }}',
        False)
    add(f"free_zero_ok_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 1; free(p+0); return 0; }}""",
        False)
    add(f"memset_ok_{n}", f"#include <string.h>\nint main(void){{ char d[{n}]={{0}}; memset(d,0,{n}); return d[0]; }}",
        False)
    add(f"var_index_ok_{n}", f"int main(void){{ int a[{n}]={{0}}; int i=0; return a[i]; }}",
        False, runtime=(n % 8 == 0))
    add(f"strcat_ok_{n}", f'#include <string.h>\nint main(void){{ char d[{n+8}]="a"; strcat(d,"bc"); return d[0]==\'a\'?0:1; }}',
        False, runtime=(n % 8 == 0))
    add(f"strncpy_ok_{n}", f'#include <string.h>\nint main(void){{ char d[{n+4}]; strncpy(d,"abc",4); return d[0]==\'a\'?0:1; }}',
        False, runtime=(n % 8 == 0))
    add(f"snprintf_ok_{n}", f'#include <stdio.h>\nint main(void){{ char d[{n+8}]; snprintf(d,sizeof(d),"%s","abc"); return d[0]==\'a\'?0:1; }}',
        False)

# ---------------------------------------------------------------------------
# Dimension 2: structural variations (nested scopes, branch merges, chains).
# ---------------------------------------------------------------------------
for n in range(1, MAX_N // 2 + 1):
    add(f"nested_uaf_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; {{ int *q=p; free(p); return *q; }} }}""",
        True, "CPLUS-LIFETIME")
    add(f"chain_uaf_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 2; int *a=p; int *b=a; int *c=b; free(p); return *c; }}""",
        True, "CPLUS-LIFETIME")
    add(f"interproc_free_uaf_{n}", f"""#include <stdlib.h>
void rel(int*p){{ free(p); }}
int main(void){{ int *p=malloc({n}); if(!p)return 2; rel(p); return *p; }}""",
        True, "CPLUS-LIFETIME")
    add(f"interproc_return_uaf_{n}", f"""#include <stdlib.h>
int *make_freed(void){{ int *p=malloc({n}*sizeof(int)); if(!p)return 0; free(p); return p; }}
int main(void){{ int *q=make_freed(); if(!q)return 1; return *q; }}""",
        True, "CPLUS-LIFETIME")
    add(f"branch_double_free_{n}", f"""#include <stdlib.h>
int main(int argc,char**argv){{ int *p=malloc({n}); if(!p)return 1; if(argc>1) free(p); free(p); return 0; }}""",
        True, "CPLUS-OWNER")
    add(f"nested_ok_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 1; {{ int *q=p; *q=5; }} int v=*p; free(p); return v==5?0:1; }}""",
        False, runtime=(n % 8 == 0))
    add(f"chain_ok_{n}", f"""#include <stdlib.h>
int main(void){{ int *p=malloc({n}); if(!p)return 1; int *a=p; int *b=a; *b=9; int v=*a; free(p); return v==9?0:1; }}""",
        False, runtime=(n % 8 == 0))
    add(f"interproc_return_ok_{n}", f"""#include <stdlib.h>
int *make_fresh(void){{ int *t=malloc(4); if(!t)return 0; free(t); int *p=malloc({n}*sizeof(int)); return p; }}
int main(void){{ int *q=make_fresh(); if(!q)return 1; *q=3; int v=*q; free(q); return v==3?0:1; }}""",
        False, runtime=(n % 8 == 0))
    add(f"branch_null_ok_{n}", f"""#include <stdlib.h>
int main(int argc,char**argv){{ int *p=malloc({n}); if(!p)return 1; if(argc>1){{ free(p); p=0; }} free(p); return 0; }}""",
        False)

# ---------------------------------------------------------------------------
# Dimension 3: seeded mutation fuzz over composable safe/unsafe patterns.
# ---------------------------------------------------------------------------
rng = random.Random(SEED)
for i in range(MUTATIONS):
    aliases = rng.randint(1, 4)
    n = rng.randint(max(4, aliases), 32)
    idx = rng.randint(0, aliases - 1)
    unsafe = rng.random() < 0.5
    decl = f"int *p=malloc({n}*sizeof(int)); if(!p)return 2;"
    alias_lines = " ".join(f"int *a{j}=p;" for j in range(aliases))
    if unsafe:
        body = f"""#include <stdlib.h>
int main(void){{ {decl} {alias_lines} free(p); return *a{idx}; }}"""
        add(f"mut_uaf_{i}", body, True, "CPLUS-LIFETIME")
    else:
        # Distinct offsets so each alias has an independently verifiable value.
        writes = " ".join(f"a{j}[{j}]={j+1};" for j in range(aliases))
        body = f"""#include <stdlib.h>
int main(void){{ {decl} {alias_lines} {writes} int v=a{idx}[{idx}]; free(p); return v=={idx+1}?0:1; }}"""
        add(f"mut_ok_{i}", body, False, runtime=(i % 16 == 0))

    # Bounds mutations.
    arr = rng.randint(1, 24)
    over = rng.random() < 0.5
    access = arr + (rng.randint(1, 4) if over else 0)
    if access >= arr:
        add(f"mut_bounds_bad_{i}", f"int main(void){{ int a[{arr}]={{0}}; return a[{access}]; }}",
            True, "CPLUS-BOUNDS")
    else:
        add(f"mut_bounds_ok_{i}", f"int main(void){{ int a[{arr}]={{0}}; a[{access}]=3; return a[{access}]==3?0:1; }}",
            False, runtime=(i % 16 == 0))


def compile_case(case):
    """Return (failures[], stats) for one case."""
    name, body, reject, category, runtime = case
    path = OUT / f"{name}.c"
    path.write_text(body + "\n")
    fails = []
    stats = {"check": 1, "strict": 0, "clang": 0, "runtime": 0}
    try:
        proc = subprocess.run([CC, "check", str(path)], capture_output=True,
                              text=True, timeout=60)
    except subprocess.TimeoutExpired:
        fails.append(f"TIMEOUT {name}")
        return fails, stats
    diag = proc.stderr + proc.stdout
    if proc.returncode < 0 or "signal" in diag.lower():
        fails.append(f"CRASH {name} rc={proc.returncode}\n{diag[:300]}")
    if STRICT:
        # STRICT must never accept an unsafe program and must never reject a
        # fully-verified positive program (no silent downgrade, no new false
        # positive for these templates).
        stats["strict"] = 1
        try:
            sp = subprocess.run([CC, "check", "--strict", str(path)],
                                capture_output=True, text=True, timeout=60)
            if (sp.returncode != 0) != reject:
                (FAILS / f"{name}.strict.c").write_text(body + "\n")
                fails.append(f"STRICT-MISMATCH {name}: rc={sp.returncode}")
        except subprocess.TimeoutExpired:
            fails.append(f"STRICT-TIMEOUT {name}")
    rejected = proc.returncode != 0
    if rejected != reject:
        (FAILS / f"{name}.c").write_text(body + "\n")
        fails.append(f"MISMATCH {name}: expected {'reject' if reject else 'accept'}, rc={proc.returncode}\n{diag[:300]}")
    elif reject and category and category not in diag:
        (FAILS / f"{name}.c").write_text(body + "\n")
        fails.append(f"CATEGORY {name}: expected {category}\n{diag[:300]}")

    if not reject:
        # Differential: the positive program must be valid, unchanged C.
        stats["clang"] = 1
        try:
            d = subprocess.run([CLANG, "-x", "c", "-std=c11", "-fsyntax-only",
                                "-I", str(ROOT / "runtime/include"),
                                str(path)], capture_output=True, text=True, timeout=60)
            if d.returncode != 0:
                (FAILS / f"{name}.clang.c").write_text(body + "\n")
                fails.append(f"CLANG-REJECT {name}\n{d.stderr[:300]}")
        except FileNotFoundError:
            stats["clang"] = 0  # differential unavailable; not a failure
        except subprocess.TimeoutExpired:
            fails.append(f"CLANG-TIMEOUT {name}")

        if runtime:
            for optflag in ("--debug", "--release"):
                stats["runtime"] = stats.get("runtime", 0) + 1
                exe = OUT / f"{name}{optflag}.bin"
                try:
                    b = subprocess.run([CC, "build", optflag, str(path),
                                        "-o", str(exe)],
                                       capture_output=True, text=True,
                                       timeout=120)
                    if b.returncode != 0:
                        fails.append(f"BUILD-FAIL {name} {optflag}\n{b.stderr[:300]}")
                    else:
                        r = subprocess.run([str(exe)], capture_output=True,
                                           text=True, timeout=30)
                        if r.returncode != 0:
                            fails.append(f"RUN-FAIL {name} {optflag} rc={r.returncode}")
                except subprocess.TimeoutExpired:
                    fails.append(f"RUNTIME-TIMEOUT {name} {optflag}")
    return fails, stats


def main():
    print(f"mass fuzz: {len(CASES)} cases, seed={SEED}, workers={WORKERS}")
    failures = []
    stats = {"check": 0, "strict": 0, "clang": 0, "runtime": 0}
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for fails, s in pool.map(compile_case, CASES):
            failures.extend(fails)
            for k in stats:
                stats[k] += s[k]
    print(f"results: {stats['check']} compiler checks, "
          f"{stats['strict']} strict-mode checks, "
          f"{stats['clang']} differential clang checks, "
          f"{stats['runtime']} build+run, {len(failures)} failures")
    for f in failures[:30]:
        print(f)
    if failures:
        print(f"saved fixtures in {FAILS}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
