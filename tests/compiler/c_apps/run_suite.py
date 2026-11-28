#!/usr/bin/env python3
"""Original plain-C apps/games: compatibility AND safety proof.

Normal apps: c+ --legacy-c must preserve source bytes (after one #line),
produce the same stdout/exit as clang, and c+ --default/--strict must run the
same program with the same output. Unsafe variants: STRICT must reject with a
named diagnostic, or (for a runtime-only overflow) the sanitizer-free build
must trap with the exact C+ exit code and message.
"""
import json, os, subprocess, sys, datetime, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
CLANG = os.environ.get("REF_CC", "/usr/bin/clang")
OUT = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(OUT, "results")
os.makedirs(RES, exist_ok=True)
APPS = ["tic_tac_toe", "game_2048", "snake", "calculator", "hash_tool"]
UNSAFE = {
    "oob_write": ("[CPLUS-SAFE-BOUNDS]", None),
    "uaf": ("[CPLUS-LIFETIME]", None),
    "null_deref": ("[CPLUS-", None),
    "invalid_shift": ("[CPLUS-FFI]", None),
    "uninit": ("uninitialized", None),
    "double_free": ("[CPLUS-OWNER]", None),
    "overflow": (None, (110, "signed integer overflow")),
}


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60, **kw)


def main():
    rows, fails = [], []
    for name in APPS:
        src = os.path.join(OUT, "apps", name + ".c")
        raw = open(src, "rb").read()
        # clang reference
        ref = os.path.join(RES, name + ".clang")
        p = run([CLANG, "-std=gnu89", "-O2", src, "-o", ref])
        if p.returncode:
            fails.append(name + ": clang build failed")
            continue
        ref_run = run([ref])
        # LEGACY c+ with generated C
        gen = os.path.join(RES, name + ".legacy.c")
        exe = os.path.join(RES, name + ".legacy")
        p = run([BIN, "build", "--legacy-c", "--sanitize=none", "--emit-c", gen,
                 src, "-o", exe])
        if p.returncode:
            fails.append(name + ": legacy build failed: " + p.stderr.decode()[:150])
            continue
        emitted = open(gen, "rb").read()
        m = re.match(rb'^#line \d+ "[^"]*"\n', emitted)
        preserved = bool(m) and emitted[m.end():] == raw
        c_run = run([exe])
        legacy_ok = (preserved and c_run.returncode == ref_run.returncode and
                     c_run.stdout == ref_run.stdout)
        # SAFE modes
        safe = {}
        for mode in ("--default", "--strict"):
            sx = os.path.join(RES, name + mode[2:] + ".bin")
            b = run([BIN, "build", mode, "--sanitize=none", src, "-o", sx])
            r = run([sx]) if b.returncode == 0 else None
            safe[mode] = {"build_rc": b.returncode, "run_rc": r.returncode if r else None,
                          "stdout": r.stdout.decode(errors="replace") if r else "",
                          "match": bool(r and r.stdout == ref_run.stdout and
                                        r.returncode == ref_run.returncode),
                          "stderr": b.stderr.decode(errors="replace")[:200]}
        rows.append({"kind": "app", "name": name,
                     "expected_stdout": ref_run.stdout.decode(errors="replace"),
                     "legacy_source_preserved": preserved,
                     "legacy_matches_clang": legacy_ok, "modes": safe})
        if not legacy_ok:
            fails.append(name + ": legacy source/stdout mismatch")
        for mode, v in safe.items():
            if not v["match"]:
                fails.append("%s: %s mismatch rc=%s" % (name, mode, v["run_rc"]))
        print("%-14s legacy=%s default=%s strict=%s out=%r" %
              (name, legacy_ok, safe["--default"]["match"],
               safe["--strict"]["match"], ref_run.stdout.decode().strip()))
    for name, (diag, trap) in UNSAFE.items():
        src = os.path.join(OUT, "unsafe", name + ".c")
        chk = run([BIN, "check", "--strict", "--sanitize=none", src])
        err = (chk.stdout + chk.stderr).decode(errors="replace")
        rec = {"kind": "unsafe", "name": name, "check_rc": chk.returncode,
               "diagnostic": err[:400], "expected_diag": diag, "expected_trap": trap}
        if diag:
            ok = chk.returncode != 0 and diag in err
        else:
            exe = os.path.join(RES, name + ".bin")
            b = run([BIN, "build", "--strict", "--sanitize=none", src, "-o", exe])
            r = run([exe]) if b.returncode == 0 else None
            out = (r.stderr + r.stdout).decode(errors="replace") if r else ""
            rec["build_rc"] = b.returncode
            rec["run_rc"] = r.returncode if r else None
            rec["runtime_output"] = out[:400]
            ok = bool(r and r.returncode == trap[0] and trap[1].lower() in out.lower())
        rec["status"] = "PASS" if ok else "FAIL"
        rows.append(rec)
        if not ok:
            fails.append(name + ": not caught")
        print("%-14s %-4s %s" % (name, rec["status"], err.strip().splitlines()[0][:110] if err.strip() else rec.get("runtime_output", "")[:110]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "cplus": BIN, "clang": CLANG, "results": rows, "failures": fails}
    with open(os.path.join(RES, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(RES, "summary.txt"), "w") as f:
        f.write("Original plain-C apps/games + unsafe variants\n")
        for r in rows:
            f.write("%-14s %s\n" % (r["name"], r.get("status") or
                    ("legacy=" + str(r["legacy_matches_clang"]))))
        f.write("\nfailures: %d\n" % len(fails))
        for x in fails:
            f.write("  " + x + "\n")
    print("c_apps: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
