#!/usr/bin/env python3
"""Deterministic regressions for compiler issues #198, #199 and #200.

#198 (libcEffect summaries) and #199 (the authoritative-summary allowlist) are
observed directly through cplus::libcEffect() / hasVerifiedEffectDeclaration()
with an EMPTY v2 contract file, so the compiled-in fallback summaries -- the
code this suite pins -- are actually exercised.  #200 (uninitialized-variable
suppression) is observed end to end with `c+ check --strict`.

The probe is compiled from the current OwnershipAnalysis.cpp with the flags in
build/compile_commands.json and linked ahead of libcplus_core.a, mirroring
tests/compiler/typechecker_issue_201/run_suite.py. Run after the parent session
has built the tree:

    CPLUS_BUILD_DIR=$PWD/build python3 tests/compiler/ownership_issues/run_suite.py

No case is skipped and no failure is suppressed.
"""
import datetime
import json
import os
import platform
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BUILD_DIR = os.path.abspath(
    os.environ.get("CPLUS_BUILD_DIR", os.path.join(ROOT, "build")))
BIN = os.environ.get("CPLUS_BIN", os.path.join(BUILD_DIR, "c+"))
OUT = os.path.join(HERE, "results")
os.makedirs(OUT, exist_ok=True)

COMPILE_TIMEOUT = 120
LINK_TIMEOUT = 300
PROBE_TIMEOUT = 90
CHECK_TIMEOUT = 60

# Exact expected fallback summaries. Full-field equality, not substring checks.
EFFECTS = {
    # dst=0, n=arg2 -- the old bug read the source pointer at argument 1.
    "strncpy":  dict(destArg=0, sizeArg=2, checkedSize=1, writes=1, borrows=1),
    "strncat":  dict(destArg=0, sizeArg=2, checkedSize=1, writes=1, borrows=1),
    "memcpy":   dict(destArg=0, sizeArg=2, checkedSize=1, writes=1, borrows=1),
    "memset":   dict(destArg=0, sizeArg=2, checkedSize=1, writes=1, borrows=1),
    "strcpy":   dict(destArg=0, sizeArg=-1, checkedSize=0, writes=1, borrows=1),
    "strcat":   dict(destArg=0, sizeArg=-1, checkedSize=0, writes=1, borrows=1),
    # Annex K _s functions carry the destination capacity at argument 1.
    "strcpy_s":   dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    "strncpy_s":  dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    "strcat_s":   dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    "strncat_s":  dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    "memcpy_s":   dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    "memmove_s":  dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    "memset_s":   dict(destArg=0, sizeArg=1, checkedSize=1, writes=1),
    # fopen variants: Annex K returns errno_t and writes FILE ** at arg 0;
    # the shipped adapter returns the owned FILE * like fopen.
    "fopen":         dict(alloc=1, returnsOwned=1, writes=0, destArg=-1),
    "fopen_s":       dict(alloc=0, returnsOwned=0, writes=1, destArg=0),
    "cplus_fopen_s": dict(alloc=1, returnsOwned=1, writes=0, destArg=-1),
    "fclose":         dict(frees=1, freedArg=0),
    "cplus_fclose_s": dict(frees=1, freedArg=0),
    "fread":          dict(writes=1, mayFail=1, borrows=1),
    "fwrite":         dict(writes=1, mayFail=1, borrows=1),
    "cplus_fread_s":  dict(writes=1, mayFail=1, borrows=1),
    "cplus_fwrite_s": dict(writes=1, mayFail=1, borrows=1),
}

# isCompilerHelperEffectName() must be a closed allowlist.
VERIFIED = {
    "cplus_unverified_sink": 0,
    "__cplus_unsafe": 0,
    "cplus_core_helper": 0,
    "cplus_check_bounds": 1,
    "__cplus_bounds_check": 1,
    "cplus_check_nonnull": 1,
    "cplus_register_allocation": 1,
    "cplus_release": 1,
    "cplus_scan_value": 1,
    "malloc": 1,
    "cplus_fopen_s": 1,
}

