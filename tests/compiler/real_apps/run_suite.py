#!/usr/bin/env python3
"""Pinned real C apps/games: c+ modes vs clang and gcc-14.

For every case in ``manifest.json`` this harness downloads the unmodified
source from a pinned URL, verifies its full SHA-256, and for every pinned C
standard (-std=c89/c99/c11/c17) builds:

  * c+ --legacy-c --sanitize=none with the -std value passed through,
  * c+ --default --sanitize=none and --strict --sanitize=none where the source
    builds under that standard,
  * /usr/bin/clang -std=<std> -O2 (primary oracle) and, when present,
    /opt/homebrew/bin/gcc-14 -std=<std> -O2 (secondary oracle).

It checks, with no suppression and no silent skip:

  * one leading ``#line`` directive and byte-identical remaining source in
    LEGACY ``--emit-c`` output,
  * exact exported global symbol sets from ``nm -g --defined-only`` with the
    Mach-O leading underscore normalised (LEGACY must equal clang exactly;
    safe modes must preserve every application symbol, with the documented
    ``cplus_*`` runtime additions recorded separately),
  * byte-identical stdout and identical exit status for every binary against
    the manifest expectation and against both reference compilers,
  * exact timeouts (45 s), full build stderr/runtime stderr logging.

A STRICT/DEFAULT rejection is classified using the documented Safe mode rules
(docs/SAFE_MODE_SPEC.md, docs/ARITHMETIC_SAFETY.md,
docs/compiler/compatibility-matrix.md) as SAFE_MODE_RULE; anything else is a
DEFECT. A source that no compiler builds under a pinned standard is recorded
as REFERENCE_REJECTED with the exact diagnostics (never silently skipped) and
is not counted as a c+ pass. Any DEFECT or other unexpected failure makes the
harness exit 1.

Run from the repository root:

    CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_apps/run_suite.py

This script owns only new files under tests/compiler/real_apps/. Downloads go
to the ignored cache/ directory; generated binaries/logs go to ignored
results/. It makes no edits or commits elsewhere and does not rebuild build/.
"""

from __future__ import annotations

import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"
MANIFEST_META = json.loads(MANIFEST.read_text(encoding="utf-8"))["upstream"]
CACHE = HERE / "cache"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"
BINS = RESULTS / "bin"
GEN = RESULTS / "generated"
SUMMARY_JSON = HERE / "summary.json"
SUMMARY_TXT = HERE / "summary.txt"

REPO = HERE.parents[2]
CPLUS = os.environ.get("CPLUS_BIN", str(REPO / "build" / "c+"))
CLANG = os.environ.get("REAL_APPS_CLANG", "/usr/bin/clang")
GCC = os.environ.get("REAL_APPS_GCC", "/opt/homebrew/bin/gcc-14")
NM = shutil.which("nm") or "/usr/bin/nm"
TIMEOUT = min(45, int(os.environ.get("REAL_APPS_TIMEOUT", "45")))
WORKERS = int(os.environ.get("REAL_APPS_WORKERS", "4"))
DOWNLOAD = os.environ.get("REAL_APPS_DOWNLOAD", "1") != "0"

LEADING_LINE = re.compile(rb'^#line [0-9]+ "[^"]*"\r?\n')
# Mach-O is normalised by removing one leading underscore. Generated C helpers
# can start with two underscores at the C level, so accept any run of them.
RUNTIME_SYMBOL = re.compile(r"^_*cplus_")
CURL = os.environ.get("REAL_APPS_CURL", "curl")

