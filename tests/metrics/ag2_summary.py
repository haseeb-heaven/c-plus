#!/usr/bin/env python3
"""Aggregate the nine ``ag2_*`` metric generators into one report.

This driver runs the sibling generators (listed in ``SCRIPTS``) as
subprocesses and folds their text reports into one compact table::

    NAME  STATUS  CASES  MISMATCH  FP  FIRST 3 NAMES
    ...

followed by a single summary line::

    TOTAL_CASES <n>  PASSED <p>  FAILED <f>

A generator is PASSED only when it explicitly reports zero mismatches *and*
zero false positives (and exits cleanly).  Scripts that are not present yet
are marked SKIPPED and excluded from the PASS/FAIL and case tallies.  The exit
status is non-zero whenever any executed script has mismatches or false
positives (or otherwise fails).

Implementation notes:
  * The repository root is derived with ``pathlib.Path(__file__).parents[2]``.
  * Each child is launched with ``sys.executable`` and an argument list (never
    ``shell=True``), with ``cwd=ROOT`` and a 1800-second per-script timeout.
  * The inherited environment (including ``CPLUS_BIN`` when set) reaches every
    child unchanged.
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
    "ag2_ptr_100.py",
    "ag2_extent_100.py",
    "ag2_loop_100.py",
    "ag2_field_100.py",
    "ag2_uaf_100.py",
    "ag2_free_100.py",
    "ag2_leakmem_100.py",
    "ag2_leakio_100.py",
    "ag2_init_100.py",
)

# Lines that carry report metadata rather than an individual case detail.
_INFO_PREFIXES = (
    "FINAL", "RESULTS", "RESULT", "OUTCOMES", "MISMATCH", "SAVED",
    "WARNING", "NOTE", "SUMMARY", "TOTAL", "PASSED", "FAILED",
    "FALSE POSITIVES", "FALSE POSITIVE", "FALSE-POSITIVE COUNT",
    "EXPECTED", "CATEGORIES",
)

# Generators spell out their counts in a combined final/results line, in bare
# "mismatches: N" / "false positives: N" lines, or in an "N generated cases"
# line.  Accept all of the common spellings, including the optional FP count
# in a FINAL/RESULTS line.
_FINAL_RE = re.compile(
    r"\bfinal\s*:\s*(?P<status>PASS|FAIL)\s*\(\s*(?P<cases>\d+)\s*cases\s*,\s*"
    r"(?P<mm>\d+)\s*mismatches?"
    r"(?:\s*,\s*(?P<fp>\d+)\s*(?:false[-\s_]?positives?|fps?))?"
    r"\s*\)", re.I)
_RESULTS_RE = re.compile(
    r"\bresults?\s*:\s*(?P<cases>\d+)\s*cases\s*,\s*(?P<mm>\d+)\s*mismatches?"
    r"(?:\s*,\s*(?P<fp>\d+)\s*(?:false[-\s_]?positives?|fps?))?", re.I)
_MISMATCH_RE = re.compile(
    r"\b(?:mismatches?|mismatch[-\s]*count)\s*(?:count)?\s*[:=]?\s*(?P<mm>\d+)\b",
    re.I)
_CASES_RE = re.compile(
    r"\b(?P<cases>\d+)\s+(?:generated\s+|total\s+)?cases\b", re.I)
_CASES_COLON_RE = re.compile(r"\bcases\s*[:=]\s*(?P<cases>\d+)\b", re.I)

_FP_SPLIT_RE = re.compile(
    r"\bfalse[-\s_]?positives?\s*(?:\([^)]*\))?\s*[:=]\s*"
    r"(?P<a>\d+)\s*/\s*(?P<b>\d+)", re.I)
_FP_RE = re.compile(
    r"\b(?:false[-\s_]?positives?|fp[-\s_]*count|fps?)"
    r"(?:\s*\([^)]*\)|\s*/[-\w\s]+?)?\s*[:=]\s*(?P<fp>\d+)\b", re.I)
_FP_INLINE_RE = re.compile(
    r"(?<![\w-])(?P<fp>\d+)\s+false[-\s_]?positives?\b", re.I)

# Used only to recognise *detail* lines (e.g. "DEFAULT-FALSE-POSITIVE name ...")
# when no explicit false-positive count was printed by the generator.
_FP_DETAIL_RE = re.compile(r"(?:DEFAULT-|STRICT-)?FALSE-POSITIVE\b", re.I)

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
    """Pull the case name out of a mismatch / false-positive detail line."""
    s = line.strip()
    s = _BULLET_RE.sub("", s)
    s = _BRACKET_TAG_RE.sub("", s)
    s = _NAME_TAG_RE.sub("", s)
    if s.startswith("..."):
        return None
    match = _TOKEN_RE.match(s)
    return match.group(0) if match else None


def _is_summary_line(stripped):
    """True when the line is metadata rather than a per-case detail line."""
    if stripped.upper().startswith(_INFO_PREFIXES):
        return True
    if stripped.upper().startswith(("FP:", "FP COUNT", "FP=")):
        return True
    for pattern in (_FINAL_RE, _RESULTS_RE, _FP_RE, _FP_SPLIT_RE,
                    _FP_INLINE_RE, _CASES_COLON_RE):
        if pattern.search(stripped):
            return True
    return False


def parse_report(output):
    """Return (cases, mismatches, false_positives, first_three_names)."""
    lines = output.splitlines()

    cases = None
    mismatches = None
    fps = None

    match = _FINAL_RE.search(output)
    if match:
        cases = int(match.group("cases"))
        mismatches = int(match.group("mm"))
        if match.group("fp") is not None:
            fps = int(match.group("fp"))

    if cases is None or mismatches is None:
        match = _RESULTS_RE.search(output)
        if match:
            if cases is None and match.group("cases") is not None:
                cases = int(match.group("cases"))
            if mismatches is None and match.group("mm") is not None:
                mismatches = int(match.group("mm"))
            if fps is None and match.group("fp") is not None:
                fps = int(match.group("fp"))

    if mismatches is None:
        match = _MISMATCH_RE.search(output)
        if match:
            mismatches = int(match.group("mm"))
    if cases is None:
        match = _CASES_RE.search(output) or _CASES_COLON_RE.search(output)
        if match:
            cases = int(match.group("cases"))
    if fps is None:
        match = _FP_SPLIT_RE.search(output)
        if match:
            fps = int(match.group("a")) + int(match.group("b"))
    if fps is None:
        match = _FP_RE.search(output)
        if match:
            fps = int(match.group("fp"))
    if fps is None:
        match = _FP_INLINE_RE.search(output)
        if match:
            fps = int(match.group("fp"))

    names = []
    fp_details = 0
    detail_names = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        if _FP_DETAIL_RE.search(stripped):
            fp_details += 1
        # Detail lines are indented or start with an uppercase tag/bracket.
        if not (line[:1].isspace() or stripped[:1].isupper()
                or stripped.startswith("[")):
            continue
        if _is_summary_line(stripped):
            continue
        name = _extract_name(stripped)
        if name and name not in detail_names:
            detail_names.append(name)

    if fps is None:
        # No stated count: fall back to the number of false-positive details.
        fps = fp_details
    if mismatches is None:
        # No stated count: fall back to the number of detail lines seen.
        mismatches = len(detail_names)
    names = detail_names[:3]

    if not mismatches:
        names = names[:0]
    return cases, mismatches, fps, names


def run_script(path):
    """Execute one generator; return (cases, mismatches, fps, names, ok, note)."""
    # The inherited environment (including CPLUS_BIN when set) reaches the child.
    env = os.environ.copy()
    try:
        proc = subprocess.run(
            [sys.executable, str(path)],
            cwd=str(ROOT),
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

    cases, mismatches, fps, names = parse_report(output)

    note = ""
    if timed_out:
        note = f"TIMEOUT after {TIMEOUT}s"
    elif rc != 0:
        note = f"rc={rc}"
    ok = (mismatches == 0) and (fps == 0) and not timed_out and rc == 0
    return cases, mismatches, fps, names, ok, note


def main():
    rows = []
    for filename in SCRIPTS:
        path = METRICS_DIR / filename
        if not path.is_file():
            rows.append({
                "script": filename, "status": "SKIPPED", "cases": None,
                "mismatches": None, "fp": None, "names": "not present yet",
                "note": "",
            })
            continue
        cases, mismatches, fps, names, ok, note = run_script(path)
        rows.append({
            "script": filename,
            "status": "PASSED" if ok else "FAILED",
            "cases": cases,
            "mismatches": mismatches,
            "fp": fps,
            "names": names,
            "note": note,
        })

    headers = ("NAME", "STATUS", "CASES", "MISMATCH", "FP", "FIRST 3 NAMES")

    def cell(row, key):
        value = row[key]
        if key in ("cases", "mismatches", "fp"):
            return str(value) if value is not None else "?"
        return value

    widths = [
        max([len(headers[0])] + [len(r["script"]) for r in rows]),
        max([len(headers[1])] + [len(r["status"]) for r in rows]),
        max([len(headers[2])] + [len(cell(r, "cases")) for r in rows]),
        max([len(headers[3])] + [len(cell(r, "mismatches")) for r in rows]),
        max([len(headers[4])] + [len(cell(r, "fp")) for r in rows]),
        len(headers[5]),
    ]

    def fmt(cols):
        return (f"{cols[0]:<{widths[0]}}  {cols[1]:<{widths[1]}}  "
                f"{cols[2]:>{widths[2]}}  {cols[3]:>{widths[3]}}  "
                f"{cols[4]:>{widths[4]}}  {cols[5]}")

    print(fmt(headers))
    print("  ".join("-" * w for w in widths))
    for row in rows:
        if row["status"] == "SKIPPED":
            names_text = row["names"]
        else:
            names_text = ", ".join(row["names"]) if row["names"] else row["note"]
        if len(names_text) > 68:
            names_text = names_text[:65] + "..."
        print(fmt((row["script"], row["status"], cell(row, "cases"),
                   cell(row, "mismatches"), cell(row, "fp"), names_text)))

    executed = [r for r in rows if r["status"] in ("PASSED", "FAILED")]
    total_cases = sum(r["cases"] for r in executed if r["cases"] is not None)
    passed = sum(1 for r in executed if r["status"] == "PASSED")
    failed = sum(1 for r in executed if r["status"] == "FAILED")
    print(f"TOTAL_CASES {total_cases}  PASSED {passed}  FAILED {failed}")

    bad = any((r["mismatches"] or 0) > 0 or (r["fp"] or 0) > 0
              for r in executed)
    return 1 if (failed or bad) else 0


if __name__ == "__main__":
    sys.exit(main())
