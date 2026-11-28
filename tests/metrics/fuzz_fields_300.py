#!/usr/bin/env python3
"""Deterministic SEEDED STRUCT-FIELD / ARRAY-ELEMENT fuzz corpus (390 cases).

Targets ownership and bounds of struct fields and array elements:

  * pointer field assigned malloc, freed, then used via ``s->p`` / ``s->p[i]``;
  * array of structs where one element's pointer is freed and another element's
    alias (or a second element's identical pointer) is used;
  * struct copied by value, then both copies' pointer fields freed;
  * nested structs: outer object freed while an inner pointer field / inner
    array element is still used;
  * struct field used as a bounds-checked buffer (``s->buf[i]`` past extent);
  * ``malloc(sizeof(struct))`` (or smaller) for a struct with a field array
    that is subsequently written through an index;
  * flexible array member writes past the actual allocation;
  * safe controls: distinct fields independently owned, fields freed exactly
    once, field writes within a known extent, free-then-null guards.

Every case declares an expected outcome, following the sibling corpora:

  reject - proven ownership/bounds error: must fail BOTH modes.
  strict - conditional / unverified error: may pass --default, must fail
           --strict.
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Programs come from deterministic parameter sweeps plus a seeded RNG
(FUZZ_SEED); a final mutation family re-rolls its parameters until every
generated program body is distinct.  Cases run in parallel (FUZZ_WORKERS) with
a per-check timeout.  Mismatches are saved under
tests/out/core/fuzz_fields/failures/.  This is a differential measurement, not
a proof of safety.

Environment:
  FUZZ_SEED    default 0xF1E1D5
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
OUT = ROOT / "tests/out/core/fuzz_fields"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xF1E1D5"), 0)
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)
STD = "#include <stdlib.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 50)


def jump(n):
    return RNG.randint(1, 4)


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))


# --- 1. pointer field freed, then dereferenced via s->p ---------------------
for _n in range(1, 13):
    add("field_ptr_free_deref", tpl("field_free_deref_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;s->p[0]=@@V@@;free(s->p);return *s->p;}""", N=_n, V=val()),
        "reject")

# --- 2. pointer field freed, then read via in-bounds s->p[i] ----------------
for _n in range(1, 13):
    add("field_ptr_free_idx", tpl("field_free_idx_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;free(s->p);return s->p[@@K@@];}""", N=_n, K=_n - 1),
        "reject")

# --- 3. pointer field freed, then written via s->p[i] -----------------------
for _n in range(1, 13):
    add("field_ptr_free_write", tpl("field_free_write_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;free(s->p);s->p[@@K@@]=@@V@@;return 0;}""", N=_n, K=_n - 1, V=val()),
        "reject")

# --- 4. pointer to a field-array element used after the struct is freed -----
for _n in range(1, 13):
    add("field_elem_alias_after_free", tpl("field_elem_uaf_@@N@@", N=_n),
        STD + tpl("""struct S{int x[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;int*p=&s->x[@@K@@];free(s);return *p;}""", N=_n, K=_n - 1),
        "reject")

# --- 5. array of structs: element 0's pointer freed, element 1's alias used -
for _n in range(1, 13):
    add("arr_struct_elem_alias", tpl("arr_struct_alias_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S a[2];a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;free(a[0].p);return *a[1].p;}""", N=_n),
        "reject")

# --- 5b. array of structs: share one pointer, free it twice ----------------
for _n in range(1, 13):
    add("arr_struct_elem_df", tpl("arr_struct_df_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S a[2];a[0].p=a[1].p=malloc(@@N@@*sizeof(int));if(!a[0].p)return 2;free(a[0].p);free(a[1].p);return 0;}""", N=_n),
        "reject")

# --- 6. struct copied by value, both copies' pointer fields freed ----------
for _n in range(1, 13):
    add("struct_copy_df", tpl("struct_copy_df_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S a;a.p=malloc(@@N@@*sizeof(int));if(!a.p)return 2;struct S b=a;free(a.p);free(b.p);return 0;}""", N=_n),
        "reject")

