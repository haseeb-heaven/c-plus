#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator for NULL-gap C programs (>=100).

Produces 126 distinct small, valid C translation units (>=100) from per-case
independent seeded RNGs.  Generation and checking both run in thread pools;
``pool.map`` preserves input order and every case draws from
``random.Random(f"{SEED}:{i}")``, so a run is fully reproducible regardless of
``FUZZ_WORKERS``.  Every program carries a unique case marker plus varied
types, values, indices and identifiers, so no case is a copy of another.  All
programs are plain C (no C+ extension syntax).

The families target specific NULL-handling gaps:

  field_null_deref   struct pointer field set to NULL, then dereferenced (reject)
  array_null_use     array element set to NULL, then dereferenced / used  (reject)
  calloc_field_null  calloc()'d struct -> fields are NULL, then deref     (reject)
  libc_sink_null     NULL handed to fclose / printf("%%s") / strlen / memcpy
                                                                         (reject)
  cast_null_deref    null produced via (T*)NULL / (void*)0 cast, then used(reject)
  guard_deref        guarded deref forms (if(p), !p, !=NULL, ?:, &&)      (accept)
  null_to_addr       p=NULL reassigned to &local before deref             (accept)
  safe_control       explicit guards / malloc+calloc checks / free(NULL)  (accept)

Each case declares one expected outcome:

  reject - provably unsafe NULL use: must fail BOTH --default and --strict.
           Acceptance in either mode is a safety hole (false negative / miss).
  accept - valid, safe control: must pass BOTH modes; rejection is a false
           positive.

Mismatches are printed and the generated source plus full diagnostics are
saved under tests/out/core/hw_null/failures/.  False positives (safe controls
rejected by the checker) are counted and reported separately.  This is a
differential measurement of the checker, not a proof of NULL safety, and it
does not modify the compiler.

Environment:
  FUZZ_SEED     default 0x4857304C
  FUZZ_WORKERS  default 8
  FUZZ_TIMEOUT  default 60 seconds per check
  CPLUS_BIN     default <root>/build/c+ (existing binary; no rebuild needed)
