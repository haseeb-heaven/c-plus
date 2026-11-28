#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator for leak / failure-path FALSE POSITIVES (>=100).

Produces distinct, self-contained, valid C translation units that probe whether
the checker (a) reports real resource leaks on failure paths and (b) stays
silent on correctly guarded failure paths.  The emphasis of this corpus is the
second property: a correctly written `if (ptr == NULL) { fprintf(...); return; }`
guard must NOT be reported as a leak (a rejection there is a false positive).

Families (all parameters drawn from an independent per-case seeded RNG):

  guarded_malloc  malloc/calloc + `if(p==NULL){fprintf;return;}` guard, every
                  path released                                        (accept)
  guarded_fopen   fopen + `if(f==NULL){fprintf;return;}` guard, every path
                  closed                                               (accept)
  guarded_dual    two/three resources, each acquisition failure guarded and
                  all already-acquired resources released              (accept)
  early_return    live allocation crossed by an early return           (strict)
  partial_free    multiple resources, one path releases only some of them
                                                                       (strict)
  goto_cleanup    goto skips the only free (strict) / fallthrough reaches a
                  second free (reject)
  longjmp_skip    longjmp escapes the region holding the only free (strict),
                  plus a longjmp control that frees first               (accept)
  realloc_fail    realloc failure drops the still-live original (strict),
                  plus correct failure handling                         (accept)
  field_owner     ownership stored in a heap/stack struct field then dropped
                  (strict), plus explicit field cleanup                (accept)
  safe_controls   fully guarded / fully released controls              (accept)

Expected outcomes (same contract as tests/core/redteam_100.py):

  reject - proven memory error: must fail BOTH --default and --strict.
  strict - may pass --default, but must fail --strict; an acceptance in STRICT
           is a missed detection (safety hole).
  accept - valid control: must pass BOTH modes; a rejection is a FALSE POSITIVE.

Mismatches are printed, counted by class (missed detections vs false positives),
and the generated source plus diagnostics are saved under
tests/out/core/hv_leak/failures/.  This is a differential measurement against
the existing binary, not a proof of safety.

