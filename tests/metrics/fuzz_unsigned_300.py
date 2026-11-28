#!/usr/bin/env python3
"""Deterministic UNSIGNED-vs-SIGNED generator: >=300 distinct small C programs.

Every case is built from parameter sweeps plus a seeded RNG (FUZZ_SEED): array
extents, offsets, identifier names, element types and literal contents all
vary, so no program is hand-copied and no two generated programs are identical.
The generator targets failed bounds checks caused by unsigned comparison and
conversion mistakes:

  * size_t / unsigned / uint32_t value compared ``< 0`` (always false) in an
    ``if`` guard placed before a memcpy / strncpy / array index
  * unsigned underflow used as an index or size (``n - 1`` where ``n == 0``)
  * a negative int passed to a function taking size_t and used as a memcpy
    length (direct, one-hop and two-hop call chains)
  * a signed comparison that passes for negative values, followed by a cast
    to size_t that defeats the check
  * unsigned loop counters with a ``<=`` bound (off-by-one) and a ``>= 0``
    condition (underflow / infinite loop)
  * size_t length arithmetic wrap (addition and multiplication) feeding an
    allocation size, plus an under-allocating ``len - 1``
  * safe controls: ``x == 0`` guards, correct signed negative checks,
    ``x < SIZE_MAX``, unsigned counters with a correct ``< n`` bound, and an
    unreachable ``i - 1`` index behind a zero guard

Each case declares an expected outcome, following the convention of
tests/core/redteam_100.py, tests/metrics/bounds_350.py and
tests/metrics/ub_arith_260.py:

  reject - the bounds/size failure is statically provable: must fail BOTH
           --default and --strict.
  strict - value-dependent / interprocedural: DEFAULT may pass, but STRICT
           must fail (unverified operations fail closed).
  accept - valid control: must pass BOTH modes (a rejection is a false
           positive).

Mismatches (unsafe accepted, safe rejected, crash, timeout) are printed and the
generated source plus both diagnostic logs are saved under
tests/out/core/fuzz_unsigned/failures/.  Cases run in parallel (FUZZ_WORKERS,
default 8) with a 60s per-check timeout.  The run is reproducible for a fixed
FUZZ_SEED.  This is a differential measurement of the declared subset, not a
proof of arbitrary-C safety.

Environment:
  FUZZ_SEED    default 0x5F17
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
OUT = ROOT / "tests/out/core/fuzz_unsigned"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
for _d in (OUT, CASES_DIR, FAILS):
    _d.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x5F17"), 0)
TIMEOUT = 60
MIN_CASES = 300

CASES = []   # dict(name, body, expect, family)
SEEN = set()  # exact generated bodies, used to enforce distinctness


def tpl(template, **kw):
    """Token templating: @@TOKEN@@ placeholders plus doubled C braces."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    template = template.replace("{{", "{").replace("}}", "}")
    assert "@@" not in template, template
    return template


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = body.strip() + "\n"
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({
        "name": name,
        "body": normalized,
        "expect": expect,
        "family": family,
    })


# ---------------------------------------------------------------------------
# Seeded generation helpers.  Every body embeds N (the sweep index) so
# parameter steps are distinct even when the RNG repeats a small offset.
# ---------------------------------------------------------------------------
RNG = random.Random(SEED)
NS = range(2, 16)                 # 14 steps
UTYPES = ("size_t", "unsigned", "uint32_t", "unsigned long")
NAMES = ("a", "buf", "arr", "data", "table", "dst", "tmp")
ETYPES = ("int", "short", "long", "char")
ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


def nm():
    return RNG.choice(NAMES)


def et():
    return RNG.choice(ETYPES)


def off(hi=4):
    return RNG.randint(1, hi)


def lit(length):
    return "".join(RNG.choice(ALPHABET) for _ in range(length))


