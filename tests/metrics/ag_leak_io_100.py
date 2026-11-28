#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of NON-MEMORY resource / I/O RAII cases (120 cases).

This is a differential measurement metric in the style of ``fuzz_raii_300.py``.
It generates small, self-contained C programs that acquire and release *non
heap-memory* resources (FILE streams, directory streams, mmap regions, dynamic
library handles, pthread mutexes, stdio buffering, atexit callbacks and
non-local exits) and checks them with the existing ``c+ check`` binary in both
DEFAULT and STRICT modes.

Generation and checking both run in thread pools.  ``pool.map`` preserves
input order and every case draws from ``random.Random(f"{SEED}:{case_index}")``,
so a run is fully reproducible regardless of ``FUZZ_WORKERS``.  Every program
carries a unique case marker, so no case is a copy of another.

Families (all target resource acquisition / release mismatches):

  fopen_leak           FILE* acquired, early return skips fclose       (strict)
  freopen_leak         freopen/second stream acquired, early return
                       skips fclose on one or both handles             (strict)
  opendir_leak         DIR* acquired, early return skips closedir      (strict)
  mmap_leak            mmap region acquired, early return skips munmap (strict)
  dlopen_leak          dlopen handle acquired, early return skips
                       dlclose                                        (strict)
  fclose_double        fclose called twice on the same FILE*           (reject)
  use_after_close      fprintf/fputc/fflush on a closed FILE*         (reject)
  fclose_null          fclose(NULL) / fclose of a null FILE*           (reject)
  unchecked_io         ignored fread/fwrite return values              (reject)
  mutex_no_unlock      lock held across an early return (no unlock)   (strict)
  mutex_relock         lock the same default mutex twice (deadlock)    (reject)
  mutex_destroy_locked pthread_mutex_destroy while still locked        (reject)
  setbuf_stack         setbuf/setvbuf handed a stack array that dangles(reject)
  pthread_cancel_cleanup thread cancelled with no cleanup handler     (reject)
  longjmp_skip         longjmp past fclose/free of a live resource    (strict)
  atexit_dangling      atexit callback closes/frees an already
                       released (or stack) context                    (reject)
  safe                 all paths release, null guards, balanced
                       mutex, static setbuf buffer, valid atexit      (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py
and tests/metrics/fuzz_raii_300.py:

  reject - proven resource error: must fail BOTH --default and --strict.
  strict - plain-C / conditional resource error: these are frequently only
           warnings in DEFAULT, so the case is required to fail --strict
           (a pass in DEFAULT is tolerated).
  accept - valid control: must pass BOTH modes (a rejection is a false
           positive).

Mismatches are printed, classified as false_negative / false_positive /
hard_error, and the generated source plus diagnostics are saved under
tests/out/core/ag_leak_io/failures/.  This is a differential measurement,
not a proof of safety.

Environment:
  FUZZ_SEED    default 0x4c45
  FUZZ_WORKERS default 8
  FUZZ_TIMEOUT default 60 seconds per check
  CPLUS_BIN    default <root>/build/c+ (existing binary; no rebuild needed)
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
OUT = ROOT / "tests/out/core/ag_leak_io"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4c45"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases, builder.  A case index maps deterministically
# to exactly one family in this order.
FAMILIES = (
    ("fopen_leak", 8, None),
    ("freopen_leak", 6, None),
    ("opendir_leak", 8, None),
    ("mmap_leak", 8, None),
    ("dlopen_leak", 6, None),
    ("fclose_double", 8, None),
    ("use_after_close", 8, None),
    ("fclose_null", 6, None),
    ("unchecked_io", 8, None),
    ("mutex_no_unlock", 8, None),
    ("mutex_relock", 6, None),
    ("mutex_destroy_locked", 6, None),
    ("setbuf_stack", 6, None),
    ("pthread_cancel_cleanup", 6, None),
    ("longjmp_skip", 6, None),
    ("atexit_dangling", 6, None),
    ("safe", 10, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag_leak_io {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: FILE* acquired, early return skips fclose.
# ---------------------------------------------------------------------------
def gen_fopen_leak(i, rng):
    v = rng.randint(2, 60)
    paths = (i % 3) + 1
    branches = " ".join(f'if(fprintf(f,"x{j}")<0)return {3 + j};'
                        for j in range(paths))
    body = (
        "#include <stdio.h>\n"
        f"int main(void){{FILE *f=fopen(\"agfo_{i}_{v}.txt\",\"w\");if(!f)return 2;"
        f"{branches} if(fclose(f)!=0)return 9;return 0;}}"
    )
    name = f"fopen_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 2: freopen / a second stream acquired, early return leaks a handle.
# ---------------------------------------------------------------------------
def gen_freopen_leak(i, rng):
    v = rng.randint(2, 40)
    t = rng.randint(1, 7)
    body = (
        "#include <stdio.h>\n"
        f"int main(int argc,char **argv){{(void)argv;"
        f"FILE *a=fopen(\"agfr_a_{i}.txt\",\"w\");if(!a)return 2;"
        f"FILE *g=freopen(\"agfr_b_{i}_{v}.txt\",\"w\",stdout);if(!g)return 3;"
        f"if(argc>{t})return 4;if(fprintf(g,\"x\")<0)return 5;"
        "fclose(g);fclose(a);return 0;}"
    )
    name = f"freopen_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 3: DIR* acquired, early return skips closedir.
# ---------------------------------------------------------------------------
def gen_opendir_leak(i, rng):
    paths = (i % 3) + 1
    branches = " ".join(f"if(!readdir(d))return {3 + j};"
                        for j in range(paths))
    body = (
        "#include <dirent.h>\n"
        f"int main(void){{DIR *d=opendir(\".\");if(!d)return 2;"
        f"{branches} closedir(d);return 0;}}"
    )
    name = f"opendir_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 4: mmap region acquired, early return skips munmap.
# ---------------------------------------------------------------------------
def gen_mmap_leak(i, rng):
    size = rng.choice([4096, 8192, 16384])
    t = rng.randint(1, 6)
    body = (
        "#include <sys/mman.h>\n"
        f"int main(int argc,char **argv){{(void)argv;"
        f"void *p=mmap(0,{size},PROT_READ,MAP_PRIVATE|MAP_ANON,-1,0);"
        f"if(p==MAP_FAILED)return 2;if(argc>{t})return 3;"
        f"munmap(p,{size});return 0;}}"
    )
    name = f"mmap_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 5: dlopen handle acquired, early return skips dlclose.
# ---------------------------------------------------------------------------
def gen_dlopen_leak(i, rng):
    t = rng.randint(1, 6)
    body = (
        "#include <dlfcn.h>\n"
        f"int main(int argc,char **argv){{(void)argv;"
        f"void *h=dlopen(\"libm.dylib\",RTLD_LAZY);if(!h)return 2;"
        f"if(!dlsym(h,\"sin\"))return 3;if(argc>{t})return 4;"
        "dlclose(h);return 0;}"
    )
    name = f"dlopen_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 6: fclose called twice on the same FILE*.
# ---------------------------------------------------------------------------
def gen_fclose_double(i, rng):
    v = rng.randint(1, 50)
    shape = i % 3
    if shape == 0:
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"agdc_{i}.txt\",\"w\");if(!f)return 2;"
            "fclose(f);fclose(f);return 0;}"
        )
    elif shape == 1:
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"agdc_{i}.txt\",\"w\");if(!f)return 2;"
            f"if(fprintf(f,\"{v}\")<0){{fclose(f);return 3;}}"
            "fclose(f);fclose(f);return 0;}"
        )
    else:
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"agdc_{i}.txt\",\"w\");if(!f)return 2;"
            "FILE *g=f;fclose(f);fclose(g);return 0;}"
        )
    name = f"fclose_double_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 7: use of a FILE* after fclose.
# ---------------------------------------------------------------------------
def gen_use_after_close(i, rng):
    v = rng.randint(2, 60)
    shape = i % 3
    if shape == 0:
        use = f'fprintf(f,"x{v}");'
    elif shape == 1:
        use = f"fputc('{chr(97 + i % 26)}',f);"
    else:
        use = "fflush(f);"
    body = (
        "#include <stdio.h>\n"
        f"int main(void){{FILE *f=fopen(\"aguac_{i}.txt\",\"w\");if(!f)return 2;"
        f"fclose(f);{use}return 0;}}"
    )
    name = f"use_after_close_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: fclose(NULL) / fclose of a null FILE* (undefined behaviour).
# ---------------------------------------------------------------------------
def gen_fclose_null(i, rng):
    shape = i % 3
    if shape == 0:
        body = "#include <stdio.h>\nint main(void){fclose(NULL);return 0;}"
    elif shape == 1:
        body = (
            "#include <stdio.h>\n"
            "int main(void){FILE *f=NULL;(void)f;fclose(f);return 0;}"
        )
    else:
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"agfn_{i}.txt\",\"w\");if(f)fclose(f);"
            "fclose(NULL);return 0;}"
        )
    name = f"fclose_null_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 9: ignored fread/fwrite return values.
