#!/usr/bin/env python3
"""Execute the generated 5k+ end-to-end core scenarios and write exact counts.

Usage:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/e2e_10k/core/run_e2e.py

Every c+ / clang invocation is a forked child process (subprocess) with a hard
timeout. Every runtime scenario additionally runs inside a forked child of the
generated harness with its own 2s timeout. Assertions are exact: stdout, stderr
and exit status. No case is skipped and no failure is tolerated in the counts.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "generated", "manifest.json")
SCRATCH = os.path.join(HERE, "scratch")
CLANG = "/usr/bin/clang"
COMPILE_TIMEOUT = 900
RUN_TIMEOUT = 900
REJECT_TIMEOUT = 120


def dump_log(name: str, proc: subprocess.CompletedProcess) -> str:
    path = os.path.join(SCRATCH, name)
    with open(path, "w") as f:
        f.write("COMMAND: " + " ".join(proc.args) + "\n")
        f.write("EXIT: %d\n--- STDOUT ---\n" % proc.returncode)
        f.write(proc.stdout)
        f.write("\n--- STDERR ---\n")
        f.write(proc.stderr)
    return os.path.relpath(path, HERE)


class Counters:
    def __init__(self, manifest):
        self.gen = sum(s["scenarios"] for s in manifest["shards"]) + len(manifest["reject"])
        self.executed = self.passed = self.failed = 0
        self.categories = {
            k: {"generated": v, "executed": 0, "passed": 0, "failed": 0}
            for k, v in manifest["categories"].items()
        }
        self.categories["compile_time_violation"] = {
            "generated": len(manifest["reject"]), "executed": 0, "passed": 0, "failed": 0}
        self.clang = {"compiled": 0, "compile_failed": 0, "executed": 0,
                      "passed": 0, "failed": 0}
        self.failures: list[dict] = []
        self.timings: list[dict] = []

    def record(self, name: str, category: str, ok: bool, path: str, detail: str):
        c = self.categories.setdefault(category, {"generated": 0, "executed": 0,
                                                   "passed": 0, "failed": 0})
        self.executed += 1
        c["executed"] += 1
        if ok:
            self.passed += 1
            c["passed"] += 1
        else:
            self.failed += 1
            c["failed"] += 1
            self.failures.append({"scenario": name, "category": category,
                                  "path": path, "detail": detail[:800]})


LINE_RE = re.compile(r"^(PASS|FAIL)\s+(\S+)\s+cat=(\d+)\s+mode=(\d+)\s+(.*)$")


def run_harness(binary: str, mode: int, expected: int, tag: str,
                meta: dict, counters: Counters, clang_check: bool = False):
    outpath = os.path.join(SCRATCH, "run_" + tag + ".log")
    t0 = time.time()
    proc = subprocess.run([binary, str(mode)], cwd=HERE, capture_output=True,
                          text=True, timeout=RUN_TIMEOUT)
    with open(outpath, "w") as f:
        f.write(proc.stdout)
        f.write("\n--- STDERR ---\n" + proc.stderr)
    lines = proc.stdout.splitlines()
    seen = {}
    summary = None
    for line in lines:
        m = LINE_RE.match(line)
        if m:
            status, name, cat, md, detail = m.groups()
            if name in seen:
                counters.record(name, "harness_protocol", False,
                                os.path.relpath(outpath, HERE), "duplicate scenario line")
            seen[name] = (status == "PASS", detail)
        elif line.startswith("SUMMARY "):
            summary = line
    if summary is None:
        counters.record(tag, "harness_protocol", False,
                        os.path.relpath(outpath, HERE), "missing SUMMARY line")
        summary = ""
    parsed = re.match(r"SUMMARY mode=\d+ total=(\d+) pass=(\d+) fail=(\d+)", summary)
    if not parsed or int(parsed.group(1)) != expected or len(seen) != expected:
        counters.record(tag, "harness_protocol", False,
                        os.path.relpath(outpath, HERE),
                        "SUMMARY=%r parsed_lines=%d expected=%d" % (summary, len(seen), expected))
    for sc in meta["scenarios"]:
        name = sc["name"]
        if name not in seen:
            counters.record(name, sc["category"], False, os.path.relpath(outpath, HERE),
                            "no harness line for scenario")
            continue
        ok, detail = seen[name]
        if clang_check:
            counters.clang["executed"] += 1
            if ok:
                counters.clang["passed"] += 1
            else:
                counters.clang["failed"] += 1
                counters.failures.append({"scenario": name,
                                          "category": sc["category"] + "/clang",
                                          "path": os.path.relpath(outpath, HERE),
                                          "detail": detail[:800]})
        else:
            counters.record(name, sc["category"], ok, os.path.relpath(outpath, HERE), detail)
    counters.timings.append({"tag": tag, "seconds": round(time.time() - t0, 3)})


def main() -> int:
    cplus = os.environ.get("CPLUS_BIN")
    if not cplus:
        print("ERROR: set CPLUS_BIN to the c+ binary", file=sys.stderr)
        return 2
    cplus = os.path.abspath(cplus)
    if not os.path.isfile(cplus):
        print("ERROR: CPLUS_BIN not found: " + cplus, file=sys.stderr)
        return 2
    os.makedirs(SCRATCH, exist_ok=True)
    manifest = json.load(open(MANIFEST))
    counters = Counters(manifest)
    print("c+ =", cplus)
    print("generated =", counters.gen, flush=True)

    for shard in manifest["shards"]:
        name = shard["name"]
        source = shard["path"]
        binary = os.path.join(SCRATCH, name + ".bin")
        t0 = time.time()
        # The generated corpus deliberately contains unsafe operations whose
        # compile-time diagnostics are part of the fixture; this suite checks
        # RUNTIME behaviour, so warnings must not abort compilation.
        proc = subprocess.run([cplus, "build", "--default", "--sanitize=none",
                               "--no-werror", source,
                               "-o", os.path.relpath(binary, HERE)],
                              cwd=HERE, capture_output=True, text=True,
                              timeout=COMPILE_TIMEOUT)
        logpath = dump_log("build_" + name + ".log", proc)
        counters.timings.append({"tag": "build_" + name,
                                 "seconds": round(time.time() - t0, 3)})
        if proc.returncode != 0:
            for sc in manifest_scenarios(manifest, name):
                counters.record(sc["name"], sc["category"], False, logpath,
                                "c+ compile failed: " + proc.stderr[:400])
            print("COMPILE FAIL", name, logpath, flush=True)
            continue
        print("built", name, "rc=0", flush=True)
        scenarios = manifest_scenarios(manifest, name)
        safe = [s for s in scenarios if s["flags"] & 1]
        traps = [s for s in scenarios if s["flags"] & 2]
        plain = [s for s in scenarios if s["flags"] & 4]
        meta = {"scenarios": scenarios}
        run_harness(binary, 0, len(safe), name + "_safe", {"scenarios": safe}, counters)
        run_harness(binary, 1, len(traps), name + "_trap", {"scenarios": traps}, counters)
        print("  run", name, "safe=%d trap=%d" % (len(safe), len(traps)), flush=True)
        if not shard["cplus"] and os.path.isfile(CLANG):
            cbinary = os.path.join(SCRATCH, name + "_clang.bin")
            cp = subprocess.run([CLANG, "-std=c11", "-O2", source,
                                 "-o", os.path.relpath(cbinary, HERE)],
                                cwd=HERE, capture_output=True, text=True,
                                timeout=COMPILE_TIMEOUT)
            clog = dump_log("clang_build_" + name + ".log", cp)
            counters.clang["compiled"] += 1
            if cp.returncode != 0:
                counters.clang["compile_failed"] += 1
                counters.failures.append({"scenario": name + "/clang-build",
                                          "category": "clang_crosscheck",
                                          "path": clog, "detail": cp.stderr[:800]})
                print("  clang COMPILE FAIL", name, flush=True)
                continue
            run_harness(cbinary, 2, len(plain), name + "_clang_plain",
                        {"scenarios": plain}, counters, clang_check=True)
            print("  clang", name, "plain=%d" % len(plain), flush=True)

    for rej in manifest["reject"]:
        t0 = time.time()
        proc = subprocess.run([cplus, "build", "--default", "--sanitize=none",
                               rej["path"], "-o", os.path.join(SCRATCH, rej["name"] + ".bin")],
                              cwd=HERE, capture_output=True, text=True,
                              timeout=REJECT_TIMEOUT)
        logpath = dump_log("reject_" + rej["name"] + ".log", proc)
        combined = proc.stdout + proc.stderr
        ok = proc.returncode == 1 and rej["diagnostic"] in combined
        counters.record(rej["name"], "compile_time_violation", ok, logpath,
                        "exit=%d diagnostic=%r found=%s" %
                        (proc.returncode, rej["diagnostic"], rej["diagnostic"] in combined))
        counters.timings.append({"tag": "reject_" + rej["name"],
                                 "seconds": round(time.time() - t0, 3)})

    total_seconds = round(sum(t["seconds"] for t in counters.timings), 3)
    summary = {
        "seed": manifest["seed"],
        "cplus_bin": cplus,
        "clang_bin": CLANG if os.path.isfile(CLANG) else None,
        "scenarios_generated": counters.gen,
        "scenarios_executed": counters.executed,
        "passed": counters.passed,
        "failed": counters.failed,
        "categories": counters.categories,
        "clang_crosscheck": counters.clang,
        "failure_paths": counters.failures,
        "timings": counters.timings,
        "total_seconds": total_seconds,
    }
    with open(os.path.join(HERE, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2, sort_keys=True)
        f.write("\n")
    with open(os.path.join(HERE, "summary.txt"), "w") as f:
        f.write("C+ core end-to-end scenario measurement\n")
        f.write("c+ binary: %s\n" % cplus)
        f.write("scenarios generated: %d\n" % counters.gen)
        f.write("scenarios executed:  %d\n" % counters.executed)
        f.write("passed: %d\n" % counters.passed)
        f.write("failed: %d\n" % counters.failed)
        f.write("clang cross-check: compiled=%d executed=%d passed=%d failed=%d\n" %
                (counters.clang["compiled"], counters.clang["executed"],
                 counters.clang["passed"], counters.clang["failed"]))
        f.write("total seconds: %s\n\n" % total_seconds)
        f.write("categories (generated/executed/passed/failed):\n")
        for k in sorted(counters.categories):
            c = counters.categories[k]
            f.write("  %-24s %d/%d/%d/%d\n" %
                    (k, c["generated"], c["executed"], c["passed"], c["failed"]))
        f.write("\nfailures:\n")
        if not counters.failures:
            f.write("  none\n")
        for fl in counters.failures[:200]:
            f.write("  %s [%s] %s :: %s\n" %
                    (fl["scenario"], fl["category"], fl["path"], fl["detail"].replace("\n", " | ")))
    print("SUMMARY generated=%d executed=%d passed=%d failed=%d clang=%d/%d/%d" %
          (counters.gen, counters.executed, counters.passed, counters.failed,
           counters.clang["executed"], counters.clang["passed"], counters.clang["failed"]),
          flush=True)
    return 0 if counters.failed == 0 else 1


def manifest_scenarios(manifest, shard_name):
    # scenario metadata is read from the shard's generated manifest sidecar
    with open(os.path.join(HERE, "generated", shard_name + ".scenarios.json")) as f:
        return json.load(f)


if __name__ == "__main__":
    sys.exit(main())