# --- unsigned value compared < 0 in a guard before a copy / index -----------
for n in NS:
    v, k, src = nm(), off(), lit(RNG.randint(12, 32))
    add("ut_lt_zero_guard", f"lt0_memcpy_sz_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[64]={{0}}; "
            "size_t n=(size_t)-@@K@@; if(n<0) return 0; memcpy(@@V@@,s,n); return 0; }}",
            V=v, N=n, K=k), "reject")
    add("ut_lt_zero_guard", f"lt0_memcpy_u32_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[64]={{0}}; "
            "uint32_t n=0xFFFFFFFFu-(@@K@@-1); if(n<0) return 0; memcpy(@@V@@,s,n); return 0; }}",
            V=v, N=n, K=k), "reject")
    add("ut_lt_zero_guard", f"lt0_strncpy_uint_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; unsigned n=0u-@@K@@; if(n<0) return 0; "
            "strncpy(@@V@@,\"@@SRC@@\",n); return 0; }}",
            V=v, N=n, K=k, SRC=src), "reject")
    add("ut_lt_zero_guard", f"lt0_index_unsigned_{n}",
        tpl("#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; unsigned i=0u-@@K@@; "
            "if(i<0) return 0; return @@V@@[i]; }}",
            T=et(), V=v, N=n, K=k), "reject")
    add("ut_lt_zero_guard", f"lt0_index_param_{n}",
        tpl("#include <stdint.h>\n"
            "@@T@@ a[@@N@@]={{0}};\n"
            "int f(unsigned i){{ if(i<0) return 0; return a[i]; }}\n"
            "int main(void){{ return f(0u-@@K@@); }}",
            T=et(), N=n, K=k), "strict")

# --- unsigned underflow used as an index or size (n - 1 where n == 0) ------
for n in NS:
    v, k = nm(), off()
    add("ut_underflow_index", f"under_idx_sz_{n}",
        tpl("#include <stddef.h>\n#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; size_t n=0; return @@V@@[n-1]; }}",
            T=et(), V=v, N=n), "reject")
    add("ut_underflow_index", f"under_size_memcpy_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[64]={{0}}; size_t n=0; "
            "memcpy(@@V@@,s,n-1); return 0; }}",
            V=v, N=n), "reject")
    add("ut_underflow_index", f"under_idx_u32_{n}",
        tpl("#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; uint32_t n=0; n-=@@K@@; return @@V@@[n]; }}",
            T=et(), V=v, N=n, K=k), "reject")

# --- negative int passed to a size_t parameter used as a memcpy length -----
for n in NS:
    v, k = nm(), off()
    add("neg_to_size", f"neg_len_direct_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[64]={{0}}; int x=-@@K@@; "
            "memcpy(@@V@@,s,(size_t)x); return 0; }}",
            V=v, N=n, K=k), "reject")
    add("neg_to_size", f"neg_len_call_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "void cp(size_t m){{ char d[@@N@@+8]; char s[64]={{0}}; memcpy(d,s,m); }}\n"
            "int main(void){{ cp(-@@K@@); return 0; }}",
            N=n, K=k), "reject")
    add("neg_to_size", f"neg_len_indirect_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "void cp(size_t m){{ char d[@@N@@+8]; char s[64]={{0}}; memcpy(d,s,m); }}\n"
            "void fwd(int x){{ cp((size_t)x); }}\n"
            "int main(void){{ fwd(-@@K@@); return 0; }}",
            N=n, K=k), "strict")

# --- signed comparison passes for negatives, then casts to size_t ----------
for n in NS:
    v, k = nm(), off()
    add("signed_pass_then_cast", f"sneg_lt_memcpy_{n}",
        tpl("#include <string.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[64]={{0}}; int x=-@@K@@; "
            "if(x<@@N@@){{ memcpy(@@V@@,s,(size_t)x); }} return 0; }}",
            V=v, N=n, K=k), "reject")
    add("signed_pass_then_cast", f"sneg_cast_index_{n}",
        tpl("#include <stddef.h>\n#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; int x=-@@K@@; if(x<=0){{ "
            "size_t i=(size_t)x; return @@V@@[i]; }} return 0; }}",
            T=et(), V=v, N=n, K=k), "reject")
    add("signed_pass_then_cast", f"sneg_lt_param_{n}",
        tpl("#include <string.h>\n"
            "char d[@@N@@+8]; char s[64]={{0}};\n"
            "void h(int x){{ if(x<@@N@@) memcpy(d,s,(size_t)x); }}\n"
            "int main(void){{ h(-@@K@@); return 0; }}",
            N=n, K=k), "strict")

