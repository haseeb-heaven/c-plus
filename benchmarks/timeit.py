#!/usr/bin/env python3
"""Time a program and report its median wall-clock time.

Usage: timeit.py RUNS PROGRAM

Prints two numbers: the median time in seconds and the fastest time, both to
six decimal places.  The median is what run_bench.sh reports, because a single
scheduler hiccup on a laptop should not decide a benchmark.
"""

import statistics
import subprocess
import sys
import time


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: timeit.py RUNS PROGRAM\n")
        return 2

    runs = int(sys.argv[1])
    program = sys.argv[2]
    samples = []

    # One untimed run so that page faults and CPU frequency ramp-up do not
    # land in the first sample.
    warmup = subprocess.run(
        [program], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    if warmup.returncode != 0:
        sys.stderr.write(f"{program} exited with {warmup.returncode}\n")
        return 1

    for _ in range(runs):
        start = time.perf_counter()
        proc = subprocess.run(
            [program], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        elapsed = time.perf_counter() - start
        if proc.returncode != 0:
            sys.stderr.write(f"{program} exited with {proc.returncode}\n")
            return 1
        samples.append(elapsed)

    print(f"{statistics.median(samples):.6f} {min(samples):.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
