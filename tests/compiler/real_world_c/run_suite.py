#!/usr/bin/env python3
"""Real-world C behavioural comparison harness.

Downloads unmodified single-file C programs from pinned URLs, builds each one
with ``c+ build --legacy-c --sanitize=none --emit-c`` and with
``/usr/bin/clang -std=gnu89 -O2``, requires the emitted C to equal the
downloaded original modulo a single leading ``#line`` directive, runs both
binaries with identical deterministic args/stdin/env/cwd and a hard timeout,
and compares exact stdout and exit status.

No ``|| true``. No skips. Every download, build, source-preservation, stdout or
exit mismatch is a hard failure and is printed and written to the summaries.

Run from the repository root:

    CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_world_c/run_suite.py

This script owns only new files under tests/compiler/real_world_c/ and makes
no edits or commits elsewhere.
"""

from __future__ import annotations

import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"
CACHE = HERE / "cache"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"
BINS = RESULTS / "bin"
GEN = RESULTS / "generated"

REPO = HERE.parents[2]
CPLUS = os.environ.get("CPLUS_BIN", str(REPO / "build" / "c+"))
REF_CC = os.environ.get("REF_CC", "/usr/bin/clang")
TIMEOUT = int(os.environ.get("RW_TIMEOUT", "120"))
WORKERS = int(os.environ.get("RW_WORKERS", "4"))
REDownload = os.environ.get("RW_DOWNLOAD", "1") != "0"

LEADING_LINE = re.compile(rb'^#line [0-9]+ "[^"]*"\r?\n')


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def invoke(cmd: list[str], *, stdin: bytes = b"", cwd: pathlib.Path,
           env: dict[str, str], timeout: int) -> dict:
    """Run one subprocess. Never suppress a failure with '|| true'."""
    rec: dict = {"cmd": cmd, "rc": None, "timed_out": False,
                 "stdout": "", "stderr": "",
                 "stdout_bytes": b"", "stderr_bytes": b""}
    try:
        proc = subprocess.run(cmd, input=stdin, cwd=str(cwd), env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        rec["timed_out"] = True
        rec["_out"] = exc.stdout or b""
        rec["_err"] = exc.stderr or b""
    else:
        rec["rc"] = proc.returncode
        rec["_out"] = proc.stdout
        rec["_err"] = proc.stderr
    rec["stdout_bytes"] = rec.pop("_out")
    rec["stderr_bytes"] = rec.pop("_err")
    rec["stdout"] = dec(rec["stdout_bytes"])
    rec["stderr"] = dec(rec["stderr_bytes"])
    return rec


def dec(data: bytes) -> str:
    return data.decode("utf-8", "replace")


def write_log(path: pathlib.Path, name: str, rec: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "case: %s\ncommand: %s\nrc: %s\ntimed_out: %s\n--- stdout ---\n%s\n"
        "--- stderr ---\n%s\n"
        % (name, " ".join(rec["cmd"]), rec["rc"], rec["timed_out"],
           rec["stdout"], rec["stderr"]),
        encoding="utf-8")


def download(case: dict) -> tuple[bytes | None, list[str]]:
    """Fetch the pinned source and verify its SHA-256. No fallback source."""
    failures: list[str] = []
    dest = CACHE / (case["name"] + ".c")
    if REDownload or not dest.is_file():
        cmd = ["curl", "-fsSL", "--max-time", str(TIMEOUT),
               "-o", str(dest), case["url"]]
        try:
            proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=TIMEOUT + 10,
                                  check=False)
        except subprocess.TimeoutExpired:
            return None, ["download timed out: " + case["url"]]
        if proc.returncode != 0:
            return None, ["download failed (rc=%d): %s: %s"
                          % (proc.returncode, case["url"],
                             dec(proc.stderr)[:400])]
    if not dest.is_file():
        return None, ["download produced no file: " + str(dest)]
    data = dest.read_bytes()
    actual = sha256(data)
    if actual != case["sha256"]:
        failures.append("source sha256 mismatch: expected %s, got %s"
                        % (case["sha256"], actual))
    return data, failures


def compare_run(label: str, rec: dict, case: dict, failures: list[str]) -> None:
    if rec["timed_out"]:
        failures.append("%s: timed out after %ds" % (label, TIMEOUT))
        return
    if rec["rc"] < 0:
        failures.append("%s: killed by signal (rc=%d)" % (label, rec["rc"]))
    if rec["rc"] != case["expected_rc"]:
        failures.append("%s: exit expected %d, got %d"
                        % (label, case["expected_rc"], rec["rc"]))
    if rec["stdout_bytes"] != case["expected_stdout"].encode():
        failures.append("%s: stdout expected %r, got %r"
                        % (label, case["expected_stdout"], rec["stdout"]))


