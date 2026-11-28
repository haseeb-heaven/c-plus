#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL memory-resource-leak generator (120 cases).

Every generated program is a small C (or C+ RAII) program built from a
per-case independent seeded RNG.  Generation and checking both run in thread
pools: ``pool.map`` preserves input order and each case draws from
``random.Random(f"{SEED}:{case_index}")``, so the corpus and the report are
reproducible regardless of ``FUZZ_WORKERS``.  No case is copied from another
corpus.

Families (all target resource acquisition / release mismatches):

  early        malloc / calloc / realloc / strdup acquired, then 1-3 early
               returns that skip the matching free                 (strict)
  partial      allocation inside nested if / switch / loop, released on
               only some of the paths                              (strict)
  multi        two live resources where an early path frees only one
               (two malloc, malloc+FILE*, malloc+strdup)           (strict)
  overwrite    pointer overwritten while its allocation is still live
               (direct, alias, realloc, strdup)                    (strict)
  outparam     out-parameter helper overwrites the caller's only owner
               (malloc / repeated malloc / strdup)                 (strict)
  lost_owner   allocation stored in a field / global / inner scope, then
               the owner is dropped                                (strict)
  goto_df      goto cleanup fallthrough frees an already-freed block
               (double free of a live malloc owner)                (reject)
  safe         C+ resource / managed types used correctly          (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject - proven memory error: must fail BOTH --default and --strict.
  strict - plain-C leak: MAY pass --default, but must fail --strict.
           The current compiler downgrades plain-C leak findings to warnings
           even in STRICT, so these are expected to surface as missed
           detections (safety holes) in this differential measurement.
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/ag_leak_mem/failures/.  This is a differential
measurement, not a proof of safety.

Environment:
  FUZZ_SEED    default 0x9EED
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
OUT = ROOT / "tests/out/core/ag_leak_mem"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x9EED"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, case count, builder (filled in below).  A case index maps
# deterministically to exactly one family through these ordered ranges.
FAMILIES = (
    ("early", 36, None),
    ("partial", 24, None),
    ("multi", 18, None),
    ("overwrite", 12, None),
    ("outparam", 9, None),
    ("lost_owner", 6, None),
    ("goto_df", 6, None),
    ("safe", 9, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag_leak_mem {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: acquired then early return without release (1-3 paths).
# ---------------------------------------------------------------------------
def gen_early(i, rng):
    kind = i % 4                  # malloc / calloc / realloc / strdup
    paths = (i // 4) % 3 + 1      # 1..3 early returns
    variant = (i // 12) % 3
    n = rng.randint(4, 24)
    t = rng.randint(1, 8)
    v = rng.randint(2, 60)
    kname = ("malloc", "calloc", "realloc", "strdup")[kind]
    name = f"early_{kname}_{paths}_{variant}_{i}"
    first = 4 if kind == 2 else 3  # realloc already uses return 3 on failure
    branches = " ".join(f"if(x>{t + j})return {first + j};"
                        for j in range(paths))

    if kind == 0:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"p[0]={v};int x=p[0];{branches}free(p);return 0;}}"
        )
    elif kind == 1:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=calloc({n},sizeof(int));if(!p)return 2;"
            f"p[0]={v};int x=p[0];{branches}free(p);return 0;}}"
        )
    elif kind == 2:
        # The only pointer to the original block is overwritten by realloc;
        # a failure return before p=q leaks the original allocation.
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"p[0]={v};int *q=realloc(p,({n}+4)*sizeof(int));if(!q)return 3;"
            f"p=q;int x=p[0];{branches}free(p);return 0;}}"
        )
    else:
        lit = f"aglm_e{i}"
        body = (
            "#include <stdlib.h>\n#include <string.h>\n"
            f"int main(void){{char *s=strdup(\"{lit}\");if(!s)return 2;"
            f"int x=s[0];{branches}free(s);return 0;}}"
        )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 2: nested if / switch / loop with partial cleanup.
# ---------------------------------------------------------------------------
def gen_partial(i, rng):
    shape = i % 3              # nested_if / switch / loop
    variant = (i // 3) % 2     # conditional free / conditional early return
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
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;p[0]={v};"
                f"if(argc>{t + 1})free(p);return 0;}}return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;if(argc>{t}){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;p[0]={v};"
                f"if(argc>{t + 1})return 3;free(p);return 0;}}return 0;}}"
            )
    elif shape == 1:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;int k=argc+{v};"
                f"switch(k){{case 1:{{int *p=malloc({n}*sizeof(int));"
                f"if(!p)return 2;p[0]={v};if(argc>2)free(p);return 0;}}"
                f"case 2:break;default:break;}}return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(int argc,char **argv){{(void)argv;int k=argc+{v};"
                f"switch(k){{case 1:{{int *p=malloc({n}*sizeof(int));"
                f"if(!p)return 2;p[0]={v};if(argc>2)return 3;free(p);"
                f"return 0;}}case 2:break;default:break;}}return 0;}}"
            )
    else:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{for(int j=0;j<{n};j++){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;p[0]={v};"
                f"if(j=={t})free(p);else return 3;}}return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{for(int j=0;j<{n};j++){{"
                f"int *p=malloc({n}*sizeof(int));if(!p)return 2;p[0]={v};"
                f"if(j=={t})return 3;free(p);}}return 0;}}"
            )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 3: multiple live resources, an early path frees only one.
# ---------------------------------------------------------------------------
def gen_multi(i, rng):
    shape = i % 3              # two_malloc / malloc_file / malloc_strdup
    variant = (i // 3) % 2
    n = rng.randint(4, 20)
    t = rng.randint(1, 7)
    v = rng.randint(2, 50)
    sname = ("two_malloc", "malloc_file", "malloc_strdup")[shape]
    name = f"multi_{sname}_{variant}_{i}"

    if shape == 0:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"int *q=malloc(({n}+1)*sizeof(int));if(!p||!q)return 2;"
                f"p[0]={v};q[0]={v + 1};if(p[0]>{t}){{free(p);return 3;}}"
                f"free(p);free(q);return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"int *q=malloc(({n}+1)*sizeof(int));if(!p||!q)return 2;"
                f"p[0]={v};q[0]={v + 1};if(q[0]>{t}){{free(q);return 3;}}"
                f"free(p);free(q);return 0;}}"
            )
    elif shape == 1:
        if variant == 0:
            body = (
                "#include <stdio.h>\n#include <stdlib.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"FILE *f=fopen(\"aglm_m{i}.txt\",\"w\");if(!p||!f)return 2;"
                f"p[0]={v};if(p[0]>{t}){{free(p);return 3;}}free(p);"
                f"if(fclose(f)!=0)return 4;return 0;}}"
            )
        else:
            body = (
                "#include <stdio.h>\n#include <stdlib.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"FILE *f=fopen(\"aglm_m{i}.txt\",\"w\");if(!p||!f)return 2;"
                f"p[0]={v};if(p[0]>{t}){{if(fclose(f)!=0)return 3;return 4;}}"
                f"free(p);if(fclose(f)!=0)return 5;return 0;}}"
            )
    else:
        if variant == 0:
            body = (
                "#include <stdlib.h>\n#include <string.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"char *s=strdup(\"aglm_s{i}\");if(!p||!s)return 2;"
                f"p[0]={v};if(p[0]>{t}){{free(s);return 3;}}"
                f"free(p);free(s);return 0;}}"
            )
        else:
            body = (
                "#include <stdlib.h>\n#include <string.h>\n"
                f"int main(void){{int *p=malloc({n}*sizeof(int));"
                f"char *s=strdup(\"aglm_s{i}\");if(!p||!s)return 2;"
                f"p[0]={v};if(p[0]>{t}){{free(p);return 3;}}"
                f"free(p);free(s);return 0;}}"
            )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 4: pointer overwritten while its allocation is still live.
# ---------------------------------------------------------------------------
def gen_overwrite(i, rng):
    shape = i % 4              # direct / alias / realloc / strdup
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("direct", "alias", "realloc", "strdup")[shape]
    name = f"overwrite_{sname}_{i}"

    if shape == 0:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"p[0]={v};p=malloc(({n}+2)*sizeof(int));if(!p)return 3;"
            f"p[0]={v + 1};free(p);return 0;}}"
        )
    elif shape == 1:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"int *q=p;p=malloc(({n}+2)*sizeof(int));if(!p)return 3;"
            f"q[0]={v};p[0]={v + 1};free(p);return 0;}}"
        )
    elif shape == 2:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"p[0]={v};p=realloc(p,({n}+8)*sizeof(int));if(!p)return 3;"
            f"p[0]={v + 1};free(p);return 0;}}"
        )
    else:
        body = (
            "#include <stdlib.h>\n#include <string.h>\n"
            f"int main(void){{char *s=strdup(\"aglm_o{i}a\");if(!s)return 2;"
            f"s=strdup(\"aglm_o{i}b\");if(!s)return 3;free(s);return 0;}}"
        )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 5: out-parameter helper overwrites the caller's only owner.
# ---------------------------------------------------------------------------
def gen_outparam(i, rng):
    shape = i % 3              # overwrite / repeat / strdup
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("overwrite", "repeat", "strdup")[shape]
    name = f"outparam_{sname}_{i}"

    if shape == 0:
        body = (
            "#include <stdlib.h>\n"
            f"static void make{i}(int **out){{*out=malloc({n}*sizeof(int));}}\n"
            f"int main(void){{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"make{i}(&p);if(!p)return 3;p[0]={v};free(p);return 0;}}"
        )
    elif shape == 1:
        body = (
            "#include <stdlib.h>\n"
            f"static void make{i}(int **out){{*out=malloc({n}*sizeof(int));}}\n"
            f"int main(void){{int *p=0;make{i}(&p);if(!p)return 2;p[0]={v};"
            f"make{i}(&p);if(!p)return 3;free(p);return 0;}}"
        )
    else:
        body = (
            "#include <stdlib.h>\n#include <string.h>\n"
            f"static void dup{i}(char **out){{*out=strdup(\"aglm_op{i}\");}}\n"
            f"int main(void){{char *s=strdup(\"aglm_seed{i}\");if(!s)return 2;"
            f"dup{i}(&s);if(!s)return 3;free(s);return 0;}}"
        )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 6: allocation stored, then the owner is lost.
# ---------------------------------------------------------------------------
def gen_lost_owner(i, rng):
    shape = i % 3              # field / global / scope
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("field", "global", "scope")[shape]
    name = f"lost_owner_{sname}_{i}"

    if shape == 0:
        body = (
            "#include <stdlib.h>\n"
            f"struct Box{i}{{int *p;}};\n"
            f"int main(void){{struct Box{i} b;b.p=malloc({n}*sizeof(int));"
            f"if(!b.p)return 2;b.p[0]={v};b.p=0;return 0;}}"
        )
    elif shape == 1:
        body = (
            "#include <stdlib.h>\n"
            f"static int *g{i};\n"
            f"int main(void){{g{i}=malloc({n}*sizeof(int));if(!g{i})return 2;"
            f"g{i}[0]={v};g{i}=0;return 0;}}"
        )
    else:
        body = (
            "#include <stdlib.h>\n"
            f"int main(void){{{{int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"p[0]={v};}}return 0;}}"
        )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 7: goto cleanup fallthrough double-free (proven -> reject).