# ---------------------------------------------------------------------------
def gen_unchecked_io(i, rng):
    n = rng.randint(4, 24)
    if i % 2 == 0:
        mode, call = "wb", f"fwrite(b,1,{n},f);"
    else:
        mode, call = "rb", f"fread(b,1,{n},f);"
    body = (
        "#include <stdio.h>\n"
        f"int main(void){{FILE *f=fopen(\"agio_{i}.txt\",\"{mode}\");if(!f)return 2;"
        f"char b[{n}];{call}fclose(f);return 0;}}"
    )
    name = f"unchecked_io_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 10: mutex locked, early return with no unlock.
# ---------------------------------------------------------------------------
def gen_mutex_no_unlock(i, rng):
    n = rng.randint(1, 9)
    body = (
        "#include <pthread.h>\n"
        f"static pthread_mutex_t m{i}=PTHREAD_MUTEX_INITIALIZER;\n"
        f"int main(void){{if(pthread_mutex_lock(&m{i})!=0)return 2;"
        f"if({n}>0)return 3;pthread_mutex_unlock(&m{i});return 0;}}"
    )
    name = f"mutex_no_unlock_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 11: repeated lock of the same default mutex (self-deadlock).
# ---------------------------------------------------------------------------
def gen_mutex_relock(i, rng):
    body = (
        "#include <pthread.h>\n"
        f"static pthread_mutex_t m{i}=PTHREAD_MUTEX_INITIALIZER;\n"
        f"int main(void){{pthread_mutex_lock(&m{i});"
        f"pthread_mutex_lock(&m{i});pthread_mutex_unlock(&m{i});return 0;}}"
    )
    name = f"mutex_relock_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 12: pthread_mutex_destroy while still locked.
