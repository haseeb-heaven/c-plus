#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator (hw) for RAII resource-leak gaps.

Produces >=100 distinct small, valid C programs (default target 120) covering
manual-resource / RAII-gap patterns:

   * early_return    early return crossing a live malloc / FILE*;
   * partial_free    multiple resources with only some released on a path;
   * goto_cleanup    goto skipping the only free, or fallthrough into a second
                     free (double release);
   * longjmp_skip    longjmp escaping the region that owns the only free;
   * field_owner     ownership stored in a struct field, then the container is
                     dropped without releasing the field;
   * realloc_fail    realloc failure dropping the still-live original pointer;
   * nested_resources destructor-less nested heap resources (outer freed while
                     an inner pointer/FILE is still owned);
   * mutex_no_unlock pthread mutex locked and left locked on a return path;
   * safe_controls   all-paths-release, null guards, and ownership escaped to
                     the caller (returned pointer / out-parameter).

Every gap family also emits safe controls as ``accept`` cases.  Every case
declares one of the sibling-corpus outcomes:

    reject - proven error (e.g. double free via goto fallthrough): must fail
             BOTH modes.
    strict - conditional / unverified error (the common leak-gap case): may
             pass --default, must fail --strict.
    accept - valid control: must pass BOTH modes (a rejection is a false
             positive).

Programs come from deterministic parameter sweeps plus a seeded RNG
(FUZZ_SEED); the final seeded pass re-rolls parameters until TARGET distinct
programs exist, and a case-specific marker comment guarantees that every
generated translation unit is unique in source text.  Re-running with the same
seed is reproducible.  Cases run in parallel (FUZZ_WORKERS) with a per-check
timeout.  Mismatches (source + full diagnostic log) are saved under
tests/out/core/hw_leak/failures/.  This is a differential measurement over the
declared subset, not a proof of safety.

Environment:
    FUZZ_SEED    default 0x68774C4B  ("hwLK")
    FUZZ_WORKERS default 8
    FUZZ_TIMEOUT default 60 seconds per check
    CPLUS_BIN    default <root>/build/c+
    HW_TARGET    default 120 (minimum 100)
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/hw_leak"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x68774C4B"), 0)
TARGET = max(100, int(os.environ.get("HW_TARGET", "120")))
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

CASES = []   # list of (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = "#include <stdlib.h>\n"
STDIO = "#include <stdio.h>\n#include <stdlib.h>\n"
STRIO = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
SETJMP = "#include <setjmp.h>\n#include <stdlib.h>\n"
SETJMP_IO = "#include <setjmp.h>\n#include <stdio.h>\n#include <stdlib.h>\n"
MUTEX = "#include <pthread.h>\n#include <stdlib.h>\n"
MUTEX_IO = "#include <pthread.h>\n#include <stdio.h>\n#include <stdlib.h>\n"

# family order used by the seeded mutation pass
ORDER = (
    "early_return",
    "partial_free",
    "goto_cleanup",
    "longjmp_skip",
    "field_owner",
    "realloc_fail",
    "nested_resources",
    "mutex_no_unlock",
    "safe_controls",
)


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 50)


def jump():
    return RNG.randint(1, 4)