# --- unsigned loop counters with <= / >= 0 bounds --------------------------
for n in NS:
    v = nm()
    add("ut_loop_le", f"loop_le_sz_{n}",
        tpl("#include <stddef.h>\n#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; "
            "for(size_t i=0;i<=@@N@@;i++) @@V@@[i]=0; return 0; }}",
            T=et(), V=v, N=n), "reject")
    add("ut_loop_le", f"loop_le_u32_{n}",
        tpl("#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; "
            "for(unsigned i=0;i<=@@N@@;i++) @@V@@[i]=0; return 0; }}",
            T=et(), V=v, N=n), "reject")
    add("ut_loop_le", f"loop_ge_wrap_{n}",
        tpl("#include <stddef.h>\n#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; "
            "for(size_t i=@@N@@;i>=0;--i) @@V@@[i]=0; return 0; }}",
            T=et(), V=v, N=n), "reject")

# --- size_t length arithmetic wrap feeding an allocation size --------------
for n in NS:
    v, k = nm(), off()
    add("size_arith_wrap_alloc", f"alloc_add_wrap_{n}",
        tpl("#include <stdlib.h>\n#include <stdint.h>\n"
            "int main(void){{ size_t len=SIZE_MAX-@@K@@+@@N@@; size_t z=len+@@K@@; "
            "char*@@V@@=malloc(z); if(!@@V@@) return 2; @@V@@[0]=1; free(@@V@@); return 0; }}",
            V=v, N=n, K=k), "reject")
    add("size_arith_wrap_alloc", f"alloc_mul_wrap_{n}",
        tpl("#include <stdlib.h>\n#include <stdint.h>\n"
            "int main(void){{ size_t len=SIZE_MAX/2+@@N@@; "
            "char*@@V@@=malloc(len*2); if(!@@V@@) return 2; @@V@@[0]=1; free(@@V@@); return 0; }}",
            V=v, N=n), "reject")
    add("size_arith_wrap_alloc", f"alloc_underalloc_{n}",
        tpl("#include <stdlib.h>\n#include <stdint.h>\n"
            "int main(void){{ size_t len=@@N@@; char*@@V@@=malloc(len-1); if(!@@V@@) return 2; "
            "@@V@@[len-1]=1; free(@@V@@); return 0; }}",
            V=v, N=n), "reject")

# --- safe controls ----------------------------------------------------------
for n in NS:
    v, k = nm(), off()
    add("safe_control", f"ok_eq_zero_{n}",
        tpl("#include <string.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[@@N@@+8]; size_t m=0; "
            "if(m==0) return 0; memcpy(@@V@@,s,m); return 0; }}",
            V=v, N=n), "accept")
    add("safe_control", f"ok_neg_check_{n}",
        tpl("#include <string.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[@@N@@+8]; int x=-@@K@@; "
            "if(x<0) return 0; if(x>@@N@@) return 0; memcpy(@@V@@,s,(size_t)x); return 0; }}",
            V=v, N=n, K=k), "accept")
    add("safe_control", f"ok_lt_sizemax_{n}",
        tpl("#include <string.h>\n#include <stdint.h>\n"
            "int main(void){{ char @@V@@[@@N@@+8]; char s[@@N@@+8]; size_t m=@@N@@; "
            "if(m<SIZE_MAX) memcpy(@@V@@,s,m); return 0; }}",
            V=v, N=n), "accept")
    add("safe_control", f"ok_loop_lt_unsigned_{n}",
        tpl("#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; "
            "for(unsigned i=0;i<@@N@@;i++) @@V@@[i]=0; return @@V@@[@@N@@-1]; }}",
            T=et(), V=v, N=n), "accept")
    add("safe_control", f"ok_eq_zero_index_{n}",
        tpl("#include <stddef.h>\n#include <stdint.h>\n"
            "int main(void){{ @@T@@ @@V@@[@@N@@]={{0}}; size_t i=0; "
            "if(i==0) return 0; return @@V@@[i-1]; }}",
            T=et(), V=v, N=n), "accept")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def _first(text):
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[0][:240] if lines else "(no diagnostic)"


