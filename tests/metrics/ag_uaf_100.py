#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator of ADVANCED use-after-free cases (112).

Every generated program is a small C (or C+ RAII) program built from a
per-case independent seeded RNG.  Generation and checking both run in thread
pools; ``pool.map`` preserves input order and case ``i`` always draws from
``random.Random(f"{SEED}:{i}")``, so a run is fully reproducible regardless of
``FUZZ_WORKERS``.  The per-case marker plus the seeded parameters guarantee
that no two generated programs are identical.

Families (all target use-after-free / dangling-borrow patterns):

  alias_chain    alias chains; free via one link, dereference via another  (reject)
  branch_loop    conditional free in if / switch / loop bodies            (strict)
  interproc      interprocedural frees via direct calls (incl. 2-hop)     (reject)
  struct_field   borrow of a struct field, then free/drop of the owner    (reject)
  array_elem     alias through array-of-pointer elements                  (reject)
  ret_freed      function returns of freed pointers / freed aliases       (reject)
  realloc_stale  realloc success invalidates pre-realloc aliases          (reject)
  destructor     C+ resource destructor invalidates a borrowed field      (reject)
  libc_printf    freed pointer handed to libc / printf family sinks        (reject)
  safe           valid controls (use-before-free, reset, correct realloc) (accept)

Each case declares one expected outcome, following tests/core/redteam_100.py:

  reject - proven use-after-free: must fail BOTH --default and --strict.
  strict - conditional / unverified use-after-free: MAY pass --default, but
           must fail --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Expectations describe CORRECT C/C+ semantics, not the current checker's
behaviour.  Consequently a case the checker misses is reported as a false
negative (unsafe accepted), and a safe case it rejects is reported as a false
positive.  Programs that are rejected only for a non-safety reason (a bad
generated program) are flagged separately as malformed generator cases.

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/ag_uaf/failures/.  This is a differential
measurement, not a proof of safety.

Environment:
  FUZZ_SEED    default 0xA9AF
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
OUT = ROOT / "tests/out/core/ag_uaf"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA9AF"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# Family name, number of cases, builder.  Case index -> family deterministically.
FAMILIES = (
    ("alias_chain", 16, None),
    ("branch_loop", 16, None),
    ("interproc", 12, None),
    ("struct_field", 12, None),
    ("array_elem", 10, None),
    ("ret_freed", 10, None),
    ("realloc_stale", 8, None),
    ("destructor", 8, None),
    ("libc_printf", 8, None),
    ("safe", 12, None),
)
TOTAL = sum(count for _name, count, _fn in FAMILIES)
STAGE = None  # temporary directory with one staged .c file per case

