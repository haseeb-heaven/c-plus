#!/usr/bin/env python3
"""Header-compatibility regressions for the C+ rewriter (compile-only).

For every case and both modes (--legacy-c and --default, always with
--sanitize=none) this harness:

  * invokes "c+ check" with a hard per-invocation timeout -- no linking,
    no execution, no sanitizer instrumentation;
  * requires rc == 0 (a negative rc is a fatal signal, never a pass) and zero
    "[CPLUS-" diagnostics on either stream;
  * requires --emit-c to contain exactly one leading #line 1 "<src>" directive
    followed by the original source byte for byte. The #include lines are
    therefore preserved verbatim and no header-derived token is injected or
    rewritten by the rewriter;
  * hashes the vendored headers and the local sqlite3 stub before and after
    the run, so a rewriter that wrote into a header is a hard failure.

"windows_guard" includes <windows.h> only under #ifdef _WIN32. This host is
macOS, so its Windows branch is reported as UNTESTED_ON_HOST and is never
counted as a pass. The vendored headers are included as declarations (the
reference-suite pattern); their implementation macros are deliberately not
defined because those bodies legitimately trigger DEFAULT-mode safety gates
and would turn this passthrough harness into a diagnostic expectation test.

No network, no downloads, no wall-clock-dependent assertions.

Run from the repository root:
    CPLUS_BIN=$PWD/build/c+ python3 tests/header_compat/run_suite.py
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
ENV = dict(os.environ)
ENV.pop("SDKROOT", None)      # same pinning as real_world_headers/run_suite.py
ENV.pop("CPLUS_CLANG", None)  # pin the backend to the driver default
TIMEOUT = 90                  # hard timeout per c+ invocation, seconds
MODES = ("--legacy-c", "--default")
GATE = "[CPLUS-"
STUB_DIR = os.path.join(HERE, "fixtures")
STB_INC = ["-I" + os.path.join(ROOT, "third_party", "stb")]
MINIAUDIO_INC = ["-I" + os.path.join(ROOT, "third_party", "miniaudio")]
SQLITE_INC = ["-I" + STUB_DIR]

# Hash before and after; the rewriter must never write into these files.
WATCHED_HEADERS = [
    os.path.join(ROOT, "third_party", "stb", "stb_image.h"),
    os.path.join(ROOT, "third_party", "miniaudio", "miniaudio.h"),
    os.path.join(STUB_DIR, "sqlite3.h"),
]


def src(*lines):
    """Build a deterministic C translation unit with LF line endings."""
    return "\n".join(lines) + "\n"


# (name, source, extra include flags, status). "untested" is never a pass.
CASES = [
    ("system_pthread", src(
        "#include <pthread.h>",
        "#include <stdio.h>",
        "int main(void) { pthread_t t; (void)t; printf(\"ok\\n\"); return 0; }",
    ), [], "pass"),
    ("system_socket", src(
        "#include <sys/socket.h>",
        "#include <stdio.h>",
        "int main(void) { int fd = -1; (void)fd; printf(\"ok\\n\"); return 0; }",
    ), [], "pass"),
    ("windows_guard", src(
        "#ifndef _WIN32",
        "#include <stdio.h>",
        "int main(void) { printf(\"ok\\n\"); return 0; }",
        "#else",
        "#include <windows.h>",
        "int main(void) { Sleep(0); return 0; }",
        "#endif",
    ), [], "untested"),
    ("vendored_stb", src(
        "#include <stdio.h>",
        "#include \"stb_image.h\"",
        "int main(void) { printf(\"ok\\n\"); return 0; }",
    ), STB_INC, "pass"),
    ("vendored_miniaudio", src(
        "#include <stdio.h>",
        "#include \"miniaudio.h\"",
        "int main(void) { printf(\"ok\\n\"); return 0; }",
    ), MINIAUDIO_INC, "pass"),
    ("sqlite3_stub", src(
        "#include <stdio.h>",
        "#include \"sqlite3.h\"",
        "int main(void) { printf(\"ok %d\\n\", SQLITE_OK); return 0; }",
    ), SQLITE_INC, "pass"),
    ("complex_attribute", src(
        "#define HC_UNUSED __attribute__((unused))",
        "#define HC_VIS __attribute__((visibility(\"default\")))",
        "static int hc_add(int x) HC_UNUSED;",
        "static int hc_add(int x) { return x + 1; }",
        "HC_VIS int hc_public(int x) { return hc_add(x); }",
        "int main(void) { return hc_public(0) == 1 ? 0 : 1; }",
    ), [], "pass"),
    ("complex_generic", src(
        "#include <stdio.h>",
        "int main(void) {",
        "  int x = 3;",
        "  int chosen = _Generic((x), int: 1, default: 0);",
        "  printf(\"ok %d\\n\", chosen);",
        "  return 0;",
        "}",
    ), [], "pass"),
    ("complex_variadic", src(
        "#include <stdio.h>",
        "#define HC_LOG(fmt, ...) printf(fmt, __VA_ARGS__)",
        "int main(void) { HC_LOG(\"ok %d\\n\", 7); return 0; }",
    ), [], "pass"),
    ("complex_compound", src(
        "#include <stdio.h>",
        "int main(void) {",
        "  int *items = (int[]){1, 2, 3};",
        "  printf(\"ok %d\\n\", items[1]);",
        "  return 0;",
        "}",
    ), [], "pass"),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def invoke(argv):
    rec = {"argv": argv, "rc": None, "timed_out": False,
           "stdout": b"", "stderr": b""}
    try:
        p = subprocess.run(argv, env=ENV, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=TIMEOUT, check=False)
    except subprocess.TimeoutExpired as e:
        rec["timed_out"] = True
        rec["stdout"] = e.stdout or b""
        rec["stderr"] = e.stderr or b""
    else:
        rec["rc"] = p.returncode
        rec["stdout"] = p.stdout
        rec["stderr"] = p.stderr
    return rec


def run_one(case, mode, workdir):
    name, text, includes, _status = case
    path = os.path.join(workdir, name + ".c")
    lowered = os.path.join(workdir, "%s.%s.c" % (name, mode.lstrip("-")))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    argv = [BIN, "check", mode, "--sanitize=none", "--emit-c", lowered,
            path] + includes
    rec = invoke(argv)
    diags = [line for line in (rec["stderr"] + b"\n" + rec["stdout"])
             .decode("utf-8", "replace").splitlines() if GATE in line]
    expected = ('#line 1 "%s"\n' % path).encode("utf-8") + text.encode("utf-8")
    emitted = None
    if os.path.isfile(lowered):
        with open(lowered, "rb") as f:
            emitted = f.read()
    passthrough_ok = emitted == expected
    problems = []
    if rec["timed_out"]:
        problems.append("timeout >%ds" % TIMEOUT)
    if rec["rc"] != 0:
        detail = "rc=%s" % rec["rc"]
        if rec["rc"] is not None and rec["rc"] < 0:
            detail += " (crash: signal %d)" % (-rec["rc"])
        problems.append(detail)
    if diags:
        problems.append("%d [CPLUS- diagnostic(s)]: %s"
                        % (len(diags), diags[0][:160]))
    if not passthrough_ok:
        problems.append("passthrough mismatch (emitted=%s expected=%d bytes)"
                        % ("missing" if emitted is None else str(len(emitted)),
                           len(expected)))
    return {"argv": argv, "rc": rec["rc"], "timed_out": rec["timed_out"],
            "crashed": rec["rc"] is not None and rec["rc"] < 0,
            "diagnostics": diags, "passthrough_ok": passthrough_ok,
            "problems": problems, "stderr": rec["stderr"]}


def main():
    if not os.path.isfile(BIN):
        print("missing compiler: " + BIN)
        return 2
    missing = [p for p in WATCHED_HEADERS if not os.path.isfile(p)]
    if missing:
        print("missing watched header(s): " + ", ".join(missing))
        return 2
    before = {p: sha256(p) for p in WATCHED_HEADERS}

    results = []
    with tempfile.TemporaryDirectory(prefix="cplus-header-compat-") as d:
        for case in CASES:
            runs = {mode: run_one(case, mode, d) for mode in MODES}
            results.append((case, runs))

    after = {p: sha256(p) for p in WATCHED_HEADERS}
    changed = [p for p in WATCHED_HEADERS if before[p] != after[p]]

    all_runs = [run for _case, runs in results for run in runs.values()]
    failures = []
    for case, runs in results:
        name, _text, _includes, status = case
        case_ok = all(not run["problems"] for run in runs.values())
        if not case_ok:
            label = "FAIL"
            failures.append((name, runs))
        else:
            label = "UNTESTED_ON_HOST" if status == "untested" else "PASS"
        cells = " ".join("%s=%s" % (m.lstrip("-"),
                                    "ok" if not runs[m]["problems"] else "FAIL")
                         for m in MODES)
        print("%-22s %-16s %s" % (name, label, cells))

    for p in changed:
        print("HEADER MODIFIED: " + p)
    for name, runs in failures:
        for mode in MODES:
            for problem in runs[mode]["problems"]:
                print("  %s %s: %s" % (name, mode, problem))
            if runs[mode]["problems"]:
                tail = runs[mode]["stderr"].decode("utf-8", "replace").strip()
                if tail:
                    print("    stderr: " + tail[:300].replace("\n", " | "))

    passed = sum(1 for case, runs in results
                 if case[3] == "pass" and all(not r["problems"]
                                              for r in runs.values()))
    untested = sum(1 for case, runs in results
                   if case[3] == "untested" and all(not r["problems"]
                                                    for r in runs.values()))
    print("header_compat: cases=%d passed=%d untested_on_host=%d failed=%d"
          % (len(CASES), passed, untested, len(failures)))
    print("header_compat: invocations=%d clean=%d diagnostics=%d timeouts=%d "
          "crashes=%d passthrough_mismatches=%d headers_modified=%d"
          % (len(all_runs),
             sum(1 for r in all_runs if not r["problems"]),
             sum(len(r["diagnostics"]) for r in all_runs),
             sum(1 for r in all_runs if r["timed_out"]),
             sum(1 for r in all_runs if r["crashed"]),
             sum(1 for r in all_runs if not r["passthrough_ok"]), len(changed)))
    return 1 if failures or changed else 0


if __name__ == "__main__":
    sys.exit(main())
