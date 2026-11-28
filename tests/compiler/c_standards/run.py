#!/usr/bin/env python3
"""C standard matrix for the c+ LEGACY C backend.

Every (standard, fixture) pair is built with:

    <CPLUS_BIN> build --legacy-c --sanitize=none --emit-c <generated.c> \
        <fixture.c> -o <binary> -- -std=<std>

and with one or more C reference compilers:

    <REF> -std=<std> -O2 [-isysroot <sdk>] <fixture.c> -o <binary>

Each case asserts three things:

1. The c+ and reference binaries produce byte-identical stdout and the exact
   same process exit code.
2. The C emitted by c+ (--emit-c) equals the original fixture byte-for-byte
   after removing exactly one leading `#line <n> "<path>"` directive.
3. If a standard is rejected by both c+ and the primary reference, the exact
   compiler diagnostics are captured verbatim and the case is reported as
   `unsupported` rather than silently skipped.

There is no failure suppression: every non-zero build/run exit, timeout or
mismatch is recorded and contributes to the failure count and the exit status.
"""

from __future__ import annotations

import datetime
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MANIFEST = HERE / "manifest.json"
RESULTS = HERE / "results"
GENERATED = RESULTS / "generated"
DIAGNOSTICS = RESULTS / "diagnostics"
CASES_JSON = RESULTS / "cases.json"
SUMMARY_TXT = HERE / "summary.txt"
SUMMARY_JSON = HERE / "summary.json"

BUILD_TIMEOUT = 120
RUN_TIMEOUT = 60

LINE_DIRECTIVE = re.compile(rb'^#line \d+ "[^"]*"\n')


def printable(argv) -> str:
    return " ".join(shlex_quote(str(a)) for a in argv)


def shlex_quote(value: str) -> str:
    # Avoid a shlex dependency subtlety on older Pythons; POSIX shell quoting.
    if re.fullmatch(r"[A-Za-z0-9_@%+=:,./-]+", value):
        return value
    return "'" + value.replace("'", "'\"'\"'") + "'"


def resolve_env_path(name: str, default: str) -> pathlib.Path:
    value = os.environ.get(name)
    path = pathlib.Path(value).expanduser() if value else pathlib.Path(default)
    if not path.is_absolute():
        path = ROOT / path
    return path.resolve()