def run_case(case: dict) -> dict:
    name = case["name"]
    result: dict = {"name": name, "upstream_path": case["path"],
                    "url": case["url"], "failures": [], "steps": {}}
    failures: list[str] = result["failures"]

    src, dl_failures = download(case)
    failures.extend(dl_failures)
    if src is None:
        return result
    result["sha256_actual"] = sha256(src)

    srcpath = CACHE / (name + ".c")
    cbin = (BINS / (name + ".cplus")).resolve()
    rbin = (BINS / (name + ".clang")).resolve()
    genpath = (GEN / (name + ".generated.c")).resolve()
    for p in (cbin, rbin, genpath):
        p.unlink(missing_ok=True)

    # Fixed env/cwd for both tracks and for all runs.
    env = dict(os.environ)
    env.update({"TZ": "UTC", "LC_ALL": "C", "LANG": "C"})
    workdir = RESULTS.resolve()

    cb = invoke([CPLUS, "build", "--legacy-c", "--sanitize=none",
                 "--emit-c", str(genpath), str(srcpath.resolve()),
                 "-o", str(cbin)], cwd=workdir, env=env, timeout=TIMEOUT)
    result["steps"]["cplus_build"] = cb
    write_log(LOGS / (name + ".cplus_build.txt"), name, cb)
    if cb["timed_out"] or cb["rc"] != 0:
        failures.append("c+ build failed (rc=%s, timed_out=%s): %s"
                        % (cb["rc"], cb["timed_out"], cb["stderr"][:400]))

    rb = invoke([REF_CC, "-std=gnu89", "-O2", str(srcpath.resolve()),
                 "-o", str(rbin)], cwd=workdir, env=env, timeout=TIMEOUT)
    result["steps"]["ref_build"] = rb
    write_log(LOGS / (name + ".ref_build.txt"), name, rb)
    if rb["timed_out"] or rb["rc"] != 0:
        failures.append("clang build failed (rc=%s, timed_out=%s): %s"
                        % (rb["rc"], rb["timed_out"], rb["stderr"][:400]))

    # Source preservation: one leading #line, then byte-identical original.
    if genpath.is_file():
        generated = genpath.read_bytes()
        prefix = LEADING_LINE.match(generated)
        rest = generated[prefix.end():] if prefix else generated
        identical = bool(prefix) and rest == src
        result["source_preservation"] = {
            "has_leading_line_directive": bool(prefix),
            "byte_identical_after_line": identical,
            "generated_bytes": len(generated),
            "original_bytes": len(src),
        }
        if not prefix:
            failures.append("source preservation: emitted C lacks a leading "
                            "#line directive")
        if not identical:
            diff_at = next((i for i, (a, b) in enumerate(zip(rest, src))
                            if a != b), min(len(rest), len(src)))
            failures.append(
                "source preservation: emitted C differs from original at "
                "byte %d (generated %d bytes vs original %d bytes)"
                % (diff_at, len(generated), len(src)))
            (GEN / (name + ".diff.txt")).write_text(
                "generated-after-#line:\n%s\n\noriginal:\n%s\n"
                % (dec(rest), dec(src)), encoding="utf-8")
    elif not (cb["timed_out"] or cb["rc"] != 0):
        failures.append("source preservation: --emit-c produced no file")

    stdin = case["stdin"].encode()
    args = [str(x) for x in case.get("args", [])]
    for label, binary in (("cplus_run", cbin), ("ref_run", rbin)):
        if not binary.is_file():
            failures.append("%s: binary not produced" % label)
            continue
        rec = invoke([str(binary)] + args, stdin=stdin, cwd=workdir,
                     env=env, timeout=TIMEOUT)
        result["steps"][label] = rec
        write_log(LOGS / (name + "." + label + ".txt"), name, rec)
        compare_run(label, rec, case, failures)
        result.setdefault("runs", {})[label] = {
            "rc": rec["rc"], "timed_out": rec["timed_out"],
            "stdout": rec["stdout"], "stderr": rec["stderr"],
        }

    cst = result.get("steps", {}).get("cplus_run")
    rst = result.get("steps", {}).get("ref_run")
    if cst and rst and not cst["timed_out"] and not rst["timed_out"]:
        so_eq = cst["stdout_bytes"] == rst["stdout_bytes"]
        rc_eq = cst["rc"] == rst["rc"]
        same = so_eq and rc_eq
        result["cplus_vs_clang"] = {"stdout_equal": so_eq,
                                    "exit_equal": rc_eq, "match": same}
        if not same:
            failures.append("c+ vs clang mismatch: stdout_equal=%s exit_equal=%s"
                            % (so_eq, rc_eq))
    return result


