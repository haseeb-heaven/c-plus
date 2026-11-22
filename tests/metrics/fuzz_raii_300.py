#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL RAII / resource-lifetime fuzz generator (360 cases).

Every generated program is a small C (or C+ RAII) program built from a
per-case independent seeded RNG.  Generation and checking are both carried out
in thread pools; ``pool.map`` preserves input order, and each case draws from
``random.Random(f"{FUZZ_SEED}:{case_index}")``, so a run is fully reproducible
regardless of ``FUZZ_WORKERS``.  No case is copied from another corpus.

Families (all target resource acquisition / release mismatches):

  early_leak       malloc/calloc/fopen/opendir, then 1-3 early returns that
                   skip the matching free/fclose/closedir  (strict)
  partial_cleanup  resource acquired inside nested if / switch / loop bodies
                   and released on only some paths          (strict)
  mutex            lock without unlock on an early return, repeated lock,
                   and mutex_destroy while still locked     (strict/reject)
  callee_free      resource handed to a function that frees it; caller keeps
                   using the stale handle                    (reject)
  multi_resource   two live resources with a path that frees only one
                   (or frees both and then uses them)        (strict)
  longjmp          setjmp/longjmp skipping cleanup of a live allocation,
                   plus clean longjmp controls               (strict/accept)
  double_release   double free, double cplus_release, use-after-close,
                   double fclose                             (reject)
  safe             all-path release, null guards, ownership escaped via
                   return / out-parameter, C+ resource + managed RAII (accept)
  seeded_mut       seeded composition of the patterns above  (mixed)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject - proven lifetime error: must fail BOTH --default and --strict.
  strict - plain-C / conditional lifetime error: plain-C leaks are often only
           warnings in DEFAULT, so these are required to fail --strict
           (a pass in DEFAULT is tolerated).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus diagnostics are saved
under tests/out/core/fuzz_raii/failures/.  This is a differential measurement,
not a proof of safety.

