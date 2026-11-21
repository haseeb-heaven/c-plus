#!/usr/bin/env python3
"""Check the 52 user-provided memory-bug examples in DEFAULT and STRICT."""
import concurrent.futures
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
FILES = sorted((ROOT / "tests/metrics/bugs52").glob("*.c"))
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))


def check(f):
    out = {}
    for mode in ("--default", "--strict"):
        p = subprocess.run([CC, "check", mode, str(f)], capture_output=True,
                           text=True, timeout=60)
        diag = (p.stderr + p.stdout).strip().splitlines()
        out[mode] = (p.returncode != 0, diag[0] if diag else "")
    return f.stem.replace("bug_", ""), out


def main():
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = dict(pool.map(check, FILES))
    dd = sorted(k for k, v in results.items() if v["--default"][0])
    ss = sorted(k for k, v in results.items() if v["--strict"][0])
    gd = sorted(set(results) - set(dd))
    gs = sorted(set(results) - set(ss))
    print(f"52 bug examples: DEFAULT rejected {len(dd)}/52, "
          f"STRICT rejected {len(ss)}/52")
    print(f"DEFAULT gaps: {' '.join(gd)}")
    print(f"STRICT gaps: {' '.join(gs)}")
    if os.environ.get("VERBOSE"):
        for k in gd:
            print(f"  {k}: {results[k]['--default'][1][:100]}")
    return 0 if not gd else 1


if __name__ == "__main__":
    sys.exit(main())
