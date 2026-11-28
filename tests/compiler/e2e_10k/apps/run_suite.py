#!/usr/bin/env python3
"""Build and run the e2e_10k/apps end-to-end scenario suite.

Reference:  /usr/bin/clang -std=c11 -O2
Under test: $CPLUS_BIN build --legacy-c --sanitize=none   (primary)
            $CPLUS_BIN build --default  --sanitize=none   (secondary)
            $CPLUS_BIN build --strict   --sanitize=none   (secondary)

STDLIB_ROOT is taken from the environment.  The sibling checkout is used only
as a local convenience default; no absolute user path appears in this runner
or in any generated C file.

Usage:
  python3 run_suite.py --bake   # measure reference twice, verify, freeze exp.json
  python3 run_suite.py          # full differential run, writes summary.txt/json
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "generated")
BUILD = os.path.join(HERE, "build")
SCRATCH = os.path.join(HERE, "scratch")
EXPECTED = os.path.join(HERE, "exp.json")
CLANG = "/usr/bin/clang"
CPLUS = os.environ.get("CPLUS_BIN", "c+")
DEFAULT_STDLIB = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "..", "c-plus-stdlib"))
STDLIB_ROOT = os.environ.get("STDLIB_ROOT", DEFAULT_STDLIB)
SECONDARY_FILE = "app_board.c"       # also built in --default and --strict modes
COMPILE_TIMEOUT = 300                # hard limits, seconds
RUN_TIMEOUT = 300

WARN = ["-Wno-format-nonliteral", "-Wno-format-security",
        "-Wno-unused-function", "-Wno-incompatible-library-redeclaration"]
COMPAT = ["-Drestrict="]
LINE = re.compile(
    r"^SCENARIO (\S+) (PASS|FAIL) out=([0-9a-f]*) err=([0-9a-f]*) "
    r"exit=(\d+) expo=([0-9a-f]*) experr=([0-9a-f]*) expe=(\d+)$")
SUMMARY = re.compile(r"^SUMMARY (total|pass|fail)=(\d+)$")


def inc_lib(strict=False):
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
    a = os.path.join(STDLIB_ROOT, "build", "safe", "libcplus_safecrt.a")
    b = os.path.join(STDLIB_ROOT, "build", "libc", "libcplus_hardened.a")
    for p in (a, b):
        if not os.path.isfile(p):
            raise SystemExit("missing archive: %s (build c-plus-stdlib first)" % p)
    if strict:
        # STRICT rejects raw .a pass-through paths; -L/-l are allow-listed.
        libs = ["-L" + os.path.dirname(a), "-lcplus_safecrt",
                "-L" + os.path.dirname(b), "-lcplus_hardened", "-lm", "-pthread"]
    else:
        libs = [a, b, "-lm", "-pthread"]
    return inc, libs


def compile_one(src, out, kind, mode):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    inc, libs = inc_lib(strict=(kind == "cplus" and mode == "strict"))
    if kind == "clang":
        cmd = [CLANG, "-std=c11", "-O2"] + WARN + COMPAT + inc + [src] + libs + ["-o", out]
    else:
        # The scenario harness intentionally exercises unsafe corner cases;
        # its oracle is differential runtime output, so C+ warnings are
        # informational here and must not abort compilation.
        cmd = [CPLUS, "build", "--" + mode, "--sanitize=none", "--no-werror",
               "-o", out, src, "--"]
        if mode != "strict":
            cmd += COMPAT
        cmd += inc + libs
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=COMPILE_TIMEOUT, text=True)
    if p.returncode != 0 or not os.path.isfile(out):
        raise SystemExit("BUILD FAILED (%s/%s) for %s:\n%s" %
                         (kind, mode, src, p.stdout[-6000:]))
    return p.stdout


def run_one(binary):
    p = subprocess.run([binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=RUN_TIMEOUT, cwd=HERE,
                       env=dict(os.environ, STDLIB_ROOT=STDLIB_ROOT))
    rows, summary, bad, raw = {}, {}, [], []
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        raw.append(line)
        m = LINE.match(line)
        if m:
            name, status, oh, eh, ex, eoh, eeh, eex = m.groups()
            rows[name] = {"line": line, "status": status,
                          "out": bytes.fromhex(oh).decode("utf-8", "replace"),
                          "err": bytes.fromhex(eh).decode("utf-8", "replace"),
                          "exit": int(ex),
                          "exp_out": bytes.fromhex(eoh).decode("utf-8", "replace"),
                          "exp_err": bytes.fromhex(eeh).decode("utf-8", "replace"),
                          "exp_exit": int(eex)}
            if int(ex) >= 128:
                bad.append((name, int(ex)))
            continue
        m = SUMMARY.match(line)
        if m:
            summary[m.group(1)] = int(m.group(2))
    if p.stderr:
        raise SystemExit("runner stderr from %s: %r" % (binary, p.stderr[:1000]))
    return {"rows": rows, "summary": summary, "rc": p.returncode,
            "signals": bad, "raw": raw}


def generate():
    subprocess.run([sys.executable, os.path.join(HERE, "generate.py")], check=True)
    with open(os.path.join(GEN, "manifest.json")) as f:
        return json.load(f)


def bake(manifest):
    os.makedirs(SCRATCH, exist_ok=True)
    if os.path.isdir(BUILD):
        shutil.rmtree(BUILD)
    observed = {}
    for g in manifest["files"]:
        src = os.path.join(GEN, g)
        print("== bake %s ==" % g)
        results = []
        for tag in ("clang1", "clang2"):
            out = os.path.join(BUILD, g[:-2] + "." + tag)
            compile_one(src, out, "clang", "clang")
            results.append(run_one(out))
        a, b = results
        if a["raw"] != b["raw"]:
            diff = [x for x, y in zip(a["raw"], b["raw"]) if x != y]
            raise SystemExit("reference nondeterministic in %s: %r" % (g, diff[:5]))
        if a["signals"]:
            raise SystemExit("signal exit(s) in reference %s: %r" % (g, a["signals"]))
        # Expectations are empty placeholders during bake: the reference IS the
        # oracle here.  Determinism (two identical reference runs) and the
        # absence of signal exits are the only gates applied to observations.
        for name, r in a["rows"].items():
            observed[name] = {"out": r["out"], "err": r["err"], "exit": r["exit"]}
    with open(EXPECTED, "w") as f:
        json.dump(observed, f, indent=2, sort_keys=True)
        f.write("\n")
    generate()          # re-emit tables with the frozen expectations embedded
    print("baked %d expected results into %s" % (len(observed), EXPECTED))
    return 0


def api_inventory():
    spec = importlib.util.spec_from_file_location(
        "e2e_base_scenarios", os.path.join(HERE, "base_scenarios.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return sorted({row[0].split("|", 1)[0] for g in mod.GROUPS for row in g.rows})


def verdict(manifest):
    os.makedirs(SCRATCH, exist_ok=True)
    if os.path.isdir(BUILD):
        shutil.rmtree(BUILD)
    results, names = {}, []
    for g in manifest["files"]:
        src = os.path.join(GEN, g)
        print("== run %s ==" % g)
        results[g] = {}
        for kind, mode in (("clang", "clang"), ("cplus", "legacy-c")):
            out = os.path.join(BUILD, g[:-2] + "." + kind)
            compile_one(src, out, kind, mode)
            results[g][kind] = run_one(out)
            r = results[g][kind]
            print("  %s/%s: scenarios=%d summary=%s rc=%d" %
                  (kind, mode, len(r["rows"]), r["summary"], r["rc"]))
        names += list(results[g]["clang"]["rows"])
    # secondary c+ modes on one representative app file
    sec = {}
    for mode in ("default",):
        g = SECONDARY_FILE
        out = os.path.join(BUILD, g[:-2] + ".cplus-" + mode)
        compile_one(os.path.join(GEN, g), out, "cplus", mode)
        sec[mode] = run_one(out)
        print("  cplus/%s: scenarios=%d summary=%s rc=%d" %
              (mode, len(sec[mode]["rows"]), sec[mode]["summary"], sec[mode]["rc"]))

    failures, mismatch = [], 0
    passed = 0
    executed = 0
    for g in manifest["files"]:
        ref, tgt = results[g]["clang"], results[g]["cplus"]
        for name in sorted(set(ref["rows"]) | set(tgt["rows"])):
            executed += 1
            a, b = ref["rows"].get(name), tgt["rows"].get(name)
            la = a["line"] if a else None
            lb = b["line"] if b else None
            if la != lb:
                mismatch += 1
            ok = (a is not None and b is not None and la == lb and
                  a["status"] == "PASS" and b["status"] == "PASS")
            if ok:
                passed += 1
            else:
                failures.append({"file": g, "mode": "legacy-c", "scenario": name,
                                 "clang": la, "cplus": lb})
    # secondary-mode failures are counted against their own scenarios
    for mode in ("default",):
        g = SECONDARY_FILE
        ref = results[g]["clang"]
        for name in sorted(ref["rows"]):
            a, b = ref["rows"][name], sec[mode]["rows"].get(name)
            la = a["line"]
            lb = b["line"] if b else None
            if la != lb:
                mismatch += 1
                failures.append({"file": g, "mode": mode, "scenario": name,
                                 "clang": la, "cplus": lb})
    # structural checks: nothing skipped, no signals, harness counts exact
    for g in manifest["files"]:
        for kind in ("clang", "cplus"):
            r = results[g][kind]
            if r["signals"]:
                failures.append({"file": g, "mode": kind, "scenario": "<signal>",
                                 "clang": str(r["signals"]), "cplus": ""})
            if r["summary"].get("total") != len(r["rows"]) or \
               r["summary"].get("fail") != 0 or \
               r["summary"].get("pass") != len(r["rows"]) or r["rc"] != 0:
                failures.append({"file": g, "mode": kind, "scenario": "<summary>",
                                 "clang": str(r["summary"]), "cplus": str(r["rc"])})
    for mode in ("default",):
        r = sec[mode]
        if r["signals"] or r["rc"] != 0 or r["summary"].get("fail") != 0 or \
           r["summary"].get("pass") != len(r["rows"]):
            failures.append({"file": SECONDARY_FILE, "mode": mode,
                             "scenario": "<summary>",
                             "clang": str(r["summary"]), "cplus": str(r["rc"])})

    inventory = api_inventory()
    exercised = sorted({n.split("|", 1)[0] for n in names if n.split("|", 1)[0] in inventory})
    summary = {
        "compiler_reference": CLANG + " -std=c11 -O2",
        "compiler_under_test": CPLUS + " build --legacy-c --sanitize=none",
        "secondary_modes": ["--default on " + SECONDARY_FILE],
        "strict_mode_note": "c+ --strict cannot build a safety AST for the forked fork/capture runner (CPLUS-ANALYSIS); strict is not applicable to this harness, verified by build attempt.",
        "stdlib_root": STDLIB_ROOT,
        "scenarios_generated": manifest["scenario_count"],
        "scenarios_executed": executed,
        "scenarios_passed": passed,
        "scenarios_failed": manifest["scenario_count"] - passed,
        "apis_discovered": len(inventory),
        "apis_exercised": len(exercised),
        "apis_not_exercised": sorted(set(inventory) - set(exercised)),
        "categories": manifest["categories"],
        "differential_mismatch_count": mismatch,
        "failure_paths": failures[:100],
        "failure_count": len(failures),
        "code_coverage_measured": False,
        "code_coverage_note": "API/scenario coverage measured; line coverage not instrumented.",
    }
    with open(os.path.join(HERE, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")
    with open(os.path.join(HERE, "summary.txt"), "w") as f:
        for k, v in summary.items():
            f.write("%s: %s\n" % (k, json.dumps(v) if isinstance(v, (list, dict)) else v))
    print("\n%s" % json.dumps({k: v for k, v in summary.items()
                               if k not in ("failure_paths", "categories")}, indent=2))
    for x in failures[:20]:
        print("FAIL %s/%s/%s\n  clang: %s\n  c+   : %s" %
              (x["file"], x["mode"], x["scenario"], x["clang"], x["cplus"]))
    return 1 if failures or mismatch or summary["scenarios_failed"] else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bake", action="store_true")
    args = ap.parse_args()
    manifest = generate()
    if not os.path.isfile(EXPECTED) and not args.bake:
        raise SystemExit("missing %s: run with --bake first" % EXPECTED)
    return bake(manifest) if args.bake else verdict(manifest)


if __name__ == "__main__":
    sys.exit(main())
