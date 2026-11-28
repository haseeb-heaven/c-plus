#!/usr/bin/env python3
"""Native C compatibility suite for the c+ compiler (LEGACY C mode).

Every fixture is compiled and run with:

    CPLUS_BIN run --legacy-c <fixture>.c

The LEGACY backend uses ``-std=gnu89`` (see ``Driver.cpp::backendArgs``), so
only that C standard is exercised.  Each case asserts an *exact* stdout string
and an *exact* process exit code.  Compiler stderr is captured and recorded
for diagnostics but is not part of the pass/fail assertion.

Expected and actual values are written to ``summary.txt`` and ``summary.json``
next to this script.  The process exit status is 0 if every case passes and 1
otherwise.
"""
from __future__ import annotations

import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]  # tests/compiler/compatibility -> repository root
MANIFEST = HERE / "cases.json"
SUMMARY_TXT = HERE / "summary.txt"
SUMMARY_JSON = HERE / "summary.json"
TIMEOUT_SECONDS = 60


def compiler_path() -> pathlib.Path:
    """Resolve CPLUS_BIN (absolute or repo-relative); default to build/c+."""
    env = os.environ.get("CPLUS_BIN")
    if env:
        path = pathlib.Path(env)
        if not path.is_absolute():
            path = ROOT / path
        return path.resolve()
    return (ROOT / "build" / "c+").resolve()


def load_manifest() -> dict:
    with MANIFEST.open(encoding="utf-8") as handle:
        return json.load(handle)


def run_case(binary: pathlib.Path, fixture: pathlib.Path) -> dict:
    command = [str(binary), "run", "--legacy-c", str(fixture)]
    printable = shlex.join(command)
    try:
        # c+ drops its compiled executable in the current directory; use a
        # throwaway cwd so the suite never litters the repository.
        with tempfile.TemporaryDirectory(prefix="cplus_compat_") as workdir:
            proc = subprocess.run(
                command,
                cwd=workdir,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=TIMEOUT_SECONDS,
                check=False,
            )
        return {
            "command": printable,
            "exit": proc.returncode,
            "stdout": proc.stdout.decode("utf-8", errors="replace"),
            "stderr": proc.stderr.decode("utf-8", errors="replace"),
            "timed_out": False,
        }
    except subprocess.TimeoutExpired:
        return {
            "command": printable,
            "exit": None,
            "stdout": "",
            "stderr": "TIMEOUT after %d seconds" % TIMEOUT_SECONDS,
            "timed_out": True,
        }


def render_text(summary: dict) -> str:
    """Human-readable expected-vs-actual report."""
    lines = [
        "Native C compatibility suite (LEGACY C mode)",
        "=" * 62,
        "compiler : %s" % summary["compiler"],
        "standard : %s" % summary["standard"],
        "command  : %s" % summary["command_template"],
        "totals   : %d passed / %d total, %d failed"
        % (summary["passed"], summary["total"], summary["failed"]),
        "",
    ]
    for result in summary["results"]:
        status = "PASS" if result["passed"] else "FAIL"
        lines.append("[%s] %-24s %s" % (status, result["id"], result["feature"]))
        lines.append("    file       : %s" % result["file"])
        lines.append("    exit       : expected %r, actual %r"
                     % (result["expected_exit"], result["actual_exit"]))
        lines.append("    stdout exp : %s" % repr(result["expected_stdout"]))
        lines.append("    stdout got : %s" % repr(result["actual_stdout"]))
        if result["actual_stderr"]:
            lines.append("    stderr     : %s"
                         % repr(result["actual_stderr"]))
        lines.append("")
    return "\n".join(lines) + "\n"


def main() -> int:
    binary = compiler_path()
    if not binary.is_file():
        print("error: compiler not found: %s" % binary, file=sys.stderr)
        return 2

    manifest = load_manifest()
    cases = manifest["cases"]
    results = []
    failed = 0

    for case in cases:
        fixture = HERE / case["file"]
        actual = run_case(binary, fixture)
        expected_out = case["expected_stdout"]
        expected_exit = case["expected_exit"]
        stdout_ok = actual["stdout"] == expected_out
        exit_ok = actual["exit"] == expected_exit
        passed = stdout_ok and exit_ok
        if not passed:
            failed += 1
        results.append({
            "id": case["id"],
            "feature": case["feature"],
            "file": case["file"],
            "standard": manifest.get("standard", "gnu89"),
            "expected_stdout": expected_out,
            "actual_stdout": actual["stdout"],
            "expected_exit": expected_exit,
            "actual_exit": actual["exit"],
            "stdout_match": stdout_ok,
            "exit_match": exit_ok,
            "passed": passed,
            "actual_stderr": actual["stderr"],
            "command": actual["command"],
            "timed_out": actual["timed_out"],
        })

    summary = {
        "suite": manifest.get("suite", "native C compatibility"),
        "standard": manifest.get("standard", "gnu89"),
        "compiler": str(binary),
        "command_template": manifest.get(
            "invocation", "<CPLUS_BIN> run --legacy-c <fixture>"),
        "total": len(results),
        "passed": len(results) - failed,
        "failed": failed,
        "results": results,
    }
    SUMMARY_JSON.write_text(json.dumps(summary, indent=2) + "\n",
                            encoding="utf-8")
    SUMMARY_TXT.write_text(render_text(summary), encoding="utf-8")

    print("native compatibility: %d/%d passed, %d failed"
          % (summary["passed"], summary["total"], failed))
    if failed:
        print("failures:")
        for result in results:
            if not result["passed"]:
                print("  - %s (%s): exit %r -> %r, stdout_match=%s"
                      % (result["id"], result["feature"],
                         result["expected_exit"], result["actual_exit"],
                         result["stdout_match"]))
    print("summary: %s" % SUMMARY_TXT)
    print("summary: %s" % SUMMARY_JSON)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