# --- 7. nested struct: outer freed, inner pointer field still used ---------
for _n in range(1, 13):
    add("nested_outer_free_inner_ptr", tpl("nested_inner_ptr_@@N@@", N=_n),
        STD + tpl("""struct I{int *p;};struct O{struct I in;};int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p)return 2;free(o);return *o->in.p;}""", N=_n),
        "reject")

# --- 8. nested struct: outer freed, inner array element still used ---------
for _n in range(1, 13):
    add("nested_outer_free_inner_elem", tpl("nested_inner_elem_@@N@@", N=_n),
        STD + tpl("""struct I{int v[@@N@@];};struct O{struct I in;};int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;int*p=&o->in.v[@@K@@];free(o);return *p;}""", N=_n, K=_n - 1),
        "reject")

# --- 9. struct buffer field indexed past its extent (constant) -------------
for _n in range(1, 13):
    add("field_buf_const_oob", tpl("field_buf_const_@@N@@", N=_n),
        STD + tpl("""struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;return s->buf[@@I@@];}""", N=_n, I=_n + jump(_n)),
        "reject")

# --- 10. struct buffer field indexed past its extent (computed index) ------
for _n in range(1, 13):
    add("field_buf_var_oob", tpl("field_buf_var_@@N@@", N=_n),
        STD + tpl("""struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;int i=@@N@@+@@K@@;return s->buf[i];}""", N=_n, K=jump(_n)),
        "reject")

# --- 11. struct buffer indexed by an unknown (argv-derived) index ----------
for _n in range(1, 9):
    add("field_buf_unknown_idx", tpl("field_buf_unknown_@@N@@", N=_n),
        tpl("""struct S{int buf[@@N@@];};int main(int argc,char**v){(void)v;struct S s={{0}};return s.buf[argc+@@K@@];}""", N=_n, K=jump(_n)),
        "strict")

# --- 12. allocation too small for a field array, out-of-bounds write -------
for _n in range(1, 13):
    add("malloc_too_small_field_array", tpl("small_field_arr_@@N@@", N=_n),
        STD + tpl("""struct S{int lo[@@N@@];int hi[@@N@@];};int main(void){struct S*s=malloc(@@M@@*sizeof(int));if(!s)return 2;s->hi[@@K@@]=@@V@@;free(s);return 0;}""", N=_n, M=RNG.randint(1, _n), K=RNG.randint(0, _n - 1), V=val()),
        "reject")

# --- 13. allocation too small for a field array, out-of-bounds read --------
for _n in range(1, 13):
    add("malloc_too_small_field_read", tpl("small_field_read_@@N@@", N=_n),
        STD + tpl("""struct S{int lo[@@N@@];int hi[@@N@@];};int main(void){struct S*s=malloc(@@M@@*sizeof(int));if(!s)return 2;int r=s->hi[@@K@@];free(s);return r;}""", N=_n, M=RNG.randint(1, _n), K=RNG.randint(0, _n - 1)),
        "reject")

# --- 14. flexible array member written past the allocation -----------------
for _n in range(1, 13):
    add("fam_write_past", tpl("fam_past_@@N@@", N=_n),
        STD + tpl("""struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));if(!s)return 2;s->n=@@N@@;s->data[@@I@@]=@@V@@;free(s);return 0;}""", N=_n, I=_n + jump(_n), V=val()),
        "reject")

# --- 15. flexible array member with no tail allocation at all --------------
for _n in range(1, 13):
    add("fam_zero_tail", tpl("fam_zero_@@N@@", N=_n),
        STD + tpl("""struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S));if(!s)return 2;s->n=@@N@@;s->data[0]=@@V@@;free(s);return 0;}""", N=_n, V=val()),
        "reject")

# --- 16. flexible array member indexed by an unknown value -----------------
for _n in range(1, 9):
    add("fam_var_tail", tpl("fam_var_@@N@@", N=_n),
        STD + tpl("""struct S{int n;int data[];};int main(int argc,char**v){(void)v;struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));if(!s)return 2;s->n=@@N@@;s->data[argc]=@@V@@;free(s);return 0;}""", N=_n, V=val()),
        "strict")

