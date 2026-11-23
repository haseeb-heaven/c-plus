#!/usr/bin/env python3
"""PHASE 5 safety matrix runner for the C+ compiler.

Executes every case in cases.json under six real configurations and records
hard evidence (full compiler and runtime stdout/stderr, exit codes, backend,
sanitizer flags) in summary.json and summary.txt.

Statuses (see the per-case oracle fields in cases.json):
  PASS                  - expected static reject with diagnostic, or expected
                          runtime trap with a matching message
  WARN_ONLY             - DEFAULT --sanitize=none accepted the case but emitted
                          the exact expected C+ warning diagnostic; this is a
                          documented WARN-only mode (docs/SAFE_MODE_SPEC.md,
                          docs/ARITHMETIC_SAFETY.md section 3), not a PASS
  KNOWN_OPEN            - documented static gap (docs/compiler/adversarial-wave2.md)
                          or toolchain sanitizer gap; non-failing but listed
  STATIC_ONLY_BY_DESIGN - provable-constant case with a static diagnostic and
                          deliberately no native runtime guard (non-failing)
  FAIL                  - missing required rejection/trap, wrong diagnostic,
                          or timeout; the only status that sets exit code 1

Compiling a bad program alone is never accepted as proof.  Timeouts are FAILs.

Run:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/safety_matrix/run_safety_matrix.py

Optional environment overrides:
  CPLUS_MATRIX_TIMEOUT (default 45), CPLUS_SYSROOT, CPLUS_SAN_CLANG,
  CPLUS_NONE_CLANG (default /usr/bin/clang, Apple clang 16).
"""
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
TIMEOUT = int(os.environ.get("CPLUS_MATRIX_TIMEOUT", "45"))


def first_existing(paths):
    for p in paths:
        if p and os.path.isfile(p):
            return p
    return None


NONE_CLANG = os.environ.get("CPLUS_NONE_CLANG") or first_existing(
    ["/usr/bin/clang"]
)
SAN_CLANG = os.environ.get("CPLUS_SAN_CLANG") or first_existing(
    [
        "/opt/homebrew/opt/llvm/bin/clang",
        "/usr/local/opt/llvm/bin/clang",
        "/usr/bin/clang",
    ]
)


def detect_sysroot():
    env = os.environ.get("CPLUS_SYSROOT")
    if env:
        return env
    try:
        p = subprocess.run(
            ["xcrun", "--show-sdk-path"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=15,
        )
        if p.returncode == 0:
            return p.stdout.decode().strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"


SYSROOT = detect_sysroot()
SAN = "address,undefined"

# Each config records exactly which compiler backend is used.  The sanitizer
# configs need a backend whose ASan/UBSan runtime works on this host; the
# Apple clang 16 system driver fails the driver's own compile-and-run probe,
# so the Homebrew driver is selected explicitly and given the installed SDK.
CONFIGS = [
    dict(id="DEFAULT_NONE", label="DEFAULT --sanitize=none", mode="check",
         flags=["--default", "--sanitize=none"], clang=NONE_CLANG, passthrough=False,
         san="none"),
    dict(id="STRICT_NONE", label="STRICT --sanitize=none", mode="check",
         flags=["--strict", "--sanitize=none"], clang=NONE_CLANG, passthrough=False,
         san="none"),
    dict(id="DEFAULT_SAN", label="DEFAULT --sanitize=address,undefined", mode="run",
         flags=["--default", "--sanitize=" + SAN], clang=SAN_CLANG, passthrough=True,
         san=SAN),
    dict(id="STRICT_SAN", label="STRICT --sanitize=address,undefined", mode="run",
         flags=["--strict", "--sanitize=" + SAN], clang=SAN_CLANG, passthrough=True,
         san=SAN),
    dict(id="DEBUG_NONE", label="--debug --sanitize=none", mode="run",
         flags=["--debug", "--sanitize=none"], clang=NONE_CLANG, passthrough=False,
         san="none"),
    dict(id="RELEASE_NONE", label="--release --sanitize=none", mode="run",
         flags=["--release", "--sanitize=none"], clang=NONE_CLANG, passthrough=False,
         san="none"),
]

TRAP_MARKERS = [
    "C+ runtime error:", "runtime error:", "AddressSanitizer",
    "UndefinedBehaviorSanitizer", "MemorySanitizer", "LeakSanitizer",
]

PASS = "PASS"
WARN_ONLY = "WARN_ONLY"
KNOWN_OPEN = "KNOWN_OPEN"
STATIC_ONLY_BY_DESIGN = "STATIC_ONLY_BY_DESIGN"
FAIL = "FAIL"

# Only FAIL (including timeouts, which are FAILs) makes the runner exit 1.
NON_FAILING = (WARN_ONLY, KNOWN_OPEN, STATIC_ONLY_BY_DESIGN)


def run(cmd, env=None, cwd=ROOT, timeout=TIMEOUT):
    """Run one subprocess with a hard timeout; never swallow failures."""
    start = time.time()
    rec = dict(command=cmd, cplus_clang=(env or {}).get("CPLUS_CLANG", ""),
               exit_code=None, stdout="", stderr="", timed_out=False,
               duration_s=round(time.time() - start, 3))
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, cwd=cwd, env=env)
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


