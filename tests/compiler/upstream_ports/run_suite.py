#!/usr/bin/env python3
"""Regression suite for upstream-shaped C+ safety cases.

The cases in ``cases/`` are independently authored C programs that mirror the
*shapes* of tests from four upstream projects (Checked C dynamic bounds, Apple
bounds-safety traps, Clang SA malloc/div-zero, SoftBoundCETS spatial/temporal).
No upstream source text is copied; provenance (repo, path, SHA, licence) is
recorded per case in ``manifest.json``.

For every case and both safety modes (DEFAULT and STRICT) the runner:

  1. verifies the case source hash from the manifest;
  2. runs ``c+ check --<mode> --sanitize=none``;
  3. runs ``c+ build --<mode> --sanitize=none ... -o <bin>``;
  4. runs the produced binary when the build was accepted.

Expectations are exact: compiler return codes, exact normalised diagnostic
substrings (line/column and message), forbidden markers, binary presence, and
exact run stdout, stderr and return code.  Absolute source/binary paths are
replaced by ``<SRC>`` / ``<BIN>`` before matching.  Nonzero exits are never
accepted on their own, and every subprocess has a hard timeout.

This suite owns only new files under this directory.  It does not build the
compiler, modify existing sources, or commit anything.

Run from the repository root:

  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/upstream_ports/run_suite.py

Results are written to ``results/`` (summary.txt, summary.json, logs/).
Exit status is 0 only when every expectation matches.
"""

from __future__ import annotations

import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"
BINS = RESULTS / "bin"

BIN = os.environ.get("CPLUS_BIN", str(pathlib.Path.cwd() / "build" / "c+"))
TIMEOUT = int(os.environ.get("UP_TIMEOUT", "90"))
WORKERS = int(os.environ.get("UP_WORKERS", "4"))

MODES = {
    "default": "--default",
    "strict": "--strict",
}


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalise(text: str, src: pathlib.Path, binary: pathlib.Path) -> str:
    return text.replace(str(src), "<SRC>").replace(str(binary), "<BIN>")


def invoke(cmd: list[str], src: pathlib.Path, binary: pathlib.Path) -> dict:
    """Run one subprocess; classify rc, timeout and crash without '|| true'."""
    record: dict = {"cmd": cmd, "rc": None, "stdout": "", "stderr": "",
                    "timed_out": False, "crashed": False}
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired as exc:
        record["timed_out"] = True
        record["stdout"] = normalise(exc.stdout or "", src, binary)
        record["stderr"] = normalise(exc.stderr or "", src, binary)
        return record
    record["rc"] = proc.returncode
    record["stdout"] = normalise(proc.stdout, src, binary)
    record["stderr"] = normalise(proc.stderr, src, binary)
    return record


def check_step(name: str, exp: dict, got: dict, failures: list[str]) -> bool:
    """Validate one check/build step. Returns True on match."""
    ok = True
    if got["timed_out"]:
        failures.append(f"{name}: subprocess timed out after {TIMEOUT}s")
        return False
    if got["crashed"]:
        failures.append(f"{name}: compiler crashed with rc={got['rc']}")
        return False
    if got["rc"] != exp["rc"]:
        failures.append(f"{name}: rc expected {exp['rc']}, got {got['rc']}")
        ok = False
    output = got["stdout"] + got["stderr"]
    for needle in exp.get("contains", []):
        if needle not in output:
            failures.append(f"{name}: missing exact diagnostic {needle!r}")
            ok = False
    for needle in exp.get("forbidden", []):
        if needle in output:
            failures.append(f"{name}: unexpected marker {needle!r}")
            ok = False
    return ok


def check_run(name: str, exp: dict, got: dict, failures: list[str]) -> bool:
    ok = True
    if got["timed_out"]:
        failures.append(f"{name}: binary timed out after {TIMEOUT}s")
        return False
    if got["rc"] != exp["rc"]:
        failures.append(f"{name}: rc expected {exp['rc']}, got {got['rc']}")
        ok = False
    if got["stdout"] != exp["stdout"]:
        failures.append(f"{name}: stdout expected {exp['stdout']!r}, got {got['stdout']!r}")
        ok = False
    if got["stderr"] != exp["stderr"]:
        failures.append(f"{name}: stderr expected {exp['stderr']!r}, got {got['stderr']!r}")
        ok = False
    return ok


