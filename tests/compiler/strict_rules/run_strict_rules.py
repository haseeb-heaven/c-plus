#!/usr/bin/env python3
"""Permanent strict-safe subset conformance suite (spec v0.1).

Runs one POSITIVE and one NEGATIVE fixture for every rule in rules.json,
asserts an exact exit-status contract and exact diagnostic substrings, and
derives docs/compiler/strict_rules_matrix.json from the measured outcomes.

No skips. No `|| true`. Every process has a hard timeout; a timeout is a FAIL.

Run:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/strict_rules/run_strict_rules.py

Optional: CPLUS_STRICT_TIMEOUT (default 45 seconds) raises the hard timeout.
"""
import datetime
import json
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
TIMEOUT = int(os.environ.get("CPLUS_STRICT_TIMEOUT", "45"))
RULES_FILE = os.path.join(HERE, "rules.json")
RESULTS_DIR = os.path.join(HERE, "results")
MATRIX_FILE = os.path.join(ROOT, "docs", "compiler", "strict_rules_matrix.json")

PASS = "PASS"
FAIL = "FAIL"
VERIFIED = "VERIFIED"
UNVERIFIED = "UNVERIFIED"


def run(cmd, timeout=TIMEOUT):
    """Run one subprocess with a hard timeout; never swallow a failure."""
    start = time.time()
    rec = {"command": cmd, "exit_code": None, "stdout": "", "stderr": "",
           "timed_out": False, "duration_s": 0.0}
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, cwd=ROOT)
        rec["exit_code"] = p.returncode
        rec["stdout"] = p.stdout.decode("utf-8", errors="replace")
        rec["stderr"] = p.stderr.decode("utf-8", errors="replace")
    except subprocess.TimeoutExpired as e:
        rec["timed_out"] = True
        rec["exit_code"] = "TIMEOUT"
        rec["stdout"] = (e.stdout or b"").decode("utf-8", errors="replace")
        rec["stderr"] = ((e.stderr or b"") + (
            ("\nhard timeout (%ds) exceeded" % timeout).encode())).decode(
            "utf-8", errors="replace")
    rec["duration_s"] = round(time.time() - start, 3)
    return rec


def execute_fixture(spec, tmpdir):
    """Return a single execution record for one fixture expectation."""
    src = os.path.join(ROOT, spec["fixture"])
    base = {"fixture": spec["fixture"], "mode": spec["mode"],
            "expect": spec["expect"], "build": None, "run": None,
            "combined_output": "", "missing_substrings": [],
            "forbidden_substrings": [], "timed_out": False}
    if not os.path.isfile(src):
        base["status"] = FAIL
        base["fail_reason"] = "fixture file is missing"
        return base
    if spec["mode"] == "check":
        cmd = [BIN, "check", "--strict", "--sanitize=none", src]
        result = run(cmd)
        base["run"] = result
        base["combined_output"] = result["stdout"] + result["stderr"]
        base["timed_out"] = result["timed_out"]
    elif spec["mode"] == "build_run":
        binary = os.path.join(tmpdir, os.path.basename(src).replace(".c", ".bin"))
        build_cmd = [BIN, "build", "--strict", "--sanitize=none", src, "-o", binary]
        build = run(build_cmd)
        base["build"] = build
        base["timed_out"] = build["timed_out"]
        if build["timed_out"] or build["exit_code"] != 0:
            base["status"] = FAIL
            base["fail_reason"] = "build did not succeed under STRICT"
            base["combined_output"] = build["stdout"] + build["stderr"]
            return base
        result = run([binary], timeout=TIMEOUT)
        base["run"] = result
        base["timed_out"] = base["timed_out"] or result["timed_out"]
        base["combined_output"] = (build["stdout"] + build["stderr"] +
                                   result["stdout"] + result["stderr"])
    else:
        base["status"] = FAIL
        base["fail_reason"] = "unknown fixture mode: %s" % spec["mode"]
        return base

    out = base["combined_output"]
    if base["timed_out"]:
        base["status"] = FAIL
        base["fail_reason"] = "hard timeout exceeded"
        return base

    if spec["expect"] == "accept":
        rc = base["run"]["exit_code"]
        bad = [s for s in spec.get("forbid", []) if s in out]
        base["forbidden_substrings"] = bad
        if rc == 0 and not bad:
            base["status"] = PASS
            base["fail_reason"] = ""
        else:
            base["status"] = FAIL
            base["fail_reason"] = "expected accept rc=0, got rc=%s; forbidden=%s" % (
                rc, bad)
    elif spec["expect"] == "reject":
        rc = base["run"]["exit_code"]
        missing = [s for s in spec.get("require", []) if s not in out]
        base["missing_substrings"] = missing
        if rc != 0 and not missing:
            base["status"] = PASS
            base["fail_reason"] = ""
        else:
            base["status"] = FAIL
            base["fail_reason"] = "expected reject with %s, got rc=%s; missing=%s" % (
                spec.get("require", []), rc, missing)
    else:
        base["status"] = FAIL
        base["fail_reason"] = "unknown expectation: %s" % spec["expect"]
    return base


