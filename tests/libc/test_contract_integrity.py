#!/usr/bin/env python3
"""Adversarial integrity tests for the C+ libc contract-validation tooling.

The validator under test is ``tests/libc/validate_contracts.py``.  Every
mutation below happens inside a temporary overlay of the repo's contract tree;
the working tree is never modified.  The overlay copies the validator and the
bundled header so that the validator's ROOT (computed from ``__file__``)
resolves to the temporary tree.

Run from anywhere:

    python3 tests/libc/test_contract_integrity.py

``contract_integrity: ok`` is printed only when every attack is rejected and
every cross-check is clean.  Otherwise the script lists exact findings and
returns a nonzero status.
"""
from __future__ import annotations

import collections
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
VALIDATOR_REL = pathlib.Path("tests/libc/validate_contracts.py")
HEADER_REL = pathlib.Path("libc/install-include/cplus_libc.h")
CONTRACTS_REL = pathlib.Path("libc/contracts")
LIB_REL = pathlib.Path("build/libc/libcplus_hardened.a")
MATRIX_REL = pathlib.Path("docs/libc/API_TEST_MATRIX.md")
C17_NAME = "c17_contracts.json"
REQUIRED_FILES = (
    "contracts.json",
    "ownership_contracts.json",
    "libc_contracts.json",
    "symbol_map.json",
)
REQUIRED_KEYS = {
    "contracts.json": ("apis", "name", "header", "ownership", "bounds", "invalid", "errors"),
    "ownership_contracts.json": ("functions", "name", "params", "return", "thread_safety"),
    "libc_contracts.json": ("functions", "params", "ownership", "failure", "returnState"),
    "symbol_map.json": (
        "mappings",
        "standard",
        "bundled_symbol",
        "safe_symbol",
        "header",
        "classification",
    ),
}

FINDINGS: list[str] = []
WARNINGS: list[str] = []
ATTACKS = 0


def finding(message: str) -> None:
    FINDINGS.append(message)


def warning(message: str) -> None:
    WARNINGS.append(message)


# --------------------------------------------------------------------------- #
# Temporary overlay
# --------------------------------------------------------------------------- #
class Overlay:
    """A private copy of the files the validator reads at import/parse time."""

    def __init__(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="contract-attack-")
        self.root = pathlib.Path(self._tmp.name)
        (self.root / VALIDATOR_REL.parent).mkdir(parents=True, exist_ok=True)
        (self.root / CONTRACTS_REL).mkdir(parents=True, exist_ok=True)
        (self.root / HEADER_REL.parent).mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO / VALIDATOR_REL, self.root / VALIDATOR_REL)
        self.contract_sources = sorted((REPO / CONTRACTS_REL).glob("*.json"))
        self.reset()

    def reset(self) -> None:
        for source in self.contract_sources:
            shutil.copy2(source, self.root / CONTRACTS_REL / source.name)
        shutil.copy2(REPO / HEADER_REL, self.root / HEADER_REL)

    def mutate_json(self, name: str, mutate) -> None:
        path = self.root / CONTRACTS_REL / name
        data = json.loads(path.read_text())
        mutate(data)
        path.write_text(json.dumps(data))

    def corrupt_syntax(self, name: str) -> None:
        (self.root / CONTRACTS_REL / name).write_text('{ "version": 1, "oops": ')

    def append_header(self, text: str) -> None:
        with (self.root / HEADER_REL).open("a") as handle:
            handle.write("\n" + text + "\n")

    def run(self, flags=()) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, *flags, str(self.root / VALIDATOR_REL)],
            capture_output=True,
            text=True,
        )

    def close(self) -> None:
        self._tmp.cleanup()

    def __enter__(self) -> "Overlay":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


def expect_rejected(overlay: Overlay, label: str, flags=()) -> subprocess.CompletedProcess:
    """An attack is successful only if the validator exits nonzero *usefully*."""
    global ATTACKS
    ATTACKS += 1
    result = overlay.run(flags)
    if result.returncode == 0:
        finding(
            f"SILENT ACCEPT: {label} -- validator exited 0 "
            f"(stdout={result.stdout.strip()!r})"
        )
        return result
    message = (result.stderr or result.stdout).strip()
    last = message.splitlines()[-1] if message else ""
    weak = (
        not message
        or last == "AssertionError"
        or bool(re.match(r"^(TypeError|KeyError|ValueError|AttributeError|IndexError)\b", last))
    )
    if weak:
        finding(
            f"WEAK DIAGNOSTIC: {label} -- exit {result.returncode} with no useful "
            f"message (last line={last!r})"
        )
    return result


