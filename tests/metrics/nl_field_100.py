#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator: >=100 distinct small, valid C
programs probing NULLABILITY of pointer FIELDS and array elements.

Every case is generated programmatically from parameter sweeps plus a seeded
RNG (FUZZ_SEED, default 0x6E6C4669).  Each program is a small, self-contained
C translation unit; all generated bodies are distinct (enforced by SEEN and by
a case-specific marker comment).  Re-running with the same seed is reproducible.
Cases run in parallel (FUZZ_WORKERS, default 8) with a 60s per-check timeout.

Families (11 total, each contributing reject/strict/accept cases):

   1  field_null_assign     s.p=NULL then s.p->x / s.p->x=... / s.p[k]   (reject)
   2  arr_elem_null         arr[i]=NULL then arr[i]->x / arr[i][0]        (reject)
   3  struct_zero_init      struct S s={0}; then member deref            (reject)
   4  malloc_uninit_field   malloc'd struct, uninitialized pointer field  (strict)
   5  calloc_field_null     calloc'd struct zero-fills pointer field      (reject)
   6  nested_field_null     nested struct field set NULL, then deref      (reject)
   7  field_libc            pointer field passed to strlen/strcmp/memcpy  (reject)
   8  field_guarded         pointer field guarded with if(s.p) / ternary  (accept)
   9  arr_struct_null_fields array-of-struct pointer fields set NULL       (reject)
  10  global_field_null     global struct pointer field set NULL          (reject)
  11  safe_controls         proper guards / checked alloc / real address  (accept)

Declared expectations follow the sibling generators (ag_init_100 / ag2_*):

   reject - proven nullability error: must be rejected in BOTH --default and
            --strict.  Acceptance in either mode is a safety hole.
   strict - conditional / unverified error (e.g. an indeterminate, uninitialized
            heap pointer field): MAY pass --default, must fail --strict.
   accept - valid control with a real guard / real allocation: must pass BOTH
            modes (rejection is a false positive).

Mismatches are printed and the generated source plus a full diagnostic log are
saved under tests/out/core/nl_field/failures/.  This is a differential
measurement over the declared subset, not a proof of safety.

Environment:
   FUZZ_SEED    default 0x6E6C4669
   FUZZ_WORKERS default 8
   CPLUS_BIN    default <root>/build/c+
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
OUT = ROOT / "tests/out/core/nl_field"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x6E6C4669"), 0)
TIMEOUT = 60

CASES = []   # list of dict(name, body, expect, family)
SEEN = set()  # exact generated program bodies, used to enforce distinctness
RNG = random.Random(SEED)

NS = range(1, 6)  # 5-value parameter sweep, seeded values filled below

# stdlib.h defines NULL, malloc, calloc, free; add string.h/stdio.h as needed.
BASE = "#include <stdlib.h>\n"
STR = "#include <string.h>\n"
IO = "#include <stdio.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in Python format strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 97)


def small():
    return RNG.randint(0, 7)


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    # A deterministic, case-specific marker makes every generated program
    # unique in source text (comments are valid C and do not change behavior).
    normalized = (f"/* nl_field seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({"name": name, "body": normalized,
                  "expect": expect, "family": family})


# ---------------------------------------------------------------------------
# 1. field_null_assign: s.p=NULL then s.p->x / s.p->x=... / s.p[k]
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    k = RNG.randint(0, n - 1)
    add("field_null_assign", tpl("fna_read_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S s;s.p=NULL;return s.p->x+@@V@@;}", V=v),
        "reject")
    add("field_null_assign", tpl("fna_write_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S s;s.p=NULL;s.p->x=@@V@@;return 0;}", V=v),
        "reject")
    add("field_null_assign", tpl("fna_sub_@@N@@", N=n),
        BASE + tpl("struct S{int*p;};int main(void){struct S s;"
                   "s.p=NULL;return s.p[@@K@@]+@@V@@;}", K=k, V=v),
        "reject")
    add("field_null_assign", tpl("fna_helper_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "static int rd(struct S*s){return s->p->x;}"
                   "int main(void){struct S s;s.p=NULL;return rd(&s)+@@V@@;}", V=v),
        "reject")

# ---------------------------------------------------------------------------
# 2. arr_elem_null: arr[i]=NULL then arr[i]->x / arr[i][0]
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    k = RNG.randint(0, n - 1)
    add("arr_elem_null", tpl("aen_read_@@N@@", N=n),
        BASE + tpl("struct N{int x;};int main(void){struct N*a[@@N@@];"
                   "a[@@K@@]=NULL;return a[@@K@@]->x+@@V@@;}", N=n, K=k, V=v),
        "reject")
    add("arr_elem_null", tpl("aen_write_@@N@@", N=n),
        BASE + tpl("struct N{int x;};int main(void){struct N*a[@@N@@];"
                   "a[@@K@@]=NULL;a[@@K@@]->x=@@V@@;return 0;}", N=n, K=k, V=v),
        "reject")
    add("arr_elem_null", tpl("aen_sub_@@N@@", N=n),
        BASE + tpl("int main(void){int*a[@@N@@];a[@@K@@]=NULL;"
                   "return a[@@K@@][0]+@@V@@;}", N=n, K=k, V=v),
        "reject")