# (pattern, documentation that makes the rejection an expected Safe mode rule)
DOCUMENTED_RULES = [
    (r"RAII is not guaranteed across non-local jumps",
     "docs/SAFE_MODE_SPEC.md: goto/longjmp cleanup (STRICT rejects jumps past live resources)"),
    (r"cannot build the C\+ safety AST.*STRICT would silently skip all safety analysis",
     "docs/SAFE_MODE_SPEC.md: STRICT fails closed, never a silent downgrade"),
    (r"\[CPLUS-FFI\]",
     "docs/SAFE_MODE_SPEC.md: Unsafe/FFI boundary; docs/compiler/compatibility-matrix.md: unknown contracts fail closed in STRICT"),
    (r"\[CPLUS-(OWNER|BORROW|LIFETIME|ALIAS|BOUNDS|REALLOC|LIBC|RAII)\]",
     "docs/SAFE_MODE_SPEC.md: Safe-mode diagnostic categories"),
    (r"\[CPLUS-ARITH\]",
     "docs/ARITHMETIC_SAFETY.md: integer overflow and division/remainder rules"),
    (r"use-after-free|double.free|invalid.free|null.deref|uninitiali[sz]ed|definite.initialization",
     "docs/SAFE_MODE_SPEC.md: proven errors are always rejected in both Safe modes"),
    (r"unverified|unverifiable|unproven",
     "docs/SAFE_MODE_SPEC.md: unverified operations are rejected in STRICT"),
    (r"realloc|overlapping memcpy|plain-char ctype|signal.handler|setjmp|longjmp",
     "docs/compiler/compatibility-matrix.md: Intentional differences from Clang/GCC"),
    (r"integer-forged|pointer provenance|cast from int",
     "docs/SAFE_MODE_SPEC.md: provenance / arbitrary casts are partial and fail closed"),
]


def dec(data: bytes) -> str:
    return data.decode("utf-8", "replace")


def invoke(cmd: list, *, stdin: bytes = b"", env: dict, cwd: pathlib.Path) -> dict:
    """Run one subprocess with a hard 45 s timeout; never suppress failures."""
    rec = {"cmd": [str(x) for x in cmd], "rc": None, "timed_out": False,
           "stdout_bytes": b"", "stderr_bytes": b"", "stdout": "", "stderr": ""}
    try:
        proc = subprocess.run(rec["cmd"], input=stdin, cwd=str(cwd), env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=TIMEOUT, check=False)
    except subprocess.TimeoutExpired as exc:
        rec["timed_out"] = True
        rec["stdout_bytes"] = exc.stdout or b""
        rec["stderr_bytes"] = (exc.stderr or b"") + (
            ("TIMEOUT after %d seconds\n" % TIMEOUT).encode())
    else:
        rec["rc"] = proc.returncode
        rec["stdout_bytes"] = proc.stdout
        rec["stderr_bytes"] = proc.stderr
    rec["stdout"] = dec(rec["stdout_bytes"])
    rec["stderr"] = dec(rec["stderr_bytes"])
    return rec


def write_log(path: pathlib.Path, name: str, rec: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "case: %s\ncommand: %s\nrc: %s\ntimed_out: %s\n--- stdout ---\n%s\n"
        "--- stderr ---\n%s\n"
        % (name, " ".join(rec["cmd"]), rec["rc"], rec["timed_out"],
           rec["stdout"], rec["stderr"]), encoding="utf-8")


def clean(rec: dict) -> dict:
    out = dict(rec)
    out.pop("stdout_bytes", None)
    out.pop("stderr_bytes", None)
    return out


def download(case: dict, env: dict) -> tuple[bytes | None, list[str]]:
    dest = CACHE / (case["name"] + ".c")
    if DOWNLOAD or not dest.is_file():
        rec = invoke([CURL, "-fsSL", "--max-time", str(TIMEOUT), "-o", str(dest),
                      case["url"]], env=env, cwd=CACHE)
        write_log(LOGS / (case["name"] + ".download.txt"), case["name"], rec)
        if rec["rc"] != 0 or rec["timed_out"]:
            return None, ["download failed (rc=%s, timed_out=%s): %s: %s"
                          % (rec["rc"], rec["timed_out"], case["url"],
                             rec["stderr"][:400])]
    if not dest.is_file():
        return None, ["download produced no file: " + str(dest)]
    data = dest.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != case["sha256"]:
        return None, ["SHA-256 mismatch: expected %s, got %s"
                      % (case["sha256"], actual)]
    return data, []