STD = "#include <stdlib.h>\n"
PRN = "#include <stdio.h>\n"
STR = "#include <string.h>\n"
CPL = '#include "cplus/cplus.h"\n'


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* ag_uaf {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Family 1: dangling alias chains -- free via one link, deref via another.
# ---------------------------------------------------------------------------
def gen_alias_chain(i, rng):
    n = rng.randint(2, 16)
    depth = rng.randint(2, 6)
    v = rng.randint(2, 60)
    freed = rng.randrange(depth + 1)       # 0 => p, k+1 => a{k}
    used = rng.randrange(depth + 1)
    while used == freed:
        used = rng.randrange(depth + 1)
    kind = rng.randrange(3)                # read / subscript / write
    names = ["p"] + [f"a{j}" for j in range(depth)]
    decls = " ".join(f"int *a{j}={names[j]};" for j in range(depth))
    idx = rng.randrange(n)
    if kind == 0:
        use = f"return *{names[used]};"
    elif kind == 1:
        use = f"return {names[used]}[{idx}];"
    else:
        use = f"*{names[used]}={v};return 0;"
    body = tpl(
        "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));"
        "if(!p)return 2;@@DEC@@ free(@@F@@);@@USE@@}",
        STD=STD, N=n, DEC=decls, F=names[freed], USE=use,
    )
    name = f"alias_chain_{depth}_{freed}_{used}_{i}"
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 2: branch / loop conditional frees (unverified -> strict).
# ---------------------------------------------------------------------------
def gen_branch_loop(i, rng):
    n = rng.randint(2, 16)
    t = rng.randint(1, 6)
    v = rng.randint(2, 50)
    idx = rng.randrange(n)
    shape = i % 4
    name = f"branch_loop_{shape}_{i}"
    if shape == 0:  # if(argc>T) free; then use
        body = tpl(
            "@@STD@@int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(argc>@@T@@)free(p);return *p;}",
            STD=STD, N=n, V=v, T=t,
        )
    elif shape == 1:  # fixed-iteration loop free; then use
        k = rng.randrange(n)
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 2;*p=@@V@@;"
            "for(int j=0;j<@@N@@;j++){if(j==@@K@@)free(p);}return *p;}",
            STD=STD, N=n, V=v, K=k,
        )
    elif shape == 2:  # nested if with runtime + compile-time conditions
        body = tpl(
            "@@STD@@int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "if(argc>@@T@@){if(@@N@@>1)free(p);}return p[@@I@@];}",
            STD=STD, N=n, V=v, T=t, I=idx,
        )
    else:  # switch with free on one case only
        body = tpl(
            "@@STD@@int main(int argc,char **argv){(void)argv;"
            "int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;*p=@@V@@;"
            "switch(argc){case 1:free(p);break;default:break;}return *p;}",
            STD=STD, N=n, V=v,
        )
    return name, marked(name, body), "strict"