# --------------------------------------------------------------------------- #
# Scenario 1: corruption of each required JSON file
# --------------------------------------------------------------------------- #
def attack_required_files(overlay: Overlay) -> None:
    for name in REQUIRED_FILES:
        keys = REQUIRED_KEYS[name]

        overlay.reset()
        overlay.corrupt_syntax(name)
        expect_rejected(overlay, f"{name}: invalid JSON syntax")

        overlay.reset()
        overlay.mutate_json(name, lambda data: data.__setitem__("version", 999))
        expect_rejected(overlay, f"{name}: wrong version (999)")

        for key in keys[1:]:
            overlay.reset()

            def drop(data, key=key):
                if name == "libc_contracts.json":
                    first = next(iter(data["functions"]))
                    data["functions"][first].pop(key, None)
                else:
                    data[keys[0]][0].pop(key, None)

            overlay.mutate_json(name, drop)
            expect_rejected(overlay, f"{name}: missing required key {key!r}")

        overlay.reset()

        def empty(data):
            data[keys[0]] = {} if name == "libc_contracts.json" else []

        overlay.mutate_json(name, empty)
        expect_rejected(overlay, f"{name}: empty {keys[0]} collection")

    overlay.reset()
    overlay.mutate_json(
        "symbol_map.json",
        lambda data: data["mappings"][0].__setitem__("classification", "NOT_A_CLASS"),
    )
    expect_rejected(overlay, "symbol_map.json: invalid classification value")

    # assert-based checks vanish under `python3 -O`; a validator must not rely
    # on assert for schema enforcement.
    overlay.reset()
    overlay.mutate_json("contracts.json", lambda data: data.__setitem__("version", 999))
    expect_rejected(overlay, "contracts.json: wrong version under python -O", flags=("-O",))
    overlay.reset()
    overlay.mutate_json(
        "ownership_contracts.json",
        lambda data: data["functions"][0].pop("thread_safety"),
    )
    expect_rejected(
        overlay,
        "ownership_contracts.json: missing key under python -O",
        flags=("-O",),
    )

    # c17_contracts.json is shipped alongside the four required files and the
    # docs claim it is validated, yet the validator never reads it.
    c17_attacks = (
        ("invalid JSON syntax", lambda path: path.write_text("{oops")),
        (
            "wrong schema_version",
            lambda path: _edit(path, lambda d: d.__setitem__("schema_version", 99)),
        ),
        (
            "empty functions array",
            lambda path: _edit(path, lambda d: d.__setitem__("functions", [])),
        ),
        (
            "invalid classification",
            lambda path: _edit(
                path, lambda d: d["functions"][0].__setitem__("classification", "BOGUS")
            ),
        ),
        (
            "missing name key",
            lambda path: _edit(path, lambda d: d["functions"][0].pop("name")),
        ),
        (
            "duplicate name",
            lambda path: _edit(
                path,
                lambda d: d["functions"][1].__setitem__("name", d["functions"][0]["name"]),
            ),
        ),
        (
            "missing source file",
            lambda path: _edit(
                path, lambda d: d["functions"][0].__setitem__("source", "libc/src/missing.c")
            ),
        ),
        (
            "empty host_backend",
            lambda path: _edit(
                path, lambda d: d["functions"][0].__setitem__("host_backend", "")
            ),
        ),
        (
            "empty safety_limitations",
            lambda path: _edit(
                path, lambda d: d["functions"][0].__setitem__("safety_limitations", [])
            ),
        ),
    )
    for label, mutate in c17_attacks:
        overlay.reset()
        mutate(overlay.root / CONTRACTS_REL / C17_NAME)
        expect_rejected(overlay, f"{C17_NAME}: {label}")


def _edit(path: pathlib.Path, mutate) -> None:
    data = json.loads(path.read_text())
    mutate(data)
    path.write_text(json.dumps(data))