Environment:
  FUZZ_SEED    default 0x4C34
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
OUT = ROOT / "tests/out/core/hv_leak"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4C34"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, case count, builder (filled in below).
FAMILIES = (
    ("guarded_malloc", 18, None),
    ("guarded_fopen", 18, None),
    ("guarded_dual", 12, None),
    ("early_return", 12, None),
    ("partial_free", 12, None),
    ("goto_cleanup", 10, None),
    ("longjmp_skip", 8, None),
    ("realloc_fail", 12, None),
    ("field_owner", 12, None),
    ("safe_controls", 12, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
STR = "#include <stdlib.h>\n#include <string.h>\n"
STDIO = "#include <stdio.h>\n#include <stdlib.h>\n"
STRIO = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
SETJMP = "#include <setjmp.h>\n#include <stdlib.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in .format/f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hv_leak {name} seed={SEED:#x} */\n" + body.strip() + "\n"


def fname(rng):
    return rng.choice(("input.txt", "data.bin", "cfg.ini", "log.txt")) + str(
        rng.randint(0, 9999))


# --- Family 1: guarded malloc/calloc; must NOT warn. ------------------------
def gen_guarded_malloc(i, rng):
    variant = i % 3
    kind = i % 2
    n = rng.randint(4, 32)
    v = rng.randint(2, 96)
    rc = rng.randint(1, 9)
    msg = rng.choice(("oom", "alloc failed", "out of memory", "no memory"))
    pname = rng.choice(("p", "ptr", "buf", "data"))
    alloc = (f"malloc({n}*sizeof(int))" if kind == 0
             else f"calloc({n},sizeof(int))")
    name = f"hv_guarded_malloc_{kind}_{variant}_{i}"
    if variant == 0:
        body = STDIO + tpl(
            "int main(void){int *@@P@@=@@ALLOC@@;"
            "if(@@P@@==NULL){fprintf(stderr,\"@@MSG@@\\n\");return @@RC@@;}"
            "@@P@@[0]=@@V@@;free(@@P@@);return 0;}",
            P=pname, ALLOC=alloc, MSG=msg, RC=rc, V=v)
    elif variant == 1:
        body = STDIO + tpl(
            "int main(void){int *@@P@@=@@ALLOC@@;"
            "if(@@P@@ == NULL){fprintf(stderr,\"@@MSG@@: %s\\n\",\"malloc\");"
            "return @@RC@@;}"
            "@@P@@[0]=@@V@@;int x=@@P@@[0];(void)x;free(@@P@@);return 0;}",
            P=pname, ALLOC=alloc, MSG=msg, RC=rc, V=v)
    else:
        body = STDIO + tpl(
            "int main(int argc,char **argv){(void)argv;int *@@P@@=@@ALLOC@@;"
            "if(@@P@@==NULL){fprintf(stderr,\"@@MSG@@\\n\");return @@RC@@;}"
            "if(argc>1){@@P@@[0]=@@V@@;}free(@@P@@);return 0;}",
            P=pname, ALLOC=alloc, MSG=msg, RC=rc, V=v)
    return name, marked(name, body), "accept"


# --- Family 2: guarded fopen; must NOT warn. --------------------------------
def gen_guarded_fopen(i, rng):
    variant = i % 3
    rc = rng.randint(1, 9)
    msg = rng.choice(("open failed", "cannot open", "fopen failed", "no file"))
    mode = rng.choice(("r", "rb", "r+"))
    fn = fname(rng)
    name = f"hv_guarded_fopen_{variant}_{i}"
    if variant == 0:
        body = STDIO + tpl(
            "int main(void){FILE *f=fopen(\"@@FN@@\",\"@@MODE@@\");"
            "if(f==NULL){fprintf(stderr,\"@@MSG@@\\n\");return @@RC@@;}"
            "fclose(f);return 0;}", FN=fn, MODE=mode, MSG=msg, RC=rc)
    elif variant == 1:
        body = STDIO + tpl(
            "int main(void){FILE *f=fopen(\"@@FN@@\",\"@@MODE@@\");"
            "if(f == NULL){fprintf(stderr,\"@@MSG@@: %s\\n\",\"fopen\");"
            "return @@RC@@;}"
            "int c=fgetc(f);(void)c;fclose(f);return 0;}",
            FN=fn, MODE=mode, MSG=msg, RC=rc)
    else:
        body = STDIO + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "FILE *f=fopen(\"@@FN@@\",\"@@MODE@@\");"
            "if(f==NULL){fprintf(stderr,\"@@MSG@@\\n\");return @@RC@@;}"
            "if(argc>1){int c=fgetc(f);(void)c;}fclose(f);return 0;}",
            FN=fn, MODE=mode, MSG=msg, RC=rc)
    return name, marked(name, body), "accept"


# --- Family 3: multiple guarded acquisitions, all released. ----------------
def gen_guarded_dual(i, rng):
    variant = i % 3
    n = rng.randint(4, 24)
    v = rng.randint(2, 64)
    rc1 = rng.randint(1, 9)
    rc2 = rc1 + 1
    rc3 = rc2 + 1
    fn = fname(rng)
    name = f"hv_guarded_dual_{variant}_{i}"
    if variant == 0:
        body = STDIO + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(p==NULL){fprintf(stderr,\"alloc failed\\n\");return @@RC1@@;}"
            "FILE *f=fopen(\"@@FN@@\",\"r\");"
            "if(f==NULL){fprintf(stderr,\"open failed\\n\");free(p);"
            "return @@RC2@@;}"
            "p[0]=@@V@@;int c=fgetc(f);(void)c;free(p);fclose(f);return 0;}",
            N=n, V=v, RC1=rc1, RC2=rc2, FN=fn)
    elif variant == 1:
        body = STDIO + tpl(
            "int main(void){FILE *f=fopen(\"@@FN@@\",\"r\");"
            "if(f==NULL){fprintf(stderr,\"open failed\\n\");return @@RC1@@;}"
            "int *p=malloc(@@N@@*sizeof(int));"
            "if(p==NULL){fprintf(stderr,\"alloc failed\\n\");fclose(f);"
            "return @@RC2@@;}"
            "p[0]=@@V@@;free(p);fclose(f);return 0;}",
            N=n, V=v, RC1=rc1, RC2=rc2, FN=fn)
    else:
        body = STDIO + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(p==NULL){fprintf(stderr,\"alloc1\\n\");return @@RC1@@;}"
            "int *q=malloc(@@N@@*sizeof(int));"
            "if(q==NULL){fprintf(stderr,\"alloc2\\n\");free(p);return @@RC2@@;}"
            "FILE *f=fopen(\"@@FN@@\",\"r\");"
            "if(f==NULL){fprintf(stderr,\"open failed\\n\");free(p);free(q);"
            "return @@RC3@@;}"
            "p[0]=@@V@@;q[0]=@@V@@;free(p);free(q);fclose(f);return 0;}",
            N=n, V=v, RC1=rc1, RC2=rc2, RC3=rc3, FN=fn)
    return name, marked(name, body), "accept"


# --- Family 4: early return with a live allocation. ------------------------
def gen_early_return(i, rng):
    variant = i % 3
    n = rng.randint(4, 28)
    v = rng.randint(2, 80)
    t = rng.randint(1, 6)
    alloc = (f"malloc({n}*sizeof(int))" if i % 2 == 0
             else f"calloc({n},sizeof(int))")
    name = f"hv_early_return_{variant}_{i}"
    if variant == 0:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int *p=@@ALLOC@@;"
            "if(!p)return 2;p[0]=@@V@@;if(argc>@@T@@)return 3;"
            "free(p);return 0;}", ALLOC=alloc, V=v, T=t)
    elif variant == 1:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int *p=@@ALLOC@@;"
            "if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T@@){if(argc>@@T1@@)return 4;}free(p);return 0;}",
            ALLOC=alloc, V=v, T=t, T1=t + 1)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;int *p=@@ALLOC@@;"
            "if(!p)return 2;p[0]=@@V@@;"
            "for(int k=0;k<3;k++){if(argc>@@T@@)return 5;}free(p);return 0;}",
            ALLOC=alloc, V=v, T=t)
    return name, marked(name, body), "strict"