# ---------------------------------------------------------------------------
def gen_mutex_destroy_locked(i, rng):
    body = (
        "#include <pthread.h>\n"
        f"int main(void){{pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;"
        "pthread_mutex_lock(&m);pthread_mutex_destroy(&m);return 0;}"
    )
    name = f"mutex_destroy_locked_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 13: setbuf/setvbuf given a stack array that dangles.
# ---------------------------------------------------------------------------
def gen_setbuf_stack(i, rng):
    n = rng.randint(16, 128)
    call = "setbuf(f,buf);" if i % 2 == 0 else f"setvbuf(f,buf,_IOFBF,{n});"
    body = (
        "#include <stdio.h>\n"
        f"int main(void){{char buf[{n}];FILE *f=fopen(\"agsb_{i}.txt\",\"w\");"
        f"if(!f)return 2;{call}if(fprintf(f,\"x\")<0){{fclose(f);return 3;}}"
        "fclose(f);return 0;}"
    )
    name = f"setbuf_stack_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 14: thread cancelled with no cleanup handler for its resource.
# ---------------------------------------------------------------------------
def gen_pthread_cancel_cleanup(i, rng):
    n = rng.randint(8, 32)
    if i % 2 == 0:
        body = (
            "#include <pthread.h>\n#include <stdlib.h>\n"
            f"static void *work{i}(void *a){{(void)a;int *p=malloc({n});"
            "if(!p)return 0;pthread_cancel(pthread_self());return p;}\n"
            f"int main(void){{pthread_t t;pthread_create(&t,0,work{i},0);"
            "pthread_join(t,0);return 0;}"
        )
    else:
        body = (
            "#include <pthread.h>\n#include <stdio.h>\n"
            f"static void *work{i}(void *a){{(void)a;"
            f"FILE *f=fopen(\"agpc_{i}.txt\",\"w\");if(!f)return 0;"
            "pthread_cancel(pthread_self());return f;}\n"
            f"int main(void){{pthread_t t;pthread_create(&t,0,work{i},0);"
            "pthread_join(t,0);return 0;}"
        )
    name = f"pthread_cancel_cleanup_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 15: longjmp past cleanup of a live FILE* / allocation.