def nm_symbols(binary: pathlib.Path, env: dict, name: str) -> tuple[dict, set]:
    rec = invoke([NM, "-g", "--defined-only", str(binary)], env=env, cwd=RESULTS)
    write_log(LOGS / (name + ".nm.txt"), name, rec)
    names = set()
    for line in rec["stdout"].splitlines():
        parts = line.split()
        if len(parts) >= 3:
            sym = parts[-1]
            if sym.startswith("_"):
                sym = sym[1:]  # Mach-O leading underscore
            names.add(sym)
    return rec, names


def classify_rejection(stderr: str) -> tuple[str, str]:
    matched = [reference for pattern, reference in DOCUMENTED_RULES
               if re.search(pattern, stderr, re.IGNORECASE | re.DOTALL)]
    if matched:
        return "SAFE_MODE_RULE", "; ".join(dict.fromkeys(matched))
    return "DEFECT", "diagnostic is not covered by documented Safe mode rules"


def blank_row(case: dict, std: str, mode: str) -> dict:
    return {
        "project": case["project"], "name": case["name"],
        "url": case["url"], "commit": MANIFEST_META["pinned_commit"],
        "license": MANIFEST_META["license"],
        "mode": mode, "std": std, "status": "FAIL", "pass": False,
        "unexpected_failure": True, "reason": "not evaluated",
        "build": None, "build_rc": None, "compiler_stderr": "",
        "run": None, "runtime_rc": None, "runtime_stdout": "",
        "runtime_stderr": "", "manifest_match": None,
        "source_preservation": {"applicable": False, "reason": "LEGACY only"},
        "symbol_set_equal": None, "symbols": None,
        "vs_clang": None, "vs_gcc": None,
    }


def run_reference(case: dict, std: str, ref_id: str, ref_path: str,
                  src: pathlib.Path, env: dict, out: pathlib.Path,
                  reasons: list[str], mandatory: bool) -> dict:
    rec = invoke([ref_path, "-std=" + std, "-O2", str(src), "-o", str(out)],
                 env=env, cwd=RESULTS)
    write_log(LOGS / (case["name"] + "." + std + "." + ref_id + ".build.txt"),
              case["name"], rec)
    entry = {"ref": ref_id, "path": ref_path, "build": clean(rec),
             "build_ok": rec["rc"] == 0 and not rec["timed_out"] and out.is_file(),
             "run": None, "manifest_match": None}
    if entry["build_ok"]:
        run = invoke([str(out)] + [str(x) for x in case.get("args", [])],
                     stdin=case["stdin"].encode(), env=env, cwd=RESULTS)
        write_log(LOGS / (case["name"] + "." + std + "." + ref_id + ".run.txt"),
                  case["name"], run)
        entry["run"] = clean(run)
        entry["manifest_match"] = check_run(run, case, ref_id, reasons)
    elif mandatory:
        fail_reason = "%s: reference build failed (rc=%s): %s" % (
            ref_id, rec["rc"], rec["stderr"][:400])
        reasons.append("reference: " + fail_reason)
    return entry


def check_run(run: dict, case: dict, label: str, reasons: list[str]) -> bool:
    expected_out = case["expected_stdout"].encode()
    ok = True
    if run["timed_out"]:
        reasons.append("%s: runtime timed out after %ds" % (label, TIMEOUT))
        ok = False
    if run["rc"] != case["expected_rc"]:
        reasons.append("%s: exit expected %d, got %s" %
                       (label, case["expected_rc"], run["rc"]))
        ok = False
    if run["stdout_bytes"] != expected_out:
        reasons.append("%s: stdout expected %r, got %r" %
                       (label, case["expected_stdout"], run["stdout"][:800]))
        ok = False
    return ok