# --------------------------------------------------------------------------- #
# Scenario 2: symbol-map forgery
# --------------------------------------------------------------------------- #
def attack_symbol_forgery(overlay: Overlay) -> None:
    for label, value in (
        ("near-miss cplus_mallocX", "cplus_mallocX"),
        ("trailing space 'cplus_malloc '", "cplus_malloc "),
        ("unknown cplus_no_such_wrapper", "cplus_no_such_wrapper"),
        ("macro-only cplus_alloc_len", "cplus_alloc_len"),
        ("host macro malloc", "malloc"),
        ("empty bundled_symbol", ""),
        ("non-string bundled_symbol", 123),
    ):
        overlay.reset()
        overlay.mutate_json(
            "symbol_map.json",
            lambda data, value=value: data["mappings"][0].__setitem__(
                "bundled_symbol", value
            ),
        )
        expect_rejected(overlay, f"symbol_map.json: bundled_symbol = {label}")

    overlay.reset()
    overlay.append_header("/* adversarial comment: cplus_ghost_fn( */")
    overlay.mutate_json(
        "symbol_map.json",
        lambda data: data["mappings"][0].__setitem__("bundled_symbol", "cplus_ghost_fn"),
    )
    expect_rejected(overlay, "symbol_map.json: symbol only present in a header comment")

    overlay.reset()
    overlay.mutate_json(
        "symbol_map.json",
        lambda data: data["mappings"][0].__setitem__(
            "safe_symbol", "cplus_fake_safe_symbol"
        ),
    )
    expect_rejected(overlay, "symbol_map.json: forged safe_symbol (unchecked field)")