def _save(name, body, fails, results):
    (FAILS / f"{name}.c").write_text(body)
    (FAILS / f"{name}.txt").write_text(
        "\n".join(fails)
        + f"\nDEFAULT (rc={results['--default'][0]}): {results['--default'][1][:400]}"
        + f"\nSTRICT  (rc={results['--strict'][0]}): {results['--strict'][1][:400]}\n"
    )


def check_one(case):
    name, body, expect, family = case["name"], case["body"], case["expect"], case["family"]
    path = CASES_DIR / f"{name}.c"
    path.write_text(body)
    results = {}
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
            results[mode] = (proc.returncode, _first(proc.stderr + proc.stdout))
        except subprocess.TimeoutExpired:
            results[mode] = ("timeout", f"TIMEOUT after {TIMEOUT}s")

    fails = []
    for mode, (rc, diag) in results.items():
        if rc == "timeout":
            fails.append(f"TIMEOUT {name} [{family}] in {mode}: {diag}")
        elif rc < 0:
            fails.append(f"CRASH {name} [{family}] in {mode}: rc={rc} {diag[:160]}")
    if fails:
        _save(name, body, fails, results)
        return fails

    d_rc, d_diag = results["--default"]
    s_rc, s_diag = results["--strict"]
    d_fail, s_fail = d_rc != 0, s_rc != 0
    if expect == "reject":
        if not d_fail:
            fails.append(f"DEFAULT-HOLE {name} [{family}]: expected reject, accepted in DEFAULT; "
                         f"diag: {d_diag[:160]}")
        if not s_fail:
            fails.append(f"STRICT-HOLE {name} [{family}]: expected reject, accepted in STRICT; "
                         f"diag: {s_diag[:160]}")
    elif expect == "strict":
        if not s_fail:
            fails.append(f"STRICT-HOLE {name} [{family}]: expected strict failure, accepted in STRICT; "
                         f"diag: {s_diag[:160]}")
    elif expect == "accept":
        if d_fail:
            fails.append(f"DEFAULT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in DEFAULT; "
                         f"diag: {d_diag[:160]}")
        if s_fail:
            fails.append(f"STRICT-FALSE-POSITIVE {name} [{family}]: expected accept, rejected in STRICT; "
                         f"diag: {s_diag[:160]}")
    if fails:
        _save(name, body, fails, results)
    return fails


def main():
    names = [c["name"] for c in CASES]
    assert len(names) == len(set(names)), "duplicate case name"
    assert len(CASES) >= MIN_CASES, f"only {len(CASES)} cases generated"
    counts = Counter(c["expect"] for c in CASES)
    fams = Counter(c["family"] for c in CASES)
    print(f"fuzz_unsigned: {len(CASES)} cases, seed={SEED} (0x{SEED:X}), "
          f"workers={WORKERS}, bin={CC}")
    print(f"outcomes: reject={counts['reject']} strict={counts['strict']} "
          f"accept={counts['accept']}")
    print("families: " + " ".join(f"{k}={v}" for k, v in sorted(fams.items())))

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for f in pool.map(check_one, CASES):
            failures.extend(f)
    failures.sort()

    print(f"mismatches: {len(failures)}")
    for line in failures[:10]:
        print(f"  {line}")
    if failures:
        print(f"saved fixtures in {FAILS}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