def compare_with_references(row: dict, crun: dict, refs: dict,
                            reasons: list[str]) -> None:
    for ref_id in ("clang", "gcc"):
        entry = refs.get(ref_id)
        if not entry or not entry.get("run") or crun["timed_out"] or \
                entry["run"].get("timed_out"):
            continue
        so_eq = crun["stdout"] == entry["run"]["stdout"]
        rc_eq = crun["rc"] == entry["run"]["rc"]
        comparison = {"stdout_equal": so_eq, "exit_equal": rc_eq,
                      "match": so_eq and rc_eq,
                      "cplus_rc": crun["rc"],
                      "reference_rc": entry["run"]["rc"]}
        row["vs_clang" if ref_id == "clang" else "vs_gcc"] = comparison
        if not (so_eq and rc_eq):
            reasons.append("c+ vs %s: stdout_equal=%s exit_equal=%s "
                           "(c+ stdout %r, reference stdout %r)"
                           % (ref_id, so_eq, rc_eq,
                              crun["stdout"][:400],
                              entry["run"]["stdout"][:400]))


def check_symbols(row: dict, binary: pathlib.Path, baseline: set,
                  ref_id: str, env: dict, reasons: list[str]) -> None:
    nmrec, syms = nm_symbols(binary, env,
                             "%s.%s.%s" % (row["name"], row["std"], row["mode"].lstrip("-")))
    if nmrec["rc"] != 0 or nmrec["timed_out"]:
        reasons.append("nm failed (rc=%s, timed_out=%s): %s" %
                       (nmrec["rc"], nmrec["timed_out"], nmrec["stderr"][:300]))
        row["symbol_set_equal"] = False
        return
    app_syms = {s for s in syms if not RUNTIME_SYMBOL.match(s)}
    app_baseline = {s for s in baseline if not RUNTIME_SYMBOL.match(s)}
    row["symbols"] = {
        "cplus": sorted(syms), "clang": sorted(baseline),
        "cplus_app": sorted(app_syms), "clang_app": sorted(app_baseline),
        "runtime_only": sorted(syms - baseline),
        "missing_from_cplus": sorted(baseline - syms),
    }
    exact = syms == baseline
    app_equal = app_syms == app_baseline
    row["symbol_set_equal"] = exact
    if row["mode"] == "--legacy-c":
        if not exact:
            reasons.append("LEGACY exported symbol mismatch vs %s: "
                           "missing=%s extra=%s"
                           % (ref_id, sorted(baseline - syms),
                              sorted(syms - baseline)))
    else:
        row["symbols"]["app_symbol_set_equal"] = app_equal
        row["symbols"]["documentation"] = (
            "docs/compiler/compatibility-matrix.md: Safe mode injects native "
            "guards and runtime side-table registration; cplus_* globals are "
            "expected in safe-mode binaries")
        if not app_equal:
            reasons.append("safe-mode application exported symbols differ from "
                           "%s: missing=%s extra=%s"
                           % (ref_id, sorted(app_baseline - app_syms),
                              sorted(app_syms - app_baseline)))


def check_source(row: dict, src_bytes: bytes, generated: pathlib.Path,
                 reasons: list[str]) -> None:
    info = {"applicable": True, "checked": generated.is_file(),
            "has_leading_line_directive": False, "byte_identical_after_line": False,
            "generated_bytes": generated.stat().st_size if generated.is_file() else 0,
            "original_bytes": len(src_bytes)}
    row["source_preservation"] = info
    if not generated.is_file():
        reasons.append("--emit-c produced no file: %s" % generated)
        return
    generated_bytes = generated.read_bytes()
    match = LEADING_LINE.match(generated_bytes)
    info["has_leading_line_directive"] = bool(match)
    if match:
        rest = generated_bytes[match.end():]
        info["byte_identical_after_line"] = rest == src_bytes
    if not info["has_leading_line_directive"]:
        reasons.append("source preservation: emitted C lacks a leading #line directive")
    if not info["byte_identical_after_line"]:
        rest = generated_bytes[match.end():] if match else generated_bytes
        at = next((i for i, (a, b) in enumerate(zip(rest, src_bytes)) if a != b),
                  min(len(rest), len(src_bytes)))
        reasons.append("source preservation: emitted C differs at byte %d "
                       "(generated %d bytes, original %d bytes)"
                       % (at, len(generated_bytes), len(src_bytes)))
        (GEN / (row["name"] + "." + row["std"] + ".diff.txt")).write_text(
            "generated-after-#line:\n%s\n\noriginal:\n%s\n"
            % (dec(rest), dec(src_bytes)), encoding="utf-8")