Environment:
  FUZZ_SEED    default 0x5411
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
OUT = ROOT / "tests/out/core/fuzz_raii"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x5411"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases, builder.  Families are laid out in this order;
# a case index maps deterministically to exactly one family.
FAMILIES = (
    ("early_leak", 96, None),
    ("partial_cleanup", 48, None),
    ("mutex", 24, None),
    ("callee_free", 24, None),
    ("multi_resource", 48, None),
    ("longjmp", 24, None),
    ("double_release", 32, None),
    ("safe", 40, None),
    ("seeded_mut", 24, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* fuzz_raii {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: acquired then early return without release (1-3 paths).
# ---------------------------------------------------------------------------
def gen_early_leak(i, rng):
    n = rng.randint(4, 24)
    t = rng.randint(1, 8)
    v = rng.randint(2, 40)
    kind = i % 4           # malloc / calloc / fopen / opendir
    paths = (i // 4) % 3 + 1  # 1..3 early returns
    kname = ("malloc", "calloc", "fopen", "opendir")[kind]
    name = f"early_{kname}_{paths}_{i}"

    if kind in (0, 1):
        alloc = (f"malloc({n}*sizeof(int))" if kind == 0
                 else f"calloc({n},sizeof(int))")
        init = " ".join(f"p[{j}]={v + j};" for j in range(3))
        branches = " ".join(f"if(p[{j}]>{t + j})return {3 + j};"
                            for j in range(paths))
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p={alloc};if(!p)return 2;{init} {branches} "
            "free(p);return 0;}"
        )
    elif kind == 2:
        branches = " ".join(f'if(fprintf(f,"x{j}")<0)return {3 + j};'
                            for j in range(paths))
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"raii_{i}.txt\",\"w\");if(!f)return 2;"
            f"{branches} if(fclose(f)!=0)return 9;return 0;}}"
        )
    else:
        branches = " ".join(f"if(!readdir(d))return {3 + j};"
                            for j in range(paths))
        body = (
            "#include <dirent.h>\n"
            f"int main(void){{DIR *d=opendir(\".\");if(!d)return 2;"
            f"{branches} closedir(d);return 0;}}"
        )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 2: resource acquired inside nested if / switch / loop, released on
# only some paths.
# ---------------------------------------------------------------------------
def gen_partial_cleanup(i, rng):
    shape = i % 3          # nested_if / switch / loop
    variant = (i // 3) % 2  # conditional use-after-free / skipped free
    n = rng.randint(4, 20)
    t = rng.randint(1, 6)
    v = rng.randint(2, 48)
    sname = ("nested", "switch", "loop")[shape]
    name = f"partial_{sname}_{variant}_{i}"

    if shape == 0:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;if(argc>{t}){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;*p={v};"
                f"if(argc>{t + 1})free(p);return *p;}}return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;if(argc>{t}){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;*p={v};"
                f"if(argc>{t + 1})return 1;free(p);}}return 0;}}"
            )
    elif shape == 1:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;switch(argc){{"
                f"case 1:{{int *p=malloc({n}*sizeof(int));if(!p)return 2;*p={v};"
                f"if(argc>2)free(p);return *p;}}"
                f"case 2:break;default:break;}}return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;switch(argc){{"
                f"case 1:{{int *p=malloc({n}*sizeof(int));if(!p)return 2;*p={v};"
                f"if(argc>2)return 1;free(p);return 0;}}"
                f"case 2:break;default:break;}}return 0;}}"
            )
    else:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{for(int j=0;j<{n};j++){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;*p={v};"
                f"if(j=={t})free(p);else return *p;}}return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{for(int j=0;j<{n};j++){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;*p={v};"
                f"if(j=={t})return 1;free(p);}}return 0;}}"
            )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 3: mutex lifetime misuse.
# ---------------------------------------------------------------------------
def gen_mutex(i, rng):
    shape = i % 3
    n = rng.randint(2, 16)
    name = f"mutex_{shape}_{i}"
    if shape == 0:  # locked, early return, unlock unreachable -> strict
        body = (
            "#include <pthread.h>\n"
            f"static pthread_mutex_t m{i}=PTHREAD_MUTEX_INITIALIZER;\n"
            f"int main(void){{if(pthread_mutex_lock(&m{i})!=0)return 2;"
            f"if({n}>0)return 3;pthread_mutex_unlock(&m{i});return 0;}}"
        )
        expect = "strict"
    elif shape == 1:  # repeated lock -> deadlock, proven -> reject
        body = (
            "#include <pthread.h>\n"
            f"static pthread_mutex_t m{i}=PTHREAD_MUTEX_INITIALIZER;\n"
            f"int main(void){{pthread_mutex_lock(&m{i});"
            f"pthread_mutex_lock(&m{i});pthread_mutex_unlock(&m{i});return 0;}}"
        )
        expect = "reject"
    else:  # destroy while still locked -> proven -> reject
        body = (
            "#include <pthread.h>\n"
            f"int main(void){{pthread_mutex_t m{i}=PTHREAD_MUTEX_INITIALIZER;"
            f"pthread_mutex_lock(&m{i});pthread_mutex_destroy(&m{i});return 0;}}"
        )
        expect = "reject"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 4: a callee frees the resource; caller keeps using it.
# ---------------------------------------------------------------------------
def gen_callee_free(i, rng):
    shape = i % 3
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    name = f"callee_{shape}_{i}"
    if shape == 0:
        body = (
            "#include <stdlib.h>\n"
            f"static void rel{i}(int *p){{free(p);}}\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"*p={v};rel{i}(p);return *p;}}"
        )
    elif shape == 1:
        body = (
            "#include <stdlib.h>\n"
            f"static void rel{i}(int *p){{free(p);}}\n"
            f"int main(void){{int *q=malloc({n}*sizeof(int));if(!q)return 2;"
            f"*q={v};int *r=q;rel{i}(r);return *q;}}"
        )
    else:
        body = (
            "#include <stdlib.h>\n"
            f"static void rel{i}(int *p){{free(p);}}\n"
            f"static void call{i}(void (*fn)(int *),int *p){{fn(p);}}\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"*p={v};call{i}(rel{i},p);return *p;}}"
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 5: multiple live resources, one path frees only one of them.
# ---------------------------------------------------------------------------
def gen_multi_resource(i, rng):
    shape = i % 3
    variant = (i // 3) % 2
    n = rng.randint(4, 20)
    t = rng.randint(1, 7)
    v = rng.randint(2, 50)
    sname = ("two_malloc", "malloc_file", "free_then_use")[shape]
    name = f"multi_{sname}_{variant}_{i}"

    if shape == 0:
        first, second = ("p", "q") if variant == 0 else ("q", "p")
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));"
            f"int *q=malloc(({n}+1)*sizeof(int));if(!p||!q)return 2;"
            f"*p={v};*q={v + 1};if(*{first}>{t}){{free({first});return 1;}}"
            f"free(p);free(q);return 0;}}"
        )
        # In one variant the leaked resource is q, in the other it is p.
        if variant == 1:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"int *q=malloc(({n}+1)*sizeof(int));if(!p||!q)return 2;"
                f"*p={v};*q={v + 1};if(*q>{t}){{free(q);return 1;}}"
                f"free(p);free(q);return 0;}}"
            )
    elif shape == 1:
        body = (
            "#include <stdio.h>\n#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));"
            f"FILE *f=fopen(\"raii_multi_{i}.txt\",\"w\");if(!p||!f)return 2;"
            f"*p={v};if(*p>{t}){{free(p);return 1;}}free(p);"
            f"if(fclose(f)!=0)return 1;return 0;}}"
        )
        if variant == 1:
            body = (
                "#include <stdio.h>\n#include <stdlib.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"FILE *f=fopen(\"raii_multi_{i}.txt\",\"w\");if(!p||!f)return 2;"
                f"*p={v};if(fprintf(f,\"x\")<0){{if(fclose(f)!=0)return 1;return 2;}}"
                f"free(p);if(fclose(f)!=0)return 1;return 0;}}"
            )
    else:
        body = (
            "#include <stdlib.h>\n"
            f"int main(int argc,char **argv){{(void)argv;int *p=malloc({n}*sizeof(int));"
            f"int *q=malloc(({n}+1)*sizeof(int));if(!p||!q)return 2;*p={v};*q={v + 1};"
            f"if(argc>{t}){{free(p);free(q);}}return *p+*q;}}"
        )
        if variant == 1:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;int *p=malloc({n}*sizeof(int));"
                f"int *q=malloc(({n}+1)*sizeof(int));if(!p||!q)return 2;*p={v};*q={v + 1};"
                f"if(argc>{t}){{free(p);}}return *q;}}"
            )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 6: setjmp/longjmp skipping cleanup (plus clean controls).