def main() -> int:
    manifest = json.loads(MANIFEST.read_text())
    if manifest.get("schema") != 1:
        print("unsupported manifest schema", file=sys.stderr)
        return 2
    cases = manifest["cases"]
    if len(cases) < 5:
        print("manifest must contain at least 5 cases", file=sys.stderr)
        return 2

    if not pathlib.Path(CPLUS).is_file():
        print("CPLUS_BIN not found or not a file: " + CPLUS, file=sys.stderr)
        return 2
    if not pathlib.Path(REF_CC).is_file():
        print("REF_CC not found or not a file: " + REF_CC, file=sys.stderr)
        return 2

    for d in (CACHE, RESULTS, LOGS, BINS, GEN):
        d.mkdir(parents=True, exist_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        records = list(pool.map(run_case, cases))
    records.sort(key=lambda r: r["name"])
    for rec in records:
        for step in rec.get("steps", {}).values():
            step.pop("stdout_bytes", None)
            step.pop("stderr_bytes", None)

    passed = [r for r in records if not r["failures"]]
    failed = [r for r in records if r["failures"]]
    entries = sum(len(r["failures"]) for r in records)
    src_ok = sum(1 for r in records
                 if r.get("source_preservation", {}).get("byte_identical_after_line"))
    match_ok = sum(1 for r in records
                   if r.get("cplus_vs_clang", {}).get("match"))

    summary = {
        "suite": manifest["suite"],
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "compiler": CPLUS,
        "reference_cc": REF_CC,
        "reference_flags": manifest["reference_flags"],
        "mode": manifest["mode"],
        "sanitize": manifest["sanitize"],
        "upstream": manifest["upstream"],
        "counts": {
            "projects": len(records),
            "projects_passed": len(passed),
            "projects_failed": len(failed),
            "failure_entries": entries,
            "source_preservation_passed": src_ok,
            "cplus_vs_clang_matches": match_ok,
        },
        "results": records,
        "mismatches": [{"project": r["name"], "failure": f}
                       for r in records for f in r["failures"]],
    }
    (RESULTS / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    lines = [
        "Real-world C: c+ --legacy-c --sanitize=none vs %s" % REF_CC,
        "generated: %s" % summary["generated_utc"],
        "compiler: %s" % CPLUS,
        "upstream: %s @ %s" % (manifest["upstream"]["url"],
                               manifest["upstream"]["pinned_commit"]),
        "",
        "counts: projects=%d passed=%d failed=%d failure_entries=%d "
        "source_preservation_passed=%d cplus_vs_clang_matches=%d"
        % (len(records), len(passed), len(failed), entries, src_ok, match_ok),
        "",
        "per-project:",
    ]
    for r in records:
        sp = r.get("source_preservation", {})
        mm = r.get("cplus_vs_clang", {})
        runs = r.get("runs", {})
        lines.append(
            "  %-12s status=%s source=%s stdout+exit=%s "
            "cplus_rc=%s clang_rc=%s"
            % (r["name"], "PASS" if not r["failures"] else "FAIL",
               sp.get("byte_identical_after_line", "N/A"),
               mm.get("match", "N/A"),
               runs.get("cplus_run", {}).get("rc", "N/A"),
               runs.get("ref_run", {}).get("rc", "N/A")))
    lines += ["", "mismatches (%d):" % entries]
    if summary["mismatches"]:
        for m in summary["mismatches"]:
            lines.append("  [%s] %s" % (m["project"], m["failure"]))
    else:
        lines.append("  none")
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")

    print("real_world_c: %d projects, %d passed, %d failed, %d failure entries"
          % (len(records), len(passed), len(failed), entries))
    print("source preservation: %d/%d; c+ vs clang stdout+exit: %d/%d"
          % (src_ok, len(records), match_ok, len(records)))
    for m in summary["mismatches"]:
        print("MISMATCH [%s]: %s" % (m["project"], m["failure"]))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