def build_cplus(row: dict, case: dict, std: str, src: pathlib.Path,
                generated: pathlib.Path | None, env: dict, reasons: list[str]) -> tuple[dict, pathlib.Path]:
    binary = BINS / ("%s.%s.%s" % (case["name"], std, row["mode"].lstrip("-")))
    binary.unlink(missing_ok=True)
    if generated is not None:
        generated.unlink(missing_ok=True)
    cmd = [CPLUS, "build", row["mode"], "--sanitize=none"]
    if generated is not None:
        cmd += ["--emit-c", str(generated)]
    cmd += [str(src), "-o", str(binary), "--", "-std=" + std]
    rec = invoke(cmd, env=env, cwd=RESULTS)
    write_log(LOGS / ("%s.%s.%s.build.txt" %
                      (case["name"], std, row["mode"].lstrip("-"))),
              case["name"], rec)
    row["build"] = clean(rec)
    row["build_rc"] = rec["rc"]
    row["compiler_stderr"] = rec["stderr"]
    ok = rec["rc"] == 0 and not rec["timed_out"] and binary.is_file()
    if rec["timed_out"]:
        reasons.append("c+ %s build timed out after %ds" % (row["mode"], TIMEOUT))
    elif rec["rc"] != 0:
        if row["mode"] == "--legacy-c":
            reasons.append("c+ LEGACY build failed (rc=%s): %s"
                           % (rec["rc"], rec["stderr"][:600]))
        else:
            classification, reference = classify_rejection(rec["stderr"])
            row["rejection_class"] = classification
            row["rejection_documentation"] = reference
            row["rejection_diagnostic"] = rec["stderr"]
    elif not binary.is_file():
        reasons.append("c+ %s build reported success but produced no binary"
                       % row["mode"])
        ok = False
    return rec, binary if ok else None


