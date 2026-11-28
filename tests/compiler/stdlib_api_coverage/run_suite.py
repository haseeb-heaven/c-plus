#!/usr/bin/env python3
"""Build and run the generated safe-C API coverage suite under two compilers.

Reference:  /usr/bin/clang -std=c11 -O2
Under test: c+ build --legacy-c --sanitize=none

Both builds use the same generated C files, headers and link archives from
STDLIB_ROOT (accepted from the environment; the sibling repo is used only as a
fallback for local convenience).  Every scenario's exact stdout and exit code
must be identical between the two builds.

Usage:
  python3 run_suite.py            # full differential run
  python3 run_suite.py --bake     # measure reference twice, then freeze exp.json
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "generated")
BUILD = os.path.join(HERE, "build")
EXPECTED = os.path.join(HERE, "exp.json")
CLANG = "/usr/bin/clang"
CPLUS = "c+"

# Compute the sibling repo relative to this script; never hard-code an
# absolute user path in the runner or in generated C.
DEFAULT_STDLIB = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "..", "c-plus-stdlib"))
STDLIB_ROOT = os.environ.get("STDLIB_ROOT", DEFAULT_STDLIB)

LINE = re.compile(
    r"^SCENARIO (\S+) (PASS|FAIL) out=([0-9a-f]*) exit=(\d+) "
    r"expo=([0-9a-f]*) expe=(\d+)$")
SUMMARY = re.compile(r"^SUMMARY (total|pass|fail)=(\d+)$")

WARN = ["-Wno-format-nonliteral", "-Wno-format-security",
        "-Wno-unused-function", "-Wno-incompatible-library-redeclaration"]
# c+'s legacy-C lowering does not accept the `restrict` keyword used by
# cplus_libc.h; neutralise it identically for both compilers. No sources or
# headers differ between the two builds.
COMPAT = ["-Drestrict="]


def inc_lib():
    if not os.path.isdir(STDLIB_ROOT):
        raise SystemExit("STDLIB_ROOT not found: %s" % STDLIB_ROOT)
    inc = ["-I" + HERE, "-I" + STDLIB_ROOT,
           "-I" + os.path.join(STDLIB_ROOT, "stdlib", "safe"),
           "-I" + os.path.join(STDLIB_ROOT, "libc", "install-include"),
           "-I" + os.path.join(STDLIB_ROOT, "libc"),
           "-I" + os.path.join(STDLIB_ROOT, "third_party", "safeclib", "include"),
           "-I" + os.path.join(STDLIB_ROOT, "third_party",
                               "intel-safestringlib", "include"),
           "-I" + os.path.join(STDLIB_ROOT, "third_party", "exceptions4c")]
    libs = [os.path.join(STDLIB_ROOT, "build", "safe", "libcplus_safecrt.a"),
            os.path.join(STDLIB_ROOT, "build", "libc", "libcplus_hardened.a"),
            "-lm", "-pthread"]
    for p in libs[:2]:
        if not os.path.isfile(p):
            raise SystemExit("missing archive: %s (build c-plus-stdlib first)" % p)
    return inc, libs


def compile_one(src, out, kind, inc, libs):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if kind == "clang":
        cmd = [CLANG, "-std=c11", "-O2"] + WARN + COMPAT + inc + [src] + libs + ["-o", out]
    else:
        cmd = [CPLUS, "build", "--legacy-c", "--sanitize=none", "-o", out,
               src, "--"] + COMPAT + inc + libs
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=300, text=True)
    if p.returncode != 0 or not os.path.isfile(out):
        raise SystemExit("BUILD FAILED (%s) for %s:\n%s" %
                         (kind, src, p.stdout[-4000:]))
    return p.stdout


def run_one(binary):
    p = subprocess.run([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=180, env=dict(os.environ, STDLIB_ROOT=STDLIB_ROOT))
    rows = {}
    summary = {}
    bad = []
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        m = LINE.match(line)
        if m:
            name, status, outh, ex, expoh, expe = m.groups()
            rows[name] = {"line": line, "status": status,
                          "out": bytes.fromhex(outh).decode("utf-8", "replace"),
                          "exit": int(ex)}
            if int(ex) >= 128:
                bad.append((name, int(ex)))  # signal, not a defined trap
            continue
        m = SUMMARY.match(line)
        if m:
            summary[m.group(1)] = int(m.group(2))
    return {"rows": rows, "summary": summary, "rc": p.returncode,
            "stderr": p.stderr.decode("utf-8", "replace"), "signals": bad}


def bake(results, manifest):
    observed = {}
    for g in manifest["files"]:
        for name, r in results[g]["clang"]["rows"].items():
            b = r["out"]
            if any(ord(c) < 32 and c != "\n" for c in b) or "\x00" in b:
                raise SystemExit("non-string stdout for %s: %r" % (name, b))
            observed[name] = {"out": b, "exit": r["exit"]}
    with open(EXPECTED, "w") as f:
        json.dump(observed, f, indent=2, sort_keys=True)
        f.write("\n")
    print("baked %d expected results into %s" % (len(observed), EXPECTED))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bake", action="store_true",
                    help="measure reference twice, verify determinism, freeze exp.json")
    args = ap.parse_args()

    subprocess.run([sys.executable, os.path.join(HERE, "generate.py")], check=True)
    with open(os.path.join(GEN, "manifest.json")) as f:
        manifest = json.load(f)
    inc, libs = inc_lib()
    if os.path.isdir(BUILD):
        shutil.rmtree(BUILD)

    results = {}
    print("STDLIB_ROOT=%s" % STDLIB_ROOT)
    for g in manifest["files"]:
        src = os.path.join(GEN, g)
        print("== %s ==" % g)
        results[g] = {}
        for kind in (["clang", "clang"] if args.bake else ["clang", "cplus"]):
            out = os.path.join(BUILD, g[:-2] + "." + kind)
            build_log = compile_one(src, out, kind, inc, libs)
            if build_log.strip():
                print("  %s build diagnostics: %d bytes" % (kind, len(build_log)))
            if kind == "clang" and args.bake and "clang2" in results[g]:
                continue
            key = "clang2" if (args.bake and "clang" in results[g]) else kind
            results[g][key] = run_one(out)
            print("  %s: %d scenarios, rc=%d" %
                  (key, len(results[g][key]["rows"]), results[g][key]["rc"]))

    for g in manifest["files"]:
        for key in ("clang", "clang2", "cplus"):
            r = results[g].get(key)
            if not r:
                continue
            if r["signals"]:
                raise SystemExit("signal exit(s) in %s/%s: %r" % (g, key, r["signals"]))
    if args.bake:
        for g in manifest["files"]:
            a, b = results[g]["clang"], results[g]["clang2"]
            ka = {k: v["line"] for k, v in a["rows"].items()}
            kb = {k: v["line"] for k, v in b["rows"].items()}
            if ka != kb:
                diffs = [k for k in ka if ka.get(k) != kb.get(k)]
                raise SystemExit("reference is nondeterministic in %s: %r" %
                                 (g, diffs[:10]))
        bake(results, manifest)
        return

    # ---- differential verdict ---------------------------------------------
    all_names = []
    pass_n = fail_n = 0
    failures = []
    for g in manifest["files"]:
        ref = results[g]["clang"]
        tgt = results[g]["cplus"]
        for name in sorted(set(ref["rows"]) | set(tgt["rows"])):
            all_names.append(name)
            a = ref["rows"].get(name, {}).get("line")
            b = tgt["rows"].get(name, {}).get("line")
            if a == b and a is not None:
                pass_n += 1
            else:
                fail_n += 1
                failures.append({"scenario": name, "clang": a, "cplus": b})
    apis = set(manifest["apis"])
    covered = set(n.split("|", 1)[0] for n in all_names)
    zero = sorted(apis - covered)
    summary = {
        "compiler_reference": CLANG + " -std=c11 -O2",
        "compiler_under_test": CPLUS + " build --legacy-c --sanitize=none",
        "stdlib_root": STDLIB_ROOT,
        "apis_discovered": len(apis),
        "apis_with_scenarios": len(covered & apis),
        "apis_with_zero_scenarios": zero,
        "apis_excluded_external_state": [],
        "scenarios_generated": manifest["scenario_count"],
        "scenarios_executed": len(all_names),
        "scenarios_passed": pass_n,
        "scenarios_failed": fail_n,
        "code_coverage_measured": False,
        "code_coverage_note": "No gcov instrumentation in this suite; "
                              "API/scenario coverage is measured, line coverage is not.",
        "failures": failures[:50],
    }
    with open(os.path.join(HERE, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")
    with open(os.path.join(HERE, "summary.txt"), "w") as f:
        for k, v in summary.items():
            f.write("%s: %s\n" % (k, json.dumps(v) if isinstance(v, (list, dict)) else v))
    print("\napis=%d executed=%d passed=%d failed=%d zero_api=%d" %
          (summary["apis_discovered"], summary["scenarios_executed"],
           pass_n, fail_n, len(zero)))
    for x in failures[:20]:
        print("DIFF %s\n  clang: %s\n  c+   : %s" %
              (x["scenario"], x["clang"], x["cplus"]))
    return 1 if fail_n or zero else 0


if __name__ == "__main__":
    sys.exit(main())
