#!/usr/bin/env python3
"""Permanent evidence for the opt-in ``--auto-runtime`` constructor.

The rewriter emits, ahead of the first #line in the generated C::

  #if defined(CPLUS_AUTO_RUNTIME)
  extern void cplus_set_auto_runtime(int);
  static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
    cplus_set_auto_runtime(1);
  }
  #endif

The driver defines CPLUS_AUTO_RUNTIME=1 only for ``--auto-runtime`` builds, so
all other builds (LEGACY included) keep their previous bytes and behaviour.

Three runtime cases share one opaque shape: a ``volatile`` divisor passed
through a function parameter.  Static analysis cannot fold it away, so the
failure is reached at run time through ``__cplus_checked_div_i64`` ->
``cplus_fatal`` (CPLUS_EXIT_ARITHMETIC = 110), the path the runtime converts to
a catchable exception when auto-runtime is enabled.

  1. no ``--auto-runtime``: fatal with the original exit code 110;
  2. ``--auto-runtime`` + try/catch: diagnostic printed, handler runs, code
     after the try runs (exact stdout asserted);
  3. ``--auto-runtime`` without a handler: still fatal with exit code 110.

Two structural checks complete the suite: the emitted C contains the guarded
constructor before its first #line, and LEGACY byte preservation is unchanged.
Hard timeouts, no ``|| true``, exact stdout/exit assertions.

The two .cp fixtures deliberately emit [CPLUS-ARITH] ("division by zero cannot
be excluded"): the opaque volatile divisor is exactly what keeps the violation
runtime-reachable instead of compile-time-folded. Since the default policy
promotes every C+ warning to an error (docs/LANGUAGE.md), those builds use
``--no-werror``, the documented diagnostic-only opt-out. It does not disable
the analysis or the runtime check; the warning is still printed and the runtime
path is still asserted below. The LEGACY control needs no opt-out: --legacy-c
does not promote warnings by default.
"""
import datetime
import json
import os
import platform
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
OUT = os.path.join(HERE, "results")
os.makedirs(OUT, exist_ok=True)

BUILD_TIMEOUT = 60
RUN_TIMEOUT = 10
EXPECTED_RC = 110  # CPLUS_EXIT_ARITHMETIC
DIAGNOSTIC = "C+ runtime error: division by zero"
STDOUT_START = "start\n"
STDOUT_HANDLED = "start\ncaught 110\nafter try\n"


def run(args, timeout):
    """Run a command with a hard timeout; never mask a failure."""
    return subprocess.run(args, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout)


def describe(result):
    return {"rc": result.returncode,
            "stdout": result.stdout.decode(errors="replace"),
            "stderr": result.stderr.decode(errors="replace")}


def add(rows, fails, name, ok, detail, **evidence):
    row = {"case": name, "status": "PASS" if ok else "FAIL", "detail": detail}
    row.update(evidence)
    rows.append(row)
    if not ok:
        fails.append(name)
    print("%-30s %s" % (name, row["status"]))
    return ok


def runtime_case(rows, fails, name, src, out_name, extra, want_rc, want_out):
    """Build one fixture, run it, and assert exact rc and stdout."""
    exe = os.path.join(OUT, out_name)
    # The .cp fixtures intentionally warn [CPLUS-ARITH] so the failure is
    # reached at run time; --no-werror is the documented diagnostic-only
    # opt-out from the default warnings-as-errors policy.
    argv = [BIN, "build", "--default", "--sanitize=none", "--no-werror", *extra,
            os.path.join(FIX, src), "-o", exe]
    b = run(argv, BUILD_TIMEOUT)
    evidence = {"build_command": " ".join(argv), "build": describe(b)}
    if b.returncode != 0 or not os.path.exists(exe):
        add(rows, fails, name, False, "build failed (rc=%d)" % b.returncode,
            **evidence)
        return
    r = run([exe], RUN_TIMEOUT)
    evidence["run_command"] = exe
    evidence["run"] = describe(r)
    out = r.stdout.decode(errors="replace")
    err = r.stderr.decode(errors="replace")
    ok = (r.returncode == want_rc and out == want_out and DIAGNOSTIC in err)
    add(rows, fails, name, ok,
        "rc=%d want=%d, stdout=%r want=%r, diagnostic=%s" %
        (r.returncode, want_rc, out, want_out, DIAGNOSTIC in err), **evidence)


