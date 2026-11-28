#!/usr/bin/env python3
"""Adversarial addition to the strict-safe subset conformance suite (spec v0.1).

Table-driven from adversarial.json: for a representative five-rule subset,
one MUTATION fixture per rule must be rejected with an exact diagnostic
substring, and one NEAR-MISS fixture per rule must be accepted with no
forbidden substring. Execution and assertion conventions are reused verbatim
from run_strict_rules.py (hard timeout, no skips, no `|| true`).

The 20-rule base suite (rules.json, run_strict_rules.py, its matrix and its
summary) is deliberately untouched: this runner writes only
results/summary_adversarial.json and results/summary_adversarial.txt.

Run:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/strict_rules/run_adversarial.py
"""
import datetime
import json
import os
import sys
import tempfile

sys.dont_write_bytecode = True  # importing the base runner must leave no artifacts
from run_strict_rules import (
    BIN, PASS, RESULTS_DIR, TIMEOUT, execute_fixture,
)

HERE = os.path.dirname(os.path.abspath(__file__))
CASES_FILE = os.path.join(HERE, "adversarial.json")
SUMMARY_JSON = os.path.join(RESULTS_DIR, "summary_adversarial.json")
SUMMARY_TXT = os.path.join(RESULTS_DIR, "summary_adversarial.txt")
RUN_COMMAND = "CPLUS_BIN=%s python3 tests/compiler/strict_rules/run_adversarial.py" % BIN


def main():
    with open(CASES_FILE, encoding="utf-8") as fh:
        doc = json.load(fh)
    os.makedirs(RESULTS_DIR, exist_ok=True)

    records = []
    false_positives = []   # safe near-miss was rejected
    false_negatives = []   # unsafe mutation was accepted
    with tempfile.TemporaryDirectory(prefix="cplus_adv_rules_") as tmpdir:
        for case in doc["cases"]:
            rec = dict(execute_fixture(case, tmpdir))
            rec["rule_id"] = case["rule_id"]
            rec["role"] = case["role"]
            rec["bypass"] = case.get("bypass", "")
            rec["require"] = case.get("require", [])
            rec["forbid"] = case.get("forbid", [])
            if rec["status"] != PASS:
                entry = {
                    "rule_id": case["rule_id"],
                    "role": case["role"],
                    "fixture": case["fixture"],
                    "reason": rec["fail_reason"],
                    "observed_exit": (rec["run"]["exit_code"]
                                      if rec["run"] else None),
                }
                if case["role"] == "mutation":
                    false_negatives.append(entry)
                else:
                    false_positives.append(entry)
            records.append(rec)

    cases_total = len(records)
    passed = sum(1 for r in records if r["status"] == PASS)
    failed = cases_total - passed
    timeouts = sum(1 for r in records if r["timed_out"])
    rules_covered = sorted(set(c["rule_id"] for c in doc["cases"]))
    mutations = sum(1 for c in doc["cases"] if c["role"] == "mutation")
    near_misses = cases_total - mutations
    totals = {
        "rules_covered": len(rules_covered),
        "cases_total": cases_total,
        "mutations_total": mutations,
        "near_misses_total": near_misses,
        "cases_passed": passed,
        "cases_failed": failed,
        "timeouts": timeouts,
        "false_positives": len(false_positives),
        "false_negatives": len(false_negatives),
    }

    summary = {
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "spec_version": doc["spec_version"],
        "kind": doc["kind"],
        "compiler_binary": BIN,
        "mode": "STRICT (--strict --sanitize=none)",
        "hard_timeout_s": TIMEOUT,
        "run_command": RUN_COMMAND,
        "rules_covered": rules_covered,
        "totals": totals,
        "false_positives": false_positives,
        "false_negatives": false_negatives,
        "records": records,
    }
    with open(SUMMARY_JSON, "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2)
        fh.write("\n")

    lines = [
        "adversarial addition to strict-safe subset conformance (spec %s)" % doc["spec_version"],
        "exact command: %s" % RUN_COMMAND,
        "compiler: %s" % BIN,
        "mode: STRICT (--strict --sanitize=none) ; hard timeout: %ds" % TIMEOUT,
        "",
        "rules covered: %d (%s)" % (len(rules_covered), ", ".join(rules_covered)),
        "cases: %d total, %d PASS, %d FAIL, %d timeouts" % (
            cases_total, passed, failed, timeouts),
        "mutations (must reject): %d ; near-misses (must accept): %d" % (
            mutations, near_misses),
        "false positives: %d ; false negatives: %d" % (
            len(false_positives), len(false_negatives)),
        "",
    ]
    for rec in records:
        lines.append("%-22s %-9s %-8s rc=%-5s %s" % (
            rec["rule_id"], rec["role"], rec["status"],
            str(rec["run"]["exit_code"] if rec["run"] else "N/A"),
            rec["fail_reason"]))
        cmd_line = (" ".join(rec["run"]["command"]) if rec["run"] else "N/A")
        lines.append("    command: %s" % cmd_line)
    lines.append("")
    lines.append("base suite is unchanged; its evidence is in results/summary.txt / summary.json")
    with open(SUMMARY_TXT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    print("\n".join(lines[:9]))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
