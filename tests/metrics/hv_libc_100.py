#!/usr/bin/env python3
"""Deterministic SEEDED PARALLEL generator for heap-extent / libc misuse (>=100).

Produces distinct, self-contained, VALID C translation units that allocate heap
storage with ``malloc`` (plus ``alloca`` / VLA alternatives) and then hand that
storage to a C standard-library memory/string function with a byte count that
exceeds the allocation extent.  Also covers element-size mistakes, missing NUL
terminators, and source-buffer over-reads, together with safe exact-fit controls.

Every case declares one expected outcome:

  reject - a write/read beyond the allocation extent (or an alloca/VLA that the
           checker must flag).  A correct checker must fail BOTH --default and
           --strict.  Acceptance in either mode is a safety hole ("miss").
  accept - safe control (exact fit, or the correct strlen + 1 for the NUL).
           A correct checker must pass BOTH modes; rejection is a false positive.

FAMILIES (family -> outcome):

  * memset_over   - malloc(n); memset(p, c, n+e)                    (reject)
  * memcpy_over   - memcpy into a malloc(n) dst with count n+e      (reject)
  * memmove_over  - memmove into a malloc(n) dst with count n+e     (reject)
  * strcpy_over   - strcpy of a string longer than malloc(n)        (reject)
  * strncpy_over  - strncpy with a count larger than malloc(n)      (reject)
  * snprintf_over - snprintf with a size larger than malloc(n)      (reject)
  * strcat_over   - strcat beyond a malloc(n) destination           (reject)
  * elem_size     - char-vs-int element size mistake (short alloc)  (reject)
  * nop1_strlen   - malloc(strlen(s)) then strcpy(s): no room for NUL(reject)
  * nop1_explicit - malloc(L) then strcpy of an L-byte string       (reject)
  * src_overread  - memcpy from a src buffer smaller than the count (reject)
  * alloca_over   - alloca(n) then memset n+e bytes                 (reject)
  * vla_over      - VLA a[n] then memset n+e bytes                  (reject)
  * safe_exact    - controls: exact-fit memset/memcpy/... /str*     (accept)
  * safe_nop1     - control: malloc(strlen(s)+1) then strcpy        (accept)

Programs are generated in a thread pool from a per-case seeded RNG; ``pool.map``
preserves input order and ``random.Random(f"{SEED}:{i}")`` makes generation
independent of FUZZ_WORKERS.  The case marker plus seeded sizes, byte values and
string literals make every translation unit distinct.  Checking also runs in a
thread pool.  Mismatches and combined diagnostics are saved under
tests/out/core/hv_libc/failures/.  This is a differential measurement of the
checker, not a proof of heap safety; it does not modify the compiler.

Environment:
  FUZZ_SEED    default 0x48564C42  ("HVLB")
  FUZZ_WORKERS default 8
  FUZZ_TIMEOUT default 60 seconds per check
  CPLUS_BIN    default <root>/build/c+ (existing binary; no rebuild needed)
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
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/hv_libc"
FAILS = OUT / "failures"
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x48564C42"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

# family, case count, builder name, declared outcome.
FAMILIES = (
    ("memset_over",   12, "memset_over",   "reject"),
    ("memcpy_over",   12, "memcpy_over",   "reject"),
    ("memmove_over",  12, "memmove_over",  "reject"),
    ("strcpy_over",   12, "strcpy_over",   "reject"),
    ("strncpy_over",  12, "strncpy_over",  "reject"),
    ("snprintf_over", 12, "snprintf_over", "reject"),
    ("strcat_over",   12, "strcat_over",   "reject"),
    ("elem_size",     12, "elem_size",     "reject"),
    ("nop1_strlen",   12, "nop1_strlen",   "reject"),
    ("nop1_explicit", 12, "nop1_explicit", "reject"),
    ("src_overread",  12, "src_overread",  "reject"),
    ("alloca_over",   10, "alloca_over",   "reject"),
    ("vla_over",      10, "vla_over",      "reject"),
    ("safe_exact",    14, "safe_exact",    "accept"),
    ("safe_nop1",     12, "safe_nop1",     "accept"),
)
TOTAL = sum(count for _f, count, _b, _e in FAMILIES)
STAGE = None

STD = "#include <stdlib.h>\n"
STRH = "#include <stdlib.h>\n#include <string.h>\n"
STDH = "#include <stdlib.h>\n#include <stdio.h>\n"
ALLOCAH = "#include <stdlib.h>\n#include <string.h>\n#include <alloca.h>\n"
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def lit(n):
    """A distinct, non-empty string literal of exactly ``n`` letters."""
    if n <= 0:
        return "x"
    s = ""
    while len(s) < n:
        s += ALPHABET
    return s[:n]


def marked(name, body):
    """Prefix a unique case marker so every generated program is distinct."""
    return f"/* hv_libc {name} seed={SEED:#x} */\n" + body.strip() + "\n"


# ---------------------------------------------------------------------------
# Unsafe families (declared "reject": an extent violation).
# ---------------------------------------------------------------------------
def b_memset_over(i, rng):
    n, extra, c = rng.randint(4, 12), rng.randint(1, 8), rng.randint(0, 255)
    body = STRH + tpl(
        "int main(void){char *p=malloc(@@N@@);if(!p)return 2;"
        "memset(p,@@C@@,@@K@@);free(p);return 0;}",
        N=n, C=c, K=n + extra)
    return f"memset_over_{i}", body


def b_memcpy_over(i, rng):
    n, extra = rng.randint(4, 12), rng.randint(1, 8)
    s = n + extra + 2  # source is large enough; only the destination overflows
    body = STRH + tpl(
        "int main(void){char src[@@S@@]={0};char *d=malloc(@@N@@);if(!d)return 2;"
        "memcpy(d,src,@@K@@);free(d);return 0;}",
        S=s, N=n, K=n + extra)
    return f"memcpy_over_{i}", body


def b_memmove_over(i, rng):
    n, extra = rng.randint(4, 12), rng.randint(1, 8)
    s = n + extra + 2
    body = STRH + tpl(
        "int main(void){char src[@@S@@]={0};char *d=malloc(@@N@@);if(!d)return 2;"
        "memmove(d,src,@@K@@);free(d);return 0;}",
        S=s, N=n, K=n + extra)
    return f"memmove_over_{i}", body


def b_strcpy_over(i, rng):
    n = rng.randint(3, 10)
    length = n + rng.randint(0, 6)  # strlen >= n, so strlen+1 > n
    body = STRH + tpl(
        'int main(void){char *d=malloc(@@N@@);if(!d)return 2;'
        'strcpy(d,"@@S@@");free(d);return 0;}',
        N=n, S=lit(length))
    return f"strcpy_over_{i}", body


def b_strncpy_over(i, rng):
    n, extra = rng.randint(2, 8), rng.randint(1, 8)
    k = n + extra
    body = STRH + tpl(
        'int main(void){char *d=malloc(@@N@@);if(!d)return 2;'
        'strncpy(d,"@@S@@",@@K@@);free(d);return 0;}',
        N=n, S=lit(k + 2), K=k)
    return f"strncpy_over_{i}", body


def b_snprintf_over(i, rng):
    n, extra = rng.randint(2, 8), rng.randint(1, 8)
    k = n + extra
    body = STDH + tpl(
        'int main(void){char *d=malloc(@@N@@);if(!d)return 2;'
        'snprintf(d,@@K@@,"%s","@@S@@");free(d);return 0;}',
        N=n, K=k, S=lit(k + 2))
    return f"snprintf_over_{i}", body


def b_strcat_over(i, rng):
    n = rng.randint(3, 10)
    length = n + rng.randint(0, 6)
    body = STRH + tpl(
        'int main(void){char *d=malloc(@@N@@);if(!d)return 2;d[0]=0;'
        'strcat(d,"@@S@@");free(d);return 0;}',
        N=n, S=lit(length))
    return f"strcat_over_{i}", body


def b_elem_size(i, rng):
    n, v, shape = rng.randint(2, 8), rng.randint(1, 120), i % 3
    if shape == 0:  # n bytes allocated, indexed as int[p] up to n-1
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(char));if(!p)return 2;"
            "p[@@I@@]=@@V@@;free(p);return 0;}", N=n, I=n - 1, V=v)
    elif shape == 1:  # malloc(n) bytes treated as an int array
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@);if(!p)return 2;"
            "p[@@I@@]=@@V@@;free(p);return 0;}", N=n, I=n // 2, V=v)
    else:  # n bytes allocated, one-past in int elements
        body = STD + tpl(
            "int main(void){int *p=malloc(@@N@@*sizeof(char));if(!p)return 2;"
            "p[@@I@@]=@@V@@;free(p);return 0;}", N=n, I=n, V=v)
    return f"elem_size_{i}", body


def b_nop1_strlen(i, rng):
    length = rng.randint(3, 10)
    body = STRH + tpl(
        'int main(void){const char *s="@@S@@";char *d=malloc(strlen(s));'
        'if(!d)return 2;strcpy(d,s);free(d);return 0;}', S=lit(length))
    return f"nop1_strlen_{i}", body


def b_nop1_explicit(i, rng):
    length = rng.randint(3, 10)
    body = STRH + tpl(
        'int main(void){const char *s="@@S@@";char *d=malloc(@@L@@);'
        'if(!d)return 2;strcpy(d,s);free(d);return 0;}',
        S=lit(length), L=length)
    return f"nop1_explicit_{i}", body


def b_src_overread(i, rng):
    n, extra = rng.randint(4, 12), rng.randint(1, 8)
    k = n + extra
    m = rng.randint(1, n)  # source buffer strictly smaller than the count
    body = STRH + tpl(
        "int main(void){char src[@@M@@]={0};char *d=malloc(@@K@@);if(!d)return 2;"
        "memcpy(d,src,@@K@@);free(d);return 0;}", M=m, K=k)
    return f"src_overread_{i}", body


def b_alloca_over(i, rng):
    n, extra, c = rng.randint(4, 12), rng.randint(1, 8), rng.randint(0, 255)
    body = ALLOCAH + tpl(
        "int main(void){char *p=alloca(@@N@@);if(!p)return 2;"
        "memset(p,@@C@@,@@K@@);return 0;}", N=n, C=c, K=n + extra)
    return f"alloca_over_{i}", body


def b_vla_over(i, rng):
    n, extra, c = rng.randint(4, 12), rng.randint(1, 8), rng.randint(0, 255)
    body = STRH + tpl(
        "int main(void){int n=@@N@@;char a[n];memset(a,@@C@@,@@K@@);return 0;}",
        N=n, C=c, K=n + extra)
    return f"vla_over_{i}", body


# ---------------------------------------------------------------------------
# Safe controls (declared "accept": exact fit / correct NUL reservation).
# ---------------------------------------------------------------------------
def b_safe_exact(i, rng):
    n = rng.randint(4, 12)
    length = rng.randint(2, 9)
    c = rng.randint(0, 255)
    k = rng.randint(3, 10)
    which = i % 8
    if which == 0:
        hdr, stmt = STRH, tpl(
            "char *p=malloc(@@N@@);if(!p)return 2;memset(p,@@C@@,@@N@@);"
            "free(p);return 0;", N=n, C=c)
    elif which == 1:
        hdr, stmt = STRH, tpl(
            "char a[@@N@@]={0};char *p=malloc(@@N@@);if(!p)return 2;"
            "memcpy(p,a,@@N@@);free(p);return 0;", N=n)
    elif which == 2:
        hdr, stmt = STRH, tpl(
            "char a[@@N@@]={0};char *p=malloc(@@N@@);if(!p)return 2;"
            "memmove(p,a,@@N@@);free(p);return 0;", N=n)
    elif which == 3:
        hdr, stmt = STRH, tpl(
            'char *p=malloc(@@L@@);if(!p)return 2;strcpy(p,"@@S@@");'
            'free(p);return 0;', L=length + 1, S=lit(length))
    elif which == 4:
        hdr, stmt = STRH, tpl(
            'char *p=malloc(@@K@@);if(!p)return 2;strncpy(p,"@@S@@",@@K@@);'
            'free(p);return 0;', K=length + 1, S=lit(length))
    elif which == 5:
        hdr, stmt = STDH, tpl(
            'char *p=malloc(@@K@@);if(!p)return 2;'
            'snprintf(p,@@K@@,"%s","@@S@@");free(p);return 0;',
            K=k, S=lit(k - 1))
    elif which == 6:
        hdr, stmt = STRH, tpl(
            'char *p=malloc(@@L@@);if(!p)return 2;p[0]=0;'
            'strcat(p,"@@S@@");free(p);return 0;', L=length + 1, S=lit(length))
    else:
        hdr, stmt = STRH, tpl(
            "char *p=malloc(@@N@@);if(!p)return 2;memset(p,@@C@@,@@K@@);"
            "free(p);return 0;", N=n, C=c, K=n - 1)
    return f"safe_exact_{i}", hdr + "int main(void){" + stmt + "}\n"


def b_safe_nop1(i, rng):
    length = rng.randint(3, 10)
    body = STRH + tpl(
        'int main(void){const char *s="@@S@@";char *d=malloc(strlen(s)+1);'
        'if(!d)return 2;strcpy(d,s);free(d);return 0;}', S=lit(length))
    return f"safe_nop1_{i}", body


BUILDERS = {
    "memset_over": b_memset_over,
    "memcpy_over": b_memcpy_over,
    "memmove_over": b_memmove_over,
    "strcpy_over": b_strcpy_over,
    "strncpy_over": b_strncpy_over,
    "snprintf_over": b_snprintf_over,
    "strcat_over": b_strcat_over,
    "elem_size": b_elem_size,
    "nop1_strlen": b_nop1_strlen,
    "nop1_explicit": b_nop1_explicit,
    "src_overread": b_src_overread,
    "alloca_over": b_alloca_over,
    "vla_over": b_vla_over,
    "safe_exact": b_safe_exact,
    "safe_nop1": b_safe_nop1,
}


def generate_case(i):
    """Generate case i from an independent per-case seeded RNG."""
    start = 0
    for (family, count, builder, expect) in FAMILIES:
        if i < start + count:
            rng = random.Random(f"{SEED}:{i}")
            name, body = BUILDERS[builder](i, rng)
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
    false_positive = False
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT")
    else:  # accept
        false_positive = d_rej or s_rej
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
        "reasons": reasons, "false_positive": false_positive,
        "d_rej": d_rej, "s_rej": s_rej, "tags": tags, "body": body,
    }
    if reasons:
        (FAILS / f"{name}.c").write_text(body)
        log = [
            f"case: {name}",
            f"family: {family}",
            f"expect: {expect}",
            f"kind: {'false_positive' if expect == 'accept' else 'hole'}",
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
    names = [c[0] for c in cases]
    outcomes = collections.Counter(c[2] for c in cases)
    families = collections.Counter(c[3] for c in cases)
    assert len(set(names)) == TOTAL, "duplicate case name"

    print(f"hv_libc: {len(cases)} generated cases ({unique} distinct), "
          f"seed={SEED:#x}, workers={WORKERS}, bin={CC}")
    print("expected outcomes: reject=%d accept=%d"
          % (outcomes["reject"], outcomes["accept"]))
    for family, count, _b, _e in FAMILIES:
        print(f"  {family:16} {families[family]:3d}  (expect {_e})")
    if unique != len(cases):
        print(f"WARNING: {len(cases) - unique} duplicate program bodies generated")

    STAGE = pathlib.Path(tempfile.mkdtemp(prefix="hv_libc_"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(check_case, cases))

    results.sort(key=lambda r: r["name"])
    mismatches = [r for r in results if r["reasons"]]
    false_positives = [r for r in mismatches if r["false_positive"]]
    misses = [r for r in mismatches if r["expect"] == "reject"]
    hard = sum(1 for r in mismatches
               if any("TIMEOUT" in x or "OS ERROR" in x or "CRASH" in x
                      for x in r["reasons"]))
    by_family = collections.Counter(r["family"] for r in mismatches)

    print("results: %d cases, %d mismatches (%d safety holes, %d false positives, "
          "%d hard errors)" % (len(cases), len(mismatches), len(misses),
                               len(false_positives), hard))
    print("mismatches by family: "
          + ("none" if not by_family else
             " ".join(f"{k}={by_family[k]}" for k in sorted(by_family))))
    print("false positives: "
          + ("none" if not false_positives else
             ", ".join(r["name"] for r in false_positives)))

    print("first 10 case names:")
    for nm in names[:10]:
        print("  " + nm)
    print("first 10 mismatch names:")
    for nm in [r["name"] for r in mismatches[:10]]:
        print("  " + nm)
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
