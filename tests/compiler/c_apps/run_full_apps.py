#!/usr/bin/env python3
"""Run the full playable C apps with deterministic stdin against clang and C+.

Each app is real C89 with an interactive loop.  A fixed stdin script makes its
run deterministic. We assert: LEGACY source preservation + stdout/exit equality
with clang, and DEFAULT/STRICT stdout/exit equality. No `|| true`.
"""
import json, os, subprocess, sys, datetime, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
CLANG = os.environ.get("REF_CC", "/usr/bin/clang")
OUT = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(OUT, "results")
os.makedirs(RES, exist_ok=True)
CASES = {
    "snake_full": "dddswq",
    "game2048_full": "aawsdaq",
    "tic_tac_toe_full": "1529q",
    "tetris_mini": "rrddlldq",
    "line_editor": "a hello\na world\np\nd\np\nq\n",
    "calc_repl": "1+2*3\n4*4\nq\n",
}

# Fixtures whose C+ warnings are intentional/incidental to their plain-C89
# source.  tetris_mini uses `goto next;` purely as loop control; the C+ warning
# "'goto' bypasses C+ cleanup; destructors may not run" does not apply because
# the fixture owns no C+ resources/destructors.  Per docs/LANGUAGE.md,
# --no-werror is a documented diagnostic-only opt-out from warning promotion:
# it does not disable any C+ safety analysis or runtime check, so DEFAULT and
# STRICT still exercise the full safety policy.  Keep this set minimal and
# never apply the flag globally to every fixture.
WERROR_OPT_OUT = {"tetris_mini"}


def run(cmd, data=None):
    return subprocess.run(cmd, input=(data.encode() if data else None),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)


def main():
    rows, fails = [], []
    for name, stdin in CASES.items():
        src = os.path.join(OUT, "full_apps", name + ".c")
        raw = open(src, "rb").read()
        ref = os.path.join(RES, name + ".full.clang")
        p = run([CLANG, "-std=gnu89", "-O2", src, "-o", ref])
        if p.returncode:
            fails.append(name + ": clang build failed")
            continue
        rr = run([ref], stdin)
        gen = os.path.join(RES, name + ".full.legacy.c")
        exe = os.path.join(RES, name + ".full.legacy")
        p = run([BIN, "build", "--legacy-c", "--sanitize=none", "--emit-c",
                 gen, src, "-o", exe])
        if p.returncode:
            fails.append(name + ": c+ legacy build failed: " + p.stderr.decode()[:120])
            continue
        em = open(gen, "rb").read()
        m = re.match(rb'^#line \d+ "[^"]*"\n', em)
        preserved = bool(m) and em[m.end():] == raw
        cr = run([exe], stdin)
        legacy_ok = (preserved and cr.returncode == rr.returncode and
                     cr.stdout == rr.stdout)
        modes = {}
        for mode in ("--default", "--strict"):
            sx = os.path.join(RES, name + mode[2:] + ".full.bin")
            cmd = [BIN, "build", mode, "--sanitize=none"]
            if name in WERROR_OPT_OUT:
                cmd.append("--no-werror")  # diagnostic-only; safety checks stay on
            cmd += [src, "-o", sx]
            b = run(cmd)
            r = run([sx], stdin) if b.returncode == 0 else None
            ok = bool(r and r.returncode == rr.returncode and r.stdout == rr.stdout)
            modes[mode] = {"build_rc": b.returncode, "run_rc": r.returncode if r else None,
                           "match": ok, "stderr": b.stderr.decode(errors="replace")[:200]}
        rows.append({"name": name, "stdin": stdin, "legacy_preserved": preserved,
                     "legacy_match": legacy_ok, "modes": modes,
                     "stdout": rr.stdout.decode(errors="replace")[:500]})
        if not legacy_ok:
            fails.append(name + ": legacy mismatch")
        for k, v in modes.items():
            if not v["match"]:
                fails.append("%s: %s mismatch (%s)" % (name, k, v["stderr"][:120]))
        print("%-18s legacy=%s default=%s strict=%s lines=%d" %
              (name, legacy_ok, modes["--default"]["match"],
               modes["--strict"]["match"], len(rr.stdout.splitlines())))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "cplus": BIN, "clang": CLANG, "results": rows, "failures": fails}
    with open(os.path.join(RES, "full_apps.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(RES, "full_apps.txt"), "w") as f:
        f.write("Full playable C apps: c+ vs clang\n")
        for r in rows:
            f.write("%-18s legacy=%s default=%s strict=%s\n" %
                    (r["name"], r["legacy_match"], r["modes"]["--default"]["match"],
                     r["modes"]["--strict"]["match"]))
        f.write("\nfailures: %d\n" % len(fails))
        for x in fails:
            f.write("  " + x + "\n")
    print("full apps: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