# (name, must_reject, required diagnostic fragment, source)
CLI_CASES = [
    ("199_cplus_prefix_backdoor", True,
     "[CPLUS-FFI] function 'cplus_unverified_sink'",
     "extern void cplus_unverified_sink(void *p);\n"
     "void test(int *x) { cplus_unverified_sink(x); }\n"
     "int main(void) { int v = 0; test(&v); return v; }\n"),
    ("199_double_underscore_backdoor", True,
     "[CPLUS-FFI] function '__cplus_unsafe'",
     "extern void __cplus_unsafe(void *p);\n"
     "int main(void) { int v = 0; __cplus_unsafe(&v); return v; }\n"),
    ("199_user_helper_with_body_ok", False, "",
     "int cplus_core_helper(int x) { return x + 1; }\n"
     "int main(void) { return cplus_core_helper(0) == 1 ? 0 : 1; }\n"),
    ("199_generated_helper_ok", False, "",
     "int main(void) { int a[2] = {0}; return a[1]; }\n"),
    ("200_read_only_scan_substring", True, "uninitialized",
     "static void read_only_scan(const int *p) { volatile int v = *p; (void)v; }\n"
     "int main(void) { int uninit; read_only_scan(&uninit); return uninit; }\n"),
    ("200_prescan_substring", True, "uninitialized",
     "static void prescan(const int *p) { volatile int v = *p; (void)v; }\n"
     "int main(void) { int uninit; prescan(&uninit); return uninit; }\n"),
    ("200_indirect_const_callback", True, "uninitialized",
     "typedef void (*read_cb)(const int *);\n"
     "static void call_it(read_cb cb, const int *p) { cb(p); }\n"
     "int main(void) { int uninit; call_it(0, &uninit); return uninit; }\n"),
    ("200_scanf_still_initializes_ok", False, "",
     "#include <stdio.h>\n"
     "int main(void) { int x; if (scanf(\"%d\", &x) != 1) return 1; return x == 0 ? 0 : 1; }\n"),
]


def run(argv, timeout, env=None, cwd=None):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, env=env, cwd=cwd)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def record(rows, name, ok, detail, steps):
    rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                 "detail": detail, "steps": steps})
    print("%-34s %s" % (name, "ok  " if ok else "FAIL"))


def build_probe(rows, empty_contract):
    """Compile probe + current OwnershipAnalysis.cpp and link ahead of archive."""
    db_path = os.path.join(BUILD_DIR, "compile_commands.json")
    link_path = os.path.join(BUILD_DIR, "CMakeFiles", "c+.dir", "link.txt")
    core_a = os.path.join(BUILD_DIR, "libcplus_core.a")
    for path in (db_path, link_path, core_a):
        if not os.path.exists(path):
            print("missing build artifact: %s (configure/build the tree first)"
                  % path, file=sys.stderr)
            return None
    with open(db_path) as f:
        db = json.load(f)
    entries = [e for e in db
               if os.path.basename(e.get("file", "")).endswith("OwnershipAnalysis.cpp")]
    if len(entries) != 1:
        print("expected exactly one OwnershipAnalysis.cpp compile command, found %d"
              % len(entries), file=sys.stderr)
        return None
    cmd = shlex.split(entries[0]["command"])
    out_idx = max(i for i, t in enumerate(cmd) if t == "-o")
    compiler, flags = cmd[0], cmd[1:out_idx]

    oa_obj = os.path.join(OUT, "OwnershipAnalysis.issue198_199.o")
    probe_obj = os.path.join(OUT, "ownership_probe.o")
    probe_exe = os.path.join(OUT, "ownership_probe")
    oa_src = os.path.join(ROOT, "compiler", "Analysis", "OwnershipAnalysis.cpp")
    probe_src = os.path.join(HERE, "ownership_probe.cpp")
    for label, src, obj in (("compile_ownership_analysis", oa_src, oa_obj),
                            ("compile_probe", probe_src, probe_obj)):
        step = run([compiler] + flags + ["-o", obj, "-c", src], COMPILE_TIMEOUT)
        ok = step["rc"] == 0 and not step["timed_out"]
        record(rows, label, ok, "rc=%s" % step["rc"], [step])
        if not ok:
            return None

    with open(link_path) as f:
        link = shlex.split(f.read())
    link = [t for t in link if not t.endswith("Main.cpp.o")]
    out_idx = link.index("-o")
    link[out_idx + 1] = probe_exe
    link = [link[0], oa_obj, probe_obj] + link[1:]
    step = run(link, LINK_TIMEOUT, cwd=BUILD_DIR)
    ok = step["rc"] == 0 and not step["timed_out"]
    record(rows, "link_probe", ok, "rc=%s" % step["rc"], [step])
    if not ok:
        return None
    # An empty contract file forces libcEffect() to use its built-in fallbacks;
    # it also makes the env-supplied-summary gate refuse contract-only names.
    with open(empty_contract, "w") as f:
        json.dump({"version": 2, "functions": []}, f)
    return probe_exe