def main():
    rows, fails = [], []
    if not os.path.exists(BIN):
        print("missing compiler binary: %s" % BIN)
        return 2

    # 1. Without --auto-runtime the violation keeps its exact C+ exit code.
    runtime_case(rows, fails, "fatal_without_auto_runtime", "no_handler.cp",
                 "no_handler.bin", [], EXPECTED_RC, STDOUT_START)

    # 2. With --auto-runtime and a live try frame: diagnostic + handler +
    #    continuation, with exact stdout.
    runtime_case(rows, fails, "handled_with_auto_runtime", "with_handler.cp",
                 "with_handler.bin", ["--auto-runtime"], 0, STDOUT_HANDLED)

    # 3. With --auto-runtime but no handler: no silent continuation.
    runtime_case(rows, fails, "fatal_without_handler_auto_runtime",
                 "no_handler.cp", "no_handler.auto.bin", ["--auto-runtime"],
                 EXPECTED_RC, STDOUT_START)

    # Structural check: guarded constructor is emitted before the first #line.
    gen = os.path.join(OUT, "with_handler.generated.c")
    argv = [BIN, "build", "--default", "--sanitize=none", "--no-werror",
            "--emit-c", gen, os.path.join(FIX, "with_handler.cp"), "-o",
            os.path.join(OUT, "with_handler.emit.bin")]
    b = run(argv, BUILD_TIMEOUT)
    generated = open(gen, "rb").read().decode(errors="replace") \
        if os.path.exists(gen) else ""
    guards = ("#if defined(CPLUS_AUTO_RUNTIME)",
              "extern void cplus_set_auto_runtime(int);",
              "static void __attribute__((constructor))",
              "cplus_set_auto_runtime(1);")
    first_line = generated.find("#line ")
    ctor = generated.find("__cplus_auto_runtime_init")
    ok = (b.returncode == 0 and all(g in generated for g in guards) and
          ctor != -1 and first_line != -1 and ctor < first_line)
    add(rows, fails, "constructor_before_first_line", ok,
        "build_rc=%d missing_guards=%s ctor@%d first_line@%d" %
        (b.returncode, [g for g in guards if g not in generated],
         ctor, first_line),
        build_command=" ".join(argv), build=describe(b))

    # LEGACY bytes stay identical (the constructor block must not leak).
    legacy_src = os.path.join(FIX, "legacy_preserve.c")
    legacy_gen = os.path.join(OUT, "legacy_preserve.c")
    argv = [BIN, "build", "--legacy-c", "--sanitize=none", "--emit-c",
            legacy_gen, legacy_src, "-o", os.path.join(OUT, "legacy.bin")]
    b = run(argv, BUILD_TIMEOUT)
    raw = open(legacy_src, "rb").read()
    emitted = open(legacy_gen, "rb").read() if os.path.exists(legacy_gen) else b""
    m = re.match(rb'^#line \d+ "[^"]*"\n', emitted)
    bytes_equal = bool(m) and emitted[m.end():] == raw
    ok = b.returncode == 0 and bytes_equal
    add(rows, fails, "legacy_source_preserved", ok,
        "build_rc=%d prefix=%s bytes_equal=%s" %
        (b.returncode, bool(m), bytes_equal),
        build_command=" ".join(argv), build=describe(b))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "expected_exit_code": EXPECTED_RC, "cases": len(rows),
               "passed": len(rows) - len(fails), "failed": len(fails),
               "results": rows, "failures": fails}
    json.dump(payload, open(os.path.join(OUT, "summary.json"), "w"), indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("C+ --auto-runtime evidence\n%s\nc+ %s\n\n" %
                (payload["host"], BIN))
        for row in rows:
            f.write("%-30s %s\n" % (row["case"], row["status"]))
            f.write("  %s\n" % row["detail"])
        f.write("\ncases: %d, passed: %d, failed: %d\n" %
                (payload["cases"], payload["passed"], payload["failed"]))
        for name in fails:
            f.write("FAIL %s\n" % name)
    print("auto_runtime: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