def env_for(config):
    env = dict(os.environ)
    if config["clang"]:
        env["CPLUS_CLANG"] = config["clang"]
    else:
        env.pop("CPLUS_CLANG", None)
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    env["UBSAN_OPTIONS"] = "halt_on_error=1"
    return env


def matches(text, needles):
    low = text.lower()
    return [n for n in needles if n.lower() in low]


def cplus_warning_lines(stderr, needles):
    """Exact C+ warning lines that carry an expected diagnostic marker."""
    return [ln.strip() for ln in stderr.splitlines()
            if "warning:" in ln and matches(ln, needles)]


def expected_for(case, config):
    if case["kind"] == "safe":
        if config["mode"] == "check":
            return "clean static check (accepted, no error)"
        return "clean build + run with exit %d and no trap" % case.get("exit", 0)
    diag = ", ".join(case["diag"])
    trap = ", ".join(case.get("runtime", []))
    if case.get("static_only_by_design"):
        if config["id"] == "DEFAULT_NONE":
            return "exact C+ warning diagnostic (%s) -> WARN_ONLY" % diag
        if config["id"] == "STRICT_NONE":
            return "static rejection with expected diagnostic (%s)" % diag
        if config["san"] == "none":
            return ("STATIC_ONLY_BY_DESIGN: exact C+ warning (%s), no native "
                    "runtime guard for a provable constant" % diag)
        return "static rejection (%s) or sanitizer trap (%s)" % (diag, trap)
    if case.get("known_static_gap"):
        if config["mode"] == "check":
            return "KNOWN_OPEN: %s (runtime trap required in build configs)" % case.get("why", "")
        return "runtime trap with matching diagnostic (%s)" % trap
    if config["mode"] == "check":
        if config["id"] == "DEFAULT_NONE":
            return ("static rejection with diagnostic (%s), or exact C+ warning "
                    "-> WARN_ONLY" % diag)
        return "static rejection with expected diagnostic (%s)" % diag
    return ("static rejection with expected diagnostic (%s) OR runtime trap "
            "with message (%s)" % (diag, trap))


