#!/usr/bin/env python3
"""Aggregate the four undefined-behaviour (UB) metric generators.

This driver runs the sibling generators as subprocesses and folds their
text reports into one compact table:

    SCRIPT                 STATUS   CASES  MISMATCH  FIRST 3 MISMATCH NAMES
    ...

Followed by a single summary line:

    TOTAL <n>  PASSED <n>  FAILED <n>

A generator is PASSED only when it explicitly reports 0 mismatches (and exits
cleanly).  Scripts that are not present yet are marked SKIPPED and excluded
from the PASS/FAIL tallies.  The exit status is non-zero if any executed
script failed.

Implementation notes:
  * The repository root is derived with ``pathlib.Path(__file__).parents[2]``.
  * ``CPLUS_BIN`` is passed straight through to every child via the inherited
    environment so all generators exercise the same compiler binary.
  * Children are launched with an argument list (never ``shell=True``) with a
    1800-second per-script timeout.
"""

import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
METRICS_DIR = ROOT / "tests" / "metrics"

TIMEOUT = 1800  # seconds per generator

# Order is intentionally stable for reproducible reports.
SCRIPTS = (
    "ub_alloc_260.py",
    "ub_arith_260.py",
    "ub_ptr_260.py",
    "ub_init_260.py",
)

# Lines that carry report metadata rather than an individual mismatch.
_INFO_PREFIXES = (
    "FINAL", "RESULTS", "RESULT", "OUTCOMES", "MISMATCH", "SAVED",
    "WARNING", "NOTE", "SUMMARY", "TOTAL", "PASSED", "FAILED",
)

# A few generators spell out their counts in a combined final line; others
# use a bare "mismatches: N" line.  Accept all of the common spellings.
_FINAL_RE = re.compile(
    r"\bfinal\s*:\s*(?P<status>PASS|FAIL)\s*\(\s*(?P<cases>\d+)\s*cases\s*,\s*"
    r"(?P<mm>\d+)\s*mismatches?\s*\)", re.I)
_RESULTS_RE = re.compile(
    r"\bresults?\s*:\s*(?P<cases>\d+)\s*cases\s*,\s*(?P<mm>\d+)\s*mismatches?", re.I)
_MISMATCH_RE = re.compile(r"\bmismatches?\s*(?:count)?\s*:\s*(?P<mm>\d+)", re.I)
_CASES_RE = re.compile(r"\b(?P<cases>\d+)\s+cases\b", re.I)

_NAME_TAG_RE = re.compile(
    r"^(?:DEFAULT|STRICT|CRASH|TIMEOUT|HOLE|FALSE-POSITIVE|OS-ERROR)[A-Z0-9_-]*\s+")
_BRACKET_TAG_RE = re.compile(r"^(?:[A-Z][A-Z0-9_-]*|)\[[^\]]+\]\s*")
_BULLET_RE = re.compile(r"^[*#\-\+\u2022]+\s*")
_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def _as_text(value):
    """Normalise subprocess output (str or bytes) to text."""
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return value


def _extract_name(line):
    """Pull the case name out of a mismatch detail line, if present."""
    s = line.strip()
    s = _BULLET_RE.sub("", s)
    s = _BRACKET_TAG_RE.sub("", s)
    s = _NAME_TAG_RE.sub("", s)
    if s.startswith("..."):
        return None
    match = _TOKEN_RE.match(s)
    return match.group(0) if match else None


def parse_report(output):
    """Return (cases, mismatches, first_three_names) from a generator's output."""
    lines = output.splitlines()

    cases = None
    mismatches = None

    match = _FINAL_RE.search(output)
    if match:
        cases = int(match.group("cases"))
        mismatches = int(match.group("mm"))

    if cases is None or mismatches is None:
        match = _RESULTS_RE.search(output)
        if match:
            cases = cases if cases is not None else int(match.group("cases"))
            mismatches = mismatches if mismatches is not None else int(match.group("mm"))

    if mismatches is None:
        match = _MISMATCH_RE.search(output)
        if match:
            mismatches = int(match.group("mm"))
    if cases is None:
        match = _CASES_RE.search(output)
        if match:
            cases = int(match.group("cases"))

    names = []
    if mismatches:
        for line in lines:
            stripped = line.strip()
            if not stripped:
                continue
            # Detail lines are indented or start with an uppercase tag.
            if not (line[:1].isspace() or stripped[:1].isupper() or stripped.startswith("[")):
                continue
            if stripped.upper().startswith(_INFO_PREFIXES):
                continue
            name = _extract_name(stripped)
            if name and name not in names:
                names.append(name)
            if len(names) == 3:
                break

    if mismatches is None:
        # The report did not state a count: fall back to the details we saw.
        mismatches = len(names)
    if not mismatches:
        names = []
    return cases, mismatches, names


def run_script(path):
    """Execute one generator; return (cases, mismatches, names, ok, note)."""
    # os.environ.copy() carries CPLUS_BIN (when set) through to the child.
    env = os.environ.copy()
    try:
        proc = subprocess.run(
            [sys.executable, str(path)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
            env=env,
        )
        output = (proc.stdout or "") + "\n" + (proc.stderr or "")
        rc = proc.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        output = _as_text(exc.stdout) + "\n" + _as_text(exc.stderr)
        rc = None
        timed_out = True

    cases, mismatches, names = parse_report(output)

    note = ""
    if timed_out:
        note = f"TIMEOUT after {TIMEOUT}s"
    elif rc != 0:
        note = f"rc={rc}"
    ok = (mismatches == 0) and not timed_out and rc == 0
    return cases, mismatches, names, ok, note


def main():
    rows = []
    for filename in SCRIPTS:
        path = METRICS_DIR / filename
        if not path.is_file():
            rows.append({"script": filename, "status": "SKIPPED", "cases": "-",
                         "mismatches": "-", "names": "not present yet"})
            continue
        cases, mismatches, names, ok, note = run_script(path)
        status = "PASSED" if ok else "FAILED"
        rows.append({
            "script": filename,
            "status": status,
            "cases": str(cases) if cases is not None else "?",
            "mismatches": str(mismatches),
            "names": ", ".join(names) if names else (note or "-"),
        })

    headers = ("SCRIPT", "STATUS", "CASES", "MISMATCH", "FIRST 3 MISMATCH NAMES")
    widths = [
        max([len(headers[0])] + [len(r["script"]) for r in rows]),
        max([len(headers[1])] + [len(r["status"]) for r in rows]),
        max([len(headers[2])] + [len(r["cases"]) for r in rows]),
        max([len(headers[3])] + [len(r["mismatches"]) for r in rows]),
        len(headers[4]),
    ]

    def fmt(cols):
        return (f"{cols[0]:<{widths[0]}}  {cols[1]:<{widths[1]}}  "
                f"{cols[2]:>{widths[2]}}  {cols[3]:>{widths[3]}}  {cols[4]}")

    print(fmt(headers))
    print("  ".join("-" * w for w in widths))
    for row in rows:
        names_text = row["names"]
        if len(names_text) > 68:
            names_text = names_text[:65] + "..."
        print(fmt((row["script"], row["status"], row["cases"],
                   row["mismatches"], names_text)))

    total = sum(1 for r in rows if r["status"] in ("PASSED", "FAILED"))
    passed = sum(1 for r in rows if r["status"] == "PASSED")
    failed = sum(1 for r in rows if r["status"] == "FAILED")
    print(f"TOTAL {total}  PASSED {passed}  FAILED {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