# --------------------------------------------------------------------------- #
# C declaration helpers (scenarios 3-5)
# --------------------------------------------------------------------------- #
def strip_noise(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    kept: list[str] = []
    continuation = False
    for line in text.splitlines():
        if continuation:
            if not line.rstrip().endswith("\\"):
                continuation = False
            continue
        if line.lstrip().startswith("#"):
            continuation = line.rstrip().endswith("\\")
            continue
        kept.append(line)
    return "\n".join(kept)


def find_c_calls(text: str):
    """Yield (name, return_tokens, params, terminator) for ``name(...)`` text."""
    results = []
    for match in re.finditer(r"\b(cplus_[A-Za-z0-9_]+)\s*\(", text):
        name = match.group(1)
        open_paren = match.end() - 1
        depth = 0
        close_paren = None
        for index in range(open_paren, len(text)):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    close_paren = index
                    break
        if close_paren is None:
            continue
        params = text[open_paren + 1 : close_paren]
        cursor = close_paren + 1
        while cursor < len(text) and text[cursor] in " \t\r\n":
            cursor += 1
        terminator = text[cursor] if cursor < len(text) else ""
        previous = max(
            text.rfind(";", 0, match.start()),
            text.rfind("{", 0, match.start()),
            text.rfind("}", 0, match.start()),
        )
        return_text = text[previous + 1 : match.start()].strip()
        return_text = return_text.split("\n")[-1].strip()
        # File-local helpers are not exported symbols: do not treat a
        # `static int foo(...) {` definition as a matrix export.
        if re.match(r"^static\b", return_text):
            continue
        results.append((name, return_text, params, terminator))
    return results


TYPE_KEYWORDS = {
    "void", "char", "short", "int", "long", "unsigned", "signed", "float",
    "double", "const", "volatile", "restrict", "struct", "union", "enum",
    "wchar_t", "wint_t", "size_t", "ptrdiff_t", "intmax_t", "uintmax_t",
    "FILE", "fenv_t", "fexcept_t", "mbstate_t", "wctype_t", "complex",
    "_Complex", "time_t",
}


def tokens(text: str) -> list[str]:
    return re.findall(r"[A-Za-z_][A-Za-z0-9_]*|\*|\[|\]|[0-9]+|\.\.\.", text)


def split_commas(text: str) -> list[str]:
    parts: list[str] = []
    depth = 0
    current = ""
    for char in text:
        if char in "([":
            depth += 1
        elif char in ")]":
            depth -= 1
        if char == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += char
    if current.strip():
        parts.append(current)
    return parts


def normalize_params(text: str) -> list[str]:
    text = text.strip()
    if not text or text == "void":
        return []
    normalized = []
    for part in split_commas(text):
        part_tokens = tokens(part)
        if (
            part_tokens
            and re.fullmatch(r"[A-Za-z_]\w*", part_tokens[-1])
            and part_tokens[-1] not in TYPE_KEYWORDS
        ):
            part_tokens = part_tokens[:-1]  # drop the parameter name
        normalized.append(" ".join(part_tokens))
    return normalized


def normalize_return(text: str) -> str:
    return " ".join(tokens(text))


# --------------------------------------------------------------------------- #
# Scenarios 3-6: cross-checks against the real repository data
# --------------------------------------------------------------------------- #
def cross_check_repository() -> None:
    c17_path = REPO / CONTRACTS_REL / C17_NAME
    if not c17_path.is_file():
        finding(f"cross-check: missing {c17_path}")
        return
    data = json.loads(c17_path.read_text())
    functions = data.get("functions", [])
    # C17 entry count includes the Tier-2 checked wide-format sibling
    # cplus_swprintf_checked added by the wide-format %n fix.
    if len(functions) != 203:
        finding(f"cross-check: expected 203 c17 entries, found {len(functions)}")
    names = [entry.get("name") for entry in functions]
    duplicates = sorted(name for name, count in collections.Counter(names).items() if count > 1)
    if duplicates:
        finding(f"cross-check: duplicate c17 names: {duplicates}")
    name_set = set(names)

    for entry in functions:
        label = entry.get("name", "<unnamed>")
        for key in (
            "name", "header", "classification", "source", "test", "host_backend",
            "contracts", "safety_limitations",
        ):
            if not entry.get(key):
                finding(f"cross-check: c17 entry {label} has empty/missing {key}")
        if entry.get("classification") != "PLATFORM_BACKED":
            finding(f"cross-check: c17 entry {label} classification is not PLATFORM_BACKED")
        for key in ("source", "test"):
            relative = entry.get(key, "")
            if relative and not (REPO / relative).is_file():
                finding(f"cross-check: c17 entry {label} {key} file missing: {relative}")

    header_declarations = {
        name: (ret, params)
        for name, ret, params, terminator in find_c_calls(strip_noise(
            (REPO / HEADER_REL).read_text()
        ))
        if terminator == ";"
    }

    compiled: set[str] = set()
    library = REPO / LIB_REL
    nm = shutil.which("nm")
    if library.is_file() and nm:
        result = subprocess.run(
            [nm, "-g", "--defined-only", str(library)], capture_output=True, text=True
        )
        compiled = {
            match.group(1)
            for match in re.finditer(r"\bT\s+_?(cplus_[A-Za-z0-9_]+)", result.stdout)
        }
        for name in sorted(name_set - compiled):
            finding(f"cross-check: matrix entry {name} has no compiled symbol")
    else:
        warning(f"nm or {LIB_REL} unavailable; skipped compiled-symbol cross-check")

    source_definitions: dict[str, tuple[str, str]] = {}
    source_files = sorted({entry.get("source", "") for entry in functions if entry.get("source")})
    for relative in source_files:
        path = REPO / relative
        if not path.is_file():
            continue
        for name, ret, params, terminator in find_c_calls(strip_noise(path.read_text())):
            if terminator != "{":
                continue
            if name not in name_set:
                finding(f"cross-check: exported {name} in {relative} missing from matrix")
            if compiled and name not in compiled:
                finding(f"cross-check: {name} defined in {relative} but not compiled")
            source_definitions[name] = (ret, params)

    for name in sorted(name_set - set(source_definitions)):
        finding(f"cross-check: matrix entry {name} has no source definition")

    for name in sorted(name_set):
        if name not in header_declarations:
            finding(f"cross-check: matrix symbol {name} not declared in cplus_libc.h")
            continue
        header_ret, header_params = header_declarations[name]
        if name not in source_definitions:
            continue
        source_ret, source_params = source_definitions[name]
        if normalize_return(source_ret) != normalize_return(header_ret) or normalize_params(
            source_params
        ) != normalize_params(header_params):
            finding(
                f"cross-check: signature mismatch for {name}: "
                f"header=({header_ret}) {header_params!r} vs source=({source_ret}) {source_params!r}"
            )

    doc_path = REPO / MATRIX_REL
    if doc_path.is_file():
        document = doc_path.read_text()
        rows = re.findall(r"^\|\s*(cplus_[A-Za-z0-9_]+)\s*\|", document, flags=re.M)
        if len(rows) != len(functions):
            finding(
                f"cross-check: {MATRIX_REL} has {len(rows)} rows but JSON has "
                f"{len(functions)} functions"
            )
        for name in sorted(set(rows) - name_set):
            finding(f"cross-check: doc row {name} missing from JSON")
        for name in sorted(name_set - set(rows)):
            finding(f"cross-check: JSON function {name} missing from doc rows")
    else:
        warning(f"{MATRIX_REL} unavailable; skipped doc row-count cross-check")


# --------------------------------------------------------------------------- #
def main() -> int:
    with Overlay() as overlay:
        attack_required_files(overlay)
        attack_symbol_forgery(overlay)
    cross_check_repository()

    for message in WARNINGS:
        print(f"warning: {message}", file=sys.stderr)
    if FINDINGS:
        print(f"contract_integrity: FINDINGS ({len(FINDINGS)} in {ATTACKS} attacks):", file=sys.stderr)
        for message in FINDINGS:
            print(f"  - {message}", file=sys.stderr)
        return 1
    print(f"contract_integrity: ok ({ATTACKS} attacks detected, all cross-checks clean)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
