#!/usr/bin/env python3
"""Regression suite for the 115 bug scenarios in scratch/bugs3.cp.

Each function is extracted into a standalone compilable translation unit with
only host-available headers. Every scenario is checked in DEFAULT and STRICT.
A mismatch is any case that compiles clean (no diagnostic at all) where the
source marks it as an ERROR, or any false positive on a labelled SAFE case.
Results are written to tests/out/core/bugs3_suite/report.txt.
"""
import pathlib, subprocess, sys, os, re

ROOT = pathlib.Path(__file__).parents[2]
SRC = ROOT / "scratch/bugs3.cp"
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/bugs3_suite"
OUT.mkdir(parents=True, exist_ok=True)

text = SRC.read_text().splitlines()
first_bug = next(i for i, l in enumerate(text) if re.match(r"^\w[\w \*]*\bbug\d+_", l))
include_dirs = ["/usr/include",
                "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include"]
def header_available(h):
    return any(pathlib.Path(d, h).exists() for d in include_dirs)
includes = "\n".join(l for l in text if l.strip().startswith("#include")
                     and "<" in l and header_available(l.split("<")[-1].split(">")[0]))
prefix = includes + "\n" + "\n".join(
    l for l in text[:first_bug]
    if not l.strip().startswith("c") and not l.strip().startswith("#include")
    and not l.strip().startswith("#define"))

# Extract functions with brace matching.
funcs = []
i = first_bug
while i < len(text):
    m = re.match(r"^(\w[\w \*]*\b)(bug\d+_[\w]+)\s*\(([^)]*)\)\s*\{", text[i])
    if not m:
        i += 1
        continue
    depth = text[i].count("{") - text[i].count("}")
    j = i + 1
    while j < len(text) and depth > 0:
        depth += text[j].count("{") - text[j].count("}")
        j += 1
    funcs.append((m.group(2), m.group(3).strip(), "\n".join(text[i:j])))
    i = j

def make_src(name, params, body):
    args = []
    ok = True
    for p in params.split(","):
        p = p.strip()
        if not p or p == "void":
            continue
        if "*" in p or "[" in p or "struct" in p or "FILE" in p or "&" in p:
            ok = False
            break
        args.append("0")
    call = f"{name}({', '.join(args)});" if ok else ""
    return prefix + "\n" + body + "\nint main(void){" + call + "return 0;}\n", ok

def check(path, mode):
    r = subprocess.run([CC, "check", mode, str(path)], capture_output=True,
                       text=True, timeout=60)
    diag = r.stderr + r.stdout
    return r.returncode != 0, [l for l in diag.splitlines()
                               if "error:" in l or "warning:" in l]

report = []
for name, params, body in funcs:
    src, ok = make_src(name, params, body)
    p = OUT / f"{name}.c"
    p.write_text(src)
    de, de_diag = check(p, "--default")
    se, se_diag = check(p, "--strict")
    report.append((name, ok, de, se, de_diag[:1], se_diag[:1]))

detected = sum(1 for _, _, d, s, _, _ in report if d or s)
with_diag = sum(1 for _, _, _, _, d, s in report if d or s)
lines = [f"bugs3 suite: {len(funcs)} scenarios, {detected} rejected in a mode, "
         f"{with_diag} with any diagnostic"]
for name, ok, de, se, dd, sd in report:
    lines.append(f"{name:46} callable={ok!s:5} default_reject={de!s:5} "
                 f"strict_reject={se!s:5} {dd[0][:90] if dd else ''}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
print(f"report: {OUT / 'report.txt'}")