# ---------------------------------------------------------------------------
# Family 3: interprocedural frees via direct calls.
# ---------------------------------------------------------------------------
def gen_interproc(i, rng):
    n = rng.randint(2, 16)
    v = rng.randint(2, 50)
    shape = i % 4
    name = f"interproc_{shape}_{i}"
    rel = f"rel{i}"
    if shape == 0:  # direct callee free
        body = tpl(
            "@@STD@@static void @@REL@@(int *p){free(p);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;@@REL@@(p);return *p;}",
            STD=STD, REL=rel, N=n, V=v,
        )
    elif shape == 1:  # alias passed to the freeing callee
        body = tpl(
            "@@STD@@static void @@REL@@(int *p){free(p);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int *q=p;@@REL@@(q);return *p;}",
            STD=STD, REL=rel, N=n, V=v,
        )
    elif shape == 2:  # two-hop call chain to the freeing callee
        mid = f"mid{i}"
        body = tpl(
            "@@STD@@static void @@REL@@(int *p){free(p);}"
            "static void @@MID@@(int *p){@@REL@@(p);}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;@@MID@@(p);return *p;}",
            STD=STD, REL=rel, MID=mid, N=n, V=v,
        )
    else:  # freed pointer passed to a callee that dereferences it
        use = f"use{i}"
        body = tpl(
            "@@STD@@static int @@USE@@(int *p){return *p;}"
            "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;free(p);return @@USE@@(p);}",
            STD=STD, USE=use, N=n, V=v,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 4: struct-field ownership / borrowed fields.
# ---------------------------------------------------------------------------
def gen_struct_field(i, rng):
    n = rng.randint(2, 12)
    idx = rng.randrange(n)
    v = rng.randint(2, 50)
    shape = i % 3
    name = f"struct_field_{shape}_{i}"
    if shape == 0:  # borrow array field, free owner, deref borrow
        body = tpl(
            "@@STD@@struct S{int x[@@N@@];};"
            "int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;"
            "int*p=&s->x[@@I@@];free(s);return *p;}",
            STD=STD, N=n, I=idx,
        )
    elif shape == 1:  # pointer field freed, then used via the struct
        body = tpl(
            "@@STD@@struct S{int *p;};"
            "int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;"
            "s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;*s->p=@@V@@;"
            "free(s->p);return *s->p;}",
            STD=STD, N=n, V=v,
        )
    else:  # nested struct field borrow
        body = tpl(
            "@@STD@@struct I{int v[@@N@@];};struct O{struct I in;};"
            "int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;"
            "int*p=&o->in.v[@@I@@];free(o);return *p;}",
            STD=STD, N=n, I=idx,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 5: aliasing through array-of-pointer elements.
# ---------------------------------------------------------------------------
def gen_array_elem(i, rng):
    n = rng.randint(2, 12)
    v = rng.randint(2, 50)
    idx = rng.randrange(n)
    shape = i % 3
    name = f"array_elem_{shape}_{i}"
    if shape == 0:  # two slots share one allocation
        body = tpl(
            "@@STD@@int main(void){int *a[2];"
            "a[0]=a[1]=malloc(@@N@@*sizeof(int));if(!a[0])return 2;"
            "*a[0]=@@V@@;free(a[0]);return *a[1];}",
            STD=STD, N=n, V=v,
        )
    elif shape == 1:  # alias chain stored in array slots
        body = tpl(
            "@@STD@@int main(void){int *a[3];"
            "a[0]=malloc(@@N@@*sizeof(int));if(!a[0])return 2;"
            "a[1]=a[0];a[2]=a[1];free(a[0]);return *a[2];}",
            STD=STD, N=n,
        )
    else:  # free via one slot, subscript through another
        body = tpl(
            "@@STD@@int main(void){int *a[2];"
            "a[0]=a[1]=malloc(@@N@@*sizeof(int));if(!a[0])return 2;"
            "a[0][0]=@@V@@;free(a[1]);return a[0][@@I@@];}",
            STD=STD, N=n, V=v, I=idx,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 6: functions returning freed pointers / freed aliases.
# ---------------------------------------------------------------------------
def gen_ret_freed(i, rng):
    n = rng.randint(2, 12)
    v = rng.randint(2, 50)
    shape = i % 3
    name = f"ret_freed_{shape}_{i}"
    fn = f"f{i}"
    if shape == 0:  # return the freed pointer itself
        body = tpl(
            "@@STD@@static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;free(p);return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;return *q;}",
            STD=STD, FN=fn, N=n,
        )
    elif shape == 1:  # return an alias of the freed pointer
        body = tpl(
            "@@STD@@static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;int *q=p;free(p);return q;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;return *q;}",
            STD=STD, FN=fn, N=n,
        )
    else:  # return freed pointer, caller writes through it
        body = tpl(
            "@@STD@@static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;free(p);return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;*q=@@V@@;return 0;}",
            STD=STD, FN=fn, N=n, V=v,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 7: realloc success invalidates pre-realloc aliases.
# ---------------------------------------------------------------------------
def gen_realloc_stale(i, rng):
    n = rng.randint(2, 12)
    shape = i % 3
    name = f"realloc_stale_{shape}_{i}"
    if shape == 0:  # single stale alias used after successful realloc
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *q=p;void *t=realloc(p,(@@N@@+2)*sizeof(int));"
            "if(t){p=t;return *q;}free(p);return 0;}",
            STD=STD, N=n,
        )
    elif shape == 1:  # free the stale old pointer after realloc success
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *old=p;void *t=realloc(p,(@@N@@+2)*sizeof(int));"
            "if(t){free(old);p=t;return *p;}return 0;}",
            STD=STD, N=n,
        )
    else:  # two stale aliases after successful realloc
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "int *a=p;int *b=a;void *t=realloc(p,(@@N@@+4)*sizeof(int));"
            "if(t){p=t;return *b;}free(p);return 0;}",
            STD=STD, N=n,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 8: C+ resource destructor invalidates a borrowed field.
# ---------------------------------------------------------------------------
def gen_destructor(i, rng):
    n = rng.randint(2, 10)
    idx = rng.randrange(n)
    v = rng.randint(2, 50)
    shape = i % 3
    name = f"destructor_{shape}_{i}"
    rt = f"R{i}"
    drop = f"{rt}_drop"
    preamble = (CPL + f"resource struct {rt}{{int x[{n}];}};"
                f"void {drop}({rt} *r){{(void)r;}}\n")
    if shape == 0:  # borrow field, drop owner, read borrow
        body = preamble + tpl(
            "int main(void){@@R@@ r={{0}};int *b=&r.x[@@I@@];"
            "@@DROP@@(&r);return *b;}",
            R=rt, I=idx, DROP=drop,
        )
    elif shape == 1:  # borrow field, drop owner, write through borrow
        body = preamble + tpl(
            "int main(void){@@R@@ r={{0}};int *b=&r.x[@@I@@];r.x[@@I@@]=@@V@@;"
            "@@DROP@@(&r);*b=@@V@@;return 0;}",
            R=rt, I=idx, V=v, DROP=drop,
        )
    else:  # decayed array borrow, drop owner, subscript
        body = preamble + tpl(
            "int main(void){@@R@@ r={{0}};int *b=r.x;"
            "@@DROP@@(&r);return b[@@I@@];}",
            R=rt, I=idx, DROP=drop,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 9: freed pointer passed to libc / printf family sinks.
# ---------------------------------------------------------------------------
def gen_libc_printf(i, rng):
    n = rng.randint(4, 24)
    shape = i % 4
    name = f"libc_printf_{shape}_{i}"
    if shape == 0:  # printf("%s", freed)
        body = tpl(
            "@@PRN@@@@STD@@int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            'p[0]=0;free(p);printf("%s",p);return 0;}',
            PRN=PRN, STD=STD, N=n,
        )
    elif shape == 1:  # fprintf(stdout, "%s", freed)
        body = tpl(
            "@@PRN@@@@STD@@int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            'p[0]=0;free(p);fprintf(stdout,"%s",p);return 0;}',
            PRN=PRN, STD=STD, N=n,
        )
    elif shape == 2:  # strlen(freed)
        body = tpl(
            "@@STR@@@@STD@@int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
            "p[0]=0;free(p);return (int)strlen(p);}",
            STR=STR, STD=STD, N=n,
        )
    else:  # memcpy from freed buffer
        body = tpl(
            "@@STR@@@@STD@@int main(void){char *p=malloc(@@N@@);char d[@@N@@];"
            "if(!p)return 2;p[0]=0;free(p);memcpy(d,p,4);return 0;}",
            STR=STR, STD=STD, N=n,
        )
    return name, marked(name, body), "reject"


# ---------------------------------------------------------------------------
# Family 10: valid controls -- must pass BOTH modes.
# ---------------------------------------------------------------------------
def gen_safe(i, rng):
    n = rng.randint(2, 16)
    depth = rng.randint(1, 4)
    v = rng.randint(2, 60)
    shape = i % 6
    name = f"safe_{shape}_{i}"
    names = ["p"] + [f"a{j}" for j in range(depth)]
    decls = " ".join(f"int *a{j}={names[j]};" for j in range(depth))
    if shape == 0:  # use before free
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "*p=@@V@@;int r=*p;free(p);return r==@@V@@?0:1;}",
            STD=STD, N=n, V=v,
        )
    elif shape == 1:  # alias chain used before free
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "@@DEC@@ *@@L@@=@@V@@;int r=*p;free(p);return r==@@V@@?0:1;}",
            STD=STD, N=n, DEC=decls, L=names[-1], V=v,
        )
    elif shape == 2:  # free then null-reset
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "free(p);p=0;return p?*p:0;}",
            STD=STD, N=n,
        )
    elif shape == 3:  # correct realloc success/failure handling
        body = tpl(
            "@@STD@@int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
            "void *t=realloc(p,(@@N@@+2)*sizeof(int));"
            "if(t)p=t;else{free(p);return 2;}free(p);return 0;}",
            STD=STD, N=n,
        )
    elif shape == 4:  # ownership escapes via return, freed by caller
        fn = f"mk{i}"
        body = tpl(
            "@@STD@@static int *@@FN@@(void){int *p=malloc(@@N@@*sizeof(int));"
            "if(!p)return 0;*p=@@V@@;return p;}"
            "int main(void){int *q=@@FN@@();if(!q)return 1;"
            "int r=*q;free(q);return r==@@V@@?0:1;}",
            STD=STD, FN=fn, N=n, V=v,
        )
    else:  # resource drop after last use
        rt = f"R{i}"
        drop = f"{rt}_drop"
        preamble = (CPL + f"resource struct {rt}{{int x[{n}];}};"
                    f"void {drop}({rt} *r){{(void)r;}}\n")
        body = preamble + tpl(
            "int main(void){@@R@@ r={{0}};r.x[@@I@@]=@@V@@;int a=r.x[@@I@@];"
            "@@DROP@@(&r);return a==@@V@@?0:1;}",
            R=rt, I=n - 1, V=v, DROP=drop,
        )
    return name, marked(name, body), "accept"