# ---------------------------------------------------------------------------
def gen_longjmp_skip(i, rng):
    n = rng.randint(4, 24)
    if i % 2 == 0:
        body = (
            "#include <setjmp.h>\n#include <stdio.h>\n"
            f"static jmp_buf env{i};\n"
            f"static void deep{i}(void){{FILE *f=fopen(\"agl_{i}.txt\",\"w\");"
            f"if(!f)return;longjmp(env{i},1);fclose(f);}}\n"
            f"int main(void){{if(setjmp(env{i})==0)deep{i}();return 0;}}"
        )
    else:
        body = (
            "#include <setjmp.h>\n#include <stdlib.h>\n"
            f"static jmp_buf env{i};\n"
            f"static void deep{i}(void){{int *p=malloc({n});if(!p)return;"
            f"longjmp(env{i},1);free(p);}}\n"
            f"int main(void){{if(setjmp(env{i})==0)deep{i}();return 0;}}"
        )
    name = f"longjmp_skip_{i}"
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 16: atexit callback with a dangling context.
# ---------------------------------------------------------------------------
def gen_atexit_dangling(i, rng):
    n = rng.randint(8, 32)
    shape = i % 3
    if shape == 0:  # heap pointer freed before exit -> double free in callback
        body = (
            "#include <stdlib.h>\n"
            f"static int *g{i};\n"
            f"static void cb{i}(void){{free(g{i});}}\n"
            f"int main(void){{g{i}=malloc({n});if(!g{i})return 2;"
            f"atexit(cb{i});free(g{i});return 0;}}"
        )
    elif shape == 1:  # callback writes through a pointer to main's stack
        body = (
            "#include <stdlib.h>\n"
            f"static int *g{i};\n"
            f"static void cb{i}(void){{*g{i}=1;}}\n"
            f"int main(void){{int local=0;g{i}=&local;atexit(cb{i});"
            "return local;}"
        )
    else:  # FILE* closed before exit -> double close in callback
        body = (
            "#include <stdio.h>\n#include <stdlib.h>\n"
            f"static FILE *g{i};\n"
            f"static void cb{i}(void){{fclose(g{i});}}\n"
            f"int main(void){{g{i}=fopen(\"agae_{i}.txt\",\"w\");if(!g{i})return 2;"
            f"atexit(cb{i});fclose(g{i});return 0;}}"
        )
    name = f"atexit_dangling_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 17: safe controls (all paths release, null guards, balanced locks).
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    n = rng.randint(8, 32)
    v = rng.randint(2, 50)
    shape = i % 5
    if shape == 0:  # every explicit return releases the FILE*
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"agsf_{i}.txt\",\"w\");if(!f)return 2;"
            f"if(fprintf(f,\"{v}\")<0){{fclose(f);return 3;}}"
            "fclose(f);return 0;}"
        )
    elif shape == 1:  # null guard
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"agsf_{i}.txt\",\"w\");if(!f)return 2;"
            "if(f){fprintf(f,\"x\");fclose(f);}return 0;}"
        )
    elif shape == 2:  # balanced mutex lock/unlock with a null guard
        body = (
            "#include <pthread.h>\n"
            f"static pthread_mutex_t m{i}=PTHREAD_MUTEX_INITIALIZER;\n"
            f"int main(void){{if(pthread_mutex_lock(&m{i})!=0)return 2;"
            f"pthread_mutex_unlock(&m{i});return 0;}}"
        )
    elif shape == 3:  # setbuf with a static buffer + checked fwrite
        body = (
            "#include <stdio.h>\n"
            "static char buf[64];\n"
            f"int main(void){{FILE *f=fopen(\"agsf_{i}.txt\",\"wb\");if(!f)return 2;"
            f"char b[{n}];setbuf(f,buf);"
            f"if(fwrite(b,1,{n},f)!={n}){{fclose(f);return 3;}}"
            "fclose(f);return 0;}"
        )
    else:  # atexit with valid static context + checked fwrite
        body = (
            "#include <stdio.h>\n#include <stdlib.h>\n"
            f"static int g{i};\n"
            f"static void cb{i}(void){{g{i}=1;}}\n"
            f"int main(void){{FILE *f=fopen(\"agsf_{i}.txt\",\"wb\");if(!f)return 2;"
            f"char b[{n}];atexit(cb{i});"
            f"if(fwrite(b,1,{n},f)!={n}){{fclose(f);return 3;}}"
            "fclose(f);return 0;}"
        )
    name = f"safe_{i}"
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_fopen_leak,
    gen_freopen_leak,
    gen_opendir_leak,
    gen_mmap_leak,
    gen_dlopen_leak,
    gen_fclose_double,
    gen_use_after_close,
    gen_fclose_null,
    gen_unchecked_io,
    gen_mutex_no_unlock,
    gen_mutex_relock,
    gen_mutex_destroy_locked,
    gen_setbuf_stack,
    gen_pthread_cancel_cleanup,
    gen_longjmp_skip,
    gen_atexit_dangling,
    gen_safe,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, _builder), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body, expect = builder(i, rng)
            return name, body, expect, family
        start += count
    raise IndexError(i)


