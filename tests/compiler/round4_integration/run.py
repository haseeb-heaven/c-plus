#!/usr/bin/env python3
"""Round-4 blue integration verifier.

Runs the full C+ compiler test battery in a fixed order, one step at a time,
after the parent has rebuilt ``build/c+``.  Every step gets a hard wall-clock
timeout and its exact command, return code, final summary line and pass/fail
counts are recorded.  Failures are never suppressed (no ``|| true``); a
failing step is recorded and the run continues so the whole evidence set is
collected, while the process exit code is non-zero if any required step failed.

This file only writes NEW files below its own directory.  It never edits a
suite, rebuilds the compiler, commits, or merges.

Usage:
    python3 tests/compiler/round4_integration/run.py
    python3 tests/compiler/round4_integration/run.py --quick
    ./tests/compiler/round4_integration/run.sh --quick
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import shlex
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[2]
BUILD = REPO / "build"
CPLUS = BUILD / "c+"
PY = sys.executable


# --------------------------------------------------------------------------
# Step definition
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Step:
    name: str
    argv: Optional[tuple[str, ...]]
    timeout: int
    quick: bool = False
    optional: bool = False
    summary_file: Optional[str] = None
    note: str = ""


def sh(path: str) -> tuple[str, ...]:
    """A POSIX shell script run through /bin/sh (or bash for agent1)."""
    return ("/bin/sh", str(REPO / path))


def py(path: str, *args: str) -> tuple[str, ...]:
    return (PY, str(REPO / path), *args)


STEPS: tuple[Step, ...] = (
    Step("repo_tests", sh("tests/run_tests.sh"), 600),
    Step("core", sh("tests/core/run.sh"), 900, quick=True),
    Step("agent1_full", ("bash", str(REPO / "tests/compiler/agent1/run_all.sh"),
                         "--full"), 1800, quick=True,
         summary_file="tests/compiler/agent1/results/summary.txt"),
    Step("ctest", ("ctest", "--test-dir", str(BUILD), "--output-on-failure"),
         900),
    Step("safety_matrix", py("tests/compiler/safety_matrix/run_safety_matrix.py"),
         900, quick=True,
         summary_file="tests/compiler/safety_matrix/summary.txt"),
    Step("full_coverage", py("tests/compiler/full_coverage/run_full_coverage.py"),
         1800, summary_file="tests/compiler/full_coverage/summary.txt"),
    Step("api_coverage", py("tests/compiler/api_coverage/run_suite.py"), 2400,
         note="2415 generated API scenarios",
         summary_file="tests/compiler/api_coverage/summary.txt"),
    Step("stdlib_api_coverage",
         py("tests/compiler/stdlib_api_coverage/run_suite.py"), 1200,
         summary_file="tests/compiler/stdlib_api_coverage/summary.txt"),
    Step("compile_time_oob", py("tests/compiler/compile_time_oob/run_suite.py"),
         300, summary_file="tests/compiler/compile_time_oob/summary.txt"),
    Step("exceptions_suite", py("tests/compiler/exceptions/run_suite.py"), 600,
         summary_file="tests/compiler/exceptions/summary.txt"),
    Step("exceptions_fuzz100", py("tests/compiler/exceptions/fuzz_100.py"), 900,
         summary_file="tests/compiler/exceptions/fuzz_100.txt"),
    Step("safety_breakers", py("tests/compiler/safety_breakers/run_suite.py"),
         600, quick=True,
         summary_file="tests/compiler/safety_breakers/summary.txt"),
    Step("real_apps", py("tests/compiler/real_apps/run_suite.py"), 1800,
         summary_file="tests/compiler/real_apps/summary.txt"),
    Step("abi_mixed", py("tests/compiler/abi_mixed/run_suite.py"), 600,
         summary_file="tests/compiler/abi_mixed/results/summary.txt"),
    Step("rust_proof", py("tests/compiler/rust_proof/run_proof.py"), 1200,
         summary_file="tests/compiler/rust_proof/summary.txt"),
    # Optional: the documented rust-compare runner (tests/core/rust_compare_suite.py)
    # is not present in every tree.  Resolve at run time; absence is SKIP, not FAIL.
    Step("rust_compare", None, 600, optional=True,
         note="optional C+/Rust paired compile-time comparison"),
    Step("benchmarks", sh("benchmarks/run_bench.sh"), 1200,
         note="C+ benchmark suite (needs clang)"),
)


RUST_COMPARE_CANDIDATES = (
    "tests/core/rust_compare_suite.py",
    "tests/compiler/rust_compare/run_suite.py",
    "tests/compiler/rust_compare/run.py",
)


def resolve_argv(step: Step) -> Optional[tuple[str, ...]]:
    if step.argv is not None:
        return step.argv
    if step.name == "rust_compare":
        for rel in RUST_COMPARE_CANDIDATES:
            path = REPO / rel
            if path.is_file():
                return (PY, str(path))
        return None
    return None


# --------------------------------------------------------------------------
# Output parsing
# --------------------------------------------------------------------------

SUMMARY_RE = re.compile(
    r"(C\+ tests:|core safety:|^TOTAL\s+\d+\s+PASS\b|tests passed|tests failed|"
    r"safety matrix:|discovered=|apis=|compile-time oob:|exceptions:|"
    r"fuzz100:|safety breakers:|real_apps:|mixed ABI:|rust[- ]compare|"
    r"COUNTS:|C\+ benchmarks:|\bpassed\s*=|\bfailed\s*=|failures:|"
    r"^counts:|^timeouts:|scenarios_(passed|failed|executed):|apis_discovered:)",
    re.IGNORECASE,
)

# (regex, {group index -> key}).  Applied to summary-like lines first.
COUNT_PATTERNS = (
    (r"SUMMARY scenarios=(\d+) passed=(\d+) failed=(\d+)",
     {1: "scenarios", 2: "passed", 3: "failed"}),
    (r"COUNTS:\s*scenarios=(\d+) passed=(\d+) failed=(\d+)",
     {1: "scenarios", 2: "passed", 3: "failed"}),
    (r"\bapis=(\d+) executed=(\d+) passed=(\d+) failed=(\d+) zero_api=(\d+)",
     {1: "apis", 2: "executed", 3: "passed", 4: "failed", 5: "zero_api"}),
    (r"\bdiscovered=(\d+) executed=(\d+) passed=(\d+) failed=(\d+) "
     r"observed=(\d+) unavailable=(\d+)",
     {1: "discovered", 2: "executed", 3: "passed", 4: "failed",
      5: "observed", 6: "unavailable"}),
    (r"C\+ tests:\s*(\d+) passed,\s*(\d+) failed",
     {1: "passed", 2: "failed"}),
    (r"core safety:\s*(\d+) passed,\s*(\d+) failed",
     {1: "passed", 2: "failed"}),
    (r"^TOTAL\s+(\d+)\s+PASS\s+(\d+)\s+FAIL\s+(\d+)\s+SKIP\s+(\d+)",
     {1: "total", 2: "passed", 3: "failed", 4: "skipped"}),
    # ctest prints "100% tests passed out of N" on this host; another common
    # form is "N% tests passed, M tests failed out of K".  Handled below
    # because the pass count is implicit in the percentage.
    (r"safety matrix:\s*(\d+) checks,\s*(\d+) PASS,.*?,\s*(\d+) FAIL\s*\((\d+) timeouts\)",
     {1: "checks", 2: "passed", 3: "failed", 4: "timeouts"}),
    (r"exceptions:\s*(\d+) cases,\s*(\d+) failures",
     {1: "cases", 2: "failed"}),
    (r"fuzz100:\s*(\d+) cases,\s*(\d+) failures",
     {1: "cases", 2: "failed"}),
    (r"safety breakers:\s*(\d+) checks,\s*(\d+) failures",
     {1: "checks", 2: "failed"}),
    (r"compile-time oob:\s*(\d+) cases,\s*(\d+) failures",
     {1: "cases", 2: "failed"}),
    (r"mixed ABI:\s*(\d+) steps,\s*(\d+) failures",
     {1: "total", 2: "failed"}),
    (r"real_apps:\s*(\d+) cases,\s*(\d+) configs: pass=(\d+).*?failed=(\d+) "
     r"mismatches=(\d+)",
     {1: "cases", 2: "configs", 3: "passed", 4: "failed", 5: "mismatches"}),
    (r"counts:\s*cases=(\d+)\s+configs=(\d+)\s+pass=(\d+).*?failed=(\d+)\s+"
     r"mismatch_entries=(\d+)",
     {1: "cases", 2: "configs", 3: "passed", 4: "failed", 5: "mismatches"}),
    (r"scenarios_discovered:\s*(\d+)", {1: "scenarios"}),
    (r"scenarios_executed:\s*(\d+)", {1: "executed"}),
    (r"scenarios_passed:\s*(\d+)", {1: "passed"}),
    (r"scenarios_failed:\s*(\d+)", {1: "failed"}),
    (r"apis_discovered:\s*(\d+)", {1: "apis"}),
    (r"^timeouts:\s*(\d+)", {1: "timeouts"}),
    (r"rust[- ]compare suite:\s*(\d+)/(\d+) C\+ expectations met",
     {1: "passed", 2: "total"}),
    (r"failures:\s*(\d+)", {1: "failed"}),
    (r"\bfailures\s*=\s*(\d+)", {1: "failed"}),
    (r"\bmismatches\s*=\s*(\d+)", {1: "mismatches"}),
)


def display_path(path: Path) -> str:
    """Repo-relative when possible, absolute when --out lives outside the repo."""
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)


def final_summary_line(text: str) -> str:
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    for line in reversed(lines):
        if SUMMARY_RE.search(line):
            return line
    return lines[-1] if lines else ""


def parse_counts(text: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    summary_text = "\n".join(
        ln for ln in text.splitlines() if SUMMARY_RE.search(ln)
    ) or text
    for pattern, keys in COUNT_PATTERNS:
        for match in re.finditer(pattern, summary_text, re.IGNORECASE | re.MULTILINE):
            for idx, key in keys.items():
                if key not in counts:
                    counts[key] = int(match.group(idx))

    ctest = re.search(r"(\d+)% tests passed(?:[,;]\s*(\d+) tests failed)?\s*out of (\d+)",
                      summary_text, re.IGNORECASE)
    if ctest:
        pct, failed_reported, total = (int(ctest.group(1)),
                                       ctest.group(2), int(ctest.group(3)))
        counts.setdefault("total", total)
        if failed_reported is not None:
            counts.setdefault("failed", int(failed_reported))
            counts.setdefault("passed", total - int(failed_reported))
        elif pct == 100:
            counts.setdefault("passed", total)
            counts.setdefault("failed", 0)
    return counts


# --------------------------------------------------------------------------
# Execution
# --------------------------------------------------------------------------

def run_step(step: Step, argv: tuple[str, ...], timeout: int,
             out_dir: Path, quick: bool, scale: float,
             overrides: dict[str, int], stop_on_fail: bool) -> dict:
    if timeout <= 0:
        timeout = 1
    timeout = max(1, math.ceil(timeout * scale))
    if step.name in overrides:
        timeout = overrides[step.name]

    stdout_path = out_dir / "logs" / f"{step.name}.stdout.log"
    stderr_path = out_dir / "logs" / f"{step.name}.stderr.log"
    env = os.environ.copy()
    env["CPLUS_BIN"] = str(CPLUS)
    env["PYTHONUNBUFFERED"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    # Some steps (notably benchmarks) invoke the default clang, whose Homebrew
    # SDKROOT on this machine points at a missing macOS 27 sysroot. Use the
    # system clang and a clean PATH so every step compiles against a real SDK.
    env["CPLUS_CLANG"] = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")
    env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin:" + env.get("PATH", "")
    env.pop("SDKROOT", None)

    cmd_str = shlex.join(argv)
    started = time.time()
    started_iso = datetime.now(timezone.utc).isoformat()
    rc: int
    timed_out = False
    spawn_error = ""

    try:
        with stdout_path.open("wb") as so, stderr_path.open("wb") as se:
            proc = subprocess.Popen(
                argv, cwd=str(REPO), env=env, stdout=so, stderr=se,
                start_new_session=True,
            )
            try:
                rc = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
                rc = proc.wait()
    except OSError as exc:  # missing interpreter / shell / ctest
        rc = 127
        spawn_error = f"{type(exc).__name__}: {exc}"
        stderr_path.write_text(spawn_error + "\n", encoding="utf-8")

    duration = time.time() - started

    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined = stdout_text + "\n" + stderr_text

    # Prefer the suite's own summary file, but only when freshly written.
    fresh_summary = ""
    if step.summary_file:
        spath = REPO / step.summary_file
        if spath.is_file() and spath.stat().st_mtime >= started - 2:
            fresh_summary = spath.read_text(encoding="utf-8", errors="replace")

    parse_text = (fresh_summary + "\n" if fresh_summary else "") + combined
    counts = parse_counts(parse_text)
    summary_line = final_summary_line(combined) or final_summary_line(fresh_summary)

    if timed_out:
        status = "timeout"
    elif spawn_error or rc != 0:
        status = "fail"
    else:
        status = "pass"

    record = {
        "name": step.name,
        "command": list(argv),
        "command_line": cmd_str,
        "cwd": str(REPO),
        "compiler": str(CPLUS),
        "mode": "quick" if quick else "full",
        "timeout_sec": timeout,
        "duration_sec": round(duration, 2),
        "started_utc": started_iso,
        "rc": rc if not timed_out else 124,
        "raw_rc": rc,
        "timed_out": timed_out,
        "spawn_error": spawn_error,
        "status": status,
        "final_summary_line": summary_line,
        "counts": counts,
        "logs": {
            "stdout": display_path(stdout_path),
            "stderr": display_path(stderr_path),
        },
        "note": step.note,
    }
    _print_step(record)
    if status != "pass" and stop_on_fail:
        record["stopped_run"] = True
    return record


def _print_step(rec: dict) -> None:
    label = rec["status"].upper()
    print(f"[{label:7s}] {rec['name']:20s} rc={rec['rc']:<4} "
          f"{rec['duration_sec']:>7.2f}s / {rec['timeout_sec']}s  "
          f"{rec['final_summary_line'][:110]}", flush=True)


def make_skip(step: Step, reason: str, quick: bool) -> dict:
    return {
        "name": step.name,
        "command": [],
        "command_line": "",
        "cwd": str(REPO),
        "compiler": str(CPLUS),
        "mode": "quick" if quick else "full",
        "timeout_sec": 0,
        "duration_sec": 0.0,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "rc": 0,
        "raw_rc": 0,
        "timed_out": False,
        "spawn_error": "",
        "status": "skip",
        "final_summary_line": f"SKIPPED: {reason}",
        "counts": {},
        "logs": {},
        "note": step.note,
    }


# --------------------------------------------------------------------------
# Reports
# --------------------------------------------------------------------------

def write_reports(records: list[dict], out_dir: Path, quick: bool) -> int:
    required = [r for r in records if r["status"] != "skip"]
    failed = [r for r in required if r["status"] != "pass"]
    passed = [r for r in required if r["status"] == "pass"]
    skipped = [r for r in records if r["status"] == "skip"]
    overall_rc = 0 if not failed else 1

    aggregate: dict[str, int] = {}
    for rec in required:
        for key, val in rec["counts"].items():
            if key in ("passed", "failed", "skipped"):
                aggregate[key] = aggregate.get(key, 0) + val

    doc = {
        "schema": 1,
        "suite": "round4_blue_integration_verifier",
        "mode": "quick" if quick else "full",
        "started_utc": records[0]["started_utc"] if records else "",
        "finished_utc": datetime.now(timezone.utc).isoformat(),
        "repo": str(REPO),
        "compiler": str(CPLUS),
        "compiler_exists": CPLUS.is_file(),
        "steps": records,
        "totals": {
            "steps_required": len(required),
            "steps_passed": len(passed),
            "steps_failed": len(failed),
            "steps_skipped": len(skipped),
            "aggregate_counts": aggregate,
        },
        "overall_rc": overall_rc,
    }
    (out_dir / "summary.json").write_text(
        json.dumps(doc, indent=2) + "\n", encoding="utf-8")

    lines = [
        "ROUND-4 BLUE INTEGRATION VERIFIER",
        f"mode     : {doc['mode']}",
        f"repo     : {REPO}",
        f"compiler : {CPLUS} (exists={doc['compiler_exists']})",
        f"finished : {doc['finished_utc']}",
        "",
        f"{'STATUS':<9}{'STEP':<21}{'RC':>5}{'TIME':>10}  SUMMARY / COUNTS",
        "-" * 110,
    ]
    for rec in records:
        counts = rec["counts"]
        cstr = " ".join(f"{k}={v}" for k, v in sorted(counts.items()))
        line = (f"{rec['status'].upper():<9}{rec['name']:<21}{rec['rc']:>5}"
                f"{rec['duration_sec']:>9.2f}s  {rec['final_summary_line'][:60]}")
        if cstr:
            line += f"\n{'':<47}{cstr}"
        lines.append(line)
        lines.append(f"{'':<47}$ {rec['command_line']}")
    lines += [
        "-" * 110,
        f"steps: {len(required)} required, {len(passed)} passed, "
        f"{len(failed)} failed, {len(skipped)} skipped",
        f"aggregate counts: {json.dumps(aggregate, sort_keys=True)}",
        f"OVERALL: {'PASS' if overall_rc == 0 else 'FAIL'} (rc={overall_rc})",
        "",
    ]
    (out_dir / "summary.txt").write_text("\n".join(lines), encoding="utf-8")
    return overall_rc


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true",
                    help="run only core, agent1 (--full), safety_matrix, "
                         "safety_breakers")
    ap.add_argument("--out", type=Path, default=SCRIPT_DIR,
                    help=f"report directory (default: {SCRIPT_DIR})")
    ap.add_argument("--timeout-scale", type=float, default=1.0,
                    help="multiply every per-step timeout (e.g. 2 for slow hosts)")
    ap.add_argument("--step-timeout", action="append", default=[],
                    metavar="NAME=SEC", help="override one step's hard timeout")
    ap.add_argument("--stop-on-fail", action="store_true",
                    help="stop at the first failed step (default: collect all evidence)")
    ap.add_argument("--only", default="", metavar="NAME[,NAME...]",
                    help="run only the named step(s), in the canonical order")
    ap.add_argument("--list", action="store_true",
                    help="list steps in execution order and exit")
    return ap


def main() -> int:
    args = build_parser().parse_args()
    selected = [s for s in STEPS if (not args.quick or s.quick)]
    if args.only:
        only = {n.strip() for n in args.only.split(",") if n.strip()}
        unknown = only - {s.name for s in STEPS}
        if unknown:
            print(f"error: unknown step(s): {', '.join(sorted(unknown))}",
                  file=sys.stderr)
            return 2
        selected = [s for s in STEPS if s.name in only]

    if args.list:
        for i, step in enumerate(selected, 1):
            timeout = max(1, math.ceil(step.timeout * args.timeout_scale))
            print(f"{i:2d}. {step.name:20s} {timeout:5d}s  "
                  f"{'optional' if step.optional else 'required'}  {step.note}")
        return 0

    overrides: dict[str, int] = {}
    for spec in args.step_timeout:
        if "=" not in spec:
            print(f"error: --step-timeout expects NAME=SEC, got {spec!r}",
                  file=sys.stderr)
            return 2
        name, _, value = spec.partition("=")
        try:
            overrides[name.strip()] = int(value)
        except ValueError:
            print(f"error: bad timeout value in {spec!r}", file=sys.stderr)
            return 2

    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "logs").mkdir(parents=True, exist_ok=True)

    if not CPLUS.is_file():
        print(f"warning: compiler not found at {CPLUS}; parent should rebuild "
              f"first. Steps will record their own failures.", file=sys.stderr)

    print(f"round4 integration: {len(selected)} steps "
          f"({'quick' if args.quick else 'full'} mode), "
          f"compiler={CPLUS}")
    records: list[dict] = []
    for step in selected:
        argv = resolve_argv(step)
        if argv is None:
            reason = "runner not available in this tree"
            rec = make_skip(step, reason, args.quick)
            records.append(rec)
            _print_step(rec)
        else:
            rec = run_step(step, argv, step.timeout, out_dir, args.quick,
                           args.timeout_scale, overrides, args.stop_on_fail)
            records.append(rec)
        if args.stop_on_fail and rec["status"] != "pass":
            print("stopping after first failure (--stop-on-fail)", file=sys.stderr)
            break

    overall = write_reports(records, out_dir, args.quick)
    print(f"reports: {out_dir / 'summary.txt'}, {out_dir / 'summary.json'}")
    print(f"OVERALL: {'PASS' if overall == 0 else 'FAIL'} "
          f"(rc={overall})")
    return overall


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        sys.exit(130)
