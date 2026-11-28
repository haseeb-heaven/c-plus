#!/usr/bin/env python3
"""Deterministic GENERATED HEAP-EXTENT corpus for C+ (270+ distinct cases).

Every case is produced programmatically from parameter sweeps plus a seeded RNG
(FUZZ_SEED, default 0x4E1C). Nothing is hand-copied from another corpus: each
program varies in allocation size, element type, index, offset, literal length,
loop bound, and function shape.

Covered heap-extent families:

  hb_write_*   malloc(n bytes) then write at / beyond n (int and char views)
  hb_read_*    read at / beyond the allocated extent
  hb_isz_*     int vs char element-size mistakes (allocation in bytes, int use)
  hb_calloc_*  calloc(count, size) extent violations
  hb_re_*      realloc to a new extent, then use past the new extent
  hb_sz_*      wrong sizeof (char/int/float vs int/long/double element)
  hb_str_*     string allocation missing the terminating +1
  hb_fam_*     flexible array member indexed past the allocation
  hb_st_*      undersized struct allocation
  hb_uf_*      heap underflow (negative index)
  hb_loop_*    off-by-one loops over heap storage
  hb_fn_*      extent passed across functions
  strict_*     conditional / unverified extent violations (argc, parameter,
               volatile index) - MAY pass --default, must fail --strict
  mut_*        seeded fuzz mix of the patterns above
  ok_*         valid controls (must pass both modes)

Each case declares an expected outcome:

  reject - proven heap extent violation: must fail BOTH --default and --strict
           (accepting it in either mode is a safety hole).
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Any mismatch (unsafe accepted or safe rejected) is printed and the generated
source plus a full diagnostic log is saved under
tests/out/core/ag_heap/failures/. Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout. The generator is seeded, so a run is
reproducible. All generated programs are plain C (no C+ extension syntax).
This is a differential measurement, not a proof of heap safety.
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/ag_heap"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4E1C"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))
NVALS = range(2, 9)  # 7 distinct allocation sizes

CASES = []    # list of (name, body, expect, category)
SEEN = set()  # exact generated program bodies, used to enforce distinctness
NAMES = set()

STD = "#include <stdlib.h>\n"
STRH = "#include <stdlib.h>\n#include <string.h>\n"
FAMH = "#include <stdlib.h>\nstruct S{int n;int a[];};\n"
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def lit(n):
    """A distinct, non-empty string literal that varies with n."""
    return ALPHABET[:(n % (len(ALPHABET) - 1)) + 1]


def add(name, body, expect, category):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    assert name not in NAMES, name
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    NAMES.add(name)
    SEEN.add(normalized)
    CASES.append((name, normalized, expect, category))


# ---------------------------------------------------------------------------
# 1. write beyond a malloc extent (3 x 7 = 21)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_write_int_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@N@@]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "write")
    add(tpl("hb_write_char_@@N@@", N=_n), STD + tpl(
        "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
        "p[@@N@@]='x';free(p);return 0;}", N=_n),
        "reject", "write")
    add(tpl("hb_write_far_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@N@@+2]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "write")

# ---------------------------------------------------------------------------
# 2. read beyond a malloc extent (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_read_int_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int v=p[@@N@@];free(p);return v;}", N=_n),
        "reject", "read")
    add(tpl("hb_read_char_@@N@@", N=_n), STD + tpl(
        "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
        "char v=p[@@N@@];free(p);return v;}", N=_n),
        "reject", "read")

# ---------------------------------------------------------------------------
# 3. int vs char element-size mistakes (3 x 7 = 21)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_isz_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@);if(!p)return 2;"
        "p[@@N@@-1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "elem")
    add(tpl("hb_iconv_@@N@@", N=_n), STD + tpl(
        "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
        "int *q=(int*)p;q[@@N@@]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "elem")
    add(tpl("hb_csz_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "char *q=(char*)p;q[@@N@@*4]='x';free(p);return 0;}", N=_n),
        "reject", "elem")

# ---------------------------------------------------------------------------
# 4. calloc extent violations (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_calloc_w_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
        "p[@@N@@]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "calloc")
    add(tpl("hb_calloc_r_@@N@@", N=_n), STD + tpl(
        "int main(void){char *p=calloc(1,@@N@@);if(!p)return 2;"
        "char v=p[@@N@@];free(p);return v;}", N=_n),
        "reject", "calloc")

# ---------------------------------------------------------------------------
# 5. realloc to a new extent, then use past the new extent (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_re_grow_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int *t=realloc(p,(@@N@@+1)*sizeof(int));if(!t){free(p);return 2;}p=t;"
        "p[@@N@@+1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "realloc")
    add(tpl("hb_re_shrink_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc((@@N@@+4)*sizeof(int));if(!p)return 2;"
        "int *t=realloc(p,@@N@@*sizeof(int));if(!t){free(p);return 2;}p=t;"
        "p[@@N@@+1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "realloc")

# ---------------------------------------------------------------------------
# 6. wrong sizeof: allocation element smaller than the access element (3 x 7)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_sz_char_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(char));if(!p)return 2;"
        "p[@@N@@-1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "sizeof")
    add(tpl("hb_sz_long_@@N@@", N=_n), STD + tpl(
        "int main(void){long *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@N@@-1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "sizeof")
    add(tpl("hb_sz_double_@@N@@", N=_n), STD + tpl(
        "int main(void){double *p=malloc(@@N@@*sizeof(float));if(!p)return 2;"
        "p[@@N@@-1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "sizeof")

# ---------------------------------------------------------------------------
# 7. string allocation missing the terminating +1 (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_str_cpy_@@N@@", N=_n), STRH + tpl(
        "int main(void){const char *s=\"@@S@@\";char *d=malloc(strlen(s));"
        "if(!d)return 2;strcpy(d,s);free(d);return 0;}", N=_n, S=lit(_n)),
        "reject", "str")
    add(tpl("hb_str_memcpy_@@N@@", N=_n), STRH + tpl(
        "int main(void){const char *s=\"@@S@@\";size_t k=strlen(s);"
        "char *d=malloc(k);if(!d)return 2;memcpy(d,s,k+1);free(d);return 0;}",
        N=_n, S=lit(_n)),
        "reject", "str")

# ---------------------------------------------------------------------------
# 8. flexible array member indexed past the allocation (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_fam_w_@@N@@", N=_n), FAMH + tpl(
        "int main(void){struct S *s=malloc(sizeof(struct S));if(!s)return 2;"
        "s->a[@@N@@]=@@N@@;free(s);return 0;}", N=_n),
        "reject", "fam")
    add(tpl("hb_fam_r_@@N@@", N=_n), FAMH + tpl(
        "int main(void){struct S *s=malloc(sizeof(struct S));if(!s)return 2;"
        "int v=s->a[@@N@@];free(s);return v;}", N=_n),
        "reject", "fam")

# ---------------------------------------------------------------------------
# 9. undersized struct allocation (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_st_w_@@N@@", N=_n), STD + tpl(
        "struct S{int a[@@N@@];};"
        "int main(void){struct S *s=malloc(sizeof(int));if(!s)return 2;"
        "s->a[@@N@@-1]=@@N@@;free(s);return 0;}", N=_n),
        "reject", "struct")
    add(tpl("hb_st_r_@@N@@", N=_n), STD + tpl(
        "struct S{int a[@@N@@];};"
        "int main(void){struct S *s=malloc(sizeof(int));if(!s)return 2;"
        "int v=s->a[@@N@@-1];free(s);return v;}", N=_n),
        "reject", "struct")

# ---------------------------------------------------------------------------
# 10. heap underflow: negative index (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_uf_w_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[-1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "underflow")
    add(tpl("hb_uf_r_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int v=p[-1];free(p);return v;}", N=_n),
        "reject", "underflow")

# ---------------------------------------------------------------------------
# 11. off-by-one loops over heap storage (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_loop_le_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "for(int i=0;i<=@@N@@;i++)p[i]=i;free(p);return 0;}", N=_n),
        "reject", "loop")
    add(tpl("hb_loop_lt_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "for(int i=0;i<@@N@@+1;i++)p[i]=i;free(p);return 0;}", N=_n),
        "reject", "loop")

# ---------------------------------------------------------------------------
# 12. extent passed across functions (2 x 7 = 14)
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("hb_fn_w_@@N@@", N=_n), STD + tpl(
        "static void f(int *p){p[@@N@@]=@@N@@;}"
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "f(p);free(p);return 0;}", N=_n),
        "reject", "xfn")
    add(tpl("hb_fn_r_@@N@@", N=_n), STD + tpl(
        "static int f(int *p){return p[@@N@@];}"
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int v=f(p);free(p);return v;}", N=_n),
        "reject", "xfn")

# ---------------------------------------------------------------------------
# 13. conditional / unverified variants (3 x 7 = 21, all 'strict')
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("strict_argc_@@N@@", N=_n), STD + tpl(
        "int main(int argc,char **v){(void)v;int *p=malloc(@@N@@*sizeof(int));"
        "if(!p)return 2;int i=argc?@@N@@:0;p[i]=@@N@@;free(p);return 0;}", N=_n),
        "strict", "strict")
    add(tpl("strict_param_@@N@@", N=_n), STD + tpl(
        "static void f(int *p,int i){p[i]=7;}"
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "f(p,@@N@@);free(p);return 0;}", N=_n),
        "strict", "strict")
    add(tpl("strict_vol_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "volatile int i=@@N@@;p[i]=@@N@@;free(p);return 0;}", N=_n),
        "strict", "strict")

# ---------------------------------------------------------------------------
# 14. valid heap controls (6 x 7 = 42, all 'accept')
# ---------------------------------------------------------------------------
for _n in NVALS:
    add(tpl("ok_malloc_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p[@@N@@-1]=@@N@@;int v=p[@@N@@-1];free(p);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_calloc_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=calloc(@@N@@,sizeof(int));if(!p)return 2;"
        "p[@@N@@-1]=@@N@@;int v=p[@@N@@-1];free(p);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_str_@@N@@", N=_n), STRH + tpl(
        "int main(void){const char *s=\"@@S@@\";char *d=malloc(strlen(s)+1);"
        "if(!d)return 2;strcpy(d,s);int v=d[0];free(d);return v=='a'?0:1;}",
        N=_n, S=lit(_n)),
        "accept", "ok")
    add(tpl("ok_fam_@@N@@", N=_n), FAMH + tpl(
        "int main(void){struct S *s=malloc(sizeof(struct S)+@@N@@*sizeof(int));"
        "if(!s)return 2;s->a[@@N@@-1]=@@N@@;int v=s->a[@@N@@-1];"
        "free(s);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_realloc_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "int *t=realloc(p,(@@N@@+1)*sizeof(int));if(!t){free(p);return 2;}p=t;"
        "p[@@N@@]=@@N@@;int v=p[@@N@@];free(p);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_loop_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "for(int i=0;i<@@N@@;i++)p[i]=i;int v=p[@@N@@-1];"
        "free(p);return v==@@N@@-1?0:1;}", N=_n),
        "accept", "ok")

# ---------------------------------------------------------------------------
# 15. seeded fuzz mix of the patterns above (21 cases)
# ---------------------------------------------------------------------------
_rng = random.Random(SEED)
for _i in range(21):
    # Re-roll seeded parameters until the generated program is genuinely
    # distinct from every earlier one (a collision only wastes RNG draws).
    while True:
        _n = _rng.randint(2, 12)
        _v = _rng.randint(1, 64)
        _off = _rng.randint(1, 4)
        _kind = _i % 7
        if _kind == 0:
            _name = tpl("mut_write_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));"
                              "if(!p)return 2;p[@@N@@+@@O@@]=@@V@@;free(p);return 0;}",
                              N=_n, O=_off, V=_v)
        elif _kind == 1:
            _name = tpl("mut_read_@@I@@", I=_i)
            _body = STD + tpl("int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
                              "char v=p[@@N@@+@@O@@];free(p);return v;}",
                              N=_n, O=_off)
        elif _kind == 2:
            _name = tpl("mut_calloc_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=calloc(@@N@@,sizeof(int));"
                              "if(!p)return 2;p[@@N@@+@@O@@]=@@V@@;free(p);return 0;}",
                              N=_n, O=_off, V=_v)
        elif _kind == 3:
            _name = tpl("mut_realloc_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc((@@N@@+@@O@@)*sizeof(int));"
                              "if(!p)return 2;int *t=realloc(p,@@N@@*sizeof(int));"
                              "if(!t){free(p);return 2;}p=t;p[@@N@@+@@O@@]=@@V@@;"
                              "free(p);return 0;}", N=_n, O=_off, V=_v)
        elif _kind == 4:
            _name = tpl("mut_underflow_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));"
                              "if(!p)return 2;p[-@@O@@]=@@V@@;free(p);return 0;}",
                              N=_n, O=_off, V=_v)
        elif _kind == 5:
            _name = tpl("mut_loop_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));"
                              "if(!p)return 2;for(int i=-@@O@@;i<=@@N@@;i++)p[i]=i;"
                              "free(p);return 0;}", N=_n, O=_off)
        else:
            _name = tpl("mut_str_@@I@@", I=_i)
            _body = STRH + tpl("int main(void){const char *s=\"@@S@@\";"
                               "char *d=malloc(strlen(s)+@@O@@-1);if(!d)return 2;"
                               "strcpy(d,s);free(d);return 0;}",
                               O=_off, S=lit(_n))
        if (_body.strip() + "\n") not in SEEN:
            add(_name, _body, "reject", "mut")
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
    name, body, expect, _category = case
    path = OUT / (name + ".c")
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
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    if not reasons:
        return name, expect, [], d_rej, s_rej

    # Save the generated source and a full diagnostic log for every mismatch.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        first_diag(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        first_diag(modes["--strict"][1]),
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
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    detail = ("%s [expect=%s] reasons=%s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  name, expect, ";".join(reasons),
                  d_rej, first_diag(modes["--default"][1]),
                  s_rej, first_diag(modes["--strict"][1])))
    return name, expect, [detail], d_rej, s_rej


def main():
    # Identify duplicates defensively: a real generator must yield distinct
    # programs, not renamings of one identical case.
    bodies = [b for _, b, _, _ in CASES]
    unique = len(set(bodies))
    counts = {k: sum(1 for _, _, e, _ in CASES if e == k)
              for k in ("reject", "strict", "accept")}
    categories = {}
    for _, _, _, category in CASES:
        categories[category] = categories.get(category, 0) + 1

    print("ag_heap fuzz: %d generated cases (%d distinct), seed=%s, workers=%d"
          % (len(CASES), unique, hex(SEED), WORKERS))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("categories: " + " ".join("%s=%d" % (k, categories[k])
                                    for k in sorted(categories)))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    holes = 0          # proven violations accepted by the checker
    false_positives = 0  # safe controls rejected by the checker
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for _name, expect, fails, _d_rej, _s_rej in pool.map(run_case, CASES):
            mismatches.extend(fails)
            if fails:
                if expect == "accept":
                    false_positives += 1
                else:
                    holes += 1

    print("results: %d cases, %d mismatches" % (len(CASES), len(mismatches)))
    print("mismatches by expectation: " + " ".join(
        "%s=%d" % (k, sum(1 for m in mismatches if "[expect=%s]" % k in m))
        for k in ("reject", "strict", "accept")))
    print("holes (unsafe accepted): %d; false positives (safe rejected): %d"
          % (holes, false_positives))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    status = "PASS" if not mismatches and unique == len(CASES) else "FAIL"
    print("FINAL: %s (%d cases, %d mismatches)" % (status, len(CASES), len(mismatches)))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