# --- 17. safe: distinct fields independently owned -------------------------
for _n in range(1, 13):
    _v = val()
    add("safe_distinct_fields", tpl("safe_two_own_@@N@@", N=_n),
        STD + tpl("""struct S{int *a;int *b;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->a=malloc(@@N@@*sizeof(int));s->b=malloc((@@N@@+1)*sizeof(int));if(!s->a||!s->b)return 2;s->a[0]=@@V@@;s->b[0]=@@V@@+1;int r=*s->a+*s->b;free(s->a);free(s->b);free(s);return r==@@R@@?0:1;}""", N=_n, V=_v, R=2 * _v + 1),
        "accept")

# --- 18. safe: field writes within a known extent --------------------------
for _n in range(1, 13):
    add("safe_field_extent", tpl("safe_extent_@@N@@", N=_n),
        STD + tpl("""struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;for(int i=0;i<@@N@@;i++)s->buf[i]=@@V@@;int r=s->buf[@@K@@];free(s);return r==@@V@@?0:1;}""", N=_n, K=_n - 1, V=val()),
        "accept")

# --- 19. safe: array of structs, each element owns and frees its pointer ---
for _n in range(1, 13):
    add("safe_arr_struct_own", tpl("safe_arr_own_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S a[2];a[0].p=malloc(@@N@@*sizeof(int));a[1].p=malloc((@@N@@+1)*sizeof(int));if(!a[0].p||!a[1].p)return 2;a[0].p[0]=1;a[1].p[0]=2;int r=*a[0].p+*a[1].p;free(a[0].p);free(a[1].p);return r==3?0:1;}""", N=_n),
        "accept")

# --- 20. safe: nested struct with inner pointer owned and freed once -------
for _n in range(1, 13):
    _v = val()
    add("safe_nested_own", tpl("safe_nested_@@N@@", N=_n),
        STD + tpl("""struct I{int *p;};struct O{struct I in;};int main(void){struct O*o=malloc(sizeof*o);if(!o)return 2;o->in.p=malloc(@@N@@*sizeof(int));if(!o->in.p)return 2;*o->in.p=@@V@@;int r=*o->in.p;free(o->in.p);free(o);return r==@@V@@?0:1;}""", N=_n, V=_v),
        "accept")

# --- 21. safe: free the field, null it, and guard the later use ------------
for _n in range(1, 11):
    add("safe_free_then_null", tpl("safe_null_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;free(s->p);s->p=0;int r=s->p?*s->p:0;free(s);return r;}""", N=_n),
        "accept")

# --- 22. strict: conditional free of a pointer field -----------------------
for _n in range(1, 9):
    add("strict_field_cond_free", tpl("strict_cond_free_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(int argc,char**v){(void)v;struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;s->p[0]=@@V@@;if(argc>@@T@@)free(s->p);return *s->p;}""", N=_n, T=RNG.randint(1, 3), V=val()),
        "strict")

# --- 23. strict: conditional double free through a by-value copy ----------
for _n in range(1, 9):
    add("strict_copy_cond_df", tpl("strict_copy_df_@@N@@", N=_n),
        STD + tpl("""struct S{int *p;};int main(int argc,char**v){(void)v;struct S a;a.p=malloc(@@N@@*sizeof(int));if(!a.p)return 2;struct S b=a;if(argc>@@T@@)free(a.p);free(b.p);return 0;}""", N=_n, T=RNG.randint(1, 3)),
        "strict")