def show_sdk_path() -> str | None:
    env = os.environ.get("ISYSROOT")
    if env:
        return env
    try:
        proc = subprocess.run(
            ["xcrun", "--sdk", "macosx", "--show-sdk-path"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if proc.returncode == 0:
        value = proc.stdout.decode("utf-8", errors="replace").strip()
        if value and pathlib.Path(value).is_dir():
            return value
    return None


def references() -> list[dict]:
    primary = {
        "id": "usr-bin-clang",
        "kind": "primary",
        "path": str(resolve_env_path("REF_CC", "/usr/bin/clang")),
        "isysroot": os.environ.get("REF_ISYSROOT") or None,
    }
    result = [primary]
    homebrew = os.environ.get("HOMEBREW_CLANG", "/opt/homebrew/opt/llvm/bin/clang")
    if homebrew and pathlib.Path(homebrew).expanduser().is_file():
        result.append({
            "id": "homebrew-clang",
            "kind": "optional",
            "path": str(pathlib.Path(homebrew).expanduser().resolve()),
            "isysroot": show_sdk_path(),
        })
    return result


def run_cmd(argv, timeout: int, cwd=None) -> dict:
    command = printable(argv)
    try:
        proc = subprocess.run(
            [str(a) for a in argv],
            cwd=str(cwd) if cwd else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "exit": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "timed_out": False,
        }
    except subprocess.TimeoutExpired as exc:
        return {
            "command": command,
            "exit": None,
            "stdout": exc.stdout or b"",
            "stderr": (exc.stderr or b"") +
                      ("TIMEOUT after %d seconds\n" % timeout).encode(),
            "timed_out": True,
        }
    except OSError as exc:
        return {
            "command": command,
            "exit": None,
            "stdout": b"",
            "stderr": ("spawn failed: %s\n" % exc).encode(),
            "timed_out": False,
            "spawn_error": True,
        }


def rejected_standard(stderr: bytes, std: str) -> bool:
    """True when a compiler explicitly rejects the -std value itself."""
    token = re.escape(std.encode("ascii"))
    return bool(
        re.search(rb"invalid value '" + token + rb"' in '-std=", stderr)
        or re.search(rb"unsupported option '-std=" + token + rb"'", stderr)
    )


def check_source(original: bytes, generated_path: pathlib.Path) -> dict:
    if not generated_path.is_file():
        return {
            "checked": False,
            "identical": False,
            "reason": "c+ did not write the generated C file",
            "original_bytes": len(original),
            "generated_bytes": 0,
            "line_prefix": None,
        }
    generated = generated_path.read_bytes()
    match = LINE_DIRECTIVE.match(generated)
    if not match:
        return {
            "checked": True,
            "identical": False,
            "reason": "generated C has no leading #line directive",
            "original_bytes": len(original),
            "generated_bytes": len(generated),
            "line_prefix": generated.split(b"\n", 1)[0].decode(
                "utf-8", errors="replace"),
        }
    remainder = generated[match.end():]
    identical = remainder == original
    return {
        "checked": True,
        "identical": identical,
        "reason": "" if identical else "bytes differ after removing #line",
        "original_bytes": len(original),
        "generated_bytes": len(generated),
        "line_prefix": match.group(0).decode("utf-8", errors="replace").rstrip("\n"),
    }


def enc(record: dict) -> dict:
    """Copy a run record, decoding every raw byte field for JSON output."""
    out = {}
    for key, value in record.items():
        if isinstance(value, bytes):
            out[key] = value.decode("utf-8", errors="replace")
        else:
            out[key] = value
    return out


def build_and_run_case(std: str, fixture_meta: dict, cplus: pathlib.Path,
                       refs: list[dict], workdir: pathlib.Path) -> dict:
    fixture = HERE / fixture_meta["file"]
    name = fixture_meta["name"]
    original = fixture.read_bytes() if fixture.is_file() else b""
    generated = GENERATED / ("%s__%s.c" % (std, name))
    cplus_bin = workdir / ("cplus__%s__%s" % (std, name))
    # Never let a stale artifact from a previous run masquerade as this case's
    # generated C when c+ fails before --emit-c is reached.
    if generated.exists():
        generated.unlink()

    cb = run_cmd(
        [cplus, "build", "--legacy-c", "--sanitize=none",
         "--emit-c", generated, fixture, "-o", cplus_bin,
         "--", "-std=" + std],
        timeout=BUILD_TIMEOUT,
        cwd=workdir,
    )
    cplus_ok = cb["exit"] == 0 and cplus_bin.is_file()
    source = check_source(original, generated)

    scope = fixture_meta.get("reference_scope", "all")
    applicable = [r for r in refs if scope == "all" or r["kind"] == "primary"]
    ref_cases = []
    for ref in applicable:
        ref_bin = workdir / ("%s__%s__%s" % (ref["id"], std, name))
        argv = [ref["path"], "-std=" + std, "-O2"]
        if ref.get("isysroot"):
            argv += ["-isysroot", ref["isysroot"]]
        argv += [fixture, "-o", ref_bin]
        rb = run_cmd(argv, timeout=BUILD_TIMEOUT, cwd=workdir)
        ref_ok = rb["exit"] == 0 and ref_bin.is_file()
        run = None
        if ref_ok:
            run = run_cmd([ref_bin], timeout=RUN_TIMEOUT, cwd=workdir)
        ref_cases.append({
            "ref": ref,
            "build": rb,
            "build_ok": ref_ok,
            "binary": ref_bin,
            "run": run,
        })

    cplus_run = run_cmd([cplus_bin], timeout=RUN_TIMEOUT, cwd=workdir) \
        if cplus_ok else None

    primary = next(c for c in ref_cases if c["ref"]["kind"] == "primary")
    cplus_rejected = rejected_standard(cb["stderr"], std)
    primary_rejected = rejected_standard(primary["build"]["stderr"], std)

    comparisons = []
    status = "fail"
    reason = ""
    if cplus_rejected and primary_rejected:
        status = "unsupported"
        reason = "both c+ backend and primary reference reject -std=%s" % std
    elif cplus_ok and primary["build_ok"]:
        status = "pass"
        for ref_case in ref_cases:
            if not ref_case["build_ok"]:
                status = "fail"
                reason = "reference build failed for %s" % ref_case["ref"]["id"]
                comparisons.append({
                    "reference": ref_case["ref"]["id"],
                    "build_exit": ref_case["build"]["exit"],
                    "stdout_identical": False,
                    "exit_identical": False,
                    "reference_build_stderr": ref_case["build"]["stderr"].decode(
                        "utf-8", errors="replace"),
                })
                continue
            comparison = {
                "reference": ref_case["ref"]["id"],
                "build_exit": ref_case["build"]["exit"],
                "stdout_identical": ref_case["run"]["stdout"] == cplus_run["stdout"],
                "exit_identical": ref_case["run"]["exit"] == cplus_run["exit"],
                "cplus_exit": cplus_run["exit"],
                "reference_exit": ref_case["run"]["exit"],
                "cplus_stdout": cplus_run["stdout"].decode(
                    "utf-8", errors="replace"),
                "reference_stdout": ref_case["run"]["stdout"].decode(
                    "utf-8", errors="replace"),
                "reference_run_stderr": ref_case["run"]["stderr"].decode(
                    "utf-8", errors="replace"),
            }
            comparisons.append(comparison)
            if not (comparison["stdout_identical"] and comparison["exit_identical"]):
                status = "fail"
                reason = "stdout or exit mismatch against %s" % ref_case["ref"]["id"]
    else:
        status = "fail"
        reason = (
            "asymmetric build failure: c+ exit=%r supported=%s, "
            "primary reference exit=%r rejected=%s"
            % (cb["exit"], cplus_ok, primary["build"]["exit"], primary_rejected)
        )

    if not source["identical"]:
        status = "fail"
        reason = (reason + "; " if reason else "") + source["reason"] or \
            "generated C does not preserve source bytes"

    return {
        "standard": std,
        "fixture": name,
        "file": fixture_meta["file"],
        "feature": fixture_meta["feature"],
        "status": status,
        "reason": reason,
        "source_preservation": source,
        "cplus": {
            "build": enc(cb),
            "build_ok": cplus_ok,
            "rejected_standard": cplus_rejected,
            "generated_c": str(generated),
            "run": enc(cplus_run) if cplus_run else None,
        },
        "references": [{
            "id": c["ref"]["id"],
            "kind": c["ref"]["kind"],
            "path": c["ref"]["path"],
            "isysroot": c["ref"].get("isysroot"),
            "build": enc(c["build"]),
            "build_ok": c["build_ok"],
            "rejected_standard": rejected_standard(c["build"]["stderr"], std),
            "run": enc(c["run"]) if c["run"] else None,
        } for c in ref_cases],
        "comparisons": comparisons,
    }


def render_text(summary: dict) -> str:
    lines = [
        "C standard matrix for c+ LEGACY C mode",
        "=" * 78,
        "generated : %s" % summary["generated"],
        "host      : %s" % summary["host"],
        "cplus     : %s" % summary["cplus"],
        "cplus cmd : %s" % summary["cplus_template"],
        "ref cmd   : %s" % summary["reference_template"],
    ]
    for ref in summary["references"]:
        detail = " (isysroot %s)" % ref["isysroot"] if ref.get("isysroot") else ""
        lines.append("reference : %-16s %s%s" % (ref["id"], ref["path"], detail))
    lines += [
        "",
        "totals    : %d cases | %d passed | %d failed | %d unsupported"
        % (summary["total"], summary["passed"], summary["failed"],
           summary["unsupported"]),
        "source    : %d/%d generated C files preserve source bytes (except #line)"
        % (summary["source_preserved"], summary["total"]),
        "runtime   : %d cases compared stdout and exit" % summary["compared"],
        "",
        "per standard:",
    ]
    for std, counts in summary["per_standard"].items():
        lines.append("  %-6s %2d cases | %2d passed | %d failed | %d unsupported"
                     % (std, counts["total"], counts["passed"], counts["failed"],
                        counts["unsupported"]))
    lines += ["", "per case:"]
    for case in summary["cases"]:
        src = "source=%s" % ("identical" if case["source_preservation"]["identical"]
                            else "DIFFERS")
        refs = ",".join(c["reference"] for c in case["comparisons"]) or "-"
        lines.append("[%-7s] %-6s %-16s %s refs=%s"
                     % (case["status"].upper(), case["standard"],
                        case["fixture"], src, refs))
        if case["status"] == "fail":
            lines.append("         reason: %s" % case["reason"])
            for comparison in case["comparisons"]:
                if not (comparison.get("stdout_identical")
                        and comparison.get("exit_identical")):
                    lines.append("         stdout_identical=%s exit_identical=%s c+=%r ref=%r"
                                 % (comparison.get("stdout_identical"),
                                    comparison.get("exit_identical"),
                                    comparison.get("cplus_exit"),
                                    comparison.get("reference_exit")))
        elif case["status"] == "unsupported":
            ref_status = ", ".join(
                "%s=%s" % (r["id"], "rejected" if r["rejected_standard"]
                           else "accepted" if r["build_ok"] else "build-error")
                for r in case["references"])
            lines.append("         %s; cplus=rejected; %s"
                         % (case["reason"], ref_status))
    lines += ["", "Exact diagnostics for unsupported standards:"]
    if not summary["unsupported_diagnostics"]:
        lines.append("  (none)")
    for key in sorted(summary["unsupported_diagnostics"]):
        record = summary["unsupported_diagnostics"][key]
        std, source = key.split("/", 1)
        lines += [
            "--- %s / %s (fixtures: %s) ---"
            % (std, source, ", ".join(record["fixtures"])),
            record["stderr"].rstrip("\n"),
            "--- end %s / %s ---" % (std, source),
        ]
    return "\n".join(lines) + "\n"


def main() -> int:
    with MANIFEST.open(encoding="utf-8") as handle:
        manifest = json.load(handle)

    cplus = resolve_env_path("CPLUS_BIN", "build/c+")
    if not cplus.is_file():
        print("error: compiler not found: %s" % cplus, file=sys.stderr)
        return 2
    refs = references()
    for ref in refs:
        if not pathlib.Path(ref["path"]).is_file():
            print("error: reference compiler not found: %s" % ref["path"],
                  file=sys.stderr)
            return 2

    for directory in (RESULTS, GENERATED, DIAGNOSTICS):
        directory.mkdir(parents=True, exist_ok=True)

    cases = []
    with tempfile.TemporaryDirectory(prefix="cplus_c_standards_") as tmp:
        workdir = pathlib.Path(tmp)
        for std in manifest["standards"]:
            for fixture in manifest["fixtures"]:
                cases.append(build_and_run_case(std, fixture, cplus, refs, workdir))

    unsupported_diagnostics = {}
    for case in cases:
        if case["status"] != "unsupported":
            continue
        for source, record in [("cplus", case["cplus"])] + [
                (ref["id"], ref) for ref in case["references"]]:
            if not record.get("rejected_standard"):
                continue
            key = (case["standard"], source)
            stderr = record["build"]["stderr"]
            if key in unsupported_diagnostics:
                unsupported_diagnostics[key]["fixtures"].append(case["fixture"])
            else:
                unsupported_diagnostics[key] = {
                    "stderr": stderr,
                    "fixtures": [case["fixture"]],
                    "path": str(cplus) if source == "cplus" else record["path"],
                }
    for (std, source), record in unsupported_diagnostics.items():
        (DIAGNOSTICS / ("%s__%s.txt" % (std, source))).write_text(
            record["stderr"], encoding="utf-8")

    # JSON object keys must be strings; keep the (standard, source) tuple shape
    # in the on-disk diagnostics file names and render the text report from the
    # same flattened "std/source" keys.
    json_diagnostics = {
        "%s/%s" % key: record
        for key, record in unsupported_diagnostics.items()
    }

    per_standard = {}
    for std in manifest["standards"]:
        subset = [c for c in cases if c["standard"] == std]
        per_standard[std] = {
            "total": len(subset),
            "passed": sum(1 for c in subset if c["status"] == "pass"),
            "failed": sum(1 for c in subset if c["status"] == "fail"),
            "unsupported": sum(1 for c in subset if c["status"] == "unsupported"),
        }

    summary = {
        "suite": manifest["suite"],
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(),
        "cplus": str(cplus),
        "cplus_template": manifest["cplus_template"],
        "reference_template": manifest["reference_template"],
        "references": refs,
        "standards": manifest["standards"],
        "total": len(cases),
        "passed": sum(1 for c in cases if c["status"] == "pass"),
        "failed": sum(1 for c in cases if c["status"] == "fail"),
        "unsupported": sum(1 for c in cases if c["status"] == "unsupported"),
        "source_preserved": sum(1 for c in cases
                                if c["source_preservation"]["identical"]),
        "compared": sum(1 for c in cases if c["comparisons"]),
        "per_standard": per_standard,
        "unsupported_diagnostics": json_diagnostics,
        "cases": cases,
    }

    (CASES_JSON.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8"))
    SUMMARY_JSON.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    SUMMARY_TXT.write_text(render_text(summary), encoding="utf-8")

    print("C standards: %d cases | %d passed | %d failed | %d unsupported"
          % (summary["total"], summary["passed"], summary["failed"],
             summary["unsupported"]))
    print("source preservation: %d/%d | runtime comparisons: %d"
          % (summary["source_preserved"], summary["total"], summary["compared"]))
    for std in manifest["standards"]:
        c = per_standard[std]
        print("  %-6s %d cases | %d passed | %d failed | %d unsupported"
              % (std, c["total"], c["passed"], c["failed"], c["unsupported"]))
    print("summary: %s" % SUMMARY_TXT)
    print("summary: %s" % SUMMARY_JSON)
    if summary["failed"]:
        for case in cases:
            if case["status"] == "fail":
                print("  FAIL %s %s: %s" % (case["standard"], case["fixture"],
                                            case["reason"]), file=sys.stderr)
    return 1 if summary["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
