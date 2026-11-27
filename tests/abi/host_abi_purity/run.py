#!/usr/bin/env python3
"""Host-type / public-struct ABI parity runner (no compiler build required).

Compiles ``host_types.c`` twice with the host C compiler, compares the two
canonical record streams byte-for-byte, and diffs the ABI-relevant
preprocessor facts.  Any build failure, run failure or difference exits 1.
Logs are written under ``results/`` next to this file.
"""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RESULTS = HERE / "results"
SRC = HERE / "host_types.c"
CC = os.environ.get("CPLUS_ABI_PURITY_CC") or shutil.which("clang") or shutil.which("cc")
TIMEOUT = int(os.environ.get("ABI_TIMEOUT", "90"))
INCLUDES = [
    "-I" + str(ROOT / "libc/install-include"),
    "-I" + str(ROOT / "stdlib/safe"),
    "-I" + str(ROOT / "runtime/include"),
]
COMMON = ["-std=c11", "-Wall", "-Wextra", "-Werror"]
DEFINES = [
    r"__SIZEOF_POINTER__", r"__SIZEOF_LONG__", r"__SIZEOF_INT__",
    r"__SIZEOF_SIZE_T__", r"__BYTE_ORDER__", r"__LP64__", r"__ILP32__",
    r"__MACH__", r"__linux__", r"__ELF__", r"__APPLE__", r"__arm64__",
    r"__x86_64__", r"_WIN32", r"_WIN64",
]

if not CC:
    print("HOST_ABI_PURITY: FAIL (no C compiler; set CPLUS_ABI_PURITY_CC)")
    sys.exit(1)

RESULTS.mkdir(parents=True, exist_ok=True)


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                              timeout=TIMEOUT)
    except subprocess.TimeoutExpired as exc:
        return subprocess.CompletedProcess(cmd, 124, "", str(exc))


def log(name: str, proc: subprocess.CompletedProcess[str]) -> None:
    (RESULTS / (name + ".log")).write_text(
        "CMD: %s\nRC: %d\n--- stdout ---\n%s--- stderr ---\n%s" %
        (" ".join(proc.args), proc.returncode, proc.stdout, proc.stderr))


def compile_variant(tag: str, extra: list[str]) -> pathlib.Path:
    binary = RESULTS / ("host_types." + tag)
    proc = run([CC] + COMMON + INCLUDES + extra +
               [str(SRC), "-o", str(binary)])
    log("build_" + tag, proc)
    if proc.returncode != 0:
        print("HOST_ABI_PURITY: FAIL build %s (rc=%d)" % (tag, proc.returncode))
        print(proc.stderr[-2000:])
        raise SystemExit(1)
    return binary


def records(binary: pathlib.Path, tag: str) -> str:
    proc = run([str(binary)])
    log("run_" + tag, proc)
    if proc.returncode != 0:
        print("HOST_ABI_PURITY: FAIL run %s (rc=%d)" % (tag, proc.returncode))
        raise SystemExit(1)
    (RESULTS / ("records_" + tag + ".txt")).write_text(proc.stdout)
    return proc.stdout


def cpp_defines(tag: str, extra: list[str]) -> str:
    proc = run([CC, "-std=c11", "-dM", "-E", "-x", "c"] + INCLUDES + extra +
               [str(SRC)])
    log("cpp_" + tag, proc)
    if proc.returncode != 0:
        print("HOST_ABI_PURITY: FAIL preprocessor %s (rc=%d)" %
              (tag, proc.returncode))
        raise SystemExit(1)
    pattern = re.compile(r"^#define (" + "|".join(DEFINES) + r")\b.*$")
    lines = sorted(line for line in proc.stdout.splitlines()
                   if pattern.match(line))
    (RESULTS / ("defines_" + tag + ".txt")).write_text("\n".join(lines) + "\n")
    return "\n".join(lines) + "\n"


def main() -> int:
    base_extra: list[str] = []
    with_extra = ["-DCPLUS_ABI_PURITY_WITH_HEADERS=1"]
    base_bin = compile_variant("plain", base_extra)
    with_bin = compile_variant("headers", with_extra)
    base_records = records(base_bin, "plain")
    with_records = records(with_bin, "headers")
    base_defines = cpp_defines("plain", base_extra)
    with_defines = cpp_defines("headers", with_extra)

    failures = []
    if not base_records.strip():
        failures.append("plain probe produced no records")
    if base_records != with_records:
        failures.append("layout records differ with C+ headers included")
        (RESULTS / "records.diff").write_text(
            "--- plain ---\n" + base_records + "--- headers ---\n" + with_records)
    if base_defines != with_defines:
        failures.append("ABI-relevant preprocessor defines changed")
        (RESULTS / "defines.diff").write_text(
            "--- plain ---\n" + base_defines + "--- headers ---\n" + with_defines)
    if failures:
        print("HOST_ABI_PURITY: FAIL (%d)" % len(failures))
        for failure in failures:
            print("  FAIL", failure)
        return 1
    print("HOST_ABI_PURITY: PASS (%d layout records, %d macro records)" %
          (len(base_records.splitlines()), len(base_defines.splitlines())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