def evaluate(case, config, compile_rec, run_rec):
    """Return (status, actual_str, reason_str, exact_warning_diagnostic)."""
    if compile_rec["timed_out"]:
        return FAIL, "TIMEOUT during compile", "hard timeout exceeded", ""
    cout = compile_rec["stdout"] + "\n" + compile_rec["stderr"]
    rejected = compile_rec["exit_code"] not in (0, None)
    warn_lines = (cplus_warning_lines(compile_rec["stderr"], case.get("diag", []))
                  if case["kind"] == "unsafe" else [])
    warn_diag = "\n".join(warn_lines)

    if case["kind"] == "unsafe":
        diag_hits = matches(cout, case.get("diag", []))
        if rejected:
            if diag_hits:
                return (PASS,
                        "rejected (rc=%s), diagnostic=%s" % (
                            compile_rec["exit_code"], ",".join(diag_hits)),
                        "", warn_diag)
            return (FAIL,
                    "rejected (rc=%s) but diagnostic did not match" % compile_rec["exit_code"],
                    "rejection without expected diagnostic", warn_diag)
        if config["mode"] == "check":
            if warn_lines and config["id"] == "DEFAULT_NONE" \
                    and not case.get("known_static_gap"):
                return (WARN_ONLY,
                        "accepted (rc=0) with exact C+ warning: %s" % warn_diag,
                        "DEFAULT --sanitize=none is WARN-only by design "
                        "(docs/SAFE_MODE_SPEC.md, docs/ARITHMETIC_SAFETY.md section 3)",
                        warn_diag)
            if case.get("known_static_gap"):
                return (KNOWN_OPEN,
                        "accepted by static check (rc=0); runtime trap required in build configs",
                        case.get("why", ""), warn_diag)
            return (FAIL, "accepted by static check (rc=0)",
                    "no safety evidence; compiling alone is never proof", warn_diag)
        if run_rec is None:
            return FAIL, "no runtime evidence recorded", "runtime step missing", warn_diag
        if run_rec["timed_out"]:
            return FAIL, "TIMEOUT during run", "hard timeout exceeded", warn_diag
        rout = run_rec["stdout"] + "\n" + run_rec["stderr"]
        trap_hits = matches(rout, case.get("runtime", []))
        if run_rec["exit_code"] not in (0, None) and trap_hits:
            return (PASS,
                    "runtime trap (rc=%s), message=%s" % (
                        run_rec["exit_code"], ",".join(trap_hits)),
                    "", warn_diag)
        if case.get("static_only_by_design") and warn_lines:
            return (STATIC_ONLY_BY_DESIGN,
                    "accepted (rc=0) with exact C+ warning (no native guard for a "
                    "provable constant): %s" % warn_diag,
                    case.get("why", ""), warn_diag)
        if config["san"] == SAN and case.get("sanitizer_gap"):
            return (KNOWN_OPEN,
                    "ran without matching trap (rc=%s), stdout=%r, stderr=%r" % (
                        run_rec["exit_code"], run_rec["stdout"], run_rec["stderr"]),
                    case.get("why", ""), warn_diag)
        return (FAIL,
                "ran without trap (rc=%s)" % run_rec["exit_code"],
                "no expected runtime trap evidence", warn_diag)

    # Safe control.
    if rejected:
        return FAIL, "unexpected rejection (rc=%s)" % compile_rec["exit_code"], \
            "safe control was rejected", ""
    if config["mode"] == "check":
        if "ok" in compile_rec["stdout"] or "check:" in compile_rec["stdout"]:
            return PASS, "static check accepted (rc=0)", "", ""
        return FAIL, "check accepted without confirmation", "no 'ok' confirmation", ""
    if run_rec is None:
        return FAIL, "no runtime evidence recorded", "runtime step missing", ""
    if run_rec["timed_out"]:
        return FAIL, "TIMEOUT during run", "hard timeout exceeded", ""
    want = case.get("exit", 0)
    rout = run_rec["stdout"] + "\n" + run_rec["stderr"]
    bad = matches(rout, TRAP_MARKERS)
    if run_rec["exit_code"] == want and not bad:
        return PASS, "clean run (rc=%s)" % run_rec["exit_code"], "", ""
    if run_rec["exit_code"] != want:
        return FAIL, "wrong exit (rc=%s, expected %s)" % (run_rec["exit_code"], want), \
            "safe control exited unexpectedly", ""
    return FAIL, "clean exit but trap text present (%s)" % ",".join(bad), \
        "trap text in safe control output", ""