# ---------------------------------------------------------------------------
# 3. struct_zero_init: struct S s={0}; then member deref
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("struct_zero_init", tpl("szi_read_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S s={0};return s.p->x+@@V@@;}", V=v),
        "reject")
    add("struct_zero_init", tpl("szi_write_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S s={0};s.p->x=@@V@@;return 0;}", V=v),
        "reject")

# ---------------------------------------------------------------------------
# 4. malloc_uninit_field: malloc'd struct, uninitialized pointer field
#    The field value is indeterminate (may be NULL) -> declared "strict".
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    k = small()
    add("malloc_uninit_field", tpl("muf_read_@@N@@", N=n),
        BASE + tpl("struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                   "if(!s)return 2;int r=*s->p;free(s);return r+@@V@@;}", V=v),
        "strict")
    add("malloc_uninit_field", tpl("muf_write_@@N@@", N=n),
        BASE + tpl("struct S{int*p;};int main(void){struct S*s=malloc(sizeof*s);"
                   "if(!s)return 2;s->p[@@K@@]=@@V@@;free(s);return 0;}", K=k, V=v),
        "strict")

# ---------------------------------------------------------------------------
# 5. calloc_field_null: calloc zero-fills the pointer field -> proven NULL.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("calloc_field_null", tpl("cfn_read_@@N@@", N=n),
        BASE + tpl("struct S{int*p;};int main(void){struct S*s=calloc(1,sizeof*s);"
                   "if(!s)return 2;int r=*s->p;free(s);return r+@@V@@;}", V=v),
        "reject")
    add("calloc_field_null", tpl("cfn_member_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S*s=calloc(1,sizeof*s);if(!s)return 2;"
                   "int r=s->p->x;free(s);return r+@@V@@;}", V=v),
        "reject")

# ---------------------------------------------------------------------------
# 6. nested_field_null: nested struct field set NULL, then deref
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("nested_field_null", tpl("nfn_read_@@N@@", N=n),
        BASE + tpl("struct N{int*p;};struct O{struct N in;};"
                   "int main(void){struct O o;o.in.p=NULL;return *o.in.p+@@V@@;}", V=v),
        "reject")
    add("nested_field_null", tpl("nfn_deep_@@N@@", N=n),
        BASE + tpl("struct N{int*p;};struct M{struct N in;};struct O{struct M in;};"
                   "int main(void){struct O o;o.in.in.p=NULL;"
                   "return *o.in.in.p+@@V@@;}", V=v),
        "reject")

# ---------------------------------------------------------------------------
# 7. field_libc: pointer field passed to libc routines that dereference it
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("field_libc", tpl("flc_strlen_@@N@@", N=n),
        BASE + STR + tpl("struct S{char*p;};int main(void){struct S s;s.p=NULL;"
                         "return (int)strlen(s.p)+@@V@@;}", V=v),
        "reject")
    add("field_libc", tpl("flc_strcmp_@@N@@", N=n),
        BASE + STR + tpl('struct S{char*p;};int main(void){struct S s;s.p=NULL;'
                         'return strcmp(s.p,"abc")+@@V@@;}', V=v),
        "reject")
    add("field_libc", tpl("flc_memcpy_@@N@@", N=n),
        BASE + STR + tpl("struct S{char*p;};int main(void){char d[@@N@@];"
                         "struct S s;s.p=NULL;memcpy(d,s.p,@@N@@);return 0;}", N=n),
        "reject")

# ---------------------------------------------------------------------------
# 8. field_guarded: pointer field guarded with if(s.p) / ternary (accept)
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("field_guarded", tpl("fgd_if_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S s;s.p=NULL;if(!s.p)return 0;"
                   "return s.p->x+@@V@@;}", V=v),
        "accept")
    add("field_guarded", tpl("fgd_ternary_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S s;s.p=NULL;return s.p?s.p->x:@@V@@;}", V=v),
        "accept")