def run_case(case: dict) -> dict:
    name = case["name"]
    src = (HERE / case["source"]).resolve()
    result: dict = {
        "name": name, "family": case["family"], "kind": case["kind"],
        "origin": case["origin"], "source": case["source"],
        "sha256_expected": case["sha256"], "modes": {}, "failures": [],
    }
    if not src.is_file():
        result["failures"].append(f"source file missing: {src}")
        return result
    actual_hash = sha256(src)
    result["sha256_actual"] = actual_hash
    if actual_hash != case["sha256"]:
        result["failures"].append(
            f"source hash drift: expected {case['sha256']}, got {actual_hash}")
        return result

    for mode, flag in MODES.items():
        exp = case["expect"][mode]
        binary = (BINS / f"{name}.{mode}").resolve()
        binary.unlink(missing_ok=True)
        mode_result: dict = {"steps": {}, "failures": []}
        f = mode_result["failures"]

        c = invoke([BIN, "check", flag, "--sanitize=none", str(src)], src, binary)
        if c["rc"] is not None and c["rc"] < 0:
            c["crashed"] = True
        mode_result["steps"]["check"] = c
        check_step(f"{name}/{mode}/check", exp["check"], c, f)

        b = invoke([BIN, "build", flag, "--sanitize=none", str(src), "-o", str(binary)], src, binary)
        if b["rc"] is not None and b["rc"] < 0:
            b["crashed"] = True
        mode_result["steps"]["build"] = b
        check_step(f"{name}/{mode}/build", exp["build"], b, f)

        produced = binary.is_file()
        mode_result["binary_produced"] = produced
        if produced != exp["binary"]:
            f.append(f"{name}/{mode}: binary presence expected {exp['binary']}, got {produced}")

        if exp["run"] is not None:
            if produced:
                r = invoke([str(binary)], src, binary)
                mode_result["steps"]["run"] = r
                check_run(f"{name}/{mode}/run", exp["run"], r, f)
            else:
                f.append(f"{name}/{mode}: run expected but no binary was produced")

        for step, rec in mode_result["steps"].items():
            log = LOGS / f"{name}.{mode}.{step}.txt"
            lines = [
                f"case: {name}", f"mode: {mode}", f"step: {step}",
                "command: " + " ".join(rec["cmd"]),
                f"rc: {rec['rc']}", f"timed_out: {rec['timed_out']}",
                "--- stdout ---", rec["stdout"], "--- stderr ---", rec["stderr"],
            ]
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text("\n".join(lines) + "\n")

        result["modes"][mode] = mode_result
        result["failures"].extend(f)
    return result


def main() -> int:
    if not pathlib.Path(BIN).is_file():
        print(f"CPLUS_BIN not found or not a file: {BIN}", file=sys.stderr)
        return 2
    manifest = json.loads(MANIFEST.read_text())
    if manifest.get("schema") != 1:
        print("unsupported manifest schema", file=sys.stderr)
        return 2
    cases = manifest["cases"]
    RESULTS.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    BINS.mkdir(parents=True, exist_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        records = list(pool.map(run_case, cases))
    records.sort(key=lambda r: r["name"])

    mode_checks = sum(len(r["modes"]) for r in records)
    per_family: dict[str, int] = {}
    per_kind: dict[str, int] = {}
    failed_cases = 0
    for rec in records:
        per_family[rec["family"]] = per_family.get(rec["family"], 0) + 1
        per_kind[rec["kind"]] = per_kind.get(rec["kind"], 0) + 1
        if rec["failures"]:
            failed_cases += 1
    failure_entries = sum(len(r["failures"]) for r in records)

    summary = {
        "suite": manifest["suite"],
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "compiler": BIN,
        "sanitize": "none",
        "modes": list(MODES),
        "counts": {
            "cases": len(records),
            "cases_passed": len(records) - failed_cases,
            "cases_failed": failed_cases,
            "mode_checks": mode_checks,
            "failure_entries": failure_entries,
            "per_family": per_family,
            "per_kind": per_kind,
        },
        "upstreams": manifest["upstreams"],
        "results": records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    lines = [
        f"suite: {manifest['suite']}",
        f"compiler: {BIN}",
        "flags: --default|--strict --sanitize=none",
        f"cases: {len(records)}  passed: {len(records) - failed_cases}  failed: {failed_cases}",
        f"mode checks: {mode_checks}  failure entries: {failure_entries}",
        "per family: " + ", ".join(f"{k}={v}" for k, v in sorted(per_family.items())),
        "per kind: " + ", ".join(f"{k}={v}" for k, v in sorted(per_kind.items())),
    ]
    if failure_entries:
        lines.append("")
        lines.append("FAILURES:")
        for rec in records:
            for msg in rec["failures"]:
                lines.append(f"  {msg}")
    else:
        lines.append("")
        lines.append("ALL EXPECTATIONS MATCHED")
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 1 if failed_cases else 0


if __name__ == "__main__":
    raise SystemExit(main())