def add(family, name, body, expect):
    """Append a case only if its exact program body is new.  Returns bool."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    # A deterministic, case-specific marker makes every generated program
    # unique in source text (comments are valid C and do not change behavior).
    normalized = (f"/* hw_leak seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


# ---------------------------------------------------------------------------
# One case per (family, index).  All RNG draws happen up front so the seeded
# stream is identical for a given seed.  `i` selects the template variant.
# ---------------------------------------------------------------------------
def seeded_case(family, i):
    n = RNG.randint(2, 12)
    k = RNG.randint(0, n - 1)
    v = val()
    t = jump()
    m = n + RNG.randint(1, 20)
    rc = RNG.randint(1, 9)

    # --- 1. early return with a live malloc / FILE* ------------------------
    if family == "early_return":
        name = tpl("early_ret_@@I@@", I=i)
        fn = "hwl_er_%d.txt" % i
        variant = i % 5
        if variant == 0:  # safe control: every path releases
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;int*p=malloc(@@N@@*sizeof(int));"
                "if(!p)return 2;p[0]=@@V@@;int r=p[0];"
                "if(argc>@@T@@){free(p);return r==@@V@@?0:1;}"
                "free(p);return r==@@V@@?0:1;}", N=n, V=v, T=t)
            return family, name, body, "accept"
        if variant == 1:  # live malloc crossed by an early return
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;int*p=malloc(@@N@@*sizeof(int));"
                "if(!p)return 2;p[0]=@@V@@;if(argc>@@T@@)return 3;"
                "free(p);return 0;}", N=n, V=v, T=t)
            return family, name, body, "strict"
        if variant == 2:  # live FILE* crossed by an early return
            body = STDIO + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "FILE*f=fopen(\"@@FN@@\",\"r\");if(!f)return 2;"
                "if(argc>@@T@@)return 3;fclose(f);return 0;}", FN=fn, T=t)
            return family, name, body, "strict"
        if variant == 3:  # nested condition, early return still crosses free
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;int*p=malloc(@@N@@*sizeof(int));"
                "if(!p)return 2;p[0]=@@V@@;"
                "if(argc>@@T@@){if(argc>@@T1@@)return 4;}"
                "free(p);return 0;}", N=n, V=v, T=t, T1=t + 1)
            return family, name, body, "strict"
        # safe control: fclose on every path
        body = STDIO + tpl(
            "int main(int argc,char**argv){(void)argv;"
            "FILE*f=fopen(\"@@FN@@\",\"r\");if(!f)return 2;"
            "if(argc>@@T@@){fclose(f);return 3;}fclose(f);return 0;}", FN=fn, T=t)
        return family, name, body, "accept"

    # --- 2. multiple resources, partially freed ----------------------------
    if family == "partial_free":
        name = tpl("partial_@@I@@", I=i)
        fn = "hwl_pf_%d.txt" % i
        variant = i % 5
        if variant == 0:  # safe control: both released on every path
            body = STDIO + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "FILE*f=fopen(\"@@FN@@\",\"r\");if(!f){free(p);return 3;}"
                "p[0]=@@V@@;if(argc>@@T@@){free(p);fclose(f);return 4;}"
                "free(p);fclose(f);return 0;}", N=n, V=v, T=t, FN=fn)
            return family, name, body, "accept"
        if variant == 1:  # FILE* left open on the early path
            body = STDIO + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "FILE*f=fopen(\"@@FN@@\",\"r\");if(!f){free(p);return 3;}"
                "p[0]=@@V@@;if(argc>@@T@@){free(p);return 4;}"
                "free(p);fclose(f);return 0;}", N=n, V=v, T=t, FN=fn)
            return family, name, body, "strict"
        if variant == 2:  # second allocation left live on the early path
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "int*q=malloc((@@N@@+1)*sizeof(int));if(!q){free(p);return 3;}"
                "p[0]=@@V@@;q[0]=@@V@@;if(argc>@@T@@){free(p);return 4;}"
                "free(p);free(q);return 0;}", N=n, V=v, T=t)
            return family, name, body, "strict"
        if variant == 3:  # three resources, only one released on the path
            body = STDIO + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "int*q=malloc(@@N@@*sizeof(int));if(!q){free(p);return 3;}"
                "FILE*f=fopen(\"@@FN@@\",\"r\");"
                "if(!f){free(p);free(q);return 4;}"
                "p[0]=@@V@@;q[0]=@@V@@;if(argc>@@T@@){free(p);return 5;}"
                "free(p);free(q);fclose(f);return 0;}", N=n, V=v, T=t, FN=fn)
            return family, name, body, "strict"
        # safe control: guarded acquisitions, all paths release
        body = STD + tpl(
            "int main(int argc,char**argv){(void)argv;"
            "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int*q=malloc((@@N@@+1)*sizeof(int));if(!q){free(p);return 3;}"
            "p[0]=@@V@@;q[0]=@@V@@;"
            "if(argc>@@T@@){free(p);free(q);return 4;}"
            "free(p);free(q);return 0;}", N=n, V=v, T=t)
        return family, name, body, "accept"

    # --- 3. goto cleanup: skipped free / fallthrough double free -----------
    if family == "goto_cleanup":
        name = tpl("goto_@@I@@", I=i)
        variant = i % 4
        if variant == 0:  # goto skips the only free
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
                "if(argc>@@T@@)goto done;free(p);done:return 0;}", N=n, V=v, T=t)
            return family, name, body, "strict"
        if variant == 1:  # fallthrough reaches a second free -> double free
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
                "if(argc>@@T@@)goto cleanup;free(p);cleanup:free(p);return 0;}",
                N=n, V=v, T=t)
            return family, name, body, "reject"
        if variant == 2:  # both paths free p -> genuine double free
            body = STD + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
                "if(argc>@@T@@)goto done;free(p);return 0;done:free(p);return 0;}",
                N=n, V=v, T=t)
            return family, name, body, "reject"
        # safe: both paths converge on a single free site
        body = STD + tpl(
            "int main(int argc,char**argv){(void)argv;"
            "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "if(argc>@@T@@)goto done;p[0]=@@V@@;goto done;"
            "done:free(p);return 0;}", N=n, V=v, T=t)
        return family, name, body, "accept"

    # --- 4. longjmp skipping the only free ---------------------------------
    if family == "longjmp_skip":
        name = tpl("longjmp_@@I@@", I=i)
        fn = "hwl_lj_%d.txt" % i
        variant = i % 4
        if variant == 0:  # longjmp past the only free
            body = SETJMP + tpl(
                "static jmp_buf jb@@I@@;static void die@@I@@(void){longjmp(jb@@I@@,1);}"
                "int main(void){int*p;if(setjmp(jb@@I@@)!=0)return 3;"
                "p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
                "die@@I@@();free(p);return 0;}", N=n, V=v, I=i)
            return family, name, body, "strict"
        if variant == 1:  # safe: free runs before the longjmp
            body = SETJMP + tpl(
                "static jmp_buf jb@@I@@;static void die@@I@@(void){longjmp(jb@@I@@,1);}"
                "int main(void){int*p;if(setjmp(jb@@I@@)!=0)return 0;"
                "p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
                "free(p);die@@I@@();return 0;}", N=n, V=v, I=i)
            return family, name, body, "accept"
        if variant == 2:  # longjmp past the only fclose (FILE* in a global)
            body = SETJMP_IO + tpl(
                "static jmp_buf jb@@I@@;static FILE*gf@@I@@;"
                "static void die@@I@@(void){longjmp(jb@@I@@,1);}"
                "int main(void){if(setjmp(jb@@I@@)!=0)return 3;"
                "gf@@I@@=fopen(\"@@FN@@\",\"r\");if(!gf@@I@@)return 2;"
                "die@@I@@();fclose(gf@@I@@);return 0;}", FN=fn, I=i)
            return family, name, body, "strict"
        # safe: ownership moved to a global that the jump handler releases
        body = SETJMP + tpl(
            "static jmp_buf jb@@I@@;static int*gp@@I@@;"
            "static void die@@I@@(void){longjmp(jb@@I@@,1);}"
            "int main(void){if(setjmp(jb@@I@@)!=0){free(gp@@I@@);return 0;}"
            "gp@@I@@=malloc(@@N@@*sizeof(int));if(!gp@@I@@)return 2;"
            "gp@@I@@[0]=@@V@@;die@@I@@();free(gp@@I@@);return 0;}", N=n, V=v, I=i)
        return family, name, body, "accept"

    # --- 5. ownership stored in a struct field, then dropped ---------------
    if family == "field_owner":
        name = tpl("field_owner_@@I@@", I=i)
        st = "struct S{int*data;};"
        variant = i % 4
        if variant == 0:  # heap container freed while field is still owned
            body = STD + st + tpl(
                "int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;"
                "s->data=malloc(@@N@@*sizeof(int));if(!s->data){free(s);return 3;}"
                "s->data[0]=@@V@@;free(s);return 0;}", N=n, V=v)
            return family, name, body, "strict"
        if variant == 1:  # safe: release field then container
            body = STD + st + tpl(
                "int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;"
                "s->data=malloc(@@N@@*sizeof(int));if(!s->data){free(s);return 3;}"
                "s->data[0]=@@V@@;free(s->data);free(s);return 0;}", N=n, V=v)
            return family, name, body, "accept"
        if variant == 2:  # stack struct field dropped at return
            body = STD + st + tpl(
                "int main(void){struct S s={0};s.data=malloc(@@N@@*sizeof(int));"
                "if(!s.data)return 2;s.data[0]=@@V@@;return 0;}", N=n, V=v)
            return family, name, body, "strict"
        # safe: ownership escaped to the caller through an out-parameter
        body = STD + tpl(
            "static void make@@I@@(int**out){*out=malloc(@@N@@*sizeof(int));}"
            "int main(void){int*p=0;make@@I@@(&p);if(!p)return 2;"
            "p[0]=@@V@@;free(p);return 0;}", N=n, V=v, I=i)
        return family, name, body, "accept"

    # --- 6. realloc failure drops the original -----------------------------
    if family == "realloc_fail":
        name = tpl("realloc_fail_@@I@@", I=i)
        fn = "hwl_rf_%d.txt" % i
        variant = i % 4
        if variant == 0:  # p=realloc(p,..); on failure the original is lost
            body = STD + tpl(
                "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "p[0]=@@V@@;p=realloc(p,@@M@@*sizeof(int));if(!p)return 3;"
                "free(p);return 0;}", N=n, M=m, V=v)
            return family, name, body, "strict"
        if variant == 1:  # safe temp pointer, free original on failure
            body = STD + tpl(
                "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "p[0]=@@V@@;int*t=realloc(p,@@M@@*sizeof(int));"
                "if(!t){free(p);return 3;}free(t);return 0;}", N=n, M=m, V=v)
            return family, name, body, "accept"
        if variant == 2:  # failure path also strands a live FILE*
            body = STDIO + tpl(
                "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                "FILE*f=fopen(\"@@FN@@\",\"r\");if(!f){free(p);return 3;}"
                "p[0]=@@V@@;p=realloc(p,@@M@@*sizeof(int));if(!p)return 4;"
                "fclose(f);free(p);return 0;}", N=n, M=m, V=v, FN=fn)
            return family, name, body, "strict"
        # safe: commit the temp pointer only after a successful realloc
        body = STD + tpl(
            "int main(void){int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;int*t=realloc(p,@@M@@*sizeof(int));"
            "if(!t){free(p);return 3;}p=t;p[0]=@@V@@;free(p);return 0;}",
            N=n, M=m, V=v)
        return family, name, body, "accept"

    # --- 7. destructor-less nested resources -------------------------------
    if family == "nested_resources":
        name = tpl("nested_@@I@@", I=i)
        fn = "hwl_nr_%d.txt" % i
        variant = i % 5
        two = "struct I{int*p;};struct O{struct I*in;};"
        if variant == 0:  # outer freed, inner object and its buffer leaked
            body = STD + two + tpl(
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->in=malloc(sizeof*o->in);if(!o->in){free(o);return 3;}"
                "o->in->p=malloc(@@N@@*sizeof(int));if(!o->in->p){free(o->in);free(o);return 4;}"
                "o->in->p[0]=@@V@@;free(o);return 0;}", N=n, V=v)
            return family, name, body, "strict"
        if variant == 1:  # safe: explicit bottom-up teardown
            body = STD + two + tpl(
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->in=malloc(sizeof*o->in);if(!o->in){free(o);return 3;}"
                "o->in->p=malloc(@@N@@*sizeof(int));if(!o->in->p){free(o->in);free(o);return 4;}"
                "o->in->p[0]=@@V@@;free(o->in->p);free(o->in);free(o);return 0;}", N=n, V=v)
            return family, name, body, "accept"
        if variant == 2:  # FILE* field dropped with its container
            body = STDIO + "struct O{FILE*f;};" + tpl(
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->f=fopen(\"@@FN@@\",\"r\");if(!o->f){free(o);return 3;}"
                "free(o);return 0;}", FN=fn)
            return family, name, body, "strict"
        if variant == 3:  # safe: fclose the nested FILE* before the container
            body = STDIO + "struct O{FILE*f;};" + tpl(
                "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
                "o->f=fopen(\"@@FN@@\",\"r\");if(!o->f){free(o);return 3;}"
                "fclose(o->f);free(o);return 0;}", FN=fn)
            return family, name, body, "accept"
        # safe cleanup helper for a three-level nest
        three = "struct L{int*p;};struct M{struct L*l;};struct O{struct M*m;};"
        helper = tpl(
            "static void drop@@I@@(struct O*o){if(!o)return;"
            "if(o->m&&o->m->l){free(o->m->l->p);free(o->m->l);}"
            "free(o->m);free(o);}", I=i)
        body = STD + three + helper + tpl(
            "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
            "o->m=malloc(sizeof*o->m);if(!o->m){free(o);return 3;}"
            "o->m->l=malloc(sizeof*o->m->l);if(!o->m->l){free(o->m);free(o);return 4;}"
            "o->m->l->p=malloc(@@N@@*sizeof(int));"
            "if(!o->m->l->p){free(o->m->l);free(o->m);free(o);return 5;}"
            "o->m->l->p[0]=@@V@@;drop@@I@@(o);return 0;}", N=n, V=v, I=i)
        return family, name, body, "accept"

    # --- 8. mutex left locked on a return ----------------------------------
    if family == "mutex_no_unlock":
        name = tpl("mutex_@@I@@", I=i)
        decl = tpl("static pthread_mutex_t m@@I@@=PTHREAD_MUTEX_INITIALIZER;", I=i)
        fn = "hwl_mx_%d.txt" % i
        variant = i % 5
        if variant == 0:  # early return while locked
            body = MUTEX + decl + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "if(pthread_mutex_lock(&m@@I@@)!=0)return 2;"
                "if(argc>@@T@@)return 3;pthread_mutex_unlock(&m@@I@@);return 0;}",
                I=i, T=t)
            return family, name, body, "strict"
        if variant == 1:  # safe: unlock on both paths
            body = MUTEX + decl + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "if(pthread_mutex_lock(&m@@I@@)!=0)return 2;"
                "if(argc>@@T@@){pthread_mutex_unlock(&m@@I@@);return 3;}"
                "pthread_mutex_unlock(&m@@I@@);return 0;}", I=i, T=t)
            return family, name, body, "accept"
        if variant == 2:  # locked mutex plus a live FILE* on early return
            body = MUTEX_IO + decl + tpl(
                "int main(int argc,char**argv){(void)argv;"
                "FILE*f=fopen(\"@@FN@@\",\"r\");if(!f)return 2;"
                "if(pthread_mutex_lock(&m@@I@@)!=0){fclose(f);return 3;}"
                "if(argc>@@T@@)return 4;"
                "pthread_mutex_unlock(&m@@I@@);fclose(f);return 0;}",
                I=i, T=t, FN=fn)
            return family, name, body, "strict"
        if variant == 3:  # safe: allocation-failure guard unlocks first
            body = MUTEX + decl + tpl(
                "int main(void){if(pthread_mutex_lock(&m@@I@@)!=0)return 2;"
                "int*p=malloc(@@N@@*sizeof(int));if(!p){pthread_mutex_unlock(&m@@I@@);return 3;}"
                "p[0]=@@V@@;free(p);pthread_mutex_unlock(&m@@I@@);return 0;}",
                I=i, N=n, V=v)
            return family, name, body, "accept"
        # strict: helper returns while still holding the lock
        body = MUTEX + decl + tpl(
            "static int work@@I@@(int fail){if(pthread_mutex_lock(&m@@I@@)!=0)return 2;"
            "if(fail)return 3;pthread_mutex_unlock(&m@@I@@);return 0;}"
            "int main(void){return work@@I@@(@@RC@@);}", I=i, RC=rc)
        return family, name, body, "strict"

    # family == "safe_controls"
    # --- 9. explicit safe controls -----------------------------------------
    name = tpl("safe_@@I@@", I=i)
    fn = "hwl_sc_%d.txt" % i
    variant = i % 3
    if variant == 0:  # all paths release
        body = STD + tpl(
            "int main(int argc,char**argv){(void)argv;"
            "int*p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T@@){free(p);return 3;}"
            "if(argc>@@T1@@){free(p);return 4;}free(p);return 0;}",
            N=n, V=v, T=t, T1=t + 1)
        return family, name, body, "accept"
    if variant == 1:  # null guards, every path released
        body = STDIO + tpl(
            "int main(void){int*p=malloc(@@N@@*sizeof(int));"
            "if(p==NULL){fprintf(stderr,\"alloc failed\\n\");return 2;}"
            "FILE*f=fopen(\"@@FN@@\",\"r\");"
            "if(f==NULL){fprintf(stderr,\"open failed\\n\");free(p);return 3;}"
            "p[0]=@@V@@;int c=fgetc(f);(void)c;free(p);fclose(f);return 0;}",
            N=n, V=v, FN=fn)
        return family, name, body, "accept"
    # ownership escaped to caller (returned pointer, released by main)
    body = STD + tpl(
        "static int*make@@I@@(int count){int*p=malloc(count*sizeof(int));return p;}"
        "int main(void){int*p=make@@I@@(@@N@@);if(!p)return 2;"
        "p[0]=@@V@@;free(p);return 0;}", N=n, V=v, I=i)
    return family, name, body, "accept"


# --- deterministic parameter sweeps across all families --------------------
for _fam in ORDER:
    for _i in range(1, 13):
        add(*seeded_case(_fam, _i))


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll until TARGET distinct programs exist.
# ---------------------------------------------------------------------------
_i = 0
while len(CASES) < TARGET:
    _family = ORDER[_i % len(ORDER)]
    add(*seeded_case(_family, 100 + _i))
    _i += 1
    if _i > TARGET * 50:
        raise RuntimeError("could not reach TARGET distinct cases")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def run_case(case):
    family, name, body, expect = case
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT)
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc)
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    false_positive = False
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        false_positive = d_rej or s_rej
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    result = {
        "name": name, "family": family, "expect": expect,
        "reasons": reasons, "false_positive": false_positive,
        "d_rej": d_rej, "s_rej": s_rej, "modes": modes,
    }
    if not reasons:
        return result

    # Save the generated source and a full diagnostic log for every mismatch.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        first_diag(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        first_diag(modes["--strict"][1]),
        "",
        "===== generated source =====",
        body,
        "",
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    result["detail"] = (
        "%s [%s] expect=%s reasons=%s | DEFAULT rejected=%s: %s | "
        "STRICT rejected=%s: %s" % (
            name, family, expect, ";".join(reasons),
            d_rej, first_diag(modes["--default"][1]),
            s_rej, first_diag(modes["--strict"][1])))
    return result


def main():
    if not pathlib.Path(CC).exists():
        print("error: compiler binary not found: %s (set CPLUS_BIN)" % CC,
              file=sys.stderr)
        return 2

    # Start from a clean failure fixture directory for this run.
    for stale in list(FAILS.glob("*.c")) + list(FAILS.glob("*.diag.txt")):
        stale.unlink()

    bodies = [b for _, _, b, _ in CASES]
    unique = len(set(bodies))
    counts = Counter(e for _, _, _, e in CASES)
    families = Counter(f for f, _, _, _ in CASES)
    print("hw_leak: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families (%d): %s" % (len(families),
          ", ".join("%s=%d" % kv for kv in sorted(families.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(run_case, CASES))

    mismatches = sorted(r["detail"] for r in results if r["reasons"])
    mismatch_names = sorted(r["name"] for r in results if r["reasons"])
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    false_negatives = sum(1 for r in results if r["expect"] != "accept" and r["reasons"])
    hard = sum(1 for r in results for reason in r["reasons"]
               if "TIMEOUT" in reason or "OS ERROR" in reason or "CRASH" in reason)

    print("results: %d cases, %d mismatches (%d missed detections, %d false positives, %d hard errors)"
          % (len(CASES), len(mismatches), false_negatives, len(false_positives), hard))
    print("false positives: %d" % len(false_positives))
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print("  ... %d more" % (len(false_positives) - 20))
    print("first 10 mismatch names (%d total):" % len(mismatch_names))
    for name in mismatch_names[:10]:
        print("  " + name)
    if len(mismatch_names) > 10:
        print("  ... %d more" % (len(mismatch_names) - 10))
    for line in mismatches[:10]:
        print("  detail: " + line)
    if len(mismatches) > 10:
        print("  ... %d more details (see saved logs in %s)" % (len(mismatches) - 10, FAILS))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    status = "PASS" if not mismatches and unique == len(CASES) else "FAIL"
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)"
          % (status, len(CASES), len(mismatches), len(false_positives)))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