# --- Family 5: multiple resources, partially freed on a path. --------------
def gen_partial_free(i, rng):
    variant = i % 3
    n = rng.randint(4, 24)
    v = rng.randint(2, 70)
    t = rng.randint(1, 6)
    fn = fname(rng)
    name = f"hv_partial_free_{variant}_{i}"
    if variant == 0:
        body = STDIO + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "FILE *f=fopen(\"@@FN@@\",\"r\");if(!f){free(p);return 3;}"
            "p[0]=@@V@@;if(argc>@@T@@){free(p);return 4;}"
            "free(p);fclose(f);return 0;}", N=n, V=v, T=t, FN=fn)
    elif variant == 1:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=malloc(@@N@@*sizeof(int));if(!q){free(p);return 3;}"
            "p[0]=@@V@@;q[0]=@@V@@;if(argc>@@T@@){free(p);return 4;}"
            "free(p);free(q);return 0;}", N=n, V=v, T=t)
    else:
        body = STRIO + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "char *s=malloc(16);if(!s){free(p);return 3;}strcpy(s,\"hello\");"
            "p[0]=@@V@@;if(argc>@@T@@){free(p);return 4;}"
            "free(p);free(s);return 0;}", N=n, V=v, T=t)
    return name, marked(name, body), "strict"


# --- Family 6: goto cleanup fallthrough. -----------------------------------
def gen_goto_cleanup(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 80)
    t = rng.randint(1, 6)
    name = f"hv_goto_cleanup_{i}"
    if i % 10 < 6:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T@@)goto done;free(p);done:return 0;}",
            N=n, V=v, T=t)
        expect = "strict"
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "if(argc>@@T@@)goto cleanup;free(p);cleanup:free(p);return 0;}",
            N=n, V=v, T=t)
        expect = "reject"
    return name, marked(name, body), expect


# --- Family 7: longjmp skipping / preserving the free. ---------------------
def gen_longjmp_skip(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 80)
    name = f"hv_longjmp_{i}"
    if i % 2 == 0:
        # die() longjmps past the only free while p is live -> genuine leak.
        body = SETJMP + tpl(
            "static jmp_buf jb;static void die(void){longjmp(jb,1);}"
            "int main(void){int *p;if(setjmp(jb)!=0)return 3;"
            "p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "die();free(p);return 0;}", N=n, V=v)
        expect = "strict"
    else:
        # The free runs before the longjmp; the resumed path touches nothing.
        body = SETJMP + tpl(
            "static jmp_buf jb;static void die(void){longjmp(jb,1);}"
            "int main(void){int *p;if(setjmp(jb)!=0)return 0;"
            "p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[0]=@@V@@;"
            "free(p);die();return 0;}", N=n, V=v)
        expect = "accept"
    return name, marked(name, body), expect


# --- Family 8: realloc failure paths. --------------------------------------
def gen_realloc_fail(i, rng):
    n = rng.randint(4, 24)
    m = n + rng.randint(1, 32)
    v = rng.randint(2, 80)
    name = f"hv_realloc_fail_{i}"
    if i % 2 == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;int *t=realloc(p,@@M@@*sizeof(int));"
            "if(!t)return 3;free(t);return 0;}", N=n, M=m, V=v)
        expect = "strict"
    else:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;int *t=realloc(p,@@M@@*sizeof(int));"
            "if(!t){free(p);return 3;}free(t);return 0;}", N=n, M=m, V=v)
        expect = "accept"
    return name, marked(name, body), expect


