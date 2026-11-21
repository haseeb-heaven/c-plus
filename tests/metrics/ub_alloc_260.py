#!/usr/bin/env python3
"""Deterministic GENERATED ALLOCATOR-UB corpus for C+ (400+ distinct cases).

Every case is produced programmatically from parameter sweeps plus a seeded RNG
(FUZZ_SEED, default 0xA11C). Nothing is hand-copied from another corpus: each
program varies in allocation size, byte count, alignment, offset, literal
length, deallocator choice, and branch shape.

Covered allocator undefined-behaviour families:

  neg_*     negative malloc/calloc/realloc sizes
  huge_*    absurd and wrapping size arguments (SIZE_MAX, size_t wraparound)
  m0_*      malloc(0) / calloc(0) followed by a dereference
  rz_*      realloc(ptr, 0) followed by a use or a second release
  ro_*      realloc failure overwriting the only pointer to the block
  cw_*      calloc(count, size) count*size wraparound
  aa_*      aligned_alloc size that is not a multiple of the alignment
  mm_*      mismatched allocator/deallocator pairs (mmap+free, malloc+munmap)
  df_*      double free (same pointer, alias, triple release)
  fi_*      free of an interior pointer
  fs_*      free of stack / global / string-literal storage
  ms_*      allocation with a missing sizeof (under-allocation)
  mp_*      string allocation missing the terminating +1
  strict_*  conditional/unverified variants driven by argc or parameters
  mut_*     seeded fuzz mix of the patterns above
  ok_*      valid allocation controls (must pass both modes)

Each case declares an expected outcome:

  reject - proven allocator error: must fail BOTH --default and --strict
           (accepting it in either mode is a safety hole).
  strict - conditional / unverified error: MAY pass --default, but must fail
           --strict (accepting it in STRICT is a safety hole).
  accept - valid control: must pass BOTH modes (rejection is a false positive).

Any mismatch (unsafe accepted or safe rejected) is printed and the generated
source plus a full diagnostic log is saved under
tests/out/core/ub_alloc/failures/. Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout. The generator is seeded, so a run is
reproducible. All generated programs are plain C (no C+ extension syntax).
This is a differential measurement, not a proof of allocator safety.
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/ub_alloc"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xA11C"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

CASES = []   # list of (name, body, expect, category)
SEEN = set()  # exact generated program bodies, used to enforce distinctness
NAMES = set()

STD = "#include <stdlib.h>\n"
STDIO = "#include <stdlib.h>\n#include <stdio.h>\n"
STRH = "#include <stdlib.h>\n#include <string.h>\n"
MMH = "#include <stdlib.h>\n#include <sys/mman.h>\n"
SZH = "#include <stdlib.h>\n#include <stdint.h>\n"
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
# 1. negative malloc / calloc / realloc sizes (4 x 8 = 32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("neg_malloc_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=malloc(-@@N@@);if(p)free(p);return 0;}", N=_n),
        "reject", "neg")
    add(tpl("neg_calloc_cnt_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=calloc(-@@N@@,4);if(p)free(p);return 0;}", N=_n),
        "reject", "neg")
    add(tpl("neg_calloc_size_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=calloc(4,-@@N@@);if(p)free(p);return 0;}", N=_n),
        "reject", "neg")
    add(tpl("neg_realloc_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=malloc(8);if(!p)return 2;void *q=realloc(p,-@@N@@);"
        "if(q)free(q);else free(p);return 0;}", N=_n),
        "reject", "neg")

# ---------------------------------------------------------------------------
# 2. absurd / wrapping size arguments (4 x 8 = 32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("huge_malloc_@@N@@", N=_n), SZH + tpl(
        "int main(void){void *p=malloc(SIZE_MAX-@@N@@);if(p)free(p);return 0;}", N=_n),
        "reject", "huge")
    add(tpl("wrap_malloc_@@N@@", N=_n), SZH + tpl(
        "int main(void){size_t k=SIZE_MAX/4+@@N@@;int *p=malloc(k*4);"
        "if(p){p[2]=1;free(p);}return 0;}", N=_n),
        "reject", "huge")
    add(tpl("huge_calloc_@@N@@", N=_n), SZH + tpl(
        "int main(void){void *p=calloc(SIZE_MAX,@@N@@);if(p)free(p);return 0;}", N=_n),
        "reject", "huge")
    add(tpl("cast_neg_malloc_@@N@@", N=_n), STD + tpl(
        "int main(void){long k=-@@N@@;void *p=malloc((size_t)k);if(p)free(p);return 0;}", N=_n),
        "reject", "huge")

# ---------------------------------------------------------------------------
# 3. malloc(0) / calloc(0) dereference (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("m0_write_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(0);*p=@@N@@;free(p);return 0;}", N=_n),
        "reject", "m0")
    add(tpl("m0_idx_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(0);if(!p)return 2;p[@@N@@]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "m0")
    add(tpl("m0_read_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(0);if(!p)return 2;int v=p[0];free(p);return v==@@N@@?0:1;}", N=_n),
        "reject", "m0")

# ---------------------------------------------------------------------------
# 4. realloc(ptr, 0) followed by a use / second release (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("rz_direct_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void *q=realloc(p,0);free(p);(void)q;return 0;}", N=_n),
        "reject", "rz")
    add(tpl("rz_use_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(8);if(!p)return 2;void *q=realloc(p,0);"
        "(void)q;*p=@@N@@;return 0;}", N=_n),
        "reject", "rz")
    add(tpl("rz_alias_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;int *q=p;"
        "void *t=realloc(p,0);(void)t;(void)q;return 0;}", N=_n),
        "reject", "rz")

# ---------------------------------------------------------------------------
# 5. realloc failure overwriting the only pointer (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("ro_assign_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "p=realloc(p,(@@N@@+2)*sizeof(int));free(p);return 0;}", N=_n),
        "reject", "ro")
    add(tpl("ro_nullcheck_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "if(!(p=realloc(p,256)))return 1;free(p);return 0;}", N=_n),
        "reject", "ro")
    add(tpl("ro_size_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@);if(!p)return 2;"
        "p=realloc(p,1024+@@N@@);free(p);return 0;}", N=_n),
        "reject", "ro")

# ---------------------------------------------------------------------------
# 6. calloc count*size wraparound (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(2, 10):
    add(tpl("cw_lit_@@N@@", N=_n), SZH + tpl(
        "int main(void){size_t c=SIZE_MAX/(size_t)@@N@@+1;"
        "int *p=calloc(c,@@N@@);if(p)p[0]=1;free(p);return 0;}", N=_n),
        "reject", "cw")
    add(tpl("cw_var_@@N@@", N=_n), SZH + tpl(
        "int main(void){size_t c=SIZE_MAX/(size_t)@@N@@+@@N@@;"
        "char *p=calloc(c,@@N@@);if(p)p[0]='x';free(p);return 0;}", N=_n),
        "reject", "cw")
    add(tpl("cw_add_@@N@@", N=_n), SZH + tpl(
        "int main(void){size_t c=(SIZE_MAX/@@N@@)+@@N@@;"
        "int *p=calloc(c,sizeof(int));if(p)p[1]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "cw")

# ---------------------------------------------------------------------------
# 7. aligned_alloc size not a multiple of alignment (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("aa_odd_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=aligned_alloc(16,16*@@N@@+@@N@@);if(p)free(p);return 0;}", N=_n),
        "reject", "aa")
    add(tpl("aa_prime_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=aligned_alloc(8,8*@@N@@+3);if(p)free(p);return 0;}", N=_n),
        "reject", "aa")
    add(tpl("aa_bigalign_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=aligned_alloc(64,32*@@N@@+16);if(p)free(p);return 0;}", N=_n),
        "reject", "aa")

# ---------------------------------------------------------------------------
# 8. mismatched allocator / deallocator pairs (4 x 8 = 32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("mmap_free_@@N@@", N=_n), MMH + tpl(
        "int main(void){size_t n=@@N@@*4096;void *p=mmap(0,n,PROT_READ|PROT_WRITE,"
        "MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(p==(void*)-1)return 2;free(p);return 0;}", N=_n),
        "reject", "mismatch")
    add(tpl("malloc_munmap_@@N@@", N=_n), MMH + tpl(
        "int main(void){size_t n=@@N@@*4096;void *p=malloc(n);if(!p)return 2;"
        "munmap(p,n);return 0;}", N=_n),
        "reject", "mismatch")
    add(tpl("calloc_munmap_@@N@@", N=_n), MMH + tpl(
        "int main(void){size_t n=@@N@@*4096;void *p=calloc(1,n);if(!p)return 2;"
        "munmap(p,n);return 0;}", N=_n),
        "reject", "mismatch")
    add(tpl("aligned_munmap_@@N@@", N=_n), MMH + tpl(
        "int main(void){size_t n=@@N@@*64;void *p=aligned_alloc(64,n);if(!p)return 2;"
        "munmap(p,n);return 0;}", N=_n),
        "reject", "mismatch")

# ---------------------------------------------------------------------------
# 9. double free (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("df_same_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "free(p);free(p);return 0;}", N=_n),
        "reject", "df")
    add(tpl("df_alias_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;int *q=p;"
        "free(p);free(q);return 0;}", N=_n),
        "reject", "df")
    add(tpl("df_triple_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@);if(!p)return 2;"
        "free(p);free(p);free(p);return 0;}", N=_n),
        "reject", "df")

# ---------------------------------------------------------------------------
# 10. free of an interior pointer (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("fi_int_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc((@@N@@+2)*sizeof(int));if(!p)return 2;"
        "free(p+1);return 0;}", N=_n),
        "reject", "fi")
    add(tpl("fi_char_@@N@@", N=_n), STD + tpl(
        "int main(void){char *p=malloc(64);if(!p)return 2;free(p+@@N@@);return 0;}", N=_n),
        "reject", "fi")
    add(tpl("fi_field_@@N@@", N=_n), STD + tpl(
        "struct S{int x[@@N@@+2];};int main(void){struct S*s=malloc(sizeof*s);"
        "if(!s)return 2;free(&s->x[1]);return 0;}", N=_n),
        "reject", "fi")

# ---------------------------------------------------------------------------
# 11. free of stack / global / string-literal storage (4 x 8 = 32)
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("fs_stack_@@N@@", N=_n), STD + tpl(
        "int main(void){int a[@@N@@];free(a);return 0;}", N=_n),
        "reject", "fs")
    add(tpl("fs_global_@@N@@", N=_n), STD + tpl(
        "static int g[@@N@@];int main(void){free(g);return 0;}", N=_n),
        "reject", "fs")
    add(tpl("fs_literal_@@N@@", N=_n), STD + tpl(
        "int main(void){const char *s=\"@@S@@\";free((void*)s);return 0;}", N=_n, S=lit(_n)),
        "reject", "fs")
    add(tpl("fs_stackoff_@@N@@", N=_n), STD + tpl(
        "int main(void){int a[@@N@@+2];free(&a[1]);return 0;}", N=_n),
        "reject", "fs")

# ---------------------------------------------------------------------------
# 12. allocation with a missing sizeof (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(2, 10):
    add(tpl("ms_index_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@);if(!p)return 2;p[@@N@@]=@@N@@;free(p);return 0;}", N=_n),
        "reject", "ms")
    add(tpl("ms_strcpy_@@N@@", N=_n), STRH + tpl(
        "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
        "strcpy(p,\"longerthanbuffer\");free(p);return 0;}", N=_n),
        "reject", "ms")
    add(tpl("ms_memset_@@N@@", N=_n), STRH + tpl(
        "int main(void){char *p=malloc(@@N@@);if(!p)return 2;memset(p,0,@@N@@*4);free(p);return 0;}", N=_n),
        "reject", "ms")

# ---------------------------------------------------------------------------
# 13. string allocation missing the terminating +1 (3 x 8 = 24)
# ---------------------------------------------------------------------------
for _n in range(2, 10):
    add(tpl("mp_strlen_@@N@@", N=_n), STRH + tpl(
        "int main(void){const char*s=\"@@S@@\";char*d=malloc(strlen(s));"
        "if(!d)return 2;strcpy(d,s);free(d);return 0;}", N=_n, S=lit(_n)),
        "reject", "mp")
    add(tpl("mp_memcpy_@@N@@", N=_n), STRH + tpl(
        "int main(void){const char*s=\"@@S@@\";size_t k=strlen(s);char*d=malloc(k);"
        "if(!d)return 2;memcpy(d,s,k+1);free(d);return 0;}", N=_n, S=lit(_n)),
        "reject", "mp")
    add(tpl("mp_sprintf_@@N@@", N=_n), STDIO + tpl(
        "int main(void){const char*s=\"@@S@@\";char*d=malloc(strlen(s));"
        "if(!d)return 2;sprintf(d,\"%s\",s);free(d);return 0;}", N=_n, S=lit(_n)),
        "reject", "mp")

# ---------------------------------------------------------------------------
# 14. conditional / unverified variants (2 x 8 = 16, all 'strict')
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("strict_neg_arg_@@N@@", N=_n), STD + tpl(
        "static void *f(long k){void *p=malloc((size_t)k);free(p);return p;}"
        "int main(int argc,char **v){(void)v;f(argc?-@@N@@:8);return 0;}", N=_n),
        "strict", "strict")
    add(tpl("strict_wrap_arg_@@N@@", N=_n), SZH + tpl(
        "static void *f(size_t c){void *p=calloc(c,@@N@@);if(p)free(p);return p;}"
        "int main(int argc,char **v){(void)v;f(argc?SIZE_MAX/@@N@@+@@N@@:4);return 0;}", N=_n),
        "strict", "strict")

# ---------------------------------------------------------------------------
# 15. seeded fuzz mix of all patterns above (24 cases)
# ---------------------------------------------------------------------------
_rng = random.Random(SEED)
for _i in range(24):
    # Re-roll seeded parameters until the generated program is genuinely
    # distinct from every earlier one (a collision only wastes RNG draws).
    while True:
        _n = _rng.randint(2, 16)
        _v = _rng.randint(1, 50)
        _off = _rng.randint(1, 6)
        _kind = _i % 8
        if _kind == 0:
            _name = tpl("mut_neg_malloc_@@I@@", I=_i)
            _body = STD + tpl("int main(void){long k=-@@N@@;void *p=malloc((size_t)k);"
                              "if(p)free(p);return 0;}", N=_n)
            _exp = "reject"
        elif _kind == 1:
            _name = tpl("mut_calloc_wrap_@@I@@", I=_i)
            _body = SZH + tpl("int main(void){size_t c=SIZE_MAX/(size_t)@@N@@+@@O@@;"
                              "char *p=calloc(c,@@N@@);if(p)p[0]='x';free(p);return 0;}",
                              N=_n, O=_off)
            _exp = "reject"
        elif _kind == 2:
            _name = tpl("mut_aligned_bad_@@I@@", I=_i)
            _body = STD + tpl("int main(void){void *p=aligned_alloc(16,16*@@N@@+@@O@@);"
                              "if(p)free(p);return 0;}", N=_n, O=_off)
            _exp = "reject"
        elif _kind == 3:
            _name = tpl("mut_df_alias_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                              "int *q=p;free(p);free(q);return 0;}", N=_n)
            _exp = "reject"
        elif _kind == 4:
            _name = tpl("mut_free_interior_@@I@@", I=_i)
            _body = STD + tpl("int main(void){char *p=malloc(@@N@@*8);if(!p)return 2;"
                              "free(p+@@O@@);return 0;}", N=_n, O=_off)
            _exp = "reject"
        elif _kind == 5:
            _name = tpl("mut_missing_plus1_@@I@@", I=_i)
            _body = STRH + tpl("int main(void){const char*s=\"@@S@@\";char*d=malloc(strlen(s));"
                               "if(!d)return 2;strcpy(d,s);free(d);return 0;}",
                               S=lit(_n))
            _exp = "reject"
        elif _kind == 6:
            _name = tpl("mut_realloc_loss_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                              "p=realloc(p,(@@N@@+@@O@@)*sizeof(int));free(p);return 0;}",
                              N=_n, O=_off)
            _exp = "reject"
        else:
            _name = tpl("mut_valid_control_@@I@@", I=_i)
            _body = STD + tpl("int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
                              "p[@@N@@-1]=@@V@@;int x=p[@@N@@-1];free(p);return x==@@V@@?0:1;}",
                              N=_n, V=_v)
            _exp = "accept"
        if (_body.strip() + "\n") not in SEEN:
            add(_name, _body, _exp, "mut")
            break

# ---------------------------------------------------------------------------
# 16. valid allocation controls (5 x 8 = 40, all 'accept')
# ---------------------------------------------------------------------------
for _n in range(1, 9):
    add(tpl("ok_malloc_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;p[@@N@@-1]=@@N@@;"
        "int v=p[@@N@@-1];free(p);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_calloc_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=calloc(1,@@N@@*sizeof(int));if(!p)return 2;p[0]=@@N@@;"
        "int v=p[0];free(p);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_aligned_@@N@@", N=_n), STD + tpl(
        "int main(void){void *p=aligned_alloc(8,8*@@N@@);if(!p)return 2;free(p);return 0;}", N=_n),
        "accept", "ok")
    add(tpl("ok_realloc_@@N@@", N=_n), STD + tpl(
        "int main(void){int *p=malloc(@@N@@*sizeof(int));if(!p)return 2;"
        "void *t=realloc(p,(@@N@@+1)*sizeof(int));if(!t){free(p);return 2;}p=t;p[@@N@@]=@@N@@;"
        "int v=p[@@N@@];free(p);return v==@@N@@?0:1;}", N=_n),
        "accept", "ok")
    add(tpl("ok_str_@@N@@", N=_n), STRH + tpl(
        "int main(void){const char*s=\"@@S@@\";char*d=malloc(strlen(s)+@@N@@);"
        "if(!d)return 2;strcpy(d,s);int v=d[0];free(d);return v=='a'?0:1;}", N=_n, S=lit(_n)),
        "accept", "ok")


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
            reasons.append("safe rejected in DEFAULT")
        if s_rej:
            reasons.append("safe rejected in STRICT")

    if not reasons:
        return name, expect, [], modes

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
    return name, expect, [detail], modes


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

    print("ub_alloc fuzz: %d generated cases (%d distinct), seed=%s, workers=%d"
          % (len(CASES), unique, hex(SEED), WORKERS))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("categories: " + " ".join("%s=%d" % (k, categories[k])
                                    for k in sorted(categories)))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    mismatches = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for _name, _expect, fails, _modes in pool.map(run_case, CASES):
            mismatches.extend(fails)

    print("results: %d cases, %d mismatches" % (len(CASES), len(mismatches)))
    print("mismatches by expectation: " + " ".join(
        "%s=%d" % (k, sum(1 for m in mismatches if "[expect=%s]" % k in m))
        for k in ("reject", "strict", "accept")))
    for line in mismatches[:10]:
        print("  " + line)
    if len(mismatches) > 10:
        print("  ... %d more (see saved logs)" % (len(mismatches) - 10))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