# ---------------------------------------------------------------------------
# 9. arr_struct_null_fields: array-of-struct pointer fields set NULL
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    k = RNG.randint(0, n - 1)
    add("arr_struct_null_fields", tpl("asf_read_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S a[@@N@@];a[@@K@@].p=NULL;"
                   "return a[@@K@@].p->x+@@V@@;}", N=n, K=k, V=v),
        "reject")
    add("arr_struct_null_fields", tpl("asf_write_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S a[@@N@@];a[@@K@@].p=NULL;"
                   "a[@@K@@].p->x=@@V@@;return 0;}", N=n, K=k, V=v),
        "reject")
    add("arr_struct_null_fields", tpl("asf_loop_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct S a[@@N@@];"
                   "for(int i=0;i<@@N@@;i++)a[i].p=NULL;"
                   "return a[0].p->x+@@V@@;}", N=n, V=v),
        "reject")

# ---------------------------------------------------------------------------
# 10. global_field_null: global struct pointer field set NULL, then deref
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    k = RNG.randint(0, n - 1)
    add("global_field_null", tpl("gfn_read_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};struct S g;"
                   "int main(void){g.p=NULL;return g.p->x+@@V@@;}", V=v),
        "reject")
    add("global_field_null", tpl("gfn_array_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};struct S g[@@N@@];"
                   "int main(void){g[@@K@@].p=NULL;return g[@@K@@].p->x+@@V@@;}",
                   N=n, K=k, V=v),
        "reject")

# ---------------------------------------------------------------------------
# 11. safe_controls: proper guards, checked allocation, real addresses (accept)
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("safe_controls", tpl("sct_stack_@@N@@", N=n),
        BASE + tpl("struct N{int x;};struct S{struct N*p;};"
                   "int main(void){struct N n;n.x=@@V@@;struct S s;s.p=&n;"
                   "return s.p->x==@@V@@?0:1;}", V=v),
        "accept")
    add("safe_controls", tpl("sct_checked_@@N@@", N=n),
        BASE + tpl("struct S{int*p;};int main(void){struct S s;"
                   "s.p=malloc(@@N@@*sizeof(int));if(!s.p)return 2;"
                   "s.p[0]=@@V@@;int r=s.p[0];free(s.p);return r==@@V@@?0:1;}",
                   N=n, V=v),
        "accept")
    add("safe_controls", tpl("sct_libc_guard_@@N@@", N=n),
        BASE + STR + tpl("struct S{char*p;};int main(void){struct S s;s.p=NULL;"
                         "if(!s.p)return 0;return (int)strlen(s.p)+@@V@@;}", V=v),
        "accept")


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
    name, body = case["name"], case["body"]
    expect, family = case["expect"], case["family"]
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT, "TIMEOUT")
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc, "OS-ERROR")
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag, first_diag(diag))

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    false_positive = False
    if expect == "reject":
        if not d_rej:
            reasons.append("DEFAULT-HOLE")
        if not s_rej:
            reasons.append("STRICT-HOLE")
    elif expect == "strict":
        if not s_rej:
            reasons.append("STRICT-HOLE")
    else:  # accept
        false_positive = d_rej or s_rej
        if d_rej:
            reasons.append("DEFAULT-FALSE-POSITIVE")
        if s_rej:
            reasons.append("STRICT-FALSE-POSITIVE")

    if not reasons:
        return (family, name, expect, [], modes, False)

    # Save the generated source and a full diagnostic log for every mismatch.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s: %s" % (d_rej, modes["--default"][2]),
        "--strict  rejected=%s: %s" % (s_rej, modes["--strict"][2]),
        "",
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    detail = ("%s/%s [expect=%s] %s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  family, name, expect, ";".join(reasons),
                  d_rej, modes["--default"][2],
                  s_rej, modes["--strict"][2]))
    return (family, name, expect, [detail], modes, false_positive)


def main():
    bodies = [c["body"] for c in CASES]
    unique = len(set(bodies))
    counts = Counter(c["expect"] for c in CASES)
    fams = Counter(c["family"] for c in CASES)

    print("nl_field: %d generated cases (%d distinct), seed=0x%X, workers=%d, bin=%s"
          % (len(CASES), unique, SEED, WORKERS, CC))
    print("outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families (%d): %s" % (
        len(fams), ", ".join("%s=%d" % (k, v) for k, v in sorted(fams.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    mismatch_names = []
    holes = 0
    false_positives = 0
    by_family = Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for family, name, expect, fails, _modes, fp in pool.map(run_case, CASES):
            if not fails:
                continue
            mismatches.extend(fails)
            mismatch_names.append(name)
            by_family[family] += 1
            if fp or expect == "accept":
                false_positives += 1
            else:
                holes += 1

    mismatches.sort()
    mismatch_names.sort()
    print("results: %d cases, %d mismatches (%d safety holes, %d false positives)"
          % (len(CASES), len(mismatches), holes, false_positives))
    if by_family:
        print("mismatches by family: " + ", ".join(
            "%s=%d" % (k, v) for k, v in sorted(by_family.items())))
    print("false positives: %d" % false_positives)
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
          % (status, len(CASES), len(mismatches), false_positives))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
