#!/usr/bin/env python3
"""Permanent full-coverage runner for previously BLOCKED / orphaned fixtures.

Owner: tests/compiler/full_coverage/ ONLY (no edits elsewhere, no rebuild).

Discover fixtures from the inventory locations (tests/safety/* plus the
sub-corpora under tests/safety/ and tests/known_gaps/), derive an expectation
from adjacent artifacts (.err/.stderr/.exit, embedded // EXPECT / // DIAG,
filename conventions) and execute every fixture in:

  * DEFAULT check        : c+ check --default  --sanitize=none <src>
  * STRICT  check        : c+ check --strict   --sanitize=none <src>
  * STRICT  build + run  : c+ build --strict   --sanitize=none <src> -o <exe>
                           then <exe>  (stdin=/dev/null, hard 30s timeout)

For the sanitizer/ and runtime_crashes/ corpora also add ASan and UBSan
configs using CPLUS_CLANG (default /opt/homebrew/opt/llvm/bin/clang) and
-- -isysroot $(xcrun --show-sdk-path). Toolchain failures are recorded
distinctly as TOOLCHAIN_UNAVAILABLE.

Classification (per fixture):
  PASS                    - an expectation existed and every check held
  FAIL                    - an expectation existed and a check diverged
  OBSERVED                - no expectation; actual outcomes recorded verbatim
  TOOLCHAIN_UNAVAILABLE   - a required config could not run (clang/SDK/c+
                            missing or a toolchain-level error signature)

The process exits 1 only for FAIL. OBSERVED / TOOLCHAIN_UNAVAILABLE cases are
listed explicitly with counts in summary.txt and summary.json.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent  # tests/compiler/full_coverage -> repo root

CPLUS = os.environ.get("CPLUS_BIN") or str(ROOT / "build" / "c+")
TIMEOUT = 30
CLANG = os.environ.get("CPLUS_CLANG") or "/opt/homebrew/opt/llvm/bin/clang"
CAP = 4000  # exact prefix length stored in the JSON record

OUTDIR = HERE / "out"
LOGDIR = HERE / "logs"
SUMMARY_TXT = HERE / "summary.txt"
SUMMARY_JSON = HERE / "summary.json"
RECORDS_JSONL = HERE / ".records.jsonl"

# (category, candidate directories relative to ROOT, top_only)
# Both the literal paths from the task and the real on-disk locations under
# tests/safety/ are probed so discovery does not depend on layout drift.
CATEGORY_DIRS = [
    ("safety", ["tests/safety"], True),
    ("p0new", ["tests/p0new", "tests/safety/p0new"], False),
    ("sanitizer", ["tests/sanitizer", "tests/safety/sanitizer"], False),
    ("runtime_crashes", ["tests/runtime_crashes", "tests/safety/runtime_crashes"], False),
    ("rust_compare_c", ["tests/rust_compare/c", "tests/safety/rust_compare/c"], False),
    ("fnptr_bounds", ["tests/fnptr_bounds", "tests/safety/fnptr_bounds"], False),
    ("integration", ["tests/integration", "tests/safety/integration"], False),
    ("known_gaps", ["tests/known_gaps"], False),
]

NEEDS_SANITIZERS = {"sanitizer", "runtime_crashes"}

# Toolchain-level error signatures (not source diagnostics).
TOOLCHAIN_SIGNATURES = [
    "xcrun: error",
    "unable to execute command",
    "ld: library not found for",
    "ld: can't locate or load",
    "sdk does not contain",
    "fatal error: 'stddef.h' file not found",
    "fatal error: 'stdio.h' file not found",
    "error: invalid argument '-isysroot'",
    "error: no such file or directory",
    "error: unable to find utility",
]

MAIN_RE = re.compile(r"\bint\s+main\s*\(")
# Use horizontal whitespace only: `\s` would swallow the newline after an
# empty `// DIAG:` marker and capture the following source line instead.
EXPECT_RE = re.compile(r"//[ \t]*EXPECT:[ \t]*(accept|reject)", re.I)
DIAG_RE = re.compile(r"//[ \t]*DIAG:[ \t]*(.*)")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def sdk_path() -> str:
    try:
        out = subprocess.run(
            ["xcrun", "--show-sdk-path"],
            capture_output=True, text=True, timeout=10,
        )
        if out.returncode == 0:
            return out.stdout.strip()
    except Exception:
        pass
    return ""


def cap(text: str) -> str:
    return text[:CAP]


def toolchain_sig(combined: str) -> str | None:
    low = combined.lower()
    for sig in TOOLCHAIN_SIGNATURES:
        if sig in low:
            return sig
    return None


def run_cmd(argv, env=None, timeout=TIMEOUT, stdin_null=True, cwd=None,
            record_env=None):
    """Run one command with a hard timeout; never raise on process failure.

    `record_env` is the sanitized subset written to the evidence summary. The
    full child environment must never be serialized: it contains API keys and
    other machine secrets.
    """
    rec = {
        "argv": argv,
        "env_overrides": record_env if record_env is not None else {},
        "timeout_sec": timeout,
        "rc": None,
        "timed_out": False,
        "spawn_error": None,
        "stdout_head": "",
        "stderr_head": "",
        "stdout_len": 0,
        "stderr_len": 0,
        "toolchain_unavailable": False,
        "toolchain_reason": None,
    }
    if not argv or not Path(argv[0]).exists() and shutil.which(argv[0]) is None:
        msg = "executable not found: %s" % argv[0]
        rec["spawn_error"] = msg
        rec["toolchain_unavailable"] = True
        rec["toolchain_reason"] = msg
        return rec
    try:
        p = subprocess.run(
            argv,
            env=env,
            cwd=cwd,
            stdin=subprocess.DEVNULL if stdin_null else None,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout,
        )
        rec["rc"] = p.returncode
        rec["stdout_head"] = cap(p.stdout)
        rec["stderr_head"] = cap(p.stderr)
        rec["stdout_len"] = len(p.stdout)
        rec["stderr_len"] = len(p.stderr)
        sig = toolchain_sig(p.stdout + "\n" + p.stderr)
        if sig:
            rec["toolchain_unavailable"] = True
            rec["toolchain_reason"] = "toolchain error signature: %s" % sig
    except subprocess.TimeoutExpired as e:
        rec["timed_out"] = True
        so = e.stdout or b""
        se = e.stderr or b""
        if isinstance(so, str):
            so = so.encode("utf-8", "replace")
        if isinstance(se, str):
            se = se.encode("utf-8", "replace")
        rec["stdout_head"] = cap(so.decode("utf-8", "replace"))
        rec["stderr_head"] = cap(se.decode("utf-8", "replace"))
        rec["stdout_len"] = len(so)
        rec["stderr_len"] = len(se)
    except OSError as e:
        rec["spawn_error"] = str(e)
        rec["toolchain_unavailable"] = True
        rec["toolchain_reason"] = "spawn failed: %s" % e
    return rec


def combined(rec) -> str:
    return rec["stdout_head"] + "\n" + rec["stderr_head"]


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------
def discover():
    found = {}  # absolute path str -> {rel, category, dir}
    for category, candidates, top_only in CATEGORY_DIRS:
        for rel_dir in candidates:
            d = ROOT / rel_dir
            if not d.is_dir():
                continue
            if top_only:
                files = [p for p in d.iterdir() if p.is_file()]
            elif category == "known_gaps":
                files = []
                for p in sorted(d.iterdir()):
                    if p.is_file():
                        files.append(p)
                    elif p.is_dir():
                        fixtures = [q for q in p.iterdir()
                                    if q.is_file() and q.suffix in (".c", ".cp")]
                        # Exclude subdirs with their own README unless fixtures
                        # are actually present.
                        if fixtures:
                            files.extend(fixtures)
                files = [p for p in files if p.suffix in (".c", ".cp")]
            else:
                files = [p for p in d.iterdir()
                         if p.is_file() and p.suffix in (".c", ".cp")]
            for p in files:
                if p.suffix not in (".c", ".cp"):
                    continue
                key = str(p.resolve())
                if key not in found:
                    found[key] = {
                        "path": p,
                        "rel": str(p.relative_to(ROOT)),
                        "category": category,
                        "dir": str(p.parent),
                    }
    return sorted(found.values(), key=lambda x: (x["category"], x["rel"]))


# ---------------------------------------------------------------------------
# Expectation derivation
# ---------------------------------------------------------------------------
def derive_expectation(fx):
    path = fx["path"]
    base = path.with_suffix("")
    sources = []
    expect = {
        "compile": None,
        "diag": None,
        "err_diag": None,
        "exit_code": None,
        "stderr_substr": None,
        "crash": False,
    }

    text = ""
    try:
        text = path.read_text(errors="replace")
    except OSError:
        pass

    m = EXPECT_RE.search(text)
    if m:
        expect["compile"] = m.group(1).lower()
        sources.append("embedded // EXPECT: %s" % expect["compile"])
    m = DIAG_RE.search(text)
    if m:
        d = m.group(1).strip()
        if d:
            expect["diag"] = d
            sources.append("embedded // DIAG: %s" % d)

    if base.with_suffix(".err").is_file():
        expect["err_diag"] = base.with_suffix(".err").read_text(
            errors="replace").strip()
        sources.append(".err file: %s" % expect["err_diag"])

    if base.with_suffix(".exit").is_file():
        raw = base.with_suffix(".exit").read_text(errors="replace").strip()
        try:
            expect["exit_code"] = int(raw, 0)
            sources.append(".exit file: %d" % expect["exit_code"])
        except ValueError:
            sources.append(".exit file (unparseable): %r" % raw)

    if base.with_suffix(".stderr").is_file():
        expect["stderr_substr"] = base.with_suffix(".stderr").read_text(
            errors="replace").strip()
        sources.append(".stderr file: %s" % expect["stderr_substr"])

    name = path.stem.lower()
    if expect["compile"] is None and expect["err_diag"] is None:
        if ("expect_reject" in name or "static_reject" in name
                or name.endswith("_reject") or name.startswith("reject_")):
            expect["compile"] = "reject"
            sources.append("filename convention: reject")
        elif ("expect_accept" in name or name.endswith("_ok")
              or name.startswith("safe") or name == "safe_indices"):
            expect["compile"] = "accept"
            sources.append("filename convention: accept/safe")
    if "trap" in name or "crash" in name:
        sources.append("filename convention: runtime trap/crash")
        expect["crash"] = True

    # tests/known_gaps/ carries a README declaring these .err signatures as
    # *known absent* diagnostics ("not run as passing tests"). Treat them as
    # documented-gap evidence, never as live expectations that can FAIL.
    documented_gap = False
    p = path.parent
    while p != ROOT.parent:
        if "known_gaps" in p.parts and (p / "README.md").is_file():
            documented_gap = True
            break
        if p == ROOT:
            break
        p = p.parent
    expect["documented_gap"] = documented_gap
    if documented_gap:
        sources.append("documented gap (README.md): adjacent .err is a "
                       "known-absent diagnostic, recorded as OBSERVED")

    expect["source"] = "; ".join(sources) if sources else "none (no adjacent expectation artifact)"
    expect["has_expectation"] = bool(sources)
    return expect


# ---------------------------------------------------------------------------
# Execution + evaluation
# ---------------------------------------------------------------------------
def build_argv(src, out, strict=True, sanitizer=None, extra_sources=()):
    argv = [CPLUS, "build", "--strict" if strict else "--default",
            "--sanitize=%s" % (sanitizer or "none"), src]
    argv.extend(extra_sources)
    argv.extend(["-o", str(out)])
    if sanitizer:
        argv.extend(["--", "-isysroot", SDK])
    return argv


def execute_fixture(fx, expect):
    rel = fx["rel"]
    src = str(fx["path"])
    category = fx["category"]
    stem = re.sub(r"[^A-Za-z0-9_.-]", "_", Path(rel).name)
    exe = OUTDIR / (stem + ".exe")
    commands = {}

    checks = []

    # 1. DEFAULT check
    commands["check_default"] = run_cmd(
        [CPLUS, "check", "--default", "--sanitize=none", src])

    # 2. STRICT check
    strict_check = run_cmd(
        [CPLUS, "check", "--strict", "--sanitize=none", src])
    commands["check_strict"] = strict_check

    # Multi-TU integration: link the sibling non-main source(s).
    extra = []
    if category == "integration":
        for p in sorted(Path(fx["dir"]).iterdir()):
            if p.suffix in (".c", ".cp") and p.resolve() != fx["path"].resolve():
                try:
                    if not MAIN_RE.search(p.read_text(errors="replace")):
                        extra.append(str(p))
                except OSError:
                    pass

    # 3. STRICT build + run
    if exe.exists():
        exe.unlink()
    b = run_cmd(build_argv(src, exe, strict=True, extra_sources=extra))
    commands["build_strict"] = b
    ran = False
    if exe.exists():
        rt = run_cmd([str(exe)], stdin_null=True)
        commands["runtime_strict"] = rt
        ran = True
    else:
        commands["runtime_strict"] = {
            "argv": [str(exe)], "env_overrides": {}, "timeout_sec": TIMEOUT,
            "rc": None, "timed_out": False,
            "spawn_error": "not executed: build produced no executable",
            "stdout_head": "", "stderr_head": "", "stdout_len": 0,
            "stderr_len": 0, "toolchain_unavailable": False,
            "toolchain_reason": None,
        }

    # 4. Sanitizer configs for sanitizer/ and runtime_crashes/ corpora.
    if category in NEEDS_SANITIZERS:
        for san, tag in (("address", "asan"), ("undefined", "ubsan")):
            s_exe = OUTDIR / (stem + "." + tag + ".exe")
            if s_exe.exists():
                s_exe.unlink()
            env = dict(os.environ)
            env["CPLUS_CLANG"] = CLANG
            sbuild = run_cmd(build_argv(src, s_exe, strict=True,
                                        sanitizer=san, extra_sources=extra),
                             env=env, record_env={"CPLUS_CLANG": CLANG})
            commands["build_" + tag] = sbuild
            if s_exe.exists():
                commands["runtime_" + tag] = run_cmd([str(s_exe)], stdin_null=True)
            else:
                commands["runtime_" + tag] = {
                    "argv": [str(s_exe)], "env_overrides": {}, "timeout_sec": TIMEOUT,
                    "rc": None, "timed_out": False,
                    "spawn_error": "not executed: sanitizer build produced no executable",
                    "stdout_head": "", "stderr_head": "", "stdout_len": 0,
                    "stderr_len": 0, "toolchain_unavailable": False,
                    "toolchain_reason": None,
                }

    # --- expectation checks -------------------------------------------------
    if expect["compile"] in ("accept", "reject"):
        want = expect["compile"]
        rc = strict_check["rc"]
        if want == "accept":
            checks.append(("STRICT check accepts (rc=0)", rc == 0,
                           "rc=%s" % rc))
        else:
            checks.append(("STRICT check rejects (rc!=0)", rc is not None and rc != 0,
                           "rc=%s" % rc))
        if expect["diag"]:
            ok = expect["diag"] in combined(strict_check)
            checks.append(("embedded DIAG substring present", ok,
                           repr(expect["diag"])))

    if expect["err_diag"]:
        rc = strict_check["rc"]
        checks.append(("STRICT check rejects (.err)", rc is not None and rc != 0,
                       "rc=%s" % rc))
        ok = expect["err_diag"] in combined(strict_check)
        checks.append((".err diagnostic substring present", ok,
                       repr(expect["err_diag"])))

    rt = commands["runtime_strict"]
    if expect["exit_code"] is not None:
        rc = rt["rc"]
        ok = (rc == expect["exit_code"])
        # Platform-specific abort allowance mirrors tests/run_tests.sh.
        if (not ok and expect["stderr_substr"] and rc not in (None, 0)
                and expect["stderr_substr"] in combined(rt)):
            ok = True
        checks.append(("runtime exit code matches", ok,
                       "want=%s got=%s" % (expect["exit_code"], rc)))

    if expect["stderr_substr"]:
        ok = expect["stderr_substr"] in rt["stderr_head"]
        checks.append(("runtime stderr substring present", ok,
                       repr(expect["stderr_substr"])))

    if expect["crash"]:
        ok = rt["timed_out"] or (rt["rc"] not in (None, 0))
        checks.append(("runtime crashes/nonzero exit", ok,
                       "rc=%s timeout=%s" % (rt["rc"], rt["timed_out"])))

    # --- classification -----------------------------------------------------
    toolchain_reasons = []
    for cname, crec in commands.items():
        if crec.get("toolchain_unavailable"):
            toolchain_reasons.append("%s: %s" % (
                cname, crec.get("toolchain_reason") or "unavailable"))

    failed = [c for c in checks if not c[1]]
    if expect.get("documented_gap"):
        # Divergence here is expected and documented; never a FAIL.
        classification = ("TOOLCHAIN_UNAVAILABLE" if toolchain_reasons
                          else "OBSERVED")
    elif failed:
        classification = "FAIL"
    elif toolchain_reasons:
        classification = "TOOLCHAIN_UNAVAILABLE"
    elif expect["has_expectation"]:
        classification = "PASS"
    else:
        classification = "OBSERVED"

    reasons = ["%s (%s)%s" % (desc, detail, " [OK]" if ok else " [DIVERGENT]")
               for desc, ok, detail in checks]
    reasons.extend(toolchain_reasons)
    if expect.get("documented_gap"):
        reasons.append("documented-gap corpus: divergent .err checks above are "
                       "expected/absent, classification forced to OBSERVED")
    if not reasons:
        reasons.append("no adjacent expectation; outcomes recorded as observed")

    return {
        "path": rel,
        "category": category,
        "expectation_source": expect["source"],
        "expectation": {k: v for k, v in expect.items()
                        if k not in ("source", "has_expectation")},
        "has_expectation": expect["has_expectation"],
        "main": bool(MAIN_RE.search(text_of(fx))),
        "commands": commands,
        "checks": [{"description": d, "ok": ok, "detail": det}
                   for d, ok, det in checks],
        "classification": classification,
        "reasons": reasons,
    }


def text_of(fx):
    try:
        return fx["path"].read_text(errors="replace")
    except OSError:
        return ""


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
def main():
    global SDK
    OUTDIR.mkdir(parents=True, exist_ok=True)
    LOGDIR.mkdir(parents=True, exist_ok=True)

    SDK = sdk_path()
    clang_ok = Path(CLANG).exists()
    if not clang_ok:
        print("WARNING: CPLUS_CLANG %s missing; sanitizer configs report "
              "TOOLCHAIN_UNAVAILABLE" % CLANG, file=sys.stderr)
    if not SDK:
        print("WARNING: xcrun --show-sdk-path failed; sanitizer configs report "
              "TOOLCHAIN_UNAVAILABLE", file=sys.stderr)
    if not Path(CPLUS).exists() and shutil.which(CPLUS) is None:
        print("FATAL: c+ binary not found: %s" % CPLUS, file=sys.stderr)

    fixtures = discover()
    records = []
    for i, fx in enumerate(fixtures, 1):
        expect = derive_expectation(fx)
        rec = execute_fixture(fx, expect)
        records.append(rec)
        # Capped exact-prefix evidence logs for every executed command.
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", rec["path"])
        for cname, crec in rec["commands"].items():
            (LOGDIR / (safe + "." + cname + ".stdout")).write_text(
                crec["stdout_head"] + ("... [full len=%d]" % crec["stdout_len"]
                                       if crec["stdout_len"] > CAP else ""))
            (LOGDIR / (safe + "." + cname + ".stderr")).write_text(
                crec["stderr_head"] + ("... [full len=%d]" % crec["stderr_len"]
                                       if crec["stderr_len"] > CAP else ""))
        if i % 25 == 0:
            print("executed %d/%d" % (i, len(fixtures)), file=sys.stderr)

    counts = {
        "discovered": len(fixtures),
        "executed": sum(1 for r in records if any(
            c.get("rc") is not None or c.get("timed_out") for c in r["commands"].values())),
        "passed": sum(r["classification"] == "PASS" for r in records),
        "failed": sum(r["classification"] == "FAIL" for r in records),
        "observed": sum(r["classification"] == "OBSERVED" for r in records),
        "unavailable": sum(r["classification"] == "TOOLCHAIN_UNAVAILABLE"
                           for r in records),
    }
    by_cat = {}
    for r in records:
        by_cat.setdefault(r["category"], {"total": 0, "PASS": 0, "FAIL": 0,
                                          "OBSERVED": 0, "TOOLCHAIN_UNAVAILABLE": 0})
        by_cat[r["category"]]["total"] += 1
        by_cat[r["category"]][r["classification"]] += 1

    json_doc = {
        "meta": {
            "root": str(ROOT),
            "cplus": CPLUS,
            "timeout_sec": TIMEOUT,
            "clang": CLANG,
            "clang_exists": clang_ok,
            "sdk": SDK,
            "counts": counts,
            "by_category": by_cat,
            "classification_rules": [
                "PASS: expectation existed and all checks held",
                "FAIL: expectation existed and >=1 check diverged (only FAIL sets exit 1)",
                "OBSERVED: no adjacent expectation; actual outcomes recorded verbatim",
                "TOOLCHAIN_UNAVAILABLE: clang/SDK/c+ missing or toolchain error signature",
            ],
        },
        "fixtures": records,
    }
    SUMMARY_JSON.write_text(json.dumps(json_doc, indent=2) + "\n")

    lines = []
    lines.append("FULL-COVERAGE RUNNER (permanent suite)")
    lines.append("c+ = %s   timeout = %ds   clang = %s   sdk = %s" %
                 (CPLUS, TIMEOUT, CLANG, SDK or "<unavailable>"))
    lines.append("")
    lines.append("COUNTS: discovered=%d executed=%d passed=%d failed=%d "
                 "observed=%d unavailable=%d" %
                 (counts["discovered"], counts["executed"], counts["passed"],
                  counts["failed"], counts["observed"], counts["unavailable"]))
    lines.append("")
    lines.append("BY CATEGORY (total / PASS / FAIL / OBSERVED / UNAVAILABLE):")
    for cat in sorted(by_cat):
        c = by_cat[cat]
        lines.append("  %-18s %3d / %3d / %3d / %3d / %3d" %
                     (cat, c["total"], c["PASS"], c["FAIL"],
                      c["OBSERVED"], c["TOOLCHAIN_UNAVAILABLE"]))
    lines.append("")
    for cls in ("FAIL", "TOOLCHAIN_UNAVAILABLE", "PASS", "OBSERVED"):
        subset = [r for r in records if r["classification"] == cls]
        lines.append("=== %s (%d) ===" % (cls, len(subset)))
        for r in subset:
            lines.append("%-22s %s" % (r["category"], r["path"]))
            lines.append("    expectation: %s" % r["expectation_source"])
            lines.append("    reasons    : %s" % " | ".join(r["reasons"]))
            for cname, crec in r["commands"].items():
                lines.append("    cmd %-16s rc=%-4s timeout=%-5s out=[%s] err=[%s]" % (
                    cname, crec["rc"], crec["timed_out"],
                    crec["stdout_head"].replace("\n", "\\n")[:160],
                    crec["stderr_head"].replace("\n", "\\n")[:160]))
        lines.append("")
    lines.append("OBSERVED cases are listed explicitly above (count=%d) and "
                 "are never silently skipped." % counts["observed"])
    SUMMARY_TXT.write_text("\n".join(lines) + "\n")

    print("discovered=%d executed=%d passed=%d failed=%d observed=%d unavailable=%d" %
          (counts["discovered"], counts["executed"], counts["passed"],
           counts["failed"], counts["observed"], counts["unavailable"]))
    fails = [r["path"] for r in records if r["classification"] == "FAIL"]
    if fails:
        print("FAIL cases:")
        for p in fails:
            print("  " + p)
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