# ---------------------------------------------------------------------------
# Seeded mutation fuzz: 120 more programs.  Parameters are re-rolled until the
# body is genuinely distinct from every earlier case (seeded => reproducible).
# ---------------------------------------------------------------------------
for _i in range(120):
    while True:
        _n = RNG.randint(2, 16)
        _k = RNG.randint(0, _n - 1)
        _j = jump(_n)
        _v = val()
        _t = RNG.randint(1, 3)
        _kind = _i % 8
        if _kind == 0:
            _family = "mut_field_free_deref"
            _name = tpl("mut_ff_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int *p;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;s->p[0]=@@V@@;free(s->p);return s->p[@@K@@];}""", N=_n, K=_k, V=_v)
            _expect = "reject"
        elif _kind == 1:
            _family = "mut_nested_copy_df"
            _name = tpl("mut_nc_@@I@@", I=_i)
            _body = STD + tpl("""struct I{int *p;};struct O{struct I in;};int main(void){struct O a;a.in.p=malloc(@@N@@*sizeof(int));if(!a.in.p)return 2;a.in.p[0]=@@V@@;struct O b=a;free(a.in.p);free(b.in.p);return 0;}""", N=_n, V=_v)
            _expect = "reject"
        elif _kind == 2:
            _family = "mut_field_buf_oob"
            _name = tpl("mut_fb_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->buf[0]=@@V@@;return s->buf[@@I2@@];}""", N=_n, I2=_n + _j, V=_v)
            _expect = "reject"
        elif _kind == 3:
            _family = "mut_safe_extent"
            _name = tpl("mut_se_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int buf[@@N@@];};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;for(int i=0;i<@@N@@;i++)s->buf[i]=@@V@@;int r=s->buf[@@K@@];free(s);return r==@@V@@?0:1;}""", N=_n, K=_k, V=_v)
            _expect = "accept"
        elif _kind == 4:
            _family = "mut_fam_oob"
            _name = tpl("mut_fo_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int n;int data[];};int main(void){struct S*s=malloc(sizeof(struct S)+@@N@@*sizeof(int));if(!s)return 2;s->n=@@N@@;s->data[@@I2@@]=@@V@@;free(s);return 0;}""", N=_n, I2=_n + _j, V=_v)
            _expect = "reject"
        elif _kind == 5:
            _family = "mut_cond_free"
            _name = tpl("mut_cf_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int *p;};int main(int argc,char**v){(void)v;struct S*s=malloc(sizeof*s);if(!s)return 2;s->p=malloc(@@N@@*sizeof(int));if(!s->p)return 2;s->p[0]=@@V@@;if(argc>@@T@@)free(s->p);return *s->p;}""", N=_n, T=_t, V=_v)
            _expect = "strict"
        elif _kind == 6:
            _family = "mut_arr_struct_uaf"
            _name = tpl("mut_as_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int x[@@N@@];};int main(void){struct S*s=malloc(2*sizeof*s);if(!s)return 2;s[1].x[0]=@@V@@;int*p=&s[1].x[@@K@@];free(s);return *p;}""", N=_n, K=_k, V=_v)
            _expect = "reject"
        else:
            _family = "mut_safe_two_fields"
            _name = tpl("mut_st_@@I@@", I=_i)
            _body = STD + tpl("""struct S{int *a;int *b;};int main(void){struct S*s=malloc(sizeof*s);if(!s)return 2;s->a=malloc(@@N@@*sizeof(int));s->b=malloc((@@N@@+1)*sizeof(int));if(!s->a||!s->b)return 2;*s->a=@@V@@;*s->b=@@V@@+1;int r=*s->a+*s->b;free(s->a);free(s->b);free(s);return r==@@R@@?0:1;}""", N=_n, V=_v, R=2 * _v + 1)
            _expect = "accept"
        if (_body.strip() + "\n") in SEEN:
            continue
        add(_family, _name, _body, _expect)
        break


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
        return name, [], modes

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
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    detail = ("%s [%s] expect=%s reasons=%s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  name, family, expect, ";".join(reasons),
                  d_rej, first_diag(modes["--default"][1]),
                  s_rej, first_diag(modes["--strict"][1])))
    return name, [detail], modes


def main():
    bodies = [b for _, _, b, _ in CASES]
    unique = len(set(bodies))
    counts = Counter(e for _, _, _, e in CASES)
    print("fields fuzz: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for _name, fails, _modes in pool.map(run_case, CASES):
            mismatches.extend(fails)
    mismatches.sort()

    print("results: %d cases, %d mismatches" % (len(CASES), len(mismatches)))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
