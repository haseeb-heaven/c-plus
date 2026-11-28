#!/usr/bin/env python3
"""Deterministic regression suite for issue #201 (TypeChecker marker checks).

`isCPlusRecord()` used to classify *any* record carrying an `AnnotateAttr` as a
C+ class, and `TypeChecker::run()` read only the first `AnnotateAttr` when
deciding whether it was a resource.  This suite drives `TypeChecker::run()`
directly on fixtures with unrelated, look-alike and multiple annotations and
asserts the exact TypeRegistry contents.

The c+ CLI does not call TypeChecker, so a CLI-only suite cannot observe this
bug.  The driver compiles this directory's `typechecker_probe.cpp` together
with a freshly compiled `compiler/Sema/TypeChecker.cpp`, reusing the include /
define flags from `build/compile_commands.json` and the link line from
`build/CMakeFiles/c+.dir/link.txt`.  The freshly compiled object is placed
before `build/libcplus_core.a` so a stale copy inside the archive cannot win
symbol resolution.

Run (after the parent session has configured/built the tree):

    CPLUS_BUILD_DIR=$PWD/build python3 tests/compiler/typechecker_issue_201/run_suite.py

All cases are generated into `cases/`; verbatim commands, exit codes and
outputs are recorded into `results/summary.json` and `results/summary.txt`.
No case is skipped and no failure is suppressed.  Hard timeouts throughout.
"""
import datetime
import json
import os
import platform
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.abspath(
    os.environ.get("CPLUS_BUILD_DIR", os.path.join(ROOT, "build")))
CASES_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASES_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

COMPILE_TIMEOUT = 120
LINK_TIMEOUT = 300
PROBE_TIMEOUT = 90


def rec(name, resource):
    """Expected registry entry for a marker-recognised record."""
    return {"name": name, "resource": resource,
            "ctor": name + "_new", "drop": name + "_drop"}


# Each tuple: (case name, fixture source, expected TypeRegistry records).
# All fixtures keep real resources destructible so no case fails on the
# separate 'resource must declare a destructor' rule; expected errors == 0.
CASES = [
    ("neg_unrelated_struct",
     'struct Packet { int header; };\n'
     '// ANNOTATE Packet packed_wire\n',
     []),
    ("neg_unrelated_union",
     'union Wire { int i; float f; };\n'
     '// ANNOTATE Wire ffi_export\n',
     []),
    ("neg_prefix_lookalike",
     'struct Almost { int x; };\n'
     '// ANNOTATE Almost cplus_resource_v2\n',
     []),
    ("neg_multiple_unrelated",
     'struct Plain { int x; };\n'
     '// ANNOTATE Plain reflect align_tag\n',
     []),
    ("pos_class_marker_first",
     'struct Widget { int id; };\n'
     '// ANNOTATE Widget cplus_class\n',
     [rec("Widget", 0)]),
    ("pos_class_marker_second",
     'struct Widget { int id; };\n'
     '// ANNOTATE Widget serializable cplus_class\n',
     [rec("Widget", 0)]),
    ("pos_resource_marker_second",
     'struct Res { ~Res() {} int id; };\n'
     '// ANNOTATE Res ffi_export cplus_resource\n',
     [rec("Res", 1)]),
    ("pos_resource_marker_first",
     'struct Res { ~Res() {} int id; };\n'
     '// ANNOTATE Res cplus_resource reflect\n',
     [rec("Res", 1)]),
    ("mixed_tu_only_real_markers",
     'struct Packet { int h; };\n'
     'struct Widget { int id; };\n'
     'struct Res { ~Res() {} int id; };\n'
     '// ANNOTATE Packet packed_wire\n'
     '// ANNOTATE Widget cplus_class\n'
     '// ANNOTATE Res ffi_export cplus_resource\n',
     [rec("Widget", 0), rec("Res", 1)]),
]


def run(argv, timeout, cwd=None):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, cwd=cwd)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def record(rows, fails, name, ok, detail, steps):
    row = {"case": name, "status": "PASS" if ok else "FAIL",
           "detail": detail, "steps": steps}
    rows.append(row)
    if not ok:
        fails.append(name)
    print("%-30s %s" % (name, row["status"]))
    return ok


def write_summary(rows, build_steps):
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "build_dir": BUILD_DIR,
               "build_steps": build_steps, "results": rows}
    payload["failures"] = [r["case"] for r in rows if r["status"] == "FAIL"]
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("TypeChecker issue #201 regression suite\n%s\nbuild: %s\n\n"
                % (payload["host"], BUILD_DIR))
        for s in payload["build_steps"]:
            f.write("cmd: %s\nrc=%s out=%r err=%r\n" %
                    (" ".join(s["command"]), s["rc"], s["stdout"][:1000],
                     s["stderr"][:1000]))
        f.write("\n")
        for r in rows:
            f.write("%-30s %s  %s\n" % (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:1000],
                         s["stderr"][:1000]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(payload["failures"]),
                 len(payload["failures"])))
    return payload