BUILDERS = (
    gen_alias_chain,
    gen_branch_loop,
    gen_interproc,
    gen_struct_field,
    gen_array_elem,
    gen_ret_freed,
    gen_realloc_stale,
    gen_destructor,
    gen_libc_printf,
    gen_safe,
)


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, _builder), builder in zip(FAMILIES, BUILDERS):
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body, expect = builder(i, rng)
            assert expect in ("reject", "strict", "accept"), (name, expect)
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
            return s[:240]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def check_case(case):
    """Run one case in both modes; return a mismatch record or None."""
    name, body, expect, family = case
    path = STAGE / f"{name}.c"
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run(
                [CC, "check", mode, str(path)],
                capture_output=True, text=True, timeout=TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            modes[mode] = (False, f"TIMEOUT after {TIMEOUT}s")
            hard.append(f"{mode}: TIMEOUT after {TIMEOUT}s")
            continue
        except OSError as exc:
            modes[mode] = (False, f"OS ERROR: {exc}")
            hard.append(f"{mode}: OS ERROR: {exc}")
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append(f"{mode}: CRASH rc={proc.returncode}")
        if proc.returncode != 0 and "[CPLUS-" not in diag:
            hard.append(f"{mode}: rejected without a CPLUS safety diagnostic "
                        "(possible malformed generated program)")
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT (false negative)")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT (false negative)")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted")
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

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
    return {
        "name": name,
        "family": family,
        "expect": expect,
        "reasons": reasons,
        "detail": (
            f"{name} [{family}] expect={expect}: {'; '.join(reasons)} | "
            f"DEFAULT rejected={d_rej}: {first_diag(modes['--default'][1])} | "
            f"STRICT rejected={s_rej}: {first_diag(modes['--strict'][1])}"
        ),
    }


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
    assert unique == TOTAL, "duplicate generated program body"

    print(f"ag_uaf: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("outcomes: reject=%d strict=%d accept=%d"
          % (outcomes["reject"], outcomes["strict"], outcomes["accept"]))
    for family, _count, _builder in FAMILIES:
        print(f"  {family}: {families[family]}")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="ag_uaf_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        mismatches = [m for m in pool.map(check_case, cases) if m]

    false_negatives = [m for m in mismatches if m["expect"] != "accept"]
    false_positives = [m for m in mismatches if m["expect"] == "accept"]
    malformed = [m for m in mismatches
                 if any("malformed" in r for r in m["reasons"])]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"(false negatives/unsafe-accepted={len(false_negatives)}, "
          f"false positives/safe-rejected={len(false_positives)}, "
          f"malformed generator cases={len(malformed)})")
    details = sorted(m["detail"] for m in mismatches)
    for line in details[:10]:
        print("  " + line)
    if len(details) > 10:
        print(f"  ... {len(details) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