def main():
    if not os.path.isfile(BIN):
        print("FATAL: compiler binary not found: %s" % BIN, file=sys.stderr)
        return 2
    with open(os.path.join(HERE, "cases.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    cases = manifest["cases"]

    results = []
    tmpdir = tempfile.mkdtemp(prefix="cplus_safety_matrix_")
    try:
        for case in cases:
            src = os.path.join(ROOT, case["src"])
            if not os.path.isfile(src):
                print("FATAL: missing source: %s" % src, file=sys.stderr)
                return 2
            for config in CONFIGS:
                env = env_for(config)
                exe = os.path.join(tmpdir, "%s__%s" % (case["id"], config["id"]))
                compile_cmd = [BIN, "check" if config["mode"] == "check" else "build"]
                compile_cmd += config["flags"] + [src]
                if config["mode"] == "run":
                    compile_cmd += ["-o", exe]
                    if config["passthrough"]:
                        compile_cmd += ["--", "-isysroot", SYSROOT]
                crec = run(compile_cmd, env=env)
                rrec = None
                if config["mode"] == "run" and crec["exit_code"] == 0 \
                        and not crec["timed_out"]:
                    if os.path.isfile(exe):
                        rrec = run([exe], env=env)
                    else:
                        crec["stderr"] += "\nno executable produced at %s\n" % exe
                        crec["exit_code"] = "NO_EXE"
                status, actual, reason, warn_diag = evaluate(case, config, crec, rrec)
                results.append(dict(
                    test_id=case["id"],
                    category=case["category"],
                    source_path=case["src"],
                    expected_result=expected_for(case, config),
                    actual_result=actual,
                    status=status,
                    check_evidence=case.get("check_evidence", ""),
                    runtime_evidence=case.get("runtime_evidence", ""),
                    known_static_gap=bool(case.get("known_static_gap", False)),
                    static_only_by_design=bool(case.get("static_only_by_design", False)),
                    why=case.get("why", ""),
                    warning_diagnostic=warn_diag,
                    config_id=config["id"],
                    config_label=config["label"],
                    sanitizer_config=config["san"],
                    sanitizer_flags=("--sanitize=" + SAN) if config["passthrough"] else "--sanitize=none",
                    cplus_clang=config["clang"] or "(default)",
                    sysroot=SYSROOT if config["passthrough"] else "",
                    compiler_stdout=crec["stdout"],
                    compiler_stderr=crec["stderr"],
                    compiler_exit_code=crec["exit_code"],
                    runtime_stdout=rrec["stdout"] if rrec else "",
                    runtime_stderr=rrec["stderr"] if rrec else "",
                    runtime_exit_code=rrec["exit_code"] if rrec else None,
                    timed_out=crec["timed_out"] or (rrec["timed_out"] if rrec else False),
                    compile_command=crec["command"],
                    runtime_command=rrec["command"] if rrec else None,
                    duration_s=crec["duration_s"] + (rrec["duration_s"] if rrec else 0),
                    pass_=status == PASS,
                    fail_reason=reason if status == FAIL else "",
                    non_failing_reason=reason if status in NON_FAILING else "",
                ))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

    total = len(results)
    statuses = (PASS, WARN_ONLY, KNOWN_OPEN, STATIC_ONLY_BY_DESIGN, FAIL)
    counts = {s: sum(1 for r in results if r["status"] == s) for s in statuses}
    passed, failed = counts[PASS], counts[FAIL]
    timeouts = sum(1 for r in results if r["timed_out"])

    def group_counts(rows):
        d = dict(total=len(rows), failed=sum(1 for r in rows if r["status"] == FAIL))
        for s in statuses:
            d[s] = sum(1 for r in rows if r["status"] == s)
        return d

    by_config = {c["id"]: group_counts([r for r in results
                                        if r["config_id"] == c["id"]])
                 for c in CONFIGS}
    by_case = {case["id"]: group_counts([r for r in results
                                         if r["test_id"] == case["id"]])
               for case in cases}

    def entry(r):
        return dict(test_id=r["test_id"], config_id=r["config_id"],
                    source_path=r["source_path"], actual=r["actual_result"],
                    reason=r["non_failing_reason"] or r["fail_reason"],
                    warning_diagnostic=r["warning_diagnostic"], why=r["why"])

    failures = [entry(r) for r in results if r["status"] == FAIL]
    warn_only = [entry(r) for r in results if r["status"] == WARN_ONLY]
    known_open = [entry(r) for r in results if r["status"] == KNOWN_OPEN]
    static_only = [entry(r) for r in results
                   if r["status"] == STATIC_ONLY_BY_DESIGN]

    summary = dict(
        metadata=dict(
            generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
            compiler_binary=BIN,
            timeout_seconds=TIMEOUT,
            repository_root=ROOT,
            sanitizer="address,undefined",
            none_clang=NONE_CLANG or "(unset)",
            sanitizer_clang=SAN_CLANG or "(unset)",
            sysroot=SYSROOT,
            methodology=("PASS only on a matching static reject diagnostic or a "
                         "matching runtime trap message; safe control PASS only "
                         "on a clean build+run. DEFAULT unsanitized is WARN-only "
                         "by design (exact C+ warning -> WARN_ONLY). Documented "
                         "static/sanitizer gaps -> KNOWN_OPEN; provable-constant "
                         "cases with no native guard -> STATIC_ONLY_BY_DESIGN. "
                         "Compilation alone is never proof. Timeouts are FAIL."),
        ),
        configs=[dict(id=c["id"], label=c["label"], mode=c["mode"],
                      sanitizer=c["san"], clang=c["clang"] or "(default)")
                 for c in CONFIGS],
        cases=[dict(id=c["id"], category=c["category"], source_path=c["src"],
                    kind=c["kind"], check_evidence=c.get("check_evidence", ""),
                    runtime_evidence=c.get("runtime_evidence", ""),
                    why=c.get("why", ""),
                    known_static_gap=bool(c.get("known_static_gap", False)),
                    static_only_by_design=bool(c.get("static_only_by_design", False)),
                    sanitizer_gap=bool(c.get("sanitizer_gap", False)))
               for c in cases],
        results=results,
        totals=dict(total=total, passed=passed, failed=failed, timeouts=timeouts,
                    warn_only=counts[WARN_ONLY], known_open=counts[KNOWN_OPEN],
                    static_only_by_design=counts[STATIC_ONLY_BY_DESIGN],
                    by_config=by_config, by_case=by_case),
        failures=failures,
        warn_only=warn_only,
        known_open=known_open,
        static_only_by_design=static_only,
    )
    with open(os.path.join(HERE, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")

    short = {PASS: "PASS", WARN_ONLY: "WARN", KNOWN_OPEN: "OPEN",
             STATIC_ONLY_BY_DESIGN: "STATIC", FAIL: "FAIL"}
    lines = []
    lines.append("C+ SAFETY MATRIX -- summary")
    lines.append("generated: " + summary["metadata"]["generated_utc"])
    lines.append("compiler:  " + BIN)
    lines.append("backend (none): %s | backend (sanitizers): %s | SDK: %s" %
                 (NONE_CLANG or "(default)", SAN_CLANG or "(default)", SYSROOT))
    lines.append("timeout: %ds | sanitizers: %s" % (TIMEOUT, SAN))
    lines.append("")
    lines.append("CONFIGS")
    for c in CONFIGS:
        lines.append("  %-12s %-38s mode=%-5s san=%s clang=%s" %
                     (c["id"], c["label"], c["mode"], c["san"],
                      c["clang"] or "(default)"))
    lines.append("")
    lines.append("MATRIX (PASS / WARN=WARN_ONLY / OPEN=KNOWN_OPEN / STATIC=STATIC_ONLY_BY_DESIGN / FAIL)")
    lines.append("  %-24s %s" % ("case id", " ".join("%-12s" % c["id"] for c in CONFIGS)))
    for case in cases:
        cells = []
        for c in CONFIGS:
            r = next(x for x in results
                     if x["test_id"] == case["id"] and x["config_id"] == c["id"])
            cells.append("%-12s" % short[r["status"]])
        lines.append("  %-24s %s" % (case["id"], " ".join(cells)))
    lines.append("")
    lines.append("TOTALS: %d checks, %d PASS, %d WARN_ONLY, %d KNOWN_OPEN, "
                 "%d STATIC_ONLY_BY_DESIGN, %d FAIL" %
                 (total, passed, counts[WARN_ONLY], counts[KNOWN_OPEN],
                  counts[STATIC_ONLY_BY_DESIGN], failed))
    for c in CONFIGS:
        b = by_config[c["id"]]
        lines.append("  %-12s %d PASS / %d WARN_ONLY / %d KNOWN_OPEN / %d STATIC / %d FAIL (of %d)" %
                     (c["id"], b[PASS], b[WARN_ONLY], b[KNOWN_OPEN],
                      b[STATIC_ONLY_BY_DESIGN], b[FAIL], b["total"]))

    def section(title, rows, status):
        lines.append("")
        lines.append("%s (%d) -- non-failing statuses are explicit, never silently dropped" % (title, len(rows)))
        for r in results:
            if r["status"] != status:
                continue
            lines.append("  [%s / %s] %s" % (r["test_id"], r["config_id"], r["source_path"]))
            lines.append("      expected: %s" % r["expected_result"])
            lines.append("      actual:   %s" % r["actual_result"])
            if r["warning_diagnostic"]:
                lines.append("      warning:  %s" % r["warning_diagnostic"].replace("\n", " | "))
            lines.append("      compiler_stderr: %r" % r["compiler_stderr"])
            lines.append("      runtime_stderr:  %r" % r["runtime_stderr"])
            lines.append("      why:      %s" % r["why"])
            lines.append("      reason:   %s" % (r["non_failing_reason"] or r["fail_reason"]))
            if r["timed_out"]:
                lines.append("      note:     TIMEOUT counted as failure")

    section("WARN_ONLY", warn_only, WARN_ONLY)
    section("KNOWN_OPEN", known_open, KNOWN_OPEN)
    section("STATIC_ONLY_BY_DESIGN", static_only, STATIC_ONLY_BY_DESIGN)
    section("FAILURES", failures, FAIL)
    lines.append("")
    lines.append("timeouts: %d (always FAIL and force exit 1)" % timeouts)
    lines.append("Full stdout/stderr/commands/exit codes for every check: summary.json")
    with open(os.path.join(HERE, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print("safety matrix: %d checks, %d PASS, %d WARN_ONLY, %d KNOWN_OPEN, "
          "%d STATIC_ONLY_BY_DESIGN, %d FAIL (%d timeouts)" %
          (total, passed, counts[WARN_ONLY], counts[KNOWN_OPEN],
           counts[STATIC_ONLY_BY_DESIGN], failed, timeouts))
    for c in CONFIGS:
        b = by_config[c["id"]]
        print("  %-12s %d PASS / %d WARN_ONLY / %d KNOWN_OPEN / %d STATIC / %d FAIL" %
              (c["id"], b[PASS], b[WARN_ONLY], b[KNOWN_OPEN],
               b[STATIC_ONLY_BY_DESIGN], b[FAIL]))
    print("wrote %s and %s" % (os.path.join(HERE, "summary.json"),
                               os.path.join(HERE, "summary.txt")))
    return 1 if failed > 0 or timeouts > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