"""
import collections
import concurrent.futures
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+"))
OUT = ROOT / "tests" / "out" / "core" / "hw_null"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4857304C"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))
STAGE = None

STDDEF = "#include <stddef.h>\n"
STDLIB = "#include <stdlib.h>\n"
STDIO = "#include <stdio.h>\n"
STRING = "#include <string.h>\n"

TYPES = ("int", "short", "long", "char", "unsigned", "unsigned long")
FIELDS = ("f", "ptr", "p", "data", "link")
VARS = ("p", "q", "ptr", "sp", "vp")
LOCS = ("x", "y", "cell", "tmp")


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hw_null seed={SEED:#x} case={name} */\n" + body.strip() + "\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping and %-formatting in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


# ---------------------------------------------------------------------------
# Unsafe families (declared "reject").
# ---------------------------------------------------------------------------
def gen_field_null_deref(i, rng):
    t = rng.choice(TYPES)
    v = rng.randint(1, 97)
    st, fld = "S%d" % i, rng.choice(FIELDS)
    shape = i % 3
    if shape == 0:
        body = STDDEF + tpl(
            "struct @@ST@@{@@T@@ *@@F@@;};\n"
            "int main(void){struct @@ST@@ s; s.@@F@@=NULL; return (int)*s.@@F@@;}",
            ST=st, T=t, F=fld)
    elif shape == 1:
        body = STDDEF + tpl(
            "struct @@ST@@{@@T@@ *@@F@@;};\n"
            "int main(void){struct @@ST@@ s; s.@@F@@=(@@T@@*)0; s.@@F@@[0]=@@V@@; return 0;}",
            ST=st, T=t, F=fld, V=v)
    else:
        body = STDDEF + tpl(
            "struct @@ST@@{int tag; @@T@@ *@@F@@;};\n"
            "int main(void){struct @@ST@@ s; s.tag=@@V@@; s.@@F@@=NULL;"
            " return (int)s.@@F@@[0];}",
            ST=st, T=t, F=fld, V=v)
    return f"field_null_deref_{i}", body


def gen_array_null_use(i, rng):
    t = rng.choice(TYPES)
    v = rng.randint(1, 97)
    n, idx = rng.randint(3, 6), 0
    idx = rng.randrange(n)
    shape = i % 3
    if shape == 0:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *a[@@N@@]; a[@@I@@]=NULL; return (int)*a[@@I@@];}",
            T=t, N=n, I=idx)
    elif shape == 1:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *a[@@N@@]={0}; a[@@I@@]=(@@T@@*)NULL;"
            " a[@@I@@][0]=@@V@@; return 0;}",
            T=t, N=n, I=idx, V=v)
    else:
        body = STDDEF + tpl(
            "int main(void){@@T@@ @@L@@=@@V@@; @@T@@ *a[@@N@@]={0};"
            " a[@@I@@]=NULL; return *a[@@I@@]==@@L@@;}",
            T=t, N=n, I=idx, V=v, L=rng.choice(LOCS))
    return f"array_null_use_{i}", body


def gen_calloc_field_null(i, rng):
    t = rng.choice(TYPES)
    v = rng.randint(1, 97)
    st, fld = "S%d" % i, rng.choice(FIELDS)
    n, idx = rng.randint(2, 5), 0
    idx = rng.randrange(n)
    shape = i % 3
    if shape == 0:
        body = STDLIB + STDDEF + tpl(
            "struct @@ST@@{@@T@@ *@@F@@;};\n"
            "int main(void){struct @@ST@@ *p=calloc(1,sizeof(*p)); if(!p)return 2;"
            " return (int)*p->@@F@@;}",
            ST=st, T=t, F=fld)
    elif shape == 1:
        body = STDLIB + STDDEF + tpl(
            "struct @@ST@@{int tag; @@T@@ *@@F@@;};\n"
            "int main(void){struct @@ST@@ *p=calloc(@@N@@,sizeof(*p)); if(!p)return 2;"
            " p[@@I@@].@@F@@[0]=@@V@@; free(p); return 0;}",
            ST=st, T=t, F=fld, N=n, I=idx, V=v)
    else:
        body = STDLIB + STDDEF + tpl(
            "struct @@ST@@{@@T@@ *@@F@@; @@T@@ *@@G@@;};\n"
            "int main(void){struct @@ST@@ *p=calloc(1,sizeof(*p)); if(!p)return 2;"
            " int r=(int)*p->@@F@@ + (int)*p->@@G@@; free(p); return r;}",
            ST=st, T=t, F=fld, G=fld + "_b")
    return f"calloc_field_null_{i}", body


def gen_libc_sink_null(i, rng):
    n, var = rng.randint(2, 9), rng.choice(VARS)
    sink, shape = i % 4, (i // 4) % 2
    if sink == 0:  # fclose(NULL)
        if shape == 0:
            body = STDIO + STDDEF + tpl(
                "int main(void){FILE *@@V@@=NULL; fclose(@@V@@); return 0;}", V=var)
        else:
            body = STDIO + tpl(
                "int main(void){FILE *f=fopen(\"nope\",\"r\"); if(f)fclose(f);"
                " fclose(NULL); return 0;}")
    elif sink == 1:  # printf("%s", NULL)
        if shape == 0:
            body = STDIO + STDDEF + tpl(
                "int main(void){char *@@V@@=NULL; printf(\"%s\\n\", @@V@@); return 0;}",
                V=var)
        else:
            body = STDIO + STDDEF + tpl(
                "int main(void){char *@@V@@=(char*)0; printf(\"x=%s\", @@V@@); return @@N@@;}",
                V=var, N=n)
    elif sink == 2:  # strlen(NULL)
        if shape == 0:
            body = STDIO + STRING + STDDEF + tpl(
                "int main(void){char *@@V@@=NULL; return (int)strlen(@@V@@);}", V=var)
        else:
            body = STDIO + STRING + STDDEF + tpl(
                "int main(void){char *@@V@@=(char*)NULL; size_t k=strlen(@@V@@);"
                " return (int)k;}", V=var)
    else:  # memcpy with NULL source / destination
        if shape == 0:
            body = STRING + STDDEF + tpl(
                "int main(void){char d[@@N@@]; char *@@V@@=NULL;"
                " memcpy(d, @@V@@, (size_t)@@N@@); return 0;}", N=n, V=var)
        else:
            body = STRING + STDDEF + tpl(
                "int main(void){char d[@@N@@]; char s[@@N@@]; s[0]='a';"
                " memcpy(d, NULL, (size_t)@@N@@); return d[0]==s[0];}", N=n)
    return f"libc_sink_null_{i}", body


def gen_cast_null_deref(i, rng):
    t, var = rng.choice(TYPES), rng.choice(VARS)
    v = rng.randint(1, 97)
    shape = i % 3
    if shape == 0:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@V@@=(@@T@@*)NULL; return (int)*@@V@@;}", T=t, V=var)
    elif shape == 1:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@V@@=(void*)0; @@V@@[0]=@@N@@; return 0;}",
            T=t, V=var, N=v)
    else:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@V@@=NULL; @@V@@=(@@T@@*)0; return (int)@@V@@[0];}",
            T=t, V=var)
    return f"cast_null_deref_{i}", body


# ---------------------------------------------------------------------------
# Safe families (declared "accept").
# ---------------------------------------------------------------------------
def gen_guard_deref(i, rng):
    t, var = rng.choice(TYPES), rng.choice(VARS)
    v, loc = rng.randint(1, 97), rng.choice(LOCS)
    shape = i % 6
    if shape == 0:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=NULL; if(@@P@@!=NULL)return (int)*@@P@@; return @@V@@;}",
            T=t, P=var, V=v)
    elif shape == 1:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=NULL; if(@@P@@==NULL)return @@V@@; return (int)*@@P@@;}",
            T=t, P=var, V=v)
    elif shape == 2:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=NULL; if(!@@P@@)return @@V@@; @@P@@[0]=@@V@@; return 0;}",
            T=t, P=var, V=v)
    elif shape == 3:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=NULL; return @@P@@?(int)*@@P@@:@@V@@;}",
            T=t, P=var, V=v)
    elif shape == 4:
        body = STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=NULL; if(@@P@@ && *@@P@@)return @@V@@; return 0;}",
            T=t, P=var, V=v)
    else:
        body = STDDEF + tpl(
            "int main(void){@@T@@ @@L@@=@@V@@; @@T@@ *@@P@@=&@@L@@;"
            " if(@@P@@){return (int)*@@P@@;} return @@V@@;}",
            T=t, P=var, V=v, L=loc)
    return f"guard_deref_{i}", body


def gen_null_to_addr(i, rng):
    t, var = rng.choice(TYPES), rng.choice(VARS)
    v, loc = rng.randint(1, 97), rng.choice(LOCS)
    shape = i % 3
    if shape == 0:
        body = STDDEF + tpl(
            "int main(void){@@T@@ @@L@@=@@V@@; @@T@@ *@@P@@=NULL; @@P@@=&@@L@@; return (int)*@@P@@;}",
            T=t, P=var, V=v, L=loc)
    elif shape == 1:
        body = STDDEF + tpl(
            "int main(void){@@T@@ @@L@@=@@V@@; @@T@@ *@@P@@=(@@T@@*)NULL;"
            " @@P@@=&@@L@@; *@@P@@=@@V@@; return (int)*@@P@@;}",
            T=t, P=var, V=v, L=loc)
    else:
        body = STDDEF + tpl(
            "int main(void){@@T@@ @@L@@=@@V@@; @@T@@ *@@P@@=0; @@P@@=&@@L@@;"
            " return @@P@@?(int)*@@P@@:@@V@@;}",
            T=t, P=var, V=v, L=loc)
    return f"null_to_addr_{i}", body


def gen_safe_control(i, rng):
    t, var = rng.choice(TYPES), rng.choice(VARS)
    v, loc = rng.randint(1, 97), rng.choice(LOCS)
    shape = i % 4
    if shape == 0:
        body = STDLIB + STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=malloc(sizeof(@@T@@)); if(!@@P@@)return 2;"
            " *@@P@@=@@V@@; free(@@P@@); return 0;}", T=t, P=var, V=v)
    elif shape == 1:
        body = STDDEF + tpl(
            "int main(void){@@T@@ @@L@@=@@V@@; @@T@@ *@@P@@=&@@L@@;"
            " if(@@P@@!=NULL)return (int)*@@P@@; return @@V@@;}",
            T=t, P=var, V=v, L=loc)
    elif shape == 2:
        body = STDLIB + STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=NULL; free(@@P@@); free(NULL); return 0;}",
            T=t, P=var)
    else:
        body = STDLIB + STDDEF + tpl(
            "int main(void){@@T@@ *@@P@@=calloc(1,sizeof(@@T@@)); if(@@P@@){*@@P@@=@@V@@;"
            " free(@@P@@);} return @@V@@;}", T=t, P=var, V=v)
    return f"safe_control_{i}", body


# family, number of cases, builder, declared outcome.
FAMILIES = (
    ("field_null_deref",  16, gen_field_null_deref,  "reject"),
    ("array_null_use",    14, gen_array_null_use,    "reject"),
    ("calloc_field_null", 14, gen_calloc_field_null, "reject"),
    ("libc_sink_null",    20, gen_libc_sink_null,    "reject"),
    ("cast_null_deref",   14, gen_cast_null_deref,   "reject"),
    ("guard_deref",       18, gen_guard_deref,       "accept"),
    ("null_to_addr",      14, gen_null_to_addr,      "accept"),
    ("safe_control",      16, gen_safe_control,      "accept"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)
assert TOTAL >= 100, "generator must produce at least 100 cases"


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, builder, expect) in FAMILIES:
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body = builder(i, rng)
            return name, marked(name, body), expect, family
        start += count
    raise IndexError(i)


# ---------------------------------------------------------------------------
# Verification: check each generated case in DEFAULT and STRICT.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def diag_tags(text):
    """Unique CPLUS-* diagnostic tags emitted for a case (for reporting)."""
    return tuple(dict.fromkeys(re.findall(r"\[(CPLUS-[A-Z0-9-]+)\]", text)))


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
    else:  # accept
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    tags = {
        "--default": diag_tags(modes["--default"][1]),
        "--strict": diag_tags(modes["--strict"][1]),
    }
    result = {
        "name": name, "family": family, "expect": expect,
        "reasons": reasons, "d_rej": d_rej, "s_rej": s_rej,
        "tags": tags, "modes": modes, "body": body,
    }
    if reasons:
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
    return result


def main():
    global STAGE
    OUT.mkdir(parents=True, exist_ok=True)
    FAILS.mkdir(parents=True, exist_ok=True)
    # Clear fixtures from an earlier run of this generator only.
    for stale in FAILS.glob("*.c"):
        stale.unlink()
    for stale in FAILS.glob("*.diag.txt"):
        stale.unlink()

    # Parallel, deterministic generation: pool.map preserves order and each
    # case uses its own seeded RNG, so worker count cannot affect the output.
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        cases = list(pool.map(generate_case, range(TOTAL)))

    bodies = [c[1] for c in cases]
    unique = len(set(bodies))
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len({c[0] for c in cases}) == TOTAL, "duplicate case name"

    print(f"hw_null: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d accept=%d"
          % (outcomes["reject"], outcomes["accept"]))
    expects = {f: e for f, c, b, e in FAMILIES}
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:20} {families[family]:3d}  (expect {expects[family]})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")
    print("first 10 case names: "
          + ", ".join(c[0] for c in cases[:10]))

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hw_null_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    mismatches = [r for r in results if r["reasons"]]
    mismatches.sort(key=lambda r: r["name"])
    false_positives = [r for r in mismatches if r["expect"] == "accept"]
    misses = [r for r in mismatches if r["expect"] == "reject"]

    print(f"results: {len(cases)} cases, {len(mismatches)} mismatches "
          f"({len(misses)} misses / unsafe-accepted, "
          f"{len(false_positives)} false positives)")
    print("mismatches by expectation: " + " ".join(
        "%s=%d" % (k, sum(1 for r in mismatches if r["expect"] == k))
        for k in ("reject", "accept")))
    print("false positives (safe rejected): "
          + ("none" if not false_positives else
             ", ".join(f"{r['family']}/{r['name']}" for r in false_positives)))

    by_family = collections.Counter(r["family"] for r in mismatches)
    print("mismatches by family: "
          + ("none" if not by_family else
             " ".join(f"{k}={by_family[k]}" for k in sorted(by_family))))

    print("first 10 mismatch names: "
          + ("none" if not mismatches else
             ", ".join(r["name"] for r in mismatches[:10])))
    for r in mismatches[:10]:
        print(f"  {r['family']}/{r['name']} [expect={r['expect']}] "
              f"{'; '.join(r['reasons'])} | default tags={r['tags']['--default']} "
              f"strict tags={r['tags']['--strict']}")
    if len(mismatches) > 10:
        print(f"  ... {len(mismatches) - 10} more (saved under {FAILS})")
    if mismatches:
        print(f"saved {len(mismatches)} mismatch fixtures in {FAILS}")

    status = "PASS" if not mismatches and unique == len(cases) else "FAIL"
    print(f"FINAL: {status} ({len(cases)} cases, {len(mismatches)} mismatches, "
          f"{len(false_positives)} false positives)")
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