def main():
    with open(RULES_FILE, encoding="utf-8") as fh:
        rules_doc = json.load(fh)
    os.makedirs(RESULTS_DIR, exist_ok=True)

    records = []
    matrix_rules = {}
    false_positives = []
    false_negatives = []

    with tempfile.TemporaryDirectory(prefix="cplus_strict_rules_") as tmpdir:
        for rule in rules_doc["rules"]:
            rid = rule["id"]
            pos = execute_fixture(rule["positive"], tmpdir)
            neg = execute_fixture(rule["negative"], tmpdir)
            records.append({"rule_id": rid, "category": rule["category"],
                            "role": "positive", **pos})
            records.append({"rule_id": rid, "category": rule["category"],
                            "role": "negative", **neg})

            if pos["status"] != PASS:
                false_positives.append({
                    "rule_id": rid,
                    "fixture": rule["positive"]["fixture"],
                    "reason": pos["fail_reason"],
                    "observed_exit": pos["run"]["exit_code"] if pos["run"] else None,
                })
            if neg["status"] != PASS:
                false_negatives.append({
                    "rule_id": rid,
                    "fixture": rule["negative"]["fixture"],
                    "reason": neg["fail_reason"],
                    "observed_exit": neg["run"]["exit_code"] if neg["run"] else None,
                })
            status = VERIFIED if (pos["status"] == PASS and neg["status"] == PASS) else UNVERIFIED
            matrix_rules[rid] = {
                "category": rule["category"],
                "proof_rule": rule["proof_rule"],
                "runtime_check": rule["runtime_check"],
                "trusted_boundary": rule["trusted_boundary"],
                "positive_fixture": rule["positive"]["fixture"],
                "negative_fixture": rule["negative"]["fixture"],
                "status": status,
            }

    total_rules = len(rules_doc["rules"])
    verified = sum(1 for r in matrix_rules.values() if r["status"] == VERIFIED)
    passed = sum(1 for r in records if r["status"] == PASS)
    failed = len(records) - passed
    timeouts = sum(1 for r in records if r["timed_out"])
    totals = {
        "rules_total": total_rules,
        "rules_verified": verified,
        "rules_unverified": total_rules - verified,
        "fixtures_total": len(records),
        "fixtures_passed": passed,
        "fixtures_failed": failed,
        "timeouts": timeouts,
        "false_positives": len(false_positives),
        "false_negatives": len(false_negatives),
    }

    summary = {
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "spec_version": rules_doc["spec_version"],
        "compiler_binary": BIN,
        "mode": rules_doc["mode"],
        "hard_timeout_s": TIMEOUT,
        "totals": totals,
        "unverified_rules": [rid for rid, v in matrix_rules.items()
                             if v["status"] == UNVERIFIED],
        "false_positives": false_positives,
        "false_negatives": false_negatives,
        "records": records,
    }
    with open(os.path.join(RESULTS_DIR, "summary.json"), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2)
        fh.write("\n")

    lines = [
        "strict-safe subset conformance (spec %s)" % rules_doc["spec_version"],
        "compiler: %s" % BIN,
        "mode: %s ; hard timeout: %ds" % (rules_doc["mode"], TIMEOUT),
        "",
        "rules: %d total, %d VERIFIED, %d UNVERIFIED" % (
            total_rules, verified, total_rules - verified),
        "fixtures: %d total, %d PASS, %d FAIL, %d timeouts" % (
            len(records), passed, failed, timeouts),
        "false positives: %d ; false negatives: %d" % (
            len(false_positives), len(false_negatives)),
        "",
    ]
    for rec in records:
        lines.append("%-24s %-8s %-8s rc=%-7s %s" % (
            rec["rule_id"], rec["role"], rec["status"],
            str(rec["run"]["exit_code"] if rec["run"] else "N/A"),
            rec["fail_reason"]))
    lines.append("")
    lines.append("unverified rules: %s" % (summary["unverified_rules"] or "none"))
    with open(os.path.join(RESULTS_DIR, "summary.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    matrix = {
        "spec_version": rules_doc["spec_version"],
        "generated_by": "tests/compiler/strict_rules/run_strict_rules.py",
        "generated_utc": summary["generated_utc"],
        "compiler_binary": BIN,
        "mode": rules_doc["mode"],
        "status_definition": ("VERIFIED only when both fixtures were actually executed "
                              "within the hard timeout and their asserted exit status and "
                              "diagnostic substrings held; otherwise UNVERIFIED."),
        "rules": matrix_rules,
        "totals": totals,
        "false_positives": false_positives,
        "false_negatives": false_negatives,
    }
    with open(MATRIX_FILE, "w", encoding="utf-8") as fh:
        json.dump(matrix, fh, indent=2)
        fh.write("\n")

    print("\n".join(lines[:8]))
    print("matrix written to %s" % MATRIX_FILE)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
