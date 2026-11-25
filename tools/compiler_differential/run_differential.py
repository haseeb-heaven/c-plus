#!/usr/bin/env python3
"""Differential harness: C+ (--legacy-c --sanitize=none) vs reference C compilers.

For every deterministic corpus fixture and every discovered reference compiler,
the harness

  1. compiles the fixture with the reference compiler,
  2. compiles the same fixture with C+ using ``build --legacy-c --sanitize=none``,
  3. runs both executables under a hard timeout,
  4. compares the exit status and the *exact* stdout/stderr, normalizing only
     the absolute paths that this harness itself injected,
  5. persists a full evidence bundle for every mismatch, and
  6. writes ``results/summary.txt`` and a valid ``results/summary.json``.

Nothing is hidden: every subprocess return code, timeout, missing binary or
output difference is classified explicitly.  A mismatch makes the process exit
non-zero.  Configuration errors (bad binary, corrupted corpus, no compilers)
exit 2.  ``|| true`` and silent skips are intentionally absent.

Usage:
    CPLUS_BIN=$PWD/build/c+ python3 tools/compiler_differential/run_differential.py
"""

from __future__ import annotations

import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import platform as platform_mod
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Optional

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
FIXTURES = REPO / "tests" / "compiler" / "differential"
MANIFEST = FIXTURES / "manifest.json"
RESULTS = FIXTURES / "results"
LOGS = RESULTS / "logs"
MISMATCHES = RESULTS / "mismatches"

BIN = os.environ.get("CPLUS_BIN", str(pathlib.Path.cwd() / "build" / "c+"))
BUILD_TIMEOUT = int(os.environ.get("DIFF_BUILD_TIMEOUT", "120"))
RUN_TIMEOUT = int(os.environ.get("DIFF_RUN_TIMEOUT", "20"))
WORKERS = int(os.environ.get("DIFF_WORKERS", "4"))

# Both tracks must use the SAME C dialect.  C+ defaults legacy mode to
# -std=gnu89, so without the pass-through a C99 construct such as
# `for (int i=...)` compiles with the reference (-std=c11) but fails with C+.
CPLUS_FLAGS = ["build", "--legacy-c", "--sanitize=none"]
CPLUS_EXTRA = ["--", "-std=c11"]
REF_FLAGS = ["-std=c11", "-O0"]

CANDIDATES = [
    "clang",                 # LLVM clang from PATH (e.g. Homebrew)
    "gcc",                   # GNU gcc or Apple clang alias, classified by --version
    "gcc-15", "gcc-14", "gcc-13", "gcc-12",
    "/usr/bin/clang",        # Apple clang, explicit
    "/usr/bin/gcc",          # Apple clang alias, explicit
    "cc",
]

PROBE_SOURCE = "int main(void) { return 0; }\n"


def run_cmd(argv, timeout, cwd=None):
    """Run a command; classify rc/timeout/OS error explicitly, never swallow."""
    try:
        p = subprocess.run([str(a) for a in argv], capture_output=True,
                           text=True, timeout=timeout, cwd=cwd)
        return {"command": [str(a) for a in argv], "rc": p.returncode,
                "stdout": p.stdout, "stderr": p.stderr, "timed_out": False,
                "os_error": None}
    except subprocess.TimeoutExpired as exc:
        out, err = exc.stdout or b"", exc.stderr or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return {"command": [str(a) for a in argv], "rc": None,
                "stdout": out, "stderr": err, "timed_out": True,
                "os_error": None}
    except OSError as exc:
        return {"command": [str(a) for a in argv], "rc": None,
                "stdout": "", "stderr": "", "timed_out": False,
                "os_error": str(exc)}


def log_text(res):
    return ("command: %s\nreturncode: %s\ntimed_out: %s\nos_error: %s\n\n"
            "--- stdout ---\n%s\n--- stderr ---\n%s\n"
            % (" ".join(res["command"]), res["rc"], res["timed_out"],
               res["os_error"], res["stdout"], res["stderr"]))


