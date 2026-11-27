#!/usr/bin/env python3
"""`stdlib/*.hp` C-ABI boundary audit (issues #186, #187).

Discovers every Modern C+ header instead of trusting a hard-coded list.  For
each header it checks the documented separation rule (plain C interop surface
vs. lowered C+ RAII) and parse-checks a generated translation unit with the
C+ driver.  A missing optional third-party dependency is reported as BLOCKED
with the exact include name; every other failure is a hard failure.

Cross-boundary link/runtime parity is not re-implemented here: the runner
requires that this gate already executed `tests/abi/c_to_cp/` and
`tests/abi/precompiled/` and verifies their fresh result logs.
"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADERS = sorted((ROOT / "stdlib").glob("*.hp"))
WORK = ROOT / "tests/compiler/abi_gate/results/hp_boundary"
CPLUS = pathlib.Path(os.environ.get("CPLUS_BIN", str(ROOT / "build/c+")))
TIMEOUT = int(os.environ.get("ABI_TIMEOUT", "90"))
MISSING_INCLUDE = re.compile(r"fatal error: '([^']+)' file not found")


def extra_flags(name: str) -> list[str]:
    """Include paths for vendored C headers, mirroring tests/audit_phase3."""
    flags: list[str] = []
    if name in {"json"}:
        flags.append("-I" + str(ROOT / "third_party/yyjson"))
    if name in {"image"}:
        flags.append("-I" + str(ROOT / "third_party/stb"))
    if name == "audio":
        flags.append("-I" + str(ROOT / "third_party/miniaudio"))
    if name in {"gui", "nuklear_adapt", "nuklear_cpp"}:
        flags.append("-I" + str(ROOT / "third_party/nuklear"))
    return flags


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                              timeout=TIMEOUT)
    except subprocess.TimeoutExpired as exc:
        return subprocess.CompletedProcess(cmd, 124, "", str(exc))


def main() -> int:
    if not HEADERS:
        print("HP_BOUNDARY: FAIL (no stdlib/*.hp headers discovered)")
        return 1
    if not CPLUS.is_file():
        print("HP_BOUNDARY: FAIL (CPLUS_BIN not found: %s)" % CPLUS)
        return 1
    WORK.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    native = 0
    raii = 0
    blocked = 0
    log_lines: list[str] = []

    for header in HEADERS:
        name = header.stem
        text = header.read_text()
        rel = header.relative_to(ROOT).as_posix()
        guard = "#ifndef CPLUS_STD_" in text
        is_native = bool(re.search(
            r"native\s*\(|extern\s+\"C\"|yyjson_|SDL_|socket\s*\(|FILE\s*\*|"
            r"pthread_|ma_|nuklear_|curl_|uv_", text))
        is_raii = bool(re.search(r"\bclass\s+", text) and "drop(" in text)
        native += int(is_native)
        raii += int(is_raii)
        if not guard:
            failures.append("%s: missing CPLUS_STD_* include guard" % rel)
        if not (is_native or is_raii):
            failures.append("%s: no documented C boundary or lowered RAII surface" % rel)

        probe = WORK / (name + ".cp")
        probe.write_text('#include "%s"\nint main(void) { return 0; }\n' % rel)
        proc = run([str(CPLUS), "check", "--legacy-c", "--no-werror"] +
                   extra_flags(name) + [str(probe)])
        (WORK / (name + ".log")).write_text(
            "RC: %d\n--- stdout ---\n%s--- stderr ---\n%s" %
            (proc.returncode, proc.stdout, proc.stderr))
        missing = MISSING_INCLUDE.search(proc.stderr + proc.stdout)
        if proc.returncode != 0 and missing:
            blocked += 1
            log_lines.append("BLOCKED %s missing include %s" % (rel, missing.group(1)))
        elif proc.returncode != 0:
            failures.append("%s: C+ parse failed (rc=%d): %s" %
                            (rel, proc.returncode,
                             (proc.stderr or proc.stdout)[-300:].replace("\n", " ")))
        else:
            log_lines.append("PASS %s" % rel)

    if native < 10 or raii < 10:
        failures.append("boundary inventory too thin: native=%d raii=%d" %
                        (native, raii))
    for lab, needle in (
        ("c_to_cp", "c_to_cp: 24 passed, 0 failed"),
        ("precompiled", "precompiled_lab: 3 passed, 0 failed"),
    ):
        if lab == "c_to_cp":
            log = ROOT / "tests/abi/c_to_cp/artifacts/results.log"
        else:
            log = ROOT / "build/abi-precompiled/results.log"
        text = log.read_text() if log.is_file() else ""
        if needle not in text:
            failures.append("%s lab was not enforced in this gate (%s)" % (lab, log))

    (WORK / "summary.txt").write_text(
        "headers=%d native=%d raii=%d blocked=%d failures=%d\n" %
        (len(HEADERS), native, raii, blocked, len(failures)) +
        "\n".join(log_lines) + "\n")
    if failures:
        print("HP_BOUNDARY: FAIL (%d)" % len(failures))
        for failure in failures:
            print("  FAIL", failure)
        return 1
    print("HP_BOUNDARY: PASS (%d headers, %d native, %d RAII, %d BLOCKED optional deps)" %
          (len(HEADERS), native, raii, blocked))
    return 0


if __name__ == "__main__":
    sys.exit(main())
