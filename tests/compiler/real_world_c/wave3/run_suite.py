#!/usr/bin/env python3
"""Wave-3 BLUE DEFENDER: permanent real-world C compatibility harness.

For every (case, standard, mode) cell this harness:

  * downloads the pinned MIT-licensed source and verifies its SHA-256;
  * builds it with /usr/bin/clang -std=<standard> -O2 (reference);
  * builds it with the CURRENT c+ in --legacy-c, --default and --strict,
    always with --sanitize=none and --emit-c;
  * checks --emit-c source preservation: exactly one #line directive and
    byte-identical original source in legacy-c mode; exactly one #line with
    line number 1 and the correct source path in the safety modes (where
    the rewriter legitimately injects a prelude and may rewrite operators);
  * runs both binaries with identical stdin/args/env/cwd and hard timeouts;
  * compares exact stdout bytes and exact exit status to BOTH the manifest
    expectation and the clang reference.

No ``|| true``. No skips. No suppressed failures. Every download, hash,
build, preservation, timeout, stdout or exit mismatch is printed and written
to results/summary.{txt,json} with the exact command and exact bytes.

Recognised, documented mode limitations (safety gates and the C89 `inline`
prelude limitation) are recorded as MODE_REJECTED with their exact rc/stderr
and counted separately; they are never silently discarded.

Run from the repository root:

    CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_world_c/wave3/run_suite.py

This script owns only new files under tests/compiler/real_world_c/wave3/.
It does not edit other files, rebuild the compiler, or commit anything.
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
import threading

HERE = pathlib.Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"
CACHE = HERE / "cache"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"
BINS = RESULTS / "bin"
GEN = RESULTS / "generated"

REPO = HERE.parents[3]
CPLUS = os.environ.get("CPLUS_BIN", str(REPO / "build" / "c+"))
REF_CC = os.environ.get("REF_CC", "/usr/bin/clang")
DL_TIMEOUT = int(os.environ.get("RW3_DL_TIMEOUT", "30"))
BUILD_TIMEOUT = int(os.environ.get("RW3_BUILD_TIMEOUT", "60"))
RUN_TIMEOUT = int(os.environ.get("RW3_RUN_TIMEOUT", "20"))
WORKERS = int(os.environ.get("RW3_WORKERS", "6"))
REDOWNLOAD = os.environ.get("RW3_DOWNLOAD", "1") != "0"
# Several (case, standard) units may request the same source concurrently.
# Serialise downloads and publish via atomic rename so a partial file is never
# read by another unit.
DOWNLOAD_LOCK = threading.Lock()

LEADING_LINE = re.compile(rb'^#line ([0-9]+) "([^"\r\n]*)"\r?\n')
ANY_LINE = re.compile(rb'(?m)^#line ([0-9]+) "([^"\r\n]*)"[ \t]*\r?$')

# Build-failure diagnostics that are documented mode behaviour, not bugs.
GATE_MARKERS = ("[CPLUS-", "unknown type name 'inline'")

HARD_STATUSES = {
    "FAIL_DOWNLOAD", "FAIL_HASH", "FAIL_REFERENCE", "FAIL_BUILD",
    "FAIL_TIMEOUT", "FAIL_SIGNAL", "FAIL_PRESERVATION", "FAIL_BEHAVIOR",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dec(data: bytes) -> str:
    return data.decode("utf-8", "replace")


def invoke(cmd: list[str], *, stdin: bytes = b"", timeout: int,
           cwd: pathlib.Path, env: dict[str, str]) -> dict:
    """Run one subprocess. Never suppress a failure with '|| true'."""
    rec: dict = {"cmd": cmd, "rc": None, "timed_out": False,
                 "stdout_bytes": b"", "stderr_bytes": b""}
    try:
        proc = subprocess.run(cmd, input=stdin, cwd=str(cwd), env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        rec["timed_out"] = True
        rec["stdout_bytes"] = exc.stdout or b""
        rec["stderr_bytes"] = exc.stderr or b""
    else:
        rec["rc"] = proc.returncode
        rec["stdout_bytes"] = proc.stdout
        rec["stderr_bytes"] = proc.stderr
    rec["stdout"] = dec(rec["stdout_bytes"])
    rec["stderr"] = dec(rec["stderr_bytes"])
    return rec


def write_log(path: pathlib.Path, label: str, rec: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "cell: %s\ncommand: %s\nrc: %s\ntimed_out: %s\n--- stdout ---\n%s\n"
        "--- stderr ---\n%s\n"
        % (label, " ".join(rec["cmd"]), rec["rc"], rec["timed_out"],
           rec["stdout"], rec["stderr"]), encoding="utf-8")


def download(case: dict) -> tuple[bytes | None, list[str]]:
    """Fetch the pinned source and verify its SHA-256. No fallback source."""
    dest = CACHE / (case["name"] + ".c")
    with DOWNLOAD_LOCK:
        if REDOWNLOAD or not dest.is_file():
            tmp = CACHE / (case["name"] + ".c.download")
            tmp.unlink(missing_ok=True)
            cmd = ["curl", "-fsSL", "--max-time", str(DL_TIMEOUT),
                   "-o", str(tmp), case["url"]]
            try:
                proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,
                                      timeout=DL_TIMEOUT + 10, check=False)
            except subprocess.TimeoutExpired:
                tmp.unlink(missing_ok=True)
                return None, ["download timed out: " + case["url"]]
            if proc.returncode != 0:
                tmp.unlink(missing_ok=True)
                return None, ["download failed rc=%d: %s: %s"
                              % (proc.returncode, case["url"], dec(proc.stderr)[:400])]
            if not tmp.is_file() or tmp.stat().st_size == 0:
                tmp.unlink(missing_ok=True)
                return None, ["download produced no data: " + case["url"]]
            data = tmp.read_bytes()
            if sha256(data) != case["sha256"]:
                tmp.unlink(missing_ok=True)
                return None, ["source sha256 mismatch: expected %s, got %s"
                              % (case["sha256"], sha256(data))]
            tmp.replace(dest)
        if not dest.is_file():
            return None, ["download produced no file: " + str(dest)]
        data = dest.read_bytes()
        actual = sha256(data)
        if actual != case["sha256"]:
            return None, ["source sha256 mismatch: expected %s, got %s"
                          % (case["sha256"], actual)]
        return data, []


def check_preservation(generated: bytes | None, src: bytes, srcname: str,
                       mode: dict) -> dict:
    """Verify --emit-c preservation. Exact in legacy-c, structural otherwise."""
    out: dict = {"mode": mode["name"], "exact_required": not mode["safety"],
                 "has_line_directive": False, "line_number": None,
                 "line_path": None, "line_count": 0,
                 "byte_identical_after_line": False,
                 "payload_bytes": None, "generated_bytes": None}
    if generated is None:
        out["error"] = "--emit-c produced no file"
        return out
    out["generated_bytes"] = len(generated)
    matches = list(ANY_LINE.finditer(generated))
    out["line_count"] = len(matches)
    if len(matches) != 1:
        out["error"] = "expected exactly one #line directive, found %d" % len(matches)
        return out
    m = matches[0]
    out["has_line_directive"] = True
    out["line_number"] = int(m.group(1))
    out["line_path"] = dec(m.group(2))
    # ANY_LINE ends just before the directive's terminating newline; skip it
    # so the payload starts at the first byte of the original source.
    start = m.end() + (1 if generated[m.end():m.end() + 1] == b"\n" else 0)
    payload = generated[start:]
    out["payload_bytes"] = len(payload)
    out["byte_identical_after_line"] = (payload == src)
    if mode["safety"]:
        # The rewriter injects a safety prelude and may rewrite operators;
        # byte identity is explicitly NOT required in these modes.
        ok = (out["line_number"] == 1
              and pathlib.Path(out["line_path"]).name == pathlib.Path(srcname).name)
        out["status"] = "PASS_STRUCTURAL" if ok else "FAIL"
        if not ok:
            out["error"] = ("safety-mode #line must be line 1 of the source, "
                            "got %s %r" % (out["line_number"], out["line_path"]))
        return out
    # legacy-c: exactly one leading #line then the byte-identical original.
    lead = LEADING_LINE.match(generated)
    ok = bool(lead) and out["byte_identical_after_line"]
    out["status"] = "PASS_EXACT" if ok else "FAIL"
    if not ok:
        if not lead:
            out["error"] = "legacy-c emitted C lacks a leading #line directive"
        else:
            diff = next((i for i, (a, b) in enumerate(zip(payload, src)) if a != b),
                        min(len(payload), len(src)))
            out["error"] = ("legacy-c emitted C differs at byte %d "
                            "(generated payload %d bytes, original %d bytes)"
                            % (diff, len(payload), len(src)))
    return out


def make_cell(case: dict, std: str, mode: dict, status: str,
              reason: str = "") -> dict:
    return {"case": case["name"], "standard": std, "mode": mode["name"],
            "status": status, "reason": reason}


def run_unit(unit: tuple[dict, str]) -> list[dict]:
    """One (case, standard): one clang build, then all mode cells."""
    case, std = unit
    modes: list[dict] = MANIFEST_DATA["modes"]
    env = dict(os.environ)
    env.update({"TZ": "UTC", "LC_ALL": "C", "LANG": "C", "SOURCE_DATE_EPOCH": "0"})
    workdir = RESULTS.resolve()
    src, dl_failures = download(case)
    cells: list[dict] = []
    if src is None:
        for mode in modes:
            c = make_cell(case, std, mode, "FAIL_DOWNLOAD", "; ".join(dl_failures))
            c["failures"] = dl_failures
            cells.append(c)
        return cells

    srcpath = (CACHE / (case["name"] + ".c")).resolve()
    stdin = case["stdin"].encode()
    args = [str(x) for x in case.get("args", [])]
    label = "%s.%s" % (case["name"], std)

    # Reference build/run (same for every mode of this case+standard).
    rbin = (BINS / ("%s.%s.clang" % (case["name"], std))).resolve()
    rbin.unlink(missing_ok=True)
    rb = invoke([REF_CC, "-std=" + std] + MANIFEST_DATA["reference_flags"]
                + [str(srcpath), "-o", str(rbin)],
                timeout=BUILD_TIMEOUT, cwd=workdir, env=env)
    write_log(LOGS / ("%s.ref_build.txt" % label), label, rb)
    ref_ok = not rb["timed_out"] and rb["rc"] == 0 and rbin.is_file()
    ref_run = None
    if ref_ok:
        ref_run = invoke([str(rbin)] + args, stdin=stdin,
                         timeout=RUN_TIMEOUT, cwd=workdir, env=env)
        write_log(LOGS / ("%s.ref_run.txt" % label), label, ref_run)
        ref_ok = (not ref_run["timed_out"] and ref_run["rc"] == case["expected_rc"]
                  and ref_run["stdout_bytes"] == case["expected_stdout"].encode())
    if not ref_ok:
        reason = "clang reference build/run failed or did not match manifest"
        for mode in modes:
            c = make_cell(case, std, mode, "FAIL_REFERENCE", reason)
            c["reference"] = {"rc": rb["rc"], "timed_out": rb["timed_out"],
                              "stderr": rb["stderr"][:800]}
            cells.append(c)
        return cells

    for mode in modes:
        cell = make_cell(case, std, mode, "PASS")
        mname = mode["name"]
        cell["commands"] = []
        cbin = (BINS / ("%s.%s.%s" % (case["name"], std, mname))).resolve()
        genpath = (GEN / ("%s.%s.%s.c" % (case["name"], std, mname))).resolve()
        cbin.unlink(missing_ok=True)
        genpath.unlink(missing_ok=True)

        cmd = [CPLUS, "build", mode["flag"], "--sanitize=none",
               "-std=" + std, "--emit-c", str(genpath), str(srcpath),
               "-o", str(cbin)]
        cell["commands"].append(" ".join(cmd))
        cb = invoke(cmd, timeout=BUILD_TIMEOUT, cwd=workdir, env=env)
        write_log(LOGS / ("%s.%s.build.txt" % (label, mname)), label, cb)
        cell["build"] = {"rc": cb["rc"], "timed_out": cb["timed_out"],
                         "stderr": cb["stderr"]}

        if cb["timed_out"]:
            cell.update(status="FAIL_TIMEOUT",
                        reason="c+ build timed out after %ds" % BUILD_TIMEOUT)
            cells.append(cell)
            continue
        if cb["rc"] != 0:
            stderr = cb["stderr"]
            gate = any(marker in stderr for marker in GATE_MARKERS)
            if gate:
                which = ("c89_inline_prelude" if "unknown type name 'inline'" in stderr
                         else "safety_gate")
                cell.update(status="MODE_REJECTED", reason=which)
            else:
                cell.update(status="FAIL_BUILD",
                            reason="c+ build failed rc=%s" % cb["rc"])
            cells.append(cell)
            continue

        generated = genpath.read_bytes() if genpath.is_file() else None
        if generated is not None:
            GEN.joinpath("%s.%s.%s.generated.c" % (case["name"], std, mname))\
               .write_bytes(generated)
        pres = check_preservation(generated, src, srcpath.name, mode)
        cell["preservation"] = pres
        if pres["status"] != "PASS_EXACT" and pres["status"] != "PASS_STRUCTURAL":
            cell.update(status="FAIL_PRESERVATION", reason=pres.get("error", "preservation failed"))
            cells.append(cell)
            continue
        if not cbin.is_file():
            cell.update(status="FAIL_BUILD", reason="c+ build produced no binary")
            cells.append(cell)
            continue

        cr = invoke([str(cbin)] + args, stdin=stdin,
                    timeout=RUN_TIMEOUT, cwd=workdir, env=env)
        write_log(LOGS / ("%s.%s.run.txt" % (label, mname)), label, cr)
        cell["run"] = {"rc": cr["rc"], "timed_out": cr["timed_out"],
                       "stdout_bytes": len(cr["stdout_bytes"]),
                       "stdout": cr["stdout"], "stderr": cr["stderr"]}
        if cr["timed_out"]:
            cell.update(status="FAIL_TIMEOUT",
                        reason="c+ run timed out after %ds" % RUN_TIMEOUT)
            cells.append(cell)
            continue
        if cr["rc"] < 0:
            cell.update(status="FAIL_SIGNAL", reason="c+ killed by signal rc=%d" % cr["rc"])
            cells.append(cell)
            continue

        so_ok = cr["stdout_bytes"] == case["expected_stdout"].encode()
        rc_ok = cr["rc"] == case["expected_rc"]
        ref_so_ok = (ref_run is not None
                     and cr["stdout_bytes"] == ref_run["stdout_bytes"])
        ref_rc_ok = (ref_run is not None and cr["rc"] == ref_run["rc"])
        cell["comparison"] = {
            "stdout_matches_expected": so_ok, "rc_matches_expected": rc_ok,
            "stdout_matches_clang": ref_so_ok, "rc_matches_clang": ref_rc_ok,
            "expected_stdout": case["expected_stdout"],
            "actual_stdout": cr["stdout"],
            "clang_stdout": ref_run["stdout"] if ref_run else None,
            "expected_rc": case["expected_rc"], "actual_rc": cr["rc"],
            "clang_rc": ref_run["rc"] if ref_run else None,
        }
        if not (so_ok and rc_ok and ref_so_ok and ref_rc_ok):
            detail = ("stdout expected=%r got=%r clang=%r; "
                      "rc expected=%d got=%d clang=%s"
                      % (case["expected_stdout"], cr["stdout"],
                         ref_run["stdout"] if ref_run else None,
                         case["expected_rc"], cr["rc"],
                         ref_run["rc"] if ref_run else None))
            cell.update(status="FAIL_BEHAVIOR", reason=detail)
        cells.append(cell)
    return cells


def strip_bytes(cells: list[dict]) -> None:
    for c in cells:
        for key in ("build", "run", "reference"):
            step = c.get(key)
            if isinstance(step, dict):
                step.pop("stdout_bytes", None)
        for k in list(c):
            if k.endswith("_bytes") and isinstance(c[k], int):
                continue


def main() -> int:
    global MANIFEST_DATA
    MANIFEST_DATA = json.loads(MANIFEST.read_text())
    if MANIFEST_DATA.get("schema") != 1:
        print("unsupported manifest schema", file=sys.stderr)
        return 2
    cases = MANIFEST_DATA["cases"]
    if len(cases) < 6:
        print("manifest must contain at least 6 projects", file=sys.stderr)
        return 2
    if not pathlib.Path(CPLUS).is_file():
        print("CPLUS_BIN not found or not a file: " + CPLUS, file=sys.stderr)
        return 2
    if not pathlib.Path(REF_CC).is_file():
        print("REF_CC not found or not a file: " + REF_CC, file=sys.stderr)
        return 2

    for d in (CACHE, RESULTS, LOGS, BINS, GEN):
        d.mkdir(parents=True, exist_ok=True)

    units = [(case, std) for case in cases for std in MANIFEST_DATA["standards"]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        all_cells = [c for group in pool.map(run_unit, units) for c in group]
    all_cells.sort(key=lambda c: (c["case"], c["standard"], c["mode"], c["status"]))
    for c in all_cells:
        strip_bytes([c])

    statuses = {}
    for c in all_cells:
        statuses[c["status"]] = statuses.get(c["status"], 0) + 1
    hard = [c for c in all_cells if c["status"] in HARD_STATUSES]
    mode_rejected = [c for c in all_cells if c["status"] == "MODE_REJECTED"]
    passed = [c for c in all_cells if c["status"] == "PASS"]

    per_project = {}
    for c in all_cells:
        p = per_project.setdefault(c["case"], {"cells": 0, "pass": 0,
                                                "rejected": 0, "hard_fail": 0})
        p["cells"] += 1
        p["pass"] += c["status"] == "PASS"
        p["rejected"] += c["status"] == "MODE_REJECTED"
        p["hard_fail"] += c["status"] in HARD_STATUSES

    summary = {
        "suite": MANIFEST_DATA["suite"],
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "compiler": CPLUS,
        "reference_cc": REF_CC,
        "reference_flags": MANIFEST_DATA["reference_flags"],
        "modes": [m["name"] for m in MANIFEST_DATA["modes"]],
        "standards": MANIFEST_DATA["standards"],
        "upstream": MANIFEST_DATA["upstream"],
        "counts": {
            "projects": len(cases),
            "upstream_repositories": len(MANIFEST_DATA["upstream"]),
            "standards": len(MANIFEST_DATA["standards"]),
            "modes": len(MANIFEST_DATA["modes"]),
            "cells_total": len(all_cells),
            "cells_pass": len(passed),
            "cells_mode_rejected": len(mode_rejected),
            "cells_hard_fail": len(hard),
            "projects_clean": sum(1 for p in per_project.values() if not p["hard_fail"]),
            "projects_with_hard_fail": sum(1 for p in per_project.values() if p["hard_fail"]),
            "status_breakdown": statuses,
            "exact_source_preservation_pass": sum(
                1 for c in all_cells
                if c.get("preservation", {}).get("status") == "PASS_EXACT"),
            "structural_source_preservation_pass": sum(
                1 for c in all_cells
                if c.get("preservation", {}).get("status") == "PASS_STRUCTURAL"),
        },
        "per_project": per_project,
        "cells": all_cells,
        "mismatches": [{"case": c["case"], "standard": c["standard"],
                        "mode": c["mode"], "status": c["status"],
                        "reason": c["reason"]} for c in hard],
        "mode_rejections": [{"case": c["case"], "standard": c["standard"],
                             "mode": c["mode"], "reason": c["reason"],
                             "first_stderr_line": c.get("build", {}).get("stderr", "").splitlines()[:1]}
                            for c in mode_rejected],
    }
    (RESULTS / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    lines = [
        "Wave-3 real-world C: c+ (all modes x standards) vs %s" % REF_CC,
        "generated: %s" % summary["generated_utc"],
        "compiler: %s" % CPLUS,
        "reference: %s %s" % (REF_CC, " ".join(MANIFEST_DATA["reference_flags"])),
        "modes: %s" % ", ".join(summary["modes"]),
        "standards: %s" % ", ".join(summary["standards"]),
        "",
        "counts: projects=%d repos=%d modes=%d standards=%d cells=%d pass=%d "
        "mode_rejected=%d hard_fail=%d"
        % (len(cases), len(MANIFEST_DATA["upstream"]), len(summary["modes"]),
           len(summary["standards"]), len(all_cells), len(passed),
           len(mode_rejected), len(hard)),
        "status_breakdown: %s" % json.dumps(statuses, sort_keys=True),
        "source preservation: exact=%d structural=%d"
        % (summary["counts"]["exact_source_preservation_pass"],
           summary["counts"]["structural_source_preservation_pass"]),
        "",
        "per-project:",
    ]
    for name in sorted(per_project):
        p = per_project[name]
        lines.append("  %-12s cells=%d pass=%d rejected=%d hard_fail=%d"
                     % (name, p["cells"], p["pass"], p["rejected"], p["hard_fail"]))
    lines += ["", "hard failures (%d):" % len(hard)]
    if hard:
        for c in hard:
            lines.append("  [%s %s %s] %s: %s"
                         % (c["case"], c["standard"], c["mode"],
                            c["status"], c["reason"]))
    else:
        lines.append("  none")
    lines += ["", "mode rejections (%d):" % len(mode_rejected)]
    for c in mode_rejected[:40]:
        lines.append("  [%s %s %s] %s" % (c["case"], c["standard"],
                                          c["mode"], c["reason"]))
    if len(mode_rejected) > 40:
        lines.append("  ... %d more (see summary.json)" % (len(mode_rejected) - 40))
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")

    print("wave3: %d projects / %d repos, %d cells across %d modes x %d standards"
          % (len(cases), len(MANIFEST_DATA["upstream"]), len(all_cells),
             len(summary["modes"]), len(summary["standards"])))
    print("PASS=%d MODE_REJECTED=%d HARD_FAIL=%d"
          % (len(passed), len(mode_rejected), len(hard)))
    print("status_breakdown=%s" % json.dumps(statuses, sort_keys=True))
    for c in hard:
        print("HARD [%s %s %s] %s: %s"
              % (c["case"], c["standard"], c["mode"], c["status"], c["reason"]))
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
