#!/usr/bin/env python3
"""Prove that LEGACY mode preserves original C source bytes and ABI.

For each fixture: build with c+ --legacy-c --sanitize=none and --emit-c,
strip the single leading `#line` directive, and require the remaining bytes to
equal the original file exactly.  Then build the same source with the host C
compiler and assert identical stdout, sizeof/offsetof output and exported
symbol names.  No suppression, no `|| true`, hard timeouts.
"""
import json, os, re, subprocess, sys, datetime, platform, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CPLUS = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
REF = os.environ.get("REF_CC", "/usr/bin/clang")
FIXTURES = [
    os.path.join("tests", "cases", "c_abi.c"),
    os.path.join("benchmarks", "bounds_sum.c"),
    os.path.join("tests", "legacy", "plain.c"),
    os.path.join("tests", "cases", "hello.c"),
]
OUT = os.path.join(os.path.dirname(__file__), "results")
os.makedirs(OUT, exist_ok=True)


def strip_line(text):
    return re.sub(r"^#line \d+ \"[^\"]*\"\n", "", text, count=1)


def symbols(path):
    out = subprocess.run(["/usr/bin/nm", "-g", path], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, timeout=60).stdout.decode()
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-2] in ("T", "D", "B"):
            names.add(parts[-1])
    return names


def main():
    rows, fails = [], []
    for rel in FIXTURES:
        src = os.path.join(ROOT, rel)
        if not os.path.exists(src):
            fails.append({"case": rel, "reason": "missing fixture"})
            continue
        original = open(src, "rb").read().decode()
        gen = os.path.join(OUT, os.path.basename(src) + ".c")
        cbin = os.path.join(OUT, os.path.basename(src) + ".cplus")
        rbin = os.path.join(OUT, os.path.basename(src) + ".ref")
        cb = subprocess.run([CPLUS, "build", "--legacy-c", "--sanitize=none",
                             "--emit-c", gen, src, "-o", cbin],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if cb.returncode != 0:
            fails.append({"case": rel, "reason": "c+ build failed: " + cb.stderr.decode()[:200]})
            continue
        generated = open(gen, "rb").read().decode()
        prefix = re.match(r"^#line \d+ \"[^\"]*\"\n", generated)
        src_equal = bool(prefix) and strip_line(generated) == original
        rb = subprocess.run([REF, "-std=gnu17", "-O2", src, "-o", rbin],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if rb.returncode != 0:
            fails.append({"case": rel, "reason": "reference build failed: " + rb.stderr.decode()[:200]})
            continue
        c_out = subprocess.run([cbin], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=60, check=True).stdout
        r_out = subprocess.run([rbin], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=60, check=True).stdout
        c_syms = symbols(cbin)
        r_syms = symbols(rbin)
        ok = src_equal and c_out == r_out and c_syms == r_syms
        rows.append({"case": rel, "source_identical": src_equal,
                     "stdout_identical": c_out == r_out,
                     "symbols_identical": c_syms == r_syms,
                     "stdout": c_out.decode().strip(),
                     "executable_symbols": sorted(c_syms)})
        if not ok:
            fails.append({"case": rel, "reason": "preservation mismatch", "row": rows[-1]})
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "cplus": CPLUS, "reference": REF,
               "results": rows, "failures": fails}
    json.dump(payload, open(os.path.join(OUT, "summary.json"), "w"), indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("LEGACY C source and ABI preservation\n%s\nC+ %s\nref %s\n\n" %
                (payload["host"], CPLUS, REF))
        for r in rows:
            f.write("%-40s source=%s stdout=%s symbols=%s\n" %
                    (r["case"], r["source_identical"], r["stdout_identical"],
                     r["symbols_identical"]))
        f.write("\nfailures: %d\n" % len(fails))
        for x in fails:
            f.write("  %s: %s\n" % (x["case"], x["reason"]))
    print("source preservation: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
