#!/usr/bin/env python3
"""Run the p0new_* safety scenarios through `c+ check --strict`.

Each generated source in tests/safety/p0new/ carries markers:

  // scenario: NAME
  // EXPECT: reject|accept
  // DIAG: <substring>

A ``reject`` scenario passes only when the checker fails *and* the expected
DIAG substring appears in the combined stdout/stderr.  An ``accept`` scenario
passes only when the checker succeeds with no error/warning diagnostics, so
any false positive is reported as a FAIL.  All artifacts are written under
/tmp; nothing is written back into the repository.
"""
from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys
ROOT = pathlib.Path(__file__).parents[2]
SRC = ROOT / "tests" / "safety" / "p0new"
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+"))
CLANG = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")
REPORT = pathlib.Path("/tmp/p0new_report.txt")
EXPECTED_COUNT = 40
CATEGORIES = {"trunc", "s2u", "arena", "pkt", "ring",
              "jump", "file", "field", "realloc", "out"}


def markers(path: pathlib.Path) -> tuple[str, str, str]:
    text = path.read_text().splitlines()

    def grab(prefix: str) -> str:
        for line in text:
            if line.startswith(prefix):
                return line[len(prefix):].strip()
        return ""

    return (grab("// scenario:"), grab("// EXPECT:"), grab("// DIAG:"))


def main() -> int:
    if not pathlib.Path(CC).exists():
        print(f"FAIL: c+ binary not found at {CC}")
        return 2

    paths = sorted(SRC.glob("*.c"))
    lines: list[str] = []
    results: list[tuple[str, bool, str]] = []
    seen: set[str] = set()
    false_positives = 0

    if len(paths) != EXPECTED_COUNT:
        lines.append(f"SETUP FAIL: expected {EXPECTED_COUNT} scenario files, "
                     f"found {len(paths)}")
    covered: set[str] = set()

    for path in paths:
        name, expect, diag = markers(path)
        problems = []
        if not name.startswith("p0new_"):
            problems.append(f"non-p0new name {name!r}")
        if name in seen:
            problems.append("duplicate scenario name")
        seen.add(name)
        if expect not in ("reject", "accept"):
            problems.append(f"bad EXPECT {expect!r}")
        if expect == "reject" and not diag:
            problems.append("reject case missing DIAG")
        m = re.match(r"p0new_([a-z0-9]+)_", name)
        if m:
            covered.add(m.group(1))

        env = dict(os.environ)
        env["CPLUS_CLANG"] = CLANG
        try:
            r = subprocess.run([CC, "check", "--strict", str(path)],
                               capture_output=True, text=True, env=env,
                               timeout=180)
            out = r.stderr + r.stdout
        except subprocess.TimeoutExpired:
            r, out = None, "TIMEOUT"
            problems.append("checker timeout")

        diagnostics = [ln.strip() for ln in out.splitlines()
                       if "error:" in ln or "warning:" in ln]
        if r is None:
            passed = False
            detail = "TIMEOUT"
        elif expect == "reject":
            passed = r.returncode != 0 and diag in out
            detail = "expect reject+DIAG; got " + (
                f"rc={r.returncode}, diag={diag!r}, "
                f"first={diagnostics[0][:90] if diagnostics else 'none'}")
        else:
            passed = r.returncode == 0 and not diagnostics
            if not passed:
                false_positives += 1
            detail = "expect clean accept; got " + (
                f"rc={r.returncode}, diagnostics={diagnostics[:2]}")

        for problem in problems:
            passed = False
            detail = f"{problem}; {detail}"

        results.append((name, passed, detail))
        lines.append(f"[{'PASS' if passed else 'FAIL'}] {name:42} "
                     f"expect={expect:6} {detail}")

    missing = sorted(CATEGORIES - covered)
    if missing:
        lines.append(f"SETUP FAIL: missing categories: {', '.join(missing)}")
        results.append(("<categories>", False, ", ".join(missing)))

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    lines.append("")
    lines.append(f"p0new: {passed}/{total} passed; "
                 f"{false_positives} false positives; "
                 f"{EXPECTED_COUNT} scenario files; "
                 f"10 categories; CPLUS_CLANG={CLANG}")
    lines.append("")
    lines.append("Per-category breakdown:")
    for cat in sorted(CATEGORIES):
        cat_rows = [ok for name, ok, _ in results
                    if name.startswith(f"p0new_{cat}_")]
        lines.append(f"  {cat:7} {sum(cat_rows)}/{len(cat_rows)}")

    report = "\n".join(lines) + "\n"
    REPORT.write_text(report)
    print(report, end="")
    print(f"report: {REPORT}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