def classify_version(text):
    low = text.lower()
    if "apple clang" in low:
        return "apple_clang"
    if "clang version" in low or "homebrew clang" in low:
        return "clang"
    if "free software foundation" in low and re.search(r"\bgcc\b", low):
        return "gcc"
    return "other"


def find_sdk():
    if platform_mod.system() != "Darwin":
        return None
    res = run_cmd(["xcrun", "--show-sdk-path"], timeout=15)
    if res["rc"] == 0 and res["stdout"].strip():
        return res["stdout"].strip()
    return None


def discover_compilers(sdk):
    """Probe every candidate; record exact versions and a trivial-compile probe."""
    compilers = []
    seen_realpaths = set()
    family_counts = {}
    with tempfile.TemporaryDirectory(prefix="cplus-diff-discover-") as td:
        tmp = pathlib.Path(td)
        (tmp / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
        for command in CANDIDATES:
            resolved = shutil.which(command)
            if resolved is None:
                continue
            real = str(pathlib.Path(resolved).resolve())
            if real in seen_realpaths:
                continue
            seen_realpaths.add(real)

            version = run_cmd([resolved, "--version"], timeout=15)
            if version["rc"] != 0 or version["os_error"]:
                compilers.append({
                    "tag": pathlib.Path(command).name, "command": command,
                    "path": resolved, "realpath": real, "family": "unavailable",
                    "version_first_line": "(version probe failed)",
                    "version_text": log_text(version),
                    "sysroot": sdk, "available": False,
                    "probe": {"rc": version["rc"], "timed_out": False,
                              "reason": "version probe failed: %s"
                                        % (version["os_error"] or version["stderr"].strip()[:200])},
                })
                continue

            version_text = (version["stdout"] + version["stderr"]).strip()
            first_line = version_text.splitlines()[0] if version_text else "(empty)"
            family = classify_version(version_text)
            family_counts[family] = family_counts.get(family, 0) + 1
            tag = family if family_counts[family] == 1 else "%s_%d" % (
                family, family_counts[family])

            argv = [resolved] + REF_FLAGS
            if sdk:
                argv += ["-isysroot", sdk]
            argv += [(tmp / "probe.c"), "-o", (tmp / ("probe_" + tag))]
            probe = run_cmd(argv, timeout=BUILD_TIMEOUT)

            reason = None
            if probe["os_error"]:
                reason = "OS error: %s" % probe["os_error"]
            elif probe["timed_out"]:
                reason = "compile probe timed out after %ss" % BUILD_TIMEOUT
            elif probe["rc"] != 0:
                reason = "compile probe rc=%d" % probe["rc"]
            elif not (tmp / ("probe_" + tag)).exists():
                reason = "compile probe produced no executable"

            compilers.append({
                "tag": tag, "command": command, "path": resolved,
                "realpath": real, "family": family,
                "version_first_line": first_line, "version_text": version_text,
                "sysroot": sdk, "available": reason is None,
                "probe": {
                    "rc": probe["rc"], "timed_out": probe["timed_out"],
                    "os_error": probe["os_error"],
                    "stdout": probe["stdout"], "stderr": probe["stderr"],
                    "reason": reason,
                    "command": [str(a) for a in argv],
                },
            })
    return compilers


def load_corpus():
    if not MANIFEST.is_file():
        raise SystemExit("missing manifest %s (run generate_corpus.py)" % MANIFEST)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise SystemExit("unsupported manifest schema: %r" % manifest.get("schema"))

    # Verify the checked-in fixtures against both the manifest and a fresh
    # regeneration from the seed.  No drift is tolerated.
    sys.path.insert(0, str(HERE))
    from corpus import MASTER_SEED, build_corpus

    generated = build_corpus()
    if manifest.get("master_seed") != MASTER_SEED:
        raise SystemExit("manifest master seed does not match corpus.py")
    by_name = {c["name"]: c for c in manifest["cases"]}
    if set(by_name) != {c.name for c in generated}:
        raise SystemExit("manifest case names differ from generated corpus")
    cases = []
    for gen in generated:
        entry = by_name[gen.name]
        src_path = FIXTURES / entry["source"]
        if not src_path.is_file():
            raise SystemExit("missing fixture %s" % src_path)
        source = src_path.read_text(encoding="utf-8")
        disk_hash = hashlib.sha256(source.encode("utf-8")).hexdigest()
        if disk_hash != entry["sha256"] or disk_hash != gen.sha256:
            raise SystemExit("source hash drift for %s (manifest=%s disk=%s generated=%s)"
                             % (gen.name, entry["sha256"], disk_hash, gen.sha256))
        if entry["seed"] != gen.seed:
            raise SystemExit("seed drift for %s" % gen.name)
        cases.append({"name": gen.name, "category": gen.category,
                      "seed": gen.seed, "sha256": disk_hash,
                      "path": src_path, "source": source})
    return manifest, cases


def normalize(text, paths):
    """Replace only absolute paths this harness injected; nothing else."""
    out = str(text)
    for p in sorted({str(x) for x in paths if x}, key=len, reverse=True):
        out = out.replace(p, "<PATH>")
    return out


def one_case(case, compiler, index, master_seed):
    name, tag = case["name"], compiler["tag"]
    stem = "%s__%s" % (name, tag)
    work = pathlib.Path(tempfile.mkdtemp(prefix="cplus-diff-%s-" % stem))
    ref_bin = work / (stem + ".ref")
    cplus_bin = work / (stem + ".cplus")
    ref_argv = [compiler["path"]] + REF_FLAGS
    if compiler["sysroot"]:
        ref_argv += ["-isysroot", compiler["sysroot"]]
    ref_argv += [case["path"], "-o", ref_bin]
    cplus_argv = [BIN] + CPLUS_FLAGS + [case["path"], "-o", cplus_bin] + CPLUS_EXTRA

    record = {
        "index": index, "case": name, "category": case["category"],
        "seed": case["seed"], "seed_hex": hex(case["seed"]),
        "source_sha256": case["sha256"], "compiler": tag,
        "compiler_path": compiler["path"],
        "compiler_family": compiler["family"],
        "compiler_version": compiler["version_first_line"],
        "status": "pass", "reasons": [],
        "commands": {
            "reference_compile": [str(a) for a in ref_argv],
            "cplus_compile": [str(a) for a in cplus_argv],
            "reference_run": [str(ref_bin)], "cplus_run": [str(cplus_bin)],
        },
        "logs": {},
    }
    try:
        ref_compile = run_cmd(ref_argv, BUILD_TIMEOUT)
        cplus_compile = run_cmd(cplus_argv, BUILD_TIMEOUT)
        record["reference_compile"] = ref_compile
        record["cplus_compile"] = cplus_compile

        path_set = [work, case["path"], ref_bin, cplus_bin, BIN, REPO,
                    pathlib.Path.home()]

        ref_run = None
        cplus_run = None
        if ref_compile["rc"] != 0 or ref_compile["timed_out"] or ref_compile["os_error"]:
            record["reasons"].append("reference compile failed")
        if cplus_compile["rc"] != 0 or cplus_compile["timed_out"] or cplus_compile["os_error"]:
            record["reasons"].append("cplus compile failed")

        if not record["reasons"]:
            ref_run = run_cmd([ref_bin], RUN_TIMEOUT, cwd=work)
            cplus_run = run_cmd([cplus_bin], RUN_TIMEOUT, cwd=work)
            record["reference_run"] = ref_run
            record["cplus_run"] = cplus_run

            if ref_run["timed_out"] or ref_run["os_error"]:
                record["reasons"].append("reference run failed/timed out")
            if cplus_run["timed_out"] or cplus_run["os_error"]:
                record["reasons"].append("cplus run failed/timed out")
            if not record["reasons"]:
                if ref_run["rc"] != cplus_run["rc"]:
                    record["reasons"].append("exit status differs")
                if normalize(ref_run["stdout"], path_set) != normalize(cplus_run["stdout"], path_set):
                    record["reasons"].append("stdout differs")
                if normalize(ref_run["stderr"], path_set) != normalize(cplus_run["stderr"], path_set):
                    record["reasons"].append("stderr differs")

        record["comparison"] = {
            "exit_status": {
                "reference": (ref_run or {}).get("rc"),
                "cplus": (cplus_run or {}).get("rc"),
                "match": bool(ref_run and cplus_run and ref_run["rc"] == cplus_run["rc"]),
            },
            "stdout": {
                "match": bool(ref_run and cplus_run and
                              normalize(ref_run["stdout"], path_set) ==
                              normalize(cplus_run["stdout"], path_set)),
                "reference_normalized": normalize((ref_run or {}).get("stdout", ""), path_set),
                "cplus_normalized": normalize((cplus_run or {}).get("stdout", ""), path_set),
            },
            "stderr": {
                "match": bool(ref_run and cplus_run and
                              normalize(ref_run["stderr"], path_set) ==
                              normalize(cplus_run["stderr"], path_set)),
                "reference_normalized": normalize((ref_run or {}).get("stderr", ""), path_set),
                "cplus_normalized": normalize((cplus_run or {}).get("stderr", ""), path_set),
            },
        }
        if record["reasons"]:
            record["status"] = "mismatch"

        # Every case gets a single combined log under results/logs/.
        log_parts = ["=== reference compile ===\n%s\n" % log_text(ref_compile),
                     "=== cplus compile ===\n%s\n" % log_text(cplus_compile)]
        if ref_run is not None:
            log_parts.append("=== reference run ===\n%s\n" % log_text(ref_run))
        if cplus_run is not None:
            log_parts.append("=== cplus run ===\n%s\n" % log_text(cplus_run))
        log_rel = "logs/%s.txt" % stem
        (LOGS / (stem + ".txt")).write_text("".join(log_parts), encoding="utf-8")
        record["logs"]["combined"] = log_rel

        if record["status"] == "mismatch":
            bundle = MISMATCHES / stem
            bundle.mkdir(parents=True, exist_ok=True)
            (bundle / "seed.txt").write_text(
                "case=%s\ncategory=%s\nmaster_seed=%s\nseed=%s\nseed_hex=%s\n"
                "source_sha256=%s\n"
                % (name, case["category"], master_seed, case["seed"],
                   hex(case["seed"]), case["sha256"]), encoding="utf-8")
            (bundle / "source.c").write_text(case["source"], encoding="utf-8")
            (bundle / "commands.txt").write_text(
                "\n\n".join("%s:\n%s" % (k, " ".join(v))
                            for k, v in record["commands"].items()) + "\n",
                encoding="utf-8")
            (bundle / "comparison.json").write_text(
                json.dumps({"case": name, "compiler": tag,
                            "compiler_version": compiler["version_first_line"],
                            "reasons": record["reasons"],
                            "comparison": record["comparison"]}, indent=2) + "\n",
                encoding="utf-8")
            for label, res in (("reference_compile", ref_compile),
                               ("cplus_compile", cplus_compile),
                               ("reference_run", ref_run),
                               ("cplus_run", cplus_run)):
                if res is not None:
                    (bundle / (label + ".log")).write_text(log_text(res), encoding="utf-8")
            if ref_bin.exists():
                shutil.copy2(ref_bin, bundle / "reference.bin")
            if cplus_bin.exists():
                shutil.copy2(cplus_bin, bundle / "cplus.bin")
            (bundle / "README.txt").write_text(
                "Mismatch: %s vs %s\nRe-run with:\n  %s\n  %s\n"
                "Reference version: %s\n"
                % (name, tag, record["commands"]["reference_compile"],
                   record["commands"]["cplus_compile"],
                   compiler["version_first_line"]), encoding="utf-8")
            record["logs"]["bundle"] = "mismatches/%s" % stem
        return record
    finally:
        shutil.rmtree(work, ignore_errors=True)


def write_summaries(payload, records, compilers):
    (RESULTS / "summary.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

    t = payload["totals"]
    lines = [
        "C+ reference-compiler differential harness",
        "=" * 68,
        "binary       : %s" % BIN,
        "c+ version   : %s" % payload["cplus_version"],
        "c+ flags     : %s" % " ".join(CPLUS_FLAGS),
        "platform     : %s/%s" % (platform_mod.system(), platform_mod.machine()),
        "SDK          : %s" % (payload["sdk"] or "(none / non-Darwin)"),
        "generated    : %s" % payload["generated_at"],
        "corpus       : %d fixtures, master seed %s" % (
            payload["corpus"]["fixture_count"], payload["corpus"]["master_seed"]),
        "compiles     : -std=c11 -O0%s" % (
            " -isysroot <SDK>" if payload["sdk"] else ""),
        "timeouts     : %ss build, %ss run" % (BUILD_TIMEOUT, RUN_TIMEOUT),
        "workers      : %s" % WORKERS,
        "",
        "REFERENCE COMPILERS",
        "-" * 68,
    ]
    for c in compilers:
        lines.append("  %-16s %s" % (c["tag"], c["version_first_line"]))
        lines.append("      command : %s" % c["command"])
        lines.append("      path    : %s" % c["realpath"])
        lines.append("      family  : %s" % c["family"])
        lines.append("      probe   : %s" % (
            "OK" if c["available"] else "UNAVAILABLE - %s" % c["probe"]["reason"]))
    lines += [
        "",
        "TOTALS",
        "-" * 68,
        "  comparisons : %d" % t["comparisons"],
        "  passed      : %d" % t["passed"],
        "  mismatched  : %d" % t["mismatched"],
        "  unavailable compilers : %d" % t["unavailable_compilers"],
        "",
        "MISMATCHES (%d)" % t["mismatched"],
        "-" * 68,
    ]
    if payload["mismatches"]:
        for m in payload["mismatches"]:
            lines.append("  %s vs %s: %s" % (
                m["case"], m["compiler"], "; ".join(m["reasons"])))
            cc = m["comparison"]
            lines.append("      exit ref=%s cplus=%s (match=%s)" % (
                cc["exit_status"]["reference"], cc["exit_status"]["cplus"],
                cc["exit_status"]["match"]))
            lines.append("      stdout match=%s stderr match=%s" % (
                cc["stdout"]["match"], cc["stderr"]["match"]))
            lines.append("      seed=%s evidence=%s" % (
                m["seed"], m["evidence"]))
    else:
        lines.append("  (none)")
    unavailable = [c for c in compilers if not c["available"]]
    if unavailable:
        lines += ["", "UNAVAILABLE COMPILERS (excluded from comparisons)", "-" * 68]
        for c in unavailable:
            lines.append("  %s: %s" % (c["tag"], c["probe"]["reason"]))
    lines += [
        "",
        "By category:",
    ]
    for cat, stats in sorted(payload["by_category"].items()):
        lines.append("  %-14s total=%d pass=%d mismatch=%d" % (
            cat, stats["total"], stats["pass"], stats["mismatch"]))
    lines += [
        "",
        "Artifacts: results/summary.txt, results/summary.json, results/logs/,",
        "           results/mismatches/<case>__<compiler>/ (seed, source, commands, logs)",
    ]
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    if not os.path.isfile(BIN) or not os.access(BIN, os.X_OK):
        raise SystemExit("CPLUS_BIN not found or not executable: %s" % BIN)
    if WORKERS < 1:
        raise SystemExit("DIFF_WORKERS must be >= 1")

    RESULTS.mkdir(parents=True, exist_ok=True)
    # Results describe this run only: never leave stale logs or mismatch
    # bundles behind for a case that now passes.
    for stale in (LOGS, MISMATCHES):
        if stale.exists():
            shutil.rmtree(stale)
        stale.mkdir(parents=True)

    manifest, cases = load_corpus()
    master_seed = manifest["master_seed"]

    version = run_cmd([BIN, "--version"], timeout=15)
    if version["rc"] != 0:
        raise SystemExit("c+ --version failed: %s" % log_text(version))
    cplus_version = (version["stdout"] + version["stderr"]).strip().splitlines()

    sdk = find_sdk()
    compilers = discover_compilers(sdk)
    available = [c for c in compilers if c["available"]]
    if not available:
        raise SystemExit("no reference compiler passed the discovery probe")

    jobs = [(case, compiler, i)
            for i, (case, compiler) in enumerate(
                (c, comp) for c in cases for comp in available)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        futures = [pool.submit(one_case, case, comp, i, master_seed)
                   for case, comp, i in jobs]
        records = [f.result() for f in futures]
    records.sort(key=lambda r: (r["index"], r["compiler"]))

    mismatches = [r for r in records if r["status"] == "mismatch"]
    by_category = {}
    for r in records:
        bucket = by_category.setdefault(r["category"], {
            "total": 0, "pass": 0, "mismatch": 0})
        bucket["total"] += 1
        bucket["pass" if r["status"] == "pass" else "mismatch"] += 1

    payload = {
        "schema": 1,
        "suite": "compiler_differential",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary": BIN,
        "cplus_version": cplus_version,
        "platform": {"system": platform_mod.system(),
                     "machine": platform_mod.machine()},
        "sdk": sdk,
        "cplus_flags": CPLUS_FLAGS,
        "reference_flags": REF_FLAGS,
        "timeouts": {"build_seconds": BUILD_TIMEOUT, "run_seconds": RUN_TIMEOUT},
        "workers": WORKERS,
        "corpus": {
            "master_seed": manifest["master_seed"],
            "fixture_count": len(cases),
            "cases": [{"name": c["name"], "category": c["category"],
                       "seed": c["seed"], "sha256": c["sha256"]} for c in cases],
        },
        "reference_compilers": compilers,
        "totals": {
            "fixtures": len(cases),
            "compilers_discovered": len(compilers),
            "compilers_available": len(available),
            "unavailable_compilers": len(compilers) - len(available),
            "comparisons": len(records),
            "passed": len(records) - len(mismatches),
            "mismatched": len(mismatches),
        },
        "by_category": by_category,
        "mismatches": [
            {"case": r["case"], "category": r["category"],
             "compiler": r["compiler"], "compiler_version": r["compiler_version"],
             "seed": r["seed"], "reasons": r["reasons"],
             "comparison": r["comparison"],
             "evidence": r["logs"].get("bundle")} for r in mismatches
        ],
        "records": records,
    }
    write_summaries(payload, records, compilers)

    print("comparisons=%d passed=%d mismatched=%d compilers=%d"
          % (payload["totals"]["comparisons"], payload["totals"]["passed"],
             payload["totals"]["mismatched"], len(available)))
    for c in compilers:
        print("compiler %s: %s [%s] %s"
              % (c["tag"], c["version_first_line"], c["path"],
                 "OK" if c["available"] else "UNAVAILABLE: " + c["probe"]["reason"]))
    for m in payload["mismatches"]:
        print("MISMATCH %s vs %s: %s"
              % (m["case"], m["compiler"], ", ".join(m["reasons"])))
    print("summary: %s" % (RESULTS / "summary.txt"))
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
