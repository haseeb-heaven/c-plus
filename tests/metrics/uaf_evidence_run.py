#!/usr/bin/env python3
"""Deterministic evidence for the documented gap: raw malloc/free UAF is not
detected in DEFAULT C+ builds (docs/LIMITATIONS.md, rows "Use-after-free").

Method -- no bare nonzero exit is ever used as proof:

  1. For every case, run ``c+ check --strict --sanitize=none``.  Rejected cases
     must contain an exact, named C+ diagnostic signature (e.g.
     "[CPLUS-LIFETIME] use-after-free: ..."); accepted cases must be cleanly
     accepted (rc == 0 and no "error:" in the output).
  2. For every accepted case, run
     ``c+ build --default --sanitize=address`` with the ASan-capable Homebrew
     clang and an explicit -isysroot, then execute the binary.  Unsafe cases
     must produce the exact ASan diagnostic string
     "ERROR: AddressSanitizer: heap-use-after-free" AND its SUMMARY line.
     Safe controls must exit 0 with no sanitizer diagnostic at all, which
     guards against false positives.
  3. Print a per-case table of static result and runtime diagnostic, write a
     machine-readable report, and exit 0 only when every assertion holds.

The 12 cases are fixed (no randomness, no fuzzing) and live in
``tests/metrics/uaf_evidence/cases/``.  Generated binaries/logs go to
``tests/metrics/uaf_evidence/out/``.  This script never edits existing files
and never rebuilds the compiler.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CASE_DIR = HERE / "uaf_evidence" / "cases"
OUT_DIR = HERE / "uaf_evidence" / "out"

CPLUS_BIN = pathlib.Path(
    os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+"))
)
ASAN_CLANG = os.environ.get("CPLUS_ASAN_CLANG", "/opt/homebrew/opt/llvm/bin/clang")
SDKROOT = os.environ.get(
    "CPLUS_SDKROOT", "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
)
CHECK_CLANG = os.environ.get("CPLUS_CHECK_CLANG", "/usr/bin/clang")


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    description: str
    unsafe: bool
    # "reject" -> expect a named C+ static diagnostic; "compile" -> expect rc 0.
    expect_static: str
    # C+ diagnostic tag shown in the table when the case is rejected.
    static_tag: str | None
    # Exact substring that must appear in the STRICT check output when rejected.
    static_signature: str | None
    # Exact ASan diagnostic substring expected at run time (unsafe + compiled).
    runtime_signature: str | None
    # For safe controls: exact stdout line that must be produced.
    expected_stdout: str | None = None


CASES: tuple[Case, ...] = (
    Case(
        "direct_uaf", "direct malloc/free + stale dereference", True, "reject",
        "[CPLUS-LIFETIME]",
        "use-after-free: dereference of released pointer 'p'", None,
    ),
    Case(
        "alias_uaf", "local alias dereferenced after free", True, "reject",
        "[CPLUS-LIFETIME]",
        "use-after-free: dereference of released pointer 'q'", None,
    ),
    Case(
        "field_uaf", "struct pointer field dereferenced after free", True,
        "reject", "[CPLUS-LIFETIME]",
        "use-after-free: subscript of released pointer", None,
    ),
    Case(
        "array_ptr_uaf", "array-of-pointer slot dereferenced after free", True,
        "reject", "[CPLUS-LIFETIME]",
        "use-after-free: dereference of released pointer 'slots[0]'", None,
    ),
    Case(
        "double_free", "raw double free", True, "reject", "[CPLUS-OWNER]",
        "double free of 'p'", None,
    ),
    # Wave 1: still rejected, now via the branch-aware realloc state machine
    # with a lifetime diagnostic instead of the old generic [CPLUS-ALIAS]
    # message. Keep requiring a hard reject; only the expected wording changed.
    Case(
        "use_after_realloc", "stale original pointer used after realloc", True,
        "reject", "[CPLUS-LIFETIME]",
        "use-after-free: dereference of released pointer", None,
    ),
    Case(
        "interproc_uaf", "stale pointer passed to a callee", True,
        "reject", "[CPLUS-LIFETIME]", "use-after-free: released pointer", None,
    ),
    Case(
        "cast_uaf", "alias hidden by a void* cast", True,
        "reject", "[CPLUS-LIFETIME]", "dereference of released pointer 'v'",
        None,
    ),
    Case(
        "returned_alias_uaf", "alias returned from a helper function", True,
        "reject", "[CPLUS-LIFETIME]",
        "dereference of released pointer 'q'", None,
    ),
    Case(
        "array_index_uaf", "array-of-pointer UAF with a runtime slot index",
        True, "reject", "[CPLUS-LIFETIME]",
        "possible use-after-free: element of", None,
    ),
    Case(
        "safe_free_null_guard", "free + NULL assignment + null guard", False,
        "compile", None, None, None, expected_stdout="safe",
    ),
    Case(
        "safe_alias_before_free", "alias used before free; value snapshotted",
        False, "compile", None, None, None, expected_stdout="111",
    ),
)


def run(cmd: list[str], env: dict[str, str], timeout: int = 180):
    """Return (returncode, combined_output) for a subprocess."""
    cp = subprocess.run(
        cmd, env=env, capture_output=True, text=True, timeout=timeout
    )
    return cp.returncode, cp.stdout + cp.stderr


def first_diagnostic(output: str, tag: str) -> str:
    for line in output.splitlines():
        if tag in line:
            return line.strip()
    return ""


def shell_rc(rc: int) -> int:
    """Python reports signal death as -N; shells report 128+N (SIGABRT=134)."""
    return 128 - rc if rc < 0 else rc


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def table_cell(text: str, width: int) -> str:
    if len(text) > width:
        text = text[: width - 1] + "…"
    return text.ljust(width)


def main() -> int:
    failures: list[str] = []
    commands: list[str] = []
    results: list[dict[str, object]] = []

    if not CPLUS_BIN.is_file():
        print(f"FATAL: c+ binary not found: {CPLUS_BIN}")
        return 2
    missing = [str(p) for p in (CASE_DIR,) if not p.is_dir()]
    if missing:
        print(f"FATAL: missing directories: {missing}")
        return 2
    for case in CASES:
        if not (CASE_DIR / f"{case.name}.c").is_file():
            print(f"FATAL: missing case source: {case.name}.c")
            return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # Snapshot the compiler under test so a concurrent rebuild of build/c+
    # cannot swap binaries halfway through the evidence run. CPLUS_ROOT keeps
    # the driver's runtime/source lookup pointed at this checkout.
    snapshot = OUT_DIR / "cplus-under-test"
    shutil.copy2(CPLUS_BIN, snapshot)
    digest = sha256_file(snapshot)
    test_bin = snapshot
    check_env = dict(os.environ)
    check_env["CPLUS_CLANG"] = CHECK_CLANG
    check_env["CPLUS_ROOT"] = str(ROOT)
    build_env = dict(os.environ)
    build_env["CPLUS_CLANG"] = ASAN_CLANG
    build_env["CPLUS_ROOT"] = str(ROOT)

    for case in CASES:
        src = CASE_DIR / f"{case.name}.c"
        rec: dict[str, object] = {
            "case": case.name,
            "description": case.description,
            "unsafe": case.unsafe,
        }

        # ---- Step 1: static check -------------------------------------
        cmd = [
            str(test_bin), "check", "--strict", "--sanitize=none", str(src)
        ]
        commands.append(" ".join(shlex.join(cmd).split()))
        rc, out = run(cmd, check_env)
        log = OUT_DIR / f"{case.name}.check.txt"
        log.write_text(out)
        if case.expect_static == "reject":
            static_result = "REJECT"
            sig = case.static_signature or ""
            tag = case.static_tag or ""
            ok = rc != 0 and sig in out and tag in out
            static_cell = f"{static_result} rc={rc}  {tag} {sig}"
            if rc == 0:
                failures.append(f"{case.name}: expected STRICT rejection, got rc=0")
            if tag not in out:
                failures.append(f"{case.name}: missing diagnostic tag {tag!r}")
            if sig not in out:
                failures.append(
                    f"{case.name}: missing exact static signature {sig!r}"
                )
        else:
            static_result = "COMPILE"
            ok = rc == 0 and "error:" not in out.lower()
            static_cell = f"{static_result} rc={rc}  (no static diagnostic)"
            if not ok:
                failures.append(
                    f"{case.name}: expected clean STRICT acceptance, rc={rc}: "
                    f"{out.strip().splitlines()[:1]}"
                )
        rec["static"] = {
            "result": static_result,
            "rc": rc,
            "log": str(log.relative_to(ROOT)),
            "ok": ok,
        }

        # ---- Step 2: ASan build + run (compiled cases only) -----------
        if case.expect_static != "compile":
            runtime_cell = "not built (rejected statically)"
            rec["runtime"] = {"result": "NOT_BUILT"}
        else:
            binary = OUT_DIR / f"{case.name}.asan.bin"
            bcmd = [
                str(test_bin), "build", "--default", "--sanitize=address",
                str(src), "-o", str(binary),
                "--", "-isysroot", SDKROOT,
            ]
            commands.append(
                "CPLUS_CLANG=" + shlex.quote(ASAN_CLANG) + " "
                + shlex.join(bcmd)
            )
            brc, bout = run(bcmd, build_env)
            (OUT_DIR / f"{case.name}.build.txt").write_text(bout)
            rec["build"] = {"rc": brc, "command": shlex.join(bcmd)}

            if brc != 0:
                failures.append(f"{case.name}: ASan build failed rc={brc}")
                runtime_cell = f"BUILD_FAIL rc={brc}"
                rec["runtime"] = {"result": "BUILD_FAIL", "rc": brc}
            elif not binary.is_file():
                failures.append(f"{case.name}: ASan build produced no binary")
                runtime_cell = "BUILD_FAIL (no binary)"
                rec["runtime"] = {"result": "NO_BINARY"}
            else:
                rcmd = [str(binary)]
                commands.append(shlex.join(rcmd))
                rrc, rout = run(rcmd, dict(os.environ))
                (OUT_DIR / f"{case.name}.run.txt").write_text(rout)

                if case.unsafe:
                    sig = case.runtime_signature or ""
                    summary = "SUMMARY: AddressSanitizer: heap-use-after-free"
                    # The nonzero exit is recorded but is NOT the proof: the
                    # exact ASan diagnostic lines are the asserted evidence.
                    diag_ok = sig in rout and summary in rout
                    diag = first_diagnostic(rout, "ERROR: AddressSanitizer:")
                    # Strip the "==PID==" prefix so the table stays legible;
                    # the asserted evidence is the exact diagnostic substring.
                    if "==" in diag:
                        diag = diag[diag.find("==", 2) + 2 :].lstrip("= ")
                    runtime_cell = f"ABORT rc={shell_rc(rrc)}  {diag}"
                    if not diag_ok:
                        failures.append(
                            f"{case.name}: missing exact ASan diagnostic "
                            f"{sig!r} and/or {summary!r} (rc={rrc})"
                        )
                    rec["runtime"] = {
                        "result": "ASAN_DETECTED" if diag_ok else "MISSING_DIAGNOSTIC",
                        "rc": rrc,
                        "error_line": first_diagnostic(
                            rout, "ERROR: AddressSanitizer:"
                        ),
                        "summary_line": first_diagnostic(
                            rout, "SUMMARY: AddressSanitizer:"
                        ),
                        "ok": diag_ok,
                    }
                else:
                    markers = ("AddressSanitizer", "runtime error:", "C+ error:")
                    clean = rrc == 0 and not any(m in rout for m in markers)
                    expected = case.expected_stdout or ""
                    if expected and expected not in rout:
                        clean = False
                    runtime_cell = (
                        f"CLEAN rc={shell_rc(rrc)}  no sanitizer diagnostic; "
                        f"stdout~{expected!r}"
                    )
                    if not clean:
                        failures.append(
                            f"{case.name}: safe control not clean (rc={rrc}): "
                            f"{rout.strip().splitlines()[:2]}"
                        )
                    rec["runtime"] = {
                        "result": "CLEAN" if clean else "FALSE_POSITIVE",
                        "rc": rrc,
                        "stdout": rout.strip(),
                        "ok": clean,
                    }

        results.append(rec)
        verdict = "PASS" if rec["static"]["ok"] and (
            rec["runtime"].get("ok", rec["runtime"]["result"] == "NOT_BUILT")
        ) else "FAIL"
        print(
            f"{table_cell(case.name, 24)} "
            f"{table_cell(static_cell, 58)} "
            f"{table_cell(runtime_cell, 72)} "
            f"{verdict}"
        )

    # ---- Report -------------------------------------------------------
    report = {
        "purpose": "evidence: raw malloc/free UAF not detected in default builds",
        "cplus_source_bin": str(CPLUS_BIN),
        "cplus_snapshot": str(snapshot),
        "cplus_sha256": digest,
        "asan_clang": ASAN_CLANG,
        "sdkroot": SDKROOT,
        "check_clang": CHECK_CLANG,
        "note": "nonzero exits are recorded but never used as proof; exact "
                "CPLUS/ASan diagnostic substrings are asserted",
        "cases": results,
        "commands": commands,
        "failures": failures,
        "pass": not failures,
    }
    (OUT_DIR / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (OUT_DIR / "report.txt").write_text(
        "raw-UAF evidence suite\n"
        f"compiler under test: {CPLUS_BIN}\n"
        f"snapshot: {snapshot}\n"
        f"sha256: {digest}\n"
        f"ASan clang: {ASAN_CLANG}\n"
        f"SDKROOT: {SDKROOT}\n\n"
        + "\n".join(commands)
        + "\n\n"
        + ("PASS: all assertions held\n" if not failures
           else "FAIL:\n  " + "\n  ".join(failures) + "\n")
    )

    print()
    print("Exact commands executed (in order):")
    for c in commands:
        print("  " + c)
    print()
    if failures:
        print(f"FAIL: {len(failures)} assertion(s) failed:")
        for f in failures:
            print("  - " + f)
        return 1
    rejected = sum(1 for r in results if r["static"]["result"] == "REJECT")
    compiled = len(results) - rejected
    print(
        f"PASS: {len(results)} cases; {rejected} rejected by STRICT check, "
        f"{compiled} compiled and executed under ASan; every assertion held."
    )
    print(f"Reports: {OUT_DIR / 'report.txt'} , {OUT_DIR / 'report.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