# ---------------------------------------------------------------------------
def gen_longjmp(i, rng):
    shape = i % 3
    n = rng.randint(4, 32)
    v = rng.randint(2, 12)
    name = f"longjmp_{shape}_{i}"
    if shape == 0:
        body = (
            "#include <setjmp.h>\n#include <stdlib.h>\n"
            f"static jmp_buf env{i};\n"
            f"static void deep{i}(void){{int *t=malloc({n});longjmp(env{i},1);free(t);}}\n"
            f"int main(void){{if(setjmp(env{i})==0)deep{i}();return 0;}}"
        )
        expect = "strict"
    elif shape == 1:
        body = (
            "#include <setjmp.h>\n#include <stdlib.h>\n"
            f"static jmp_buf out{i},in{i};\n"
            f"static void mid{i}(void){{int *t=malloc({n});"
            f"if(setjmp(in{i})==0)longjmp(out{i},1);free(t);}}\n"
            f"int main(void){{if(setjmp(out{i})==0)mid{i}();return 0;}}"
        )
        expect = "strict"
    else:
        body = (
            "#include <setjmp.h>\n"
            f"static jmp_buf env{i};\n"
            f"static void deep{i}(void){{longjmp(env{i},{v});}}\n"
            f"int main(void){{int r=setjmp(env{i});if(r==0)deep{i}();"
            f"return r=={v}?0:1;}}"
        )
        expect = "accept"
    return name, marked(name, body), expect