def run_case(case: dict, env: dict, manifest: dict) -> list[dict]:
    name = case["name"]
    rows: list[dict] = []
    data, errors = download(case, env)
    if data is None:
        for std in manifest["standards"]:
            for mode in manifest["modes"]:
                row = blank_row(case, std, mode)
                for e in errors:
                    fail(row, [], e, "FAIL")
                rows.append(row)
        return rows
    src = (CACHE / (name + ".c")).resolve()
    supported = set(case["standard_support"])
    baseline_syms: set = set()
    refs: dict = {}

    for std in manifest["standards"]:
        clang_bin = BINS / ("%s.%s.clang" % (name, std))
        gcc_bin = BINS / ("%s.%s.gcc" % (name, std))
        clang_bin.unlink(missing_ok=True)
        gcc_bin.unlink(missing_ok=True)
        ref_failures: list[str] = []
        refs[std] = {}
        refs[std]["clang"] = run_reference(case, std, "clang", CLANG, src,
                                           env, clang_bin, ref_failures, True)
        primary_ok = refs[std]["clang"]["build_ok"]
        gcc = pathlib.Path(GCC)
        if gcc.is_file():
            refs[std]["gcc"] = run_reference(case, std, "gcc", str(gcc), src,
                                             env, gcc_bin, ref_failures, False)
        clang_rec = refs[std]["clang"]
        baseline_syms = set()
        if primary_ok:
            nmrec, baseline_syms = nm_symbols(
                clang_bin, env, "%s.%s.clang" % (name, std))
            if nmrec["rc"] != 0:
                ref_failures.append("clang nm failed (rc=%s): %s"
                                    % (nmrec["rc"], nmrec["stderr"][:300]))
            if std not in supported:
                ref_failures.append("manifest standard_support is stale: clang "
                                    "accepted -std=%s for %s" % (std, name))

        for mode in manifest["modes"]:
            row = blank_row(case, std, mode)
            row["standard_support"] = sorted(supported)
            row["reference_builds"] = {
                k: {"rc": v["build"]["rc"], "stderr": v["build"]["stderr"],
                    "build_ok": v["build_ok"], "manifest_match": v["manifest_match"]}
                for k, v in refs[std].items()}
            reasons = list(ref_failures)
            if not primary_ok:
                row["reason"] = ("REFERENCE_REJECTED: clang cannot build this "
                                 "source with -std=%s; recorded diagnostics are "
                                 "in reference_builds" % std)
                row["status"] = "REFERENCE_REJECTED"
                row["pass"] = True
                row["unexpected_failure"] = False
                row["compiler_stderr"] = clang_rec["build"]["stderr"]
                row["build_rc"] = clang_rec["build"]["rc"]
                rows.append(row)
                continue

            generated = (GEN / ("%s.%s.c" % (name, std))) \
                if mode == "--legacy-c" else None
            build_rec, binary = build_cplus(row, case, std, src, generated,
                                            env, reasons)
            if binary is None:
                if row["status"] == "FAIL" and reasons:
                    row["reason"] = "; ".join(dict.fromkeys(reasons))
                    rows.append(row)
                    continue
                classification = row.get("rejection_class", "DEFECT")
                row["status"] = classification
                row["pass"] = classification == "SAFE_MODE_RULE"
                row["unexpected_failure"] = classification == "DEFECT"
                row["reason"] = "%s: %s" % (
                    classification,
                    row.get("rejection_documentation", "undocumented rejection"))
                if classification == "DEFECT":
                    row["failures"] = [row["reason"] +
                                       "; diagnostic: " +
                                       build_rec["stderr"][:600]]
                rows.append(row)
                continue

            if mode == "--legacy-c":
                check_source(row, data, generated, reasons)

            run = invoke([str(binary)] + [str(x) for x in case.get("args", [])],
                         stdin=case["stdin"].encode(), env=env, cwd=RESULTS)
            write_log(LOGS / ("%s.%s.%s.run.txt" %
                              (name, std, mode.lstrip("-"))), name, run)
            row["run"] = clean(run)
            row["runtime_rc"] = run["rc"]
            row["runtime_stdout"] = run["stdout"]
            row["runtime_stderr"] = run["stderr"]
            row["manifest_match"] = check_run(run, case, "c+ " + mode, reasons)
            if run["stderr"] and not row["manifest_match"]:
                reasons.append("c+ %s runtime stderr: %s"
                               % (mode, run["stderr"][:400].strip()))
            compare_with_references(row, run, refs[std], reasons)
            if mode != "--legacy-c" and reasons:
                # A valid program must behave identically under the Safe modes.
                # A runtime trap here is a false positive, i.e. a defect.
                row["status"] = "DEFECT"
                row["unexpected_failure"] = True
                row["pass"] = False
            check_symbols(row, binary, baseline_syms, "clang", env, reasons)
            if reasons:
                row.setdefault("failures", []).extend(reasons)
                row["reason"] = "; ".join(dict.fromkeys(reasons))
                if not row["unexpected_failure"]:
                    row["status"] = "FAIL"
                    row["unexpected_failure"] = True
                    row["pass"] = False
            else:
                row["status"] = "PASS"
                row["pass"] = True
                row["unexpected_failure"] = False
                row["reason"] = ("stdout and exit match manifest, clang and gcc; "
                                 "symbols and source preservation verified")
                if mode != "--legacy-c":
                    row["reason"] += ("; full symbol set differs only by documented "
                                      "cplus_* runtime exports")
            rows.append(row)
    return rows