# ---------------------------------------------------------------------------
def gen_goto_df(i, rng):
    shape = i % 3              # single / two / unconditional
    n = rng.randint(4, 24)
    t = rng.randint(1, 7)
    sname = ("single", "two", "unconditional")[shape]
    name = f"goto_df_{sname}_{i}"

    if shape == 0:
        body = (
            "#include <stdlib.h>\n"
            f"int main(int argc,char **argv){{(void)argv;"
            f"int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"if(argc>{t}){{free(p);goto cleanup;}}free(p);"
            f"cleanup:free(p);return 0;}}"
        )
    elif shape == 1:
        body = (
            "#include <stdlib.h>\n"
            f"int main(int argc,char **argv){{(void)argv;"
            f"int *p=malloc({n}*sizeof(int));int *q=malloc(({n}+1)*sizeof(int));"
            f"if(!p||!q)return 2;"
            f"if(argc>{t}){{free(p);free(q);goto cleanup;}}free(p);"
            f"cleanup:free(q);return 0;}}"
        )
    else:
        body = (
            "#include <stdlib.h>\n"
            f"int main(int argc,char **argv){{(void)argv;"
            f"int *p=malloc({n}*sizeof(int));if(!p)return 2;"
            f"free(p);if(argc>{t})goto cleanup;"
            f"cleanup:free(p);return 0;}}"
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: C+ resource / managed types used correctly (safe controls).
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    shape = i % 3              # explicit / managed / resource_move
    n = rng.randint(4, 24)
    v = rng.randint(2, 60)
    sname = ("explicit", "managed", "resource_move")[shape]
    name = f"safe_{sname}_{i}"

    if shape == 0:
        body = (
            '#include "cplus/cplus.h"\n'
            f"int main(void){{int *p=cplus_new_array(int,{n});if(!p)return 2;"
            f"p[0]={v};cplus_release(p);return 0;}}"
        )
    elif shape == 1:
        body = (
            '#include "cplus/cplus.h"\n'
            f"int main(void){{managed int *p=cplus_new_array(int,{n});if(!p)return 2;"
            f"p[0]={v};return p[0]=={v}?0:1;}}"
        )
    else:
        body = (
            '#include "cplus/cplus.h"\n'
            f"resource struct H{i}{{int id;}};\n"
            f"void H{i}_drop(H{i} *h){{if(h->id)h->id=0;}}\n"
            f"int main(void){{H{i} a={{{v}}};H{i} b=cplus_move(a);"
            f"return b.id=={v}?0:1;}}"
        )
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_early,
    gen_partial,
    gen_multi,
    gen_overwrite,
    gen_outparam,
    gen_lost_owner,
    gen_goto_df,
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
        "===== generated source =====",
        body,
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
    if not pathlib.Path(CC).exists():
        print(f"error: compiler binary not found: {CC} (set CPLUS_BIN)",
              file=sys.stderr)
        return 2

    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    # Remove only fixtures that this generator previously wrote, so a second
    # run reports exactly the mismatches of that run.
    for stale in list(FAILS.glob("*.c")) + list(FAILS.glob("*.diag.txt")):
        stale.unlink()

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the corpus.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    print(f"ag_leak_mem: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag_leak_mem_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]
    mismatches.sort()

    holes = sum(1 for m in mismatches if "expect=reject" in m or
                "expect=strict" in m)
    false_pos = sum(1 for m in mismatches if "expect=accept" in m)
    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches")
    print(f"missed detections (safety holes): {holes}")
    print(f"false positives (safe rejected): {false_pos}")
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print(f"  ... {len(mismatches) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
