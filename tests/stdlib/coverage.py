#!/usr/bin/env python3
"""Static API-coverage report for the C+ standard library.

For every stdlib/*.hp header, list the functions and methods it defines and
check whether the tests and examples under tests/stdlib call them.  A `.hp`
definition is `static inline`, so call sites are found by name:

  * a function is covered when `name(` appears in a test or example
  * a class method is covered when `method(` appears (calls are written
    `object.method(...)`)
  * a destructor (`drop` / `*_drop`) is covered when its type name is used,
    because the compiler inserts the call and tests never write it
  * internal helpers named `stdlib_*` are excluded from the public surface

Exits non-zero when any library is below the required coverage.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
THRESHOLD = 80.0

SIG = re.compile(
    r'^\s*(?:static\s+inline\s+)?[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{\s*$')
CTOR = re.compile(r'^\s*([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{\s*$')
CLASS = re.compile(r'^\s*class\s+([A-Za-z_]\w*)\s*\{')
SKIP = {"if", "for", "while", "switch", "do", "else", "return"}


def entries(path):
    """[(name, owning_class)] defined by one header."""
    out = []
    cls = None
    for line in path.read_text().splitlines():
        if line.lstrip().startswith("#"):
            continue
        m = CLASS.match(line)
        if m:
            cls = m.group(1)
            continue
        if cls and line.strip() == "};":
            cls = None
            continue
        m = SIG.match(line.rstrip("\n"))
        if not m:
            m = CTOR.match(line.rstrip("\n"))
        if not m:
            continue
        name = m.group(1)
        if name in SKIP or name == "main" or name.startswith("stdlib_"):
            continue
        out.append((name, cls))
    return out


def sources():
    paths = []
    for pattern in ("*.cp", "multi_tu/*.cp", "examples/*.cp"):
        paths.extend(sorted(ROOT.glob(pattern)))
    return [(p, p.read_text()) for p in paths]


def main():
    srcs = sources()
    libraries = sorted(ROOT.parent.parent.joinpath("stdlib").glob("*.hp"))
    print("%-18s %8s %8s %7s" % ("library", "covered", "total", "percent"))
    print("%-18s %8s %8s %7s" % ("-" * 18, "-" * 8, "-" * 8, "-" * 7))
    worst = 100.0
    failures = []
    for header in libraries:
        api = entries(header)
        if not api:
            continue
        covered = 0
        missing = []
        for name, cls in api:
            if name == "drop" and cls:
                # RAII invokes destructors; the type being used is the evidence
                hit = any(re.search(r"\b%s\b" % re.escape(cls), text)
                          for _, text in srcs)
            elif name.endswith("_drop"):
                owner = name[: -len("_drop")]
                hit = any(re.search(r"\b%s\b" % re.escape(owner), text)
                          for _, text in srcs)
            else:
                hit = any(re.search(r"\b%s\s*\(" % re.escape(name), text)
                          for _, text in srcs)
            if hit:
                covered += 1
            else:
                missing.append((cls + "." if cls else "") + name)
        pct = 100.0 * covered / len(api)
        worst = min(worst, pct)
        print("%-18s %8d %8d %6.1f%%" % (header.name, covered, len(api), pct))
        if pct < THRESHOLD:
            failures.append((header.name, pct, missing))
    print()
    if failures:
        for name, pct, missing in failures:
            print("BELOW %.0f%%: %s (%.1f%%)" % (THRESHOLD, name, pct))
            print("  not called by any test/example: %s" % ", ".join(missing))
        return 1
    print("all libraries >= %.0f%% API coverage (worst %.1f%%)" % (THRESHOLD, worst))
    return 0


if __name__ == "__main__":
    sys.exit(main())