def build_harness(rows, fails):
    """Compile TypeChecker.cpp + probe with the build tree's own flags.

    Returns (probe_path, ok).  Commands and outputs are recorded in rows.
    """
    compile_db = os.path.join(BUILD_DIR, "compile_commands.json")
    link_txt = os.path.join(BUILD_DIR, "CMakeFiles", "c+.dir", "link.txt")
    core_a = os.path.join(BUILD_DIR, "libcplus_core.a")
    for path in (compile_db, link_txt, core_a):
        if not os.path.exists(path):
            print("missing build artifact: %s (configure/build the tree first)"
                  % path, file=sys.stderr)
            return None, False

    with open(compile_db) as f:
        db = json.load(f)
    entries = [e for e in db
               if os.path.basename(e.get("file", "")).endswith("TypeChecker.cpp")]
    if len(entries) != 1:
        print("expected exactly one TypeChecker.cpp compile command, found %d"
              % len(entries), file=sys.stderr)
        return None, False
    cmd = shlex.split(entries[0]["command"])
    out_idx = max(i for i, t in enumerate(cmd) if t == "-o")
    compiler, flags = cmd[0], cmd[1:out_idx]

    tc_obj = os.path.join(OUT, "TypeChecker.issue201.o")
    probe_obj = os.path.join(OUT, "typechecker_probe.o")
    probe_exe = os.path.join(OUT, "typechecker_probe")
    tc_src = os.path.join(ROOT, "compiler", "Sema", "TypeChecker.cpp")
    probe_src = os.path.join(HERE, "typechecker_probe.cpp")

    for label, src, obj in (("compile_typechecker", tc_src, tc_obj),
                            ("compile_probe", probe_src, probe_obj)):
        step = run([compiler] + flags + ["-o", obj, "-c", src], COMPILE_TIMEOUT)
        ok = step["rc"] == 0 and not step["timed_out"]
        record(rows, fails, label, ok, "rc=%s" % step["rc"], [step])
        if not ok:
            return None, False

    with open(link_txt) as f:
        link = shlex.split(f.read())
    link = [t for t in link if not t.endswith("Main.cpp.o")]
    out_idx = link.index("-o")
    link[out_idx + 1] = probe_exe
    # Put the freshly built objects ahead of libcplus_core.a so the stale
    # TypeChecker.o inside the archive is never pulled in.
    link = [link[0], tc_obj, probe_obj] + link[1:]
    step = run(link, LINK_TIMEOUT, cwd=BUILD_DIR)
    ok = step["rc"] == 0 and not step["timed_out"]
    record(rows, fails, "link_probe", ok, "rc=%s" % step["rc"], [step])
    if not ok:
        return None, False
    return probe_exe, True


def parse_output(text):
    records, diags, errors = [], [], None
    for line in text.splitlines():
        if line.startswith("record "):
            fields = {}
            for token in line[len("record "):].split():
                key, sep, value = token.partition("=")
                if sep:
                    fields[key] = value
            try:
                resource = int(fields.get("resource", "-1"))
            except ValueError:
                resource = -1
            records.append({"name": fields.get("name"), "resource": resource,
                            "ctor": fields.get("ctor"), "drop": fields.get("drop")})
        elif line.startswith("diag "):
            diags.append(line[len("diag "):])
        elif line.startswith("errors="):
            try:
                errors = int(line[len("errors="):])
            except ValueError:
                errors = None
    return records, diags, errors


def main():
    rows, fails = [], []
    probe, built = build_harness(rows, fails)
    if built:
        for name, source, expected in CASES:
            path = os.path.join(CASES_DIR, name + ".cpp")
            with open(path, "w") as f:
                f.write(source)
            step = run([probe, path], PROBE_TIMEOUT)
            records, diags, errors = parse_output(step["stdout"])
            ok = (step["rc"] == 0 and not step["timed_out"] and
                  records == expected and errors == 0 and not diags)
            detail = ("rc=%s expected_records=%s got_records=%s errors=%s diags=%s"
                      % (step["rc"], expected, records, errors, diags))
            record(rows, fails, name, ok, detail, [step])

    # Build evidence is the first three rows (compile_typechecker,
    # compile_probe, link_probe); case evidence stays inside each case row.
    build_steps = []
    for r in rows[:3]:
        build_steps.extend(r["steps"])
    payload = write_summary(rows, build_steps)
    print("typechecker_issue_201: %d cases, %d failures" %
          (len(payload["results"]), len(payload["failures"])))
    return 1 if payload["failures"] or not built else 2


if __name__ == "__main__":
    sys.exit(main())
