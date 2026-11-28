#!/usr/bin/env python3
"""Measure C+ on three axes the project is judged by.

  1. ACCEPTANCE  - how many real, unchanged safe C programs compile.
  2. DETECTION   - how many genuine memory-safety bugs are rejected.
  3. FALSE POSITIVES - how many safe programs are wrongly rejected.

The safe corpus consists of hand-written ordinary ISO C programs (this
directory's safe_corpus/) plus the repository's positive C fixtures. Every
corpus program compiles cleanly under `clang -Wall -Wextra -Werror`, so a C+
rejection is a false positive by construction. The bug corpus is the curated
tests/safety/ directory. Results are written to tests/out/metrics/report.json.
"""
import concurrent.futures
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
CLANG = os.environ.get("CLANG_BIN", "clang")
OUT = ROOT / "tests/out/metrics"
OUT.mkdir(parents=True, exist_ok=True)
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))

CURATED = sorted((ROOT / "tests/metrics/safe_corpus").glob("*.c"))
# uaf_conditional is a documented borderline case (possible UAF on a branch)
# that is intentionally accepted only in DEFAULT; it is not a safe program.
EXCLUDE = {"uaf_conditional.c"}
FIXTURES = sorted(p for p in (ROOT / "tests/core/positive").glob("*.c")
                  if p.name not in EXCLUDE)
SAFE = CURATED + FIXTURES
BUGS = sorted((ROOT / "tests/safety").glob("*.c")) + \
       sorted((ROOT / "tests/safety").glob("*.cp"))


def check(path, strict=False):
    cmd = [CC, "check"] + (["--strict"] if strict else []) + [str(path)]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        diag = (p.stderr + p.stdout).strip().splitlines()
        first = diag[0] if diag else ""
        return p.returncode, first
    except subprocess.TimeoutExpired:
        return -99, "timeout"


def clang_ok(path):
    try:
        p = subprocess.run([CLANG, "-x", "c", "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-fsyntax-only", str(path)],
                           capture_output=True, text=True, timeout=60)
        return p.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False


def main():
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        safe_default = list(pool.map(lambda p: check(p), SAFE))
        safe_strict = list(pool.map(lambda p: check(p, True), SAFE))
        bug_default = list(pool.map(lambda p: check(p), BUGS))
        bug_strict = list(pool.map(lambda p: check(p, True), BUGS))
        clang_results = list(pool.map(clang_ok, CURATED))

    accepted = sum(1 for rc, _ in safe_default if rc == 0)
    accepted_strict = sum(1 for rc, _ in safe_strict if rc == 0)
    detected = sum(1 for rc, _ in bug_default if rc != 0)
    detected_strict = sum(1 for rc, _ in bug_strict if rc != 0)
    clang_clean = sum(1 for x in clang_results if x)

    fp_default = [(str(p.relative_to(ROOT)), d) for p, (rc, d) in zip(SAFE, safe_default)
                  if rc != 0]
    fp_strict = [(str(p.relative_to(ROOT)), d) for p, (rc, d) in zip(SAFE, safe_strict)
                 if rc != 0]
    missed = [(str(p.relative_to(ROOT)), d) for p, (rc, d) in zip(BUGS, bug_default)
              if rc == 0]

    report = {
        "safe_corpus": {
            "total": len(SAFE),
            "curated_real_c": len(CURATED),
            "repo_positive_fixtures": len(FIXTURES),
            "clang_clean_curated": clang_clean,
            "cplus_accepted_default": accepted,
            "cplus_accepted_strict": accepted_strict,
            "acceptance_rate_default": round(accepted / len(SAFE), 3),
            "acceptance_rate_strict": round(accepted_strict / len(SAFE), 3),
            "false_positives_default": len(fp_default),
            "false_positives_strict": len(fp_strict),
            "false_positive_rate_default": round(len(fp_default) / len(SAFE), 3),
            "examples": fp_default[:10],
            "strict_examples": fp_strict[:10],
        },
        "bug_corpus": {
            "total": len(BUGS),
            "detected_default": detected,
            "detected_strict": detected_strict,
            "detection_rate_default": round(detected / len(BUGS), 3),
            "detection_rate_strict": round(detected_strict / len(BUGS), 3),
            "missed_default": len(missed),
            "examples": missed[:10],
        },
    }
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    s = report["safe_corpus"]
    b = report["bug_corpus"]
    print(f"safe C acceptance (DEFAULT): {s['cplus_accepted_default']}/{s['total']} "
          f"({s['acceptance_rate_default']:.1%})")
    print(f"safe C acceptance (STRICT):  {s['cplus_accepted_strict']}/{s['total']} "
          f"({s['acceptance_rate_strict']:.1%})")
    print(f"clang -Wall -Wextra -Werror clean (curated real C): "
          f"{s['clang_clean_curated']}/{s['curated_real_c']}")
    print(f"false positives (DEFAULT/STRICT): {s['false_positives_default']}/"
          f"{s['false_positives_strict']}")
    print(f"bug detection (DEFAULT): {b['detected_default']}/{b['total']} "
          f"({b['detection_rate_default']:.1%})")
    print(f"bug detection (STRICT):  {b['detected_strict']}/{b['total']} "
          f"({b['detection_rate_strict']:.1%})")
    if fp_default:
        print("false positives:", fp_default[:5])
    if missed:
        print("missed bugs:", missed[:5])
    print(f"report: {OUT / 'report.json'}")
    # Fail only if there are false positives or clang-clean programs rejected.
    return 1 if fp_default else 0


if __name__ == "__main__":
    sys.exit(main())
