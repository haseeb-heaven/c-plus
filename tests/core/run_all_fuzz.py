#!/usr/bin/env python3
"""Meta-runner: execute the C+ fuzz/metrics scripts and print one summary table.

Each script is launched as a subprocess (never with ``shell=True``) and its
stdout is captured. The final summary line is extracted from the captured
output (falling back to the last non-empty line), together with the return
code, and reported in a compact aligned table.

Usage:
    python3 tests/core/run_all_fuzz.py

Environment:
    CPLUS_BIN      Passed through to every child (compiler under test).
    INCLUDE_HEAVY=1  Also run the long mass_fuzz_all.sh / cross_tu_fuzz.py jobs.
                     Off by default because they may already run elsewhere.

The three ``*_350.py`` metrics scripts are optional (authored separately) and
are skipped gracefully when not present.
"""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
TIMEOUT = 1800  # generous per-script limit (30 minutes)
MAX_SUMMARY_WIDTH = 100

# (display name, path relative to the repository root)
DEFAULT_SCRIPTS = [
    ("fuzz_checker", "tests/core/fuzz_checker.sh"),
    ("adversarial_fuzz", "tests/core/adversarial_fuzz.py"),
    ("redteam_100", "tests/core/redteam_100.py"),
    ("brutal_memory_fuzz", "tests/core/brutal_memory_fuzz.py"),
    ("check_52", "tests/metrics/check_52.py"),
    ("measure", "tests/metrics/measure.py"),
]

# Long-running jobs that may already be running in a separate job.
HEAVY_SCRIPTS = [
    ("mass_fuzz_all", "tests/core/mass_fuzz_all.sh"),
    ("cross_tu_fuzz", "tests/core/cross_tu_fuzz.py"),
]

# Optional scripts; silently skipped if they do not exist yet.
OPTIONAL_SCRIPTS = [
    ("lifetime_350", "tests/metrics/lifetime_350.py"),
    ("bounds_350", "tests/metrics/bounds_350.py"),
    ("ffi_350", "tests/metrics/ffi_350.py"),
]

# Prefixes used by the existing scripts for their closing summary line.
SUMMARY_MARKERS = (
    "FINAL:",
    "results:",
    "fuzz checker:",
    "adversarial fuzz:",
    "red team:",
    "brutal memory fuzz:",
    "mass fuzz:",
    "cross-TU fuzz:",
    "52 bug examples:",
    "mismatches:",
    "report:",
    "SUMMARY:",
    "TOTAL:",
)


def extract_summary(stdout, stderr=""):
    """Return the last summary-matching line, else the last non-empty line."""
    text = stdout if stdout and stdout.strip() else stderr
    lines = [line.strip() for line in (text or "").splitlines() if line.strip()]
    if not lines:
        return "(no output)"
    for line in reversed(lines):
        if line.startswith(SUMMARY_MARKERS):
            return line
    return lines[-1]


def command_for(path):
    """Build an argv list for a script (no shell involved)."""
    if path.suffix == ".py":
        return [sys.executable, str(path)]
    return [str(path)]


def run_script(name, path):
    """Run one script; return (display name, status, summary, passed)."""
    cmd = command_for(path)
    env = os.environ.copy()  # CPLUS_BIN and friends pass through unchanged
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
        )
        summary = extract_summary(proc.stdout, proc.stderr)
        if proc.returncode == 0:
            return name, "PASS", summary, True
        return name, f"FAIL(rc={proc.returncode})", summary, False
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", "replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", "replace")
        summary = extract_summary(stdout, stderr)
        return name, "TIMEOUT", f"{summary} (>{TIMEOUT}s)", False
    except OSError as exc:
        return name, "ERROR", f"{type(exc).__name__}: {exc}", False


def main():
    scripts = list(DEFAULT_SCRIPTS)
    if os.environ.get("INCLUDE_HEAVY") == "1":
        scripts.extend(HEAVY_SCRIPTS)

    rows = []
    skipped = []
    for name, rel in scripts:
        path = ROOT / rel
        if not path.is_file():
            skipped.append(name)
            continue
        rows.append(run_script(name, path))

    for name, rel in OPTIONAL_SCRIPTS:
        path = ROOT / rel
        if not path.is_file():
            skipped.append(name)
            continue
        rows.append(run_script(name, path))

    name_w = max([len("NAME")] + [len(r[0]) for r in rows])
    status_w = max([len("STATUS")] + [len(r[1]) for r in rows])
    summary_w = min(
        max([len("SUMMARY")] + [len(r[2]) for r in rows]), MAX_SUMMARY_WIDTH
    )

    def line(cols):
        return (
            f"{cols[0]:<{name_w}} | {cols[1]:<{status_w}} | "
            f"{cols[2]:<{summary_w}}"
        )

    print(line(("NAME", "STATUS", "SUMMARY")))
    print("-+-".join(["-" * name_w, "-" * status_w, "-" * summary_w]))
    for name, status, summary, _ in rows:
        print(line((name, status, summary[:MAX_SUMMARY_WIDTH])))

    if skipped:
        print(f"(skipped absent optional scripts: {', '.join(skipped)})")

    passed = sum(1 for r in rows if r[3])
    failed = len(rows) - passed
    print(f"TOTAL_SCRIPTS: {len(rows)} | PASSED: {passed} | FAILED: {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