# ---------------------------------------------------------------------------
# Family 7: double release / use after close.
# ---------------------------------------------------------------------------
def gen_double_release(i, rng):
    shape = i % 4
    n = rng.randint(4, 24)
    v = rng.randint(2, 40)
    name = f"double_{shape}_{i}"
    if shape == 0:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            "free(p);free(p);return 0;}"
        )
    elif shape == 1:
        body = (
            '#include "cplus/cplus.h"\n'
            f"int main(void){{int *p=cplus_new_array(int,{n});cplus_release(p);"
            "cplus_release(p);return 0;}"
        )
    elif shape == 2:
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"raii_close_{i}.txt\",\"w\");if(!f)return 2;"
            f'if(fprintf(f,"{v}")<0){{fclose(f);return 1;}}fclose(f);'
            'fprintf(f,"x");return 0;}'
        )
    else:
        body = (
            "#include <stdio.h>\n"
            f"int main(void){{FILE *f=fopen(\"raii_dclose_{i}.txt\",\"w\");if(!f)return 2;"
            "fclose(f);fclose(f);return 0;}"
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: safe controls.
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    shape = i % 5
    n = rng.randint(4, 24)
    t = rng.randint(1, 8)
    v = rng.randint(2, 60)
    name = f"safe_{shape}_{i}"
    if shape == 0:  # every explicit return releases
        body = (
            "#include <stdlib.h>\n"
            f"int main(int argc,char **argv){{(void)argv;int *p=malloc({n}*sizeof(int));"
            f"if(!p)return 2;*p={v};if(argc>{t}){{free(p);return 0;}}"
            "free(p);return 1;}"
        )
    elif shape == 1:  # null guards
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"if(p){{*p={v};free(p);}}return 0;}}"
        )
    elif shape == 2:  # ownership escapes via return
        body = (
            "#include <stdlib.h>\n"
            f"static int *make{i}(void){{int *p=malloc({n}*sizeof(int));return p;}}\n"
            f"int main(void){{int *q=make{i}();if(!q)return 2;*q={v};free(q);return 0;}}"
        )
    elif shape == 3:  # ownership escapes via out-parameter
        body = (
            "#include <stdlib.h>\n"
            f"static void make{i}(int **out){{int *p=malloc({n}*sizeof(int));*out=p;}}\n"
            f"int main(void){{int *q=0;make{i}(&q);if(!q)return 2;*q={v};free(q);return 0;}}"
        )
    else:  # C+ resource + managed RAII
        body = (
            '#include "cplus/cplus.h"\n'
            f"resource struct R{i} {{ int x; }};\n"
            f"void R{i}_drop(R{i} *r){{(void)r;}}\n"
            f"static int use{i}(void){{R{i} r={{{v}}};if(r.x)return 7;return 9;}}\n"
            f"int main(void){{managed int *p=cplus_new_array(int,{n});if(!p)return 2;"
            f"p[0]={v};return use{i}()==7?0:1;}}"
        )
    return name, marked(name, body), "accept"


# ---------------------------------------------------------------------------
# Family 9: seeded composition of the patterns above.
# ---------------------------------------------------------------------------
def gen_seeded_mut(i, rng):
    n = rng.randint(4, 20)
    t = rng.randint(1, 7)
    v = rng.randint(2, 55)
    pick = rng.randrange(5)
    name = f"mut_{pick}_{i}"
    if pick == 0:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"*p={v};if(*p>{t})return 3;free(p);return 0;}}"
        )
        expect = "strict"
    elif pick == 1:
        body = (
            "#include <stdlib.h>\n"
            f"int main(int argc,char **argv){{(void)argv;int *p=malloc({n}*sizeof(int));"
            f"if(!p)return 2;*p={v};if(argc>{t})free(p);return *p;}}"
        )
        expect = "strict"
    elif pick == 2:
        body = (
            "#include <stdlib.h>\n"
            f"static void rel{i}(int *p){{free(p);}}\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"*p={v};rel{i}(p);free(p);return 0;}}"
        )
        expect = "reject"
    elif pick == 3:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"*p={v};int x=*p;free(p);return x=={v}?0:1;}}"
        )
        expect = "accept"
    else:
        body = (
            '#include "cplus/cplus.h"\n'
            f"int main(void){{managed int *p=cplus_new_array(int,{n});if(!p)return 2;"
            f"p[0]={v};return p[0]=={v}?0:1;}}"
        )
        expect = "accept"
    return name, marked(name, body), expect


BUILDERS = (
    gen_early_leak,
    gen_partial_cleanup,
    gen_mutex,
    gen_callee_free,
    gen_multi_resource,
    gen_longjmp,
    gen_double_release,
    gen_safe,
    gen_seeded_mut,
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
# Verification: check each generated case in DEFAULT and STRICT.
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
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT")
        if s_rej:
            reasons.append("safe rejected in STRICT")

    if not reasons:
        return None

    (FAILS / f"{name}.c").write_text(body)
    log = [
        f"case: {name}",
        f"family: {family}",
        f"expect: {expect}",
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
    return (f"{name} [{family}] expect={expect}: {'; '.join(reasons)} | "
            f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
            f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}")


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

    print(f"fuzz_raii: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="fuzz_raii_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]
    mismatches.sort()

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches")
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print(f"  ... {len(mismatches) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    return 1 if mismatches or unique != len(cases) else 0


if __name__ == "__main__":
    sys.exit(main())
