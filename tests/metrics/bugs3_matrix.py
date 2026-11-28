#!/usr/bin/env python3
"""Extract each bugNNN function from scratch/bugs3.cp and test it standalone."""
import re, pathlib, subprocess, sys, os

ROOT = pathlib.Path(__file__).parents[2]
SRC = ROOT / "scratch/bugs3.cp"
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/bugs3"
OUT.mkdir(parents=True, exist_ok=True)

text = SRC.read_text().splitlines()
# Collect headers (all #include lines from the source)
# Only keep headers available on this host; missing headers make AST parse fail.
import shutil
include_dirs = ["/usr/include", "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include"]
def header_available(h):
    for d in include_dirs:
        if pathlib.Path(d, h).exists():
            return True
    return False
includes = "\n".join(l for l in text if l.strip().startswith("#include")
                     and header_available(l.split("<")[-1].split(">")[0])
                     and "<" in l)
# Collect struct/enum/typedef definitions and helper functions used by bugs.
# Simplest robust approach: keep the whole prefix up to the first bug function,
# which contains all includes and any shared declarations.
first_bug = next(i for i, l in enumerate(text) if re.match(r"^\w[\w \*]*\bbug\d+_", l))
prefix = includes + "\n" + "\n".join(
    l for l in text[:first_bug]
    if not l.strip().startswith("c") and not l.strip().startswith("#include")
    and not l.strip().startswith("#define"))

# Find function bodies by brace matching.
funcs = []
i = first_bug
while i < len(text):
    m = re.match(r"^(\w[\w \*]*\b)(bug\d+_[\w]+)\s*\(([^)]*)\)\s*\{", text[i])
    if not m:
        i += 1
        continue
    sig = text[i].rstrip("{").strip() + " {"
    depth = text[i].count("{") - text[i].count("}")
    j = i + 1
    while j < len(text) and depth > 0:
        depth += text[j].count("{") - text[j].count("}")
        j += 1
    body = "\n".join([sig] + text[i+1:j])
    name = m.group(2)
    params = m.group(3).strip()
    funcs.append((name, params, body))
    i = j

cases = []
for name, params, body in funcs:
    # Build a main that supplies simple arguments for scalar-only signatures.
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
    src = prefix + "\n" + body + "\nint main(void){" + call + "return 0;}\n"
    p = OUT / f"{name}.c"
    p.write_text(src)
    cases.append((name, p, ok))

results = []
for name, p, callable_ in cases:
    r = subprocess.run([CC, "check", str(p)], capture_output=True, text=True)
    diag = r.stderr + r.stdout
    nerr = diag.count("error:")
    nwarn = diag.count("warning:")
    results.append((name, callable_, nerr, nwarn, r.returncode))

diag_cases = sum(1 for _,_,e,w,_ in results if e or w)
tested = sum(1 for _,c,_,_,_ in results if c)
print(f"bugs3 matrix: {len(funcs)} functions, {tested} auto-callable, {diag_cases} with diagnostics")
for name, c, e, w, rc in results[:15]:
    print(f"  {name:45} callable={c} errors={e} warnings={w} rc={rc}")
print(f"total errors={sum(e for _,_,e,_,_ in results)} warnings={sum(w for _,_,_,w,_ in results)}")
