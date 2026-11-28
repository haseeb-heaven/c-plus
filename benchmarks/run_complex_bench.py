#!/usr/bin/env python3
"""Build, run and report the long multi-phase benchmark.

The benchmark is compiled three ways:

  cplus_safe   C+ --release with runtime checks
  cplus_legacy C+ --release --legacy-c without runtime checks
  clang        plain C with the same source and -O2

Every phase prints a deterministic checksum. The runner refuses to report
timings unless all three variants agree on every checksum, so an optimizer
that deletes work cannot be mistaken for a speedup.

Results are written to benchmarks/out/complex_benchmark.json. The default
scale (100) makes each C+ run last several minutes; CPLUS_BENCH_SCALE and
CPLUS_BENCH_RUNS can shorten it for smoke testing.

Usage:
    python3 benchmarks/run_complex_bench.py
    CPLUS_BENCH_SCALE=1 CPLUS_BENCH_RUNS=1 python3 benchmarks/run_complex_bench.py
"""

from __future__ import annotations

import datetime
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "benchmarks" / "complex_workload.c"
OUT = ROOT / "benchmarks" / "out"
SCALE = int(os.environ.get("CPLUS_BENCH_SCALE", "100"))
RUNS = int(os.environ.get("CPLUS_BENCH_RUNS", "1"))
TIMEOUT = int(os.environ.get("CPLUS_BENCH_TIMEOUT", "1800"))

CPLUS = Path(os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+")))
CLANG = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")

VARIANTS = {
    "cplus_safe": [str(CPLUS), "build", "--release", str(SOURCE)],
    "cplus_legacy": [str(CPLUS), "build", "--release", "--legacy-c",
                     str(SOURCE)],
    "clang": [CLANG, "-std=gnu17", "-O2", "-pthread",
              "-I" + str(ROOT / "runtime" / "include"),
              "-include", "cplus/cplus.h", str(SOURCE), "-lpthread", "-lm"],
}

PHASES = ("matrix", "hash", "sort", "arena")


def run(cmd: list[str], label: str,
        env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True,
                              timeout=TIMEOUT, env=env)
    except subprocess.TimeoutExpired as exc:
        print(f"error: {label} timed out after {TIMEOUT}s", file=sys.stderr)
        raise SystemExit(2) from exc
    if proc.returncode != 0:
        print(f"error: {label} failed (rc={proc.returncode})\n{proc.stderr[-2000:]}",
              file=sys.stderr)
        raise SystemExit(1)
    return proc


def parse(output: str) -> dict:
    phases: dict[str, dict[str, int]] = {}
    total: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "PHASE" and len(fields) == 5:
            phases[fields[1]] = {"ns": int(fields[3]), "checksum": int(fields[4])}
        elif fields[0] == "TOTAL" and len(fields) == 7:
            total = {"ns": int(fields[2]), "matrix": int(fields[3]),
                     "hash": int(fields[4]), "sort": int(fields[5]),
                     "arena": int(fields[6])}
    if set(phases) != set(PHASES) or not total:
        raise ValueError("benchmark output missing phases or TOTAL line")
    return {"phases": phases, "total": total}


def main() -> int:
    if SCALE < 1 or RUNS < 1:
        print("error: CPLUS_BENCH_SCALE and CPLUS_BENCH_RUNS must be positive",
              file=sys.stderr)
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, CPLUS_BENCH_SCALE=str(SCALE))

    binaries: dict[str, Path] = {}
    for name, command in VARIANTS.items():
        exe = OUT / f"complex_{name}.bin"
        log = OUT / f"complex_{name}.build.log"
        if name == "clang":
            # Keep the libraries at the end of the linker command line.
            build_cmd = command[:-2] + ["-o", str(exe)] + command[-2:]
        else:
            build_cmd = command + ["-o", str(exe)]
        proc = run(build_cmd, name, env)
        log.write_text(proc.stdout + proc.stderr)
        binaries[name] = exe

    samples: dict[str, list[dict]] = {name: [] for name in VARIANTS}
    for name, exe in binaries.items():
        for _ in range(RUNS):
            samples[name].append(parse(run([str(exe)], name, env).stdout))

    # Correctness gate: every build must compute the same checksums.
    checksums = {
        name: {phase: samples[name][0]["phases"][phase]["checksum"]
               for phase in PHASES}
        for name in VARIANTS
    }
    reference = checksums["clang"]
    mismatches = [name for name, values in checksums.items()
                  if values != reference]
    if mismatches:
        print(f"error: checksum mismatch in {', '.join(mismatches)}",
              file=sys.stderr)
        return 1

    report = {
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source": str(SOURCE.relative_to(ROOT)),
        "scale": SCALE,
        "runs": RUNS,
        "host": os.uname().sysname + " " + os.uname().machine,
        "checksums": reference,
        "phases": {},
        "totals": {},
    }
    for phase in PHASES:
        medians = {name: statistics.median(
            s["phases"][phase]["ns"] for s in samples[name])
            for name in VARIANTS}
        report["phases"][phase] = {
            "ns": medians,
            "ratio_to_clang": {name: medians[name] / medians["clang"]
                               for name in VARIANTS if medians["clang"]},
        }
    for name in VARIANTS:
        report["totals"][name] = statistics.median(
            s["total"]["ns"] for s in samples[name])
    report["total_ratio_to_clang"] = {
        name: report["totals"][name] / report["totals"]["clang"]
        for name in VARIANTS if report["totals"]["clang"]
    }

    json_path = OUT / "complex_benchmark.json"
    json_path.write_text(json.dumps(report, indent=2) + "\n")

    print(f"complex benchmark scale={SCALE} runs={RUNS} (median ns)")
    print(f"{'phase':<8} {'c+safe':>14} {'c+legacy':>14} {'clang':>14} "
          f"{'safe/clang':>11} {'legacy/clang':>13}")
    for phase in PHASES:
        entry = report["phases"][phase]
        print(f"{phase:<8} {entry['ns']['cplus_safe']:>14.0f} "
              f"{entry['ns']['cplus_legacy']:>14.0f} "
              f"{entry['ns']['clang']:>14.0f} "
              f"{entry['ratio_to_clang']['cplus_safe']:>10.2f}x "
              f"{entry['ratio_to_clang']['cplus_legacy']:>12.2f}x")
    print(f"{'TOTAL':<8} {report['totals']['cplus_safe']:>14.0f} "
          f"{report['totals']['cplus_legacy']:>14.0f} "
          f"{report['totals']['clang']:>14.0f} "
          f"{report['total_ratio_to_clang']['cplus_safe']:>10.2f}x "
          f"{report['total_ratio_to_clang']['cplus_legacy']:>12.2f}x")
    print(f"checksums verified across all variants; details: {json_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