# ---------------------------------------------------------------------------
# Verification: check each case in DEFAULT and STRICT.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:220]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:220] if lines else "(no diagnostics)"


def check_case(case):
    name, body, expect, family = case
    path = STAGE / f"{name}.c"
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, f"TIMEOUT after {TIMEOUT}s")
            hard.append(f"{mode}: TIMEOUT after {TIMEOUT}s")
            continue
        except OSError as exc:
            modes[mode] = (False, f"OS ERROR: {exc}")
            hard.append(f"{mode}: OS ERROR: {exc}")
            continue
        if proc.returncode < 0:
            hard.append(f"{mode}: CRASH rc={proc.returncode}")
        modes[mode] = (proc.returncode != 0,
                       (proc.stderr + proc.stdout).strip())

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = []
    kind = None
    if hard:
        reasons.extend(hard)
        kind = "hard_error"
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
            kind = kind or "false_negative"
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
            kind = kind or "false_negative"
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
            kind = kind or "false_negative"
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT")
            kind = "false_positive"
        if s_rej:
            reasons.append("safe rejected in STRICT")
            kind = "false_positive"

    if not reasons:
        return None

    (FAILS / f"{name}.c").write_text(body)
    log = [
        f"case: {name}",
        f"family: {family}",
        f"expect: {expect}",
        f"kind: {kind}",
        f"reasons: {'; '.join(reasons)}",
        f"--default rejected={d_rej}: {first_diag(modes['--default'][1])}",
        f"--strict rejected={s_rej}: {first_diag(modes['--strict'][1])}",
        "",
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / f"{name}.diag.txt").write_text("\n".join(log))
    line = (f"{name} [{family}] expect={expect} kind={kind}: {'; '.join(reasons)} | "
            f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
            f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}")
    return kind, line


def main():
    global STAGE
    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the output.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    print(f"ag_leak_io: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag_leak_io_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))
    mismatches = [r for r in results if r]
    mismatches.sort(key=lambda r: r[1])
    kinds = collections.Counter(r[0] for r in mismatches)
    false_positives = kinds["false_positive"]
    false_negatives = kinds["false_negative"]
    hard_errors = kinds["hard_error"]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches")
    print(f"false_negatives: {false_negatives}  "
          f"false_positives: {false_positives}  hard_errors: {hard_errors}")
    for _kind, line in mismatches[:12]:
        print("  " + line)
    if len(mismatches) > 12:
        print(f"  ... {len(mismatches) - 12} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
