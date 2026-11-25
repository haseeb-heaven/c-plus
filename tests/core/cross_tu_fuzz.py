#!/usr/bin/env python3
"""Cross-translation-unit fuzzing for C+.

Generates hundreds of two-file programs and verifies them in several ways:
  * each translation unit is accepted by `c+ check` in DEFAULT and STRICT;
  * the driver builds and links both TUs into one native executable;
  * the executable runs with the expected result;
  * the same sources are built and run with the host C compiler (differential).

This exercises the C ABI, declarations, separate compilation, the C+ runtime
link path and cross-TU linker behaviour.  It is not a claim that cross-TU
ownership effects are proven: the checker analyses one TU at a time and that
limitation is documented separately.
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
CLANG = os.environ.get("CLANG_BIN", "clang")
OUT = ROOT / "tests/out/core/cross_tu"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x5EED1234"), 0)
COUNT = int(os.environ.get("TU_COUNT", "600"))


def build_cases():
    rng = random.Random(SEED)
    cases = []
    for i in range(COUNT):
        k = i
        v = rng.randint(-40, 40)
        m = rng.randint(2, 9)
        a = rng.randint(-20, 20)
        expected = v * m + a
        main_a = f"""#include <stdio.h>
int helper_{k}(int x);
int main(void){{ printf("%d\\n", helper_{k}({v})); return 0; }}"""
        helper_a = f"int helper_{k}(int x){{ return x * {m} + {a}; }}"
        cases.append((f"arith_{k}", main_a, helper_a, f"{expected}\n"))

        n = rng.randint(2, 24)
        main_b = f"""#include <stdlib.h>
int *make_{k}(int n);
int consume_{k}(int *p);
int main(void){{ int *p=make_{k}({n}); if(!p)return 1; int r=consume_{k}(p); return r=={n-1}?0:1; }}"""
        helper_b = f"""#include <stdlib.h>
int *make_{k}(int n){{ int *p=malloc((size_t)n*sizeof(int)); if(!p)return 0; for(int i=0;i<n;i++) p[i]=i; return p; }}
int consume_{k}(int *p){{ int v=p[{n-1}]; free(p); return v; }}"""
        cases.append((f"alloc_{k}", main_b, helper_b, None))
    return cases


def save_fail(name, which, body):
    (FAILS / f"{name}.{which}").write_text(body + "\n")


def run_case(case):
    name, main_src, helper_src, expected = case
    main = OUT / f"{name}_main.c"
    helper = OUT / f"{name}_helper.c"
    main.write_text(main_src + "\n")
    helper.write_text(helper_src + "\n")
    fails = []
    stats = {"checks": 0, "builds": 0, "runs": 0, "clang": 0}

    for tag, src in (("main", main), ("helper", helper)):
        for mode in ("--default", "--strict"):
            stats["checks"] += 1
            try:
                p = subprocess.run([CC, "check", mode, str(src)],
                                   capture_output=True, text=True, timeout=60)
                if p.returncode != 0:
                    save_fail(name, f"{tag}{mode}.c", main_src if tag == "main" else helper_src)
                    fails.append(f"CHECK-FAIL {name} {tag} {mode}\n{p.stderr[:200]}")
            except subprocess.TimeoutExpired:
                fails.append(f"CHECK-TIMEOUT {name} {tag} {mode}")

    stats["builds"] += 1
    exe = OUT / f"{name}.bin"
    try:
        b = subprocess.run([CC, "build", str(main), str(helper), "-o", str(exe)],
                           capture_output=True, text=True, timeout=120)
        if b.returncode != 0:
            save_fail(name, "build.c", main_src + "\n/* --- */\n" + helper_src)
            fails.append(f"BUILD-FAIL {name}\n{b.stderr[:300]}")
        else:
            stats["runs"] += 1
            r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            if expected is not None and r.stdout != expected:
                fails.append(f"OUTPUT-FAIL {name}: got {r.stdout!r} want {expected!r}")
            if r.returncode != 0:
                fails.append(f"RUN-FAIL {name} rc={r.returncode}")
    except subprocess.TimeoutExpired:
        fails.append(f"BUILD/RUN-TIMEOUT {name}")

    # Differential: host C compiler must also build and link the same sources.
    stats["clang"] += 1
    cexe = OUT / f"{name}.clang.bin"
    try:
        d = subprocess.run([CLANG, "-x", "c", "-std=c11", str(main), str(helper),
                            "-o", str(cexe)], capture_output=True, text=True, timeout=120)
        if d.returncode != 0:
            save_fail(name, "clang.c", main_src + "\n/* --- */\n" + helper_src)
            fails.append(f"CLANG-BUILD-FAIL {name}\n{d.stderr[:200]}")
        else:
            r = subprocess.run([str(cexe)], capture_output=True, text=True, timeout=30)
            if expected is not None and r.stdout != expected:
                fails.append(f"CLANG-OUTPUT-FAIL {name}: {r.stdout!r}")
    except FileNotFoundError:
        stats["clang"] = 0
    except subprocess.TimeoutExpired:
        fails.append(f"CLANG-TIMEOUT {name}")
    return fails, stats


def main():
    cases = build_cases()
    print(f"cross-TU fuzz: {len(cases)} two-file programs, seed={SEED}, workers={WORKERS}")
    failures = []
    stats = {"checks": 0, "builds": 0, "runs": 0, "clang": 0}
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for fails, s in pool.map(run_case, cases):
            failures.extend(fails)
            for k in stats:
                stats[k] += s[k]
    print(f"results: {stats['checks']} per-TU checks, {stats['builds']} C+ links, "
          f"{stats['runs']} C+ runs, {stats['clang']} differential clang builds, "
          f"{len(failures)} failures")
    for f in failures[:30]:
        print(f)
    if failures:
        print(f"saved fixtures in {FAILS}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