# --- Family 9: ownership stored in a struct field. -------------------------
def gen_field_owner(i, rng):
    n = rng.randint(4, 24)
    v = rng.randint(2, 80)
    variant = i % 4
    st = "struct S{int *data;};"
    name = f"hv_field_owner_{variant}_{i}"
    if variant == 0:
        body = STD + st + tpl(
            "int main(void){struct S *s=malloc(sizeof *s);if(!s)return 2;"
            "s->data=malloc(@@N@@*sizeof(int));if(!s->data){free(s);return 3;}"
            "s->data[0]=@@V@@;free(s);return 0;}", N=n, V=v)
        expect = "strict"
    elif variant == 1:
        body = STD + st + tpl(
            "int main(void){struct S *s=malloc(sizeof *s);if(!s)return 2;"
            "s->data=malloc(@@N@@*sizeof(int));if(!s->data){free(s);return 3;}"
            "s->data[0]=@@V@@;free(s->data);free(s);return 0;}", N=n, V=v)
        expect = "accept"
    elif variant == 2:
        body = STD + st + tpl(
            "int main(void){struct S s;s.data=malloc(@@N@@*sizeof(int));"
            "if(!s.data)return 2;s.data[0]=@@V@@;return 0;}", N=n, V=v)
        expect = "strict"
    else:
        body = STD + st + tpl(
            "int main(void){struct S s;s.data=malloc(@@N@@*sizeof(int));"
            "if(!s.data)return 2;s.data[0]=@@V@@;free(s.data);return 0;}",
            N=n, V=v)
        expect = "accept"
    return name, marked(name, body), expect


# --- Family 10: fully safe controls. ---------------------------------------
def gen_safe_controls(i, rng):
    n = rng.randint(4, 28)
    v = rng.randint(2, 90)
    t = rng.randint(1, 6)
    variant = i % 3
    name = f"hv_safe_controls_{variant}_{i}"
    if variant == 0:
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "p[0]=@@V@@;free(p);return 0;}", N=n, V=v)
    elif variant == 1:
        body = STDIO + tpl(
            "int main(void){FILE *f=fopen(\"safe@@I@@.txt\",\"r\");"
            "if(f==NULL){fprintf(stderr,\"open failed\\n\");return 3;}"
            "int c=fgetc(f);(void)c;fclose(f);return 0;}", I=i)
    else:
        body = STD + tpl(
            "int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=malloc(@@N@@*sizeof(int));if(!q){free(p);return 3;}"
            "p[0]=@@V@@;q[0]=@@V@@;"
            "if(argc>@@T@@){free(p);free(q);return 4;}"
            "free(p);free(q);return 0;}", N=n, V=v, T=t)
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_guarded_malloc,
    gen_guarded_fopen,
    gen_guarded_dual,
    gen_early_return,
    gen_partial_free,
    gen_goto_cleanup,
    gen_longjmp_skip,
    gen_realloc_fail,
    gen_field_owner,
    gen_safe_controls,
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


# --- Verification -----------------------------------------------------------
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
    result["detail"] = (
        f"{name} [{family}] expect={expect}: {'; '.join(reasons)} | "
        f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
        f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}")
    return result


def main():
    global STAGE
    if not pathlib.Path(CC).exists():
        print(f"error: compiler binary not found: {CC} (set CPLUS_BIN)",
              file=sys.stderr)
        return 2

    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    for stale in list(FAILS.glob("*.c")) + list(FAILS.glob("*.diag.txt")):
        stale.unlink()

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    print(f"hv_leak: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hv_leak_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    mismatches = sorted(r["detail"] for r in results if r["reasons"])
    mismatch_names = sorted(r["name"] for r in results if r["reasons"])
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    holes = sum(1 for r in results if r["expect"] != "accept" and r["reasons"])
    hard = sum(1 for r in results for reason in r["reasons"]
               if "TIMEOUT" in reason or "OS ERROR" in reason or "CRASH" in reason)

    print("results: %d cases, %d mismatches (%d missed detections, "
          "%d false positives, %d hard errors)"
          % (len(cases), len(mismatches), holes, len(false_positives), hard))
    print(f"false positives: {len(false_positives)}")
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print(f"  ... {len(false_positives) - 20} more")
    print("first 10 mismatch names:")
    for name in mismatch_names[:10]:
        print("  " + name)
    if len(mismatch_names) > 10:
        print(f"  ... {len(mismatch_names) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        "FAIL" if (mismatches or unique != len(cases)) else "PASS",
        len(cases), len(mismatches), len(false_positives)))
    return 1 if mismatches or unique != len(cases) else 0


if __name__ == "__main__":
    sys.exit(main())