def parse_probe(text):
    effects, verified = {}, {}
    for line in text.splitlines():
        tokens = line.split()
        if len(tokens) < 2:
            continue
        if tokens[0] == "effect":
            fields = dict(t.split("=", 1) for t in tokens[2:] if "=" in t)
            effects[tokens[1]] = fields
        elif tokens[0] == "verified" and len(tokens) == 3:
            verified[tokens[1]] = tokens[2]
    return effects, verified


def main():
    rows = []
    if not os.path.exists(BIN):
        print("missing c+ binary at %s" % BIN, file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="cplus_ownership_") as td:
        contract = os.path.join(td, "empty_contracts.json")
        probe = build_probe(rows, contract)
        if probe:
            env = dict(os.environ, CPLUS_CONTRACT_PATH=contract)
            step = run([probe], PROBE_TIMEOUT, env=env, cwd=ROOT)
            # Evidence for every case row is the probe output itself.
            effects, verified = parse_probe(step["stdout"] + "\n" + step["stderr"])
            for name, expected in EFFECTS.items():
                got = effects.get(name, {})
                mismatches = []
                for key, value in expected.items():
                    if got.get(key) != str(value):
                        mismatches.append("%s: expected %s got %s" %
                                          (key, value, got.get(key)))
                ok = step["rc"] == 0 and not mismatches
                record(rows, "198_effect_" + name, ok,
                       "rc=%s %s" % (step["rc"], "; ".join(mismatches)), [step])
            for name, expected in VERIFIED.items():
                got = verified.get(name)
                ok = step["rc"] == 0 and got == str(expected)
                record(rows, "199_verified_" + name, ok,
                       "rc=%s expected=%s got=%s" % (step["rc"], expected, got),
                       [step])
        for name, must_reject, fragment, source in CLI_CASES:
            path = os.path.join(td, name + ".c")
            with open(path, "w") as f:
                f.write(source)
            step = run([BIN, "check", "--strict", "--sanitize=none", path],
                       CHECK_TIMEOUT)
            output = step["stdout"] + step["stderr"]
            ok = (step["rc"] != 0) if must_reject else (step["rc"] == 0)
            if ok and fragment:
                ok = fragment in output
            record(rows, name, ok,
                   "rc=%s fragment=%r output=%r" %
                   (step["rc"], fragment, output[:500]), [step])

    failures = [r["case"] for r in rows if r["status"] == "FAIL"]
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "build_dir": BUILD_DIR, "bin": BIN,
               "results": rows, "failures": failures}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Ownership issues #198/#199/#200 regression suite\n")
        for r in rows:
            f.write("%-34s %s  %s\n" % (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:800],
                         s["stderr"][:800]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(failures), len(failures)))
    print("ownership_issues: %d cases, %d failures" % (len(rows), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
