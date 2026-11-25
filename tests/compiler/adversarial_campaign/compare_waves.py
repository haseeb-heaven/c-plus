#!/usr/bin/env python3
"""Compare two verify_bypasses.py snapshots and report the closed bypasses.

Usage: python3 compare_waves.py BEFORE.json AFTER.json
Exit status is always 0; this is a measurement/reporting tool.
"""
import json, sys


def load(path):
    d = json.load(open(path))
    return {r["case"]: r for r in d["results"]}, d


def main():
    if len(sys.argv) != 3:
        print("usage: compare_waves.py BEFORE.json AFTER.json")
        return 0
    before, bm = load(sys.argv[1])
    after, am = load(sys.argv[2])
    closed, still, introduced = [], [], []
    for name in before:
        b = before[name]["accepted_by_strict"]
        a = after.get(name, {}).get("accepted_by_strict")
        if a is None:
            continue
        if b and not a:
            closed.append(name)
        elif b and a:
            still.append(name)
        elif not b and a:
            introduced.append(name)
    print("baseline binary : %s" % bm["binary"])
    print("current binary  : %s" % am["binary"])
    print("baseline accepted: %d/%d" % (sum(r["accepted_by_strict"] for r in bm["results"]), len(bm["results"])))
    print("current  accepted: %d/%d" % (sum(r["accepted_by_strict"] for r in am["results"]), len(am["results"])))
    print("\nCLOSED by fixes (%d):" % len(closed))
    for n in closed:
        print("  + %s  (now: %s)" % (n, after[n]["stderr"][:100].replace("\n", " ")))
    print("\nSTILL ACCEPTED (%d):" % len(still))
    for n in still:
        print("  - %s" % n)
    if introduced:
        print("\nNEWLY ACCEPTED — REGRESSION (%d):" % len(introduced))
        for n in introduced:
            print("  ! %s" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