def render_text(summary: dict) -> str:
    c = summary["counts"]
    lines = [
        "Pinned real C apps/games: c+ LEGACY/DEFAULT/STRICT vs clang and gcc-14",
        "=" * 78,
        "generated : %s" % summary["generated_utc"],
        "host      : %s" % summary["host"],
        "compiler  : %s (mtime %s)" % (summary["cplus"],
                                      summary["cplus_mtime_utc"]),
        "clang     : %s" % summary["clang"],
        "gcc-14    : %s (%s)" % (summary["gcc"],
                                "available" if summary["gcc_available"] else "absent"),
        "nm        : %s" % summary["nm"],
        "upstream  : %s @ %s" % (summary["upstream"]["url"],
                                summary["upstream"]["pinned_commit"]),
        "license   : %s" % summary["upstream"]["license"],
        "",
        "counts: cases=%d configs=%d pass=%d safe_mode_rule=%d "
        "reference_rejected=%d failed=%d mismatch_entries=%d"
        % (c["cases"], c["configs"], c["pass"], c["safe_mode_rule"],
           c["reference_rejected"], c["failed"], c["mismatch_entries"]),
        "source preservation: %d/%d LEGACY configs byte-identical after #line"
        % (c["source_preservation_passed"], c["legacy_configs"]),
        "symbols: LEGACY exact %d/%d; safe-mode application symbols %d/%d"
        % (c["legacy_symbol_equal"], c["legacy_configs"],
           c["safe_app_symbol_equal"], c["safe_symbol_checked"]),
        "strict rejections: %d (SAFE_MODE_RULE=%d DEFECT=%d)"
        % (c["strict_rejections"], c["strict_safe_mode_rules"],
           c["strict_defects"]),
        "",
        "per-config:",
    ]
    for r in summary["results"]:
        lines.append("  %-22s %-5s %-11s %-19s pass=%-5s %s"
                     % (r["name"], r["std"], r["mode"], r["status"],
                        str(r["pass"]), r["reason"][:160]))
    lines += ["", "mismatches (%d):" % c["mismatch_entries"]]
    if summary["mismatches"]:
        for m in summary["mismatches"]:
            lines.append("  [%s/%s/%s] %s"
                         % (m["project"], m["std"], m["mode"], m["reason"]))
    else:
        lines.append("  none")
    return "\n".join(lines) + "\n"


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        print("unsupported manifest schema", file=sys.stderr)
        return 2
    if len(manifest["cases"]) < 8:
        print("manifest must contain at least 8 cases", file=sys.stderr)
        return 2
    for path in (CPLUS, CLANG, NM):
        if not pathlib.Path(path).is_file():
            print("required binary not found: " + path, file=sys.stderr)
            return 2
    gcc_available = pathlib.Path(GCC).is_file()
    if not gcc_available:
        print("optional reference gcc-14 not found: " + GCC, file=sys.stderr)

    for d in (CACHE, RESULTS, LOGS, BINS, GEN):
        d.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.update({"TZ": "UTC", "LC_ALL": "C", "LANG": "C"})
    env.pop("CPLUS_CLANG", None)  # prevent backend skew in c+ builds

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        grouped = list(pool.map(lambda c: run_case(c, env, manifest),
                                manifest["cases"]))
    results = [r for group in grouped for r in group]
    results.sort(key=lambda r: (r["name"], r["std"], r["mode"]))
    for row in results:
        for ref in row.get("reference_builds", {}).values():
            if "stderr" in ref and len(ref["stderr"]) > 4000:
                ref["stderr"] = ref["stderr"][:4000] + "\n... truncated in summary"

    mismatches = [{"project": r["name"], "std": r["std"], "mode": r["mode"],
                   "status": r["status"], "reason": f}
                  for r in results for f in r.get("failures", [])]
    legacy = [r for r in results if r["mode"] == "--legacy-c"]
    safe = [r for r in results if r["mode"] != "--legacy-c"]
    strict = [r for r in results if r["mode"] == "--strict"]
    built_legacy = [r for r in legacy if r["status"] != "REFERENCE_REJECTED"]
    built_safe = [r for r in safe if r["status"] != "REFERENCE_REJECTED"]
    symbol_checked_safe = [r for r in built_safe if r["symbols"] is not None]
    status_count = lambda rs, s: sum(1 for r in rs if r["status"] == s)
    counts = {
        "cases": len(manifest["cases"]),
        "configs": len(results),
        "pass": status_count(results, "PASS"),
        "safe_mode_rule": status_count(results, "SAFE_MODE_RULE"),
        "reference_rejected": status_count(results, "REFERENCE_REJECTED"),
        "failed": status_count(results, "FAIL") + status_count(results, "DEFECT"),
        "mismatch_entries": len(mismatches),
        "legacy_configs": len(built_legacy),
        "safe_configs": len(built_safe),
        "safe_symbol_checked": len(symbol_checked_safe),
        "source_preservation_passed": sum(
            1 for r in built_legacy
            if r["source_preservation"].get("byte_identical_after_line")),
        "legacy_symbol_equal": sum(
            1 for r in built_legacy if r["symbol_set_equal"] is True),
        "safe_app_symbol_equal": sum(
            1 for r in symbol_checked_safe
            if r["symbols"].get("app_symbol_set_equal") is True),
        "strict_rejections": sum(1 for r in strict if r["status"] in
                                 ("SAFE_MODE_RULE", "DEFECT")),
        "strict_safe_mode_rules": status_count(strict, "SAFE_MODE_RULE"),
        "strict_defects": status_count(strict, "DEFECT"),
    }
    summary = {
        "suite": manifest["suite"],
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(),
        "cplus": CPLUS,
        "cplus_mtime_utc": datetime.datetime.fromtimestamp(
            pathlib.Path(CPLUS).stat().st_mtime,
            datetime.timezone.utc).isoformat(),
        "clang": CLANG, "gcc": GCC,
        "gcc_available": gcc_available, "nm": NM,
        "timeout_sec": TIMEOUT, "workers": WORKERS,
        "upstream": manifest["upstream"],
        "standards": manifest["standards"], "modes": manifest["modes"],
        "sanitize": manifest["sanitize"],
        "counts": counts, "results": results, "mismatches": mismatches,
    }
    SUMMARY_JSON.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    SUMMARY_TXT.write_text(render_text(summary), encoding="utf-8")

    print("real_apps: %d cases, %d configs: pass=%d SAFE_MODE_RULE=%d "
          "REFERENCE_REJECTED=%d failed=%d mismatches=%d"
          % (counts["cases"], counts["configs"], counts["pass"],
             counts["safe_mode_rule"], counts["reference_rejected"],
             counts["failed"], counts["mismatch_entries"]))
    print("source preservation %d/%d; legacy symbols %d/%d; safe app symbols %d/%d"
          % (counts["source_preservation_passed"], counts["legacy_configs"],
             counts["legacy_symbol_equal"], counts["legacy_configs"],
             counts["safe_app_symbol_equal"], counts["safe_symbol_checked"]))
    print("strict rejections: %d (SAFE_MODE_RULE=%d DEFECT=%d)"
          % (counts["strict_rejections"], counts["strict_safe_mode_rules"],
             counts["strict_defects"]))
    for m in mismatches:
        print("MISMATCH [%s/%s/%s] %s" % (m["project"], m["std"], m["mode"],
                                          m["reason"]))
    print("summaries: %s, %s" % (SUMMARY_TXT, SUMMARY_JSON))
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
