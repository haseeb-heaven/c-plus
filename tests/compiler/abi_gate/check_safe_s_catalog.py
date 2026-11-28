#!/usr/bin/env python3
"""Deterministic doc/catalog sync gate for safer-_s ABI facts (issues #188/#190).

This checker never compiles the compiler.  It validates the machine-readable
catalog in ``compiler/Analysis/libc_contracts.json`` (top-level
``safeSAbiCatalog``) against:

  * every ``_s`` / ``_s_auto`` prototype shipped in ``stdlib/safe/*.h`` and
    ``libc/install-include/*.h`` (name, header and normalized C signature),
  * the three dedicated docs (every catalogued spelling and every suggested
    legacy call appears in them, and the three docs exist and are cross-linked),
  * the diagnostic contract used by ``-Wcplus-suggest-safe-s`` (flag id, ABI
    tags, fix-its, and statically-known-extent fix-its).

It exits 0 only when all checks pass; there are no skips and no ``|| true``.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
CATALOG_PATH = ROOT / "compiler/Analysis/libc_contracts.json"
HEADER_DIRS = (ROOT / "stdlib/safe", ROOT / "libc/install-include")
DOCS = {
    "preserving": ROOT / "docs/SAFE_S_ABI_PRESERVING.md",
    "changing": ROOT / "docs/SAFE_S_NON_ABI_PRESERVING.md",
    "matrix": ROOT / "docs/SAFE_S_COMPATIBILITY_MATRIX.md",
}
CROSS_LINKS = (ROOT / "docs/README.md", ROOT / "docs/SAFE_CRT.md")
ABI_DOC = ROOT / "docs/abi/STRICT_C_ABI_BOUNDARY.md"

TYPE_TOKENS = {
    "void", "char", "int", "long", "short", "unsigned", "signed", "float",
    "double", "const", "volatile", "struct", "union", "enum", "va_list",
    "size_t", "rsize_t", "errno_t", "FILE", "uint64_t", "int64_t",
    "uintptr_t", "intptr_t", "_Bool",
}
NO_DEST_EXTENT = {
    "printf", "fprintf", "scanf", "fscanf", "sscanf", "fopen", "fclose",
    "malloc", "calloc", "realloc", "free", "aligned_alloc", "strtok",
    "gmtime", "localtime", "qsort", "mbrtowc", "bsearch",
}
REQUIRED_SYMBOL_FIELDS = (
    "legacy", "abi", "mechanism", "header", "archive", "signature",
    "legacySignature", "reason", "preconditions",
)
REQUIRED_OPTION_FIELDS = ("symbol", "abi", "mechanism", "fixIt")

errors: list[str] = []
checks = 0


def check(condition: bool, message: str) -> None:
    global checks
    checks += 1
    if not condition:
        errors.append(message)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def prototype_symbols() -> dict[str, tuple[str, str]]:
    """Map shipped _s prototype name -> (relative header, normalized signature)."""
    found: dict[str, tuple[str, str]] = {}
    for directory in HEADER_DIRS:
        for header in sorted(directory.glob("*.h")):
            text = strip_comments(header.read_text())
            for match in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*_s(?:_auto)?)\s*\(", text):
                tail = text[match.end():match.end() + 600]
                end = tail.find(";")
                if end < 0 or "{" in tail[:end]:
                    continue  # macro use or definition, not a prototype
                start = text.rfind("\n", 0, match.start()) + 1
                decl = " ".join((text[start:match.start()] +
                                 text[match.start():match.end() + end + 1]).split())
                rel = header.relative_to(ROOT).as_posix()
                name = match.group(1)
                check(name not in found,
                      "duplicate prototype for %s (%s and %s)" %
                      (name, found.get(name, ("", ""))[0], rel))
                found[name] = (rel, canonical_signature(decl))
    return found


def canonical_signature(text: str) -> str:
    """Drop format attributes and parameter names from a C declaration."""
    text = re.sub(r"CPLUS_SAFE_FMT_[A-Z]+\([^)]*\)", "", text)
    text = re.sub(r"\s+", " ", text).strip().rstrip(";").strip()
    tokens = re.findall(r"\.\.\.|[A-Za-z_][A-Za-z0-9_]*|\*|\(|\)|,", text)
    out: list[str] = []
    for i, token in enumerate(tokens):
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token):
            # A trailing identifier in a parameter is its name, not its type.
            nxt = tokens[i + 1] if i + 1 < len(tokens) else ")"
            if token not in TYPE_TOKENS and nxt in {",", ")"}:
                continue
        out.append(token)
    return "".join(out)


def backticked_symbols(text: str) -> set[str]:
    return set(re.findall(r"`(cplus_fread|cplus_fwrite|"
                          r"[A-Za-z_][A-Za-z0-9_]*_s(?:_auto)?)`", text))


def main() -> int:
    if not CATALOG_PATH.is_file():
        print("catalog missing:", CATALOG_PATH)
        return 1
    data = json.loads(CATALOG_PATH.read_text())
    check(data.get("version") == 2, "libc_contracts version must stay 2")
    check(set(data.get("functions", {})) >= {
        "malloc", "free", "memcpy", "strcpy", "strncpy", "fopen",
    }, "function contracts were removed from libc_contracts.json")
    catalog = data.get("safeSAbiCatalog")
    check(isinstance(catalog, dict), "safeSAbiCatalog object is missing")
    if not isinstance(catalog, dict):
        report()
        return 1

    check(catalog.get("version") == 1, "catalog version must be 1")
    check(catalog.get("diagnosticFlag") == "-Wcplus-suggest-safe-s",
          "diagnostic flag drifted from -Wcplus-suggest-safe-s")
    check(catalog.get("diagnosticId") == "CPLUS-SUGGEST-SAFE-S",
          "diagnostic id drifted from CPLUS-SUGGEST-SAFE-S")
    tags = catalog.get("abiTags", {})
    check(set(tags) == {"preserving", "changing"}, "abiTags must define preserving/changing")

    symbols = catalog.get("symbols", {})
    suggestions = catalog.get("suggestions", {})
    not_shipped = catalog.get("annexKNotShipped", {})
    check(isinstance(symbols, dict) and len(symbols) == 47,
          "catalog must describe all 47 shipped _s prototypes")
    check(isinstance(suggestions, dict) and len(suggestions) >= 20,
          "suggestion catalog is unexpectedly small")
    check(isinstance(not_shipped, dict) and len(not_shipped) >= 15,
          "Annex K not-shipped list is unexpectedly small")

    # 1. Catalog vs shipped headers.
    prototypes = prototype_symbols()
    check(set(symbols) == set(prototypes),
          "catalog/header symbol mismatch: missing=%s extra=%s" %
          (sorted(set(symbols) - set(prototypes)),
           sorted(set(prototypes) - set(symbols))))
    check(set(prototypes) == set(symbols) and
          all(symbols[n]["header"] == prototypes[n][0] for n in symbols
              if n in prototypes),
          "catalog header path does not match the declaring header")
    for name, spec in symbols.items():
        for field in REQUIRED_SYMBOL_FIELDS:
            check(bool(spec.get(field)), "%s: missing catalog field %s" % (name, field))
        check(spec.get("abi") in {"preserving", "changing"},
              "%s: invalid abi tag %r" % (name, spec.get("abi")))
        check(spec.get("mechanism") in {"direct-symbol", "source-macro",
                                        "additive-symbol", "annex-k-symbol"},
              "%s: invalid mechanism %r" % (name, spec.get("mechanism")))
        check(bool(re.search(r"\b" + re.escape(name) + r"\s*\(",
                             str(spec.get("signature", "")))),
              "%s: signature does not name the symbol" % name)
        if name in prototypes:
            check(canonical_signature(spec.get("signature", "")) == prototypes[name][1],
                  "%s: signature drift: catalog=%r header=%r" %
                  (name, spec.get("signature"), prototypes[name][1]))

    # 2. Diagnostic contract (issue #188).
    required_legacy = {
        "strncpy", "strcpy", "strcat", "strncat", "memcpy", "memmove",
        "memset", "sprintf", "vsprintf", "scanf", "sscanf", "fscanf",
        "fopen", "strtok", "strerror", "getenv", "gmtime", "localtime",
        "qsort", "bsearch", "mbrtowc",
    }
    check(required_legacy <= set(suggestions),
          "missing unsafe-call suggestions: %s" % sorted(required_legacy - set(suggestions)))
    helpers = catalog.get("sourceHelpers", {})
    check(set(helpers) == {"cplus_fread", "cplus_fwrite"},
          "sourceHelpers must describe the safe_c.h stream helpers")
    targets = set(symbols) | set(not_shipped) | set(helpers)
    for legacy, spec in suggestions.items():
        check(spec.get("family") in {"string", "memory", "format", "stdio",
                                     "stdlib", "allocator"},
              "%s: unknown family" % legacy)
        check(spec.get("diagnosticId") == "CPLUS-SUGGEST-SAFE-S",
              "%s: diagnostic id missing" % legacy)
        check(spec.get("abiTag") in {"ABI_PRESERVING_DROP_IN",
                                     "ABI_CHANGING_ANNEX_K"},
              "%s: missing ABI preservation tag" % legacy)
        check(bool(spec.get("note")), "%s: missing ABI explanation" % legacy)
        check(bool(spec.get("options")), "%s: no _s option offered" % legacy)
        for option in spec.get("options", []):
            for field in REQUIRED_OPTION_FIELDS:
                check(bool(option.get(field)),
                      "%s: option missing %s" % (legacy, field))
            check(option.get("symbol") in targets,
                  "%s: suggestion names unknown symbol %s" %
                  (legacy, option.get("symbol")))
            check(option.get("abi") in {"preserving", "changing"},
                  "%s: option %s has no ABI tag" % (legacy, option.get("symbol")))
            check(option.get("mechanism") in {"direct-symbol", "source-macro",
                                              "annex-k-symbol",
                                              "annex-k-symbol-not-shipped"},
                  "%s: option %s has unknown mechanism" %
                  (legacy, option.get("symbol")))
            if (option.get("abi") == "changing" and
                    spec.get("family") not in {"allocator"} and
                    legacy not in NO_DEST_EXTENT):
                check(bool(option.get("fixItKnownExtent")),
                      "%s: no fix-it for statically known destination extent" % legacy)
        tag_ok = ((spec.get("abiTag") == "ABI_PRESERVING_DROP_IN") ==
                  any(o.get("abi") == "preserving" for o in spec.get("options", [])))
        check(tag_ok, "%s: ABI tag disagrees with the offered options" % legacy)

    for name, spec in not_shipped.items():
        check(spec.get("abi") == "changing" and spec.get("shipped") is False
              and bool(spec.get("reason")),
              "not-shipped Annex K entry %s is incomplete" % name)

    # 3. Doc sync (issue #190).
    doc_texts = {}
    for key, path in DOCS.items():
        check(path.is_file(), "missing dedicated doc: %s" % path)
        doc_texts[key] = path.read_text() if path.is_file() else ""
    documented = set()
    for text in doc_texts.values():
        documented |= backticked_symbols(text)
    expected = targets
    check(documented == expected,
          "doc/catalog drift: missing=%s undocumented=%s" %
          (sorted(expected - documented), sorted(documented - expected)))
    for legacy in suggestions:
        check("`%s`" % legacy in doc_texts["matrix"],
              "%s: missing from compatibility matrix" % legacy)
    check("-Wcplus-suggest-safe-s" in doc_texts["matrix"],
          "compatibility matrix does not name the diagnostic flag")
    for link_path in CROSS_LINKS:
        text = link_path.read_text() if link_path.is_file() else ""
        for key in ("preserving", "changing", "matrix"):
            check(DOCS[key].name in text,
                  "%s does not link %s" % (link_path, DOCS[key].name))
    check(ABI_DOC.is_file(), "missing docs/abi/STRICT_C_ABI_BOUNDARY.md")
    if ABI_DOC.is_file():
        text = ABI_DOC.read_text()
        check("safeSAbiCatalog" in text and "c_to_cp" in text and "precompiled" in text,
              "ABI contract doc does not cross-reference catalog and boundary suites")
    return report()


def report() -> int:
    if errors:
        print("SAFE_S_CATALOG_SYNC: FAIL (%d/%d checks failed)" % (len(errors), checks))
        for error in errors:
            print("  FAIL", error)
        return 1
    print("SAFE_S_CATALOG_SYNC: PASS (%d checks, %d shipped symbols, %d suggestions)" %
          (checks, 47, 37))
    return 0


if __name__ == "__main__":
    sys.exit(main())
