#!/usr/bin/env python3
"""Validate machine-readable contract schemas and bundled symbol declarations.

Deliberately does NOT use ``assert``: Python's ``-O`` flag removes assertions,
so an assert-based validator silently accepts malformed input. Every check
raises SystemExit with an actionable message instead. This validator is itself
covered by tests/libc/test_contract_integrity.py.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
CONTRACTS = ROOT / "libc" / "contracts"
HEADER = (ROOT / "libc" / "install-include" / "cplus_libc.h").read_text()

REQUIRED_FILES = (
    "contracts.json",
    "ownership_contracts.json",
    "libc_contracts.json",
    "symbol_map.json",
    "c17_contracts.json",
)
VALID_CLASSIFICATIONS = {
    "IMPLEMENTED",
    "VENDORED",
    "HOST-DELEGATED",
    "PARTIAL",
    "UNSUPPORTED",
}
VALID_C17_CLASSIFICATIONS = {
    "IMPLEMENTED_BUNDLED",
    "IMPLEMENTED_UPSTREAM",
    "PLATFORM_BACKED",
    "PARTIAL",
    "MISSING",
    "UNSUPPORTED",
}
EXPECTED_VERSIONS = {
    "contracts.json": 1,
    "ownership_contracts.json": 1,
    "libc_contracts.json": 2,
    "symbol_map.json": 1,
}


def fail(message):
    sys.exit("contract validation FAILED: " + message)


def keys(obj, required, where):
    if not isinstance(obj, dict):
        fail(f"{where}: expected object")
    for key in required:
        if key not in obj:
            fail(f"{where}: missing required key '{key}'")


def load(name):
    path = CONTRACTS / name
    if not path.is_file():
        fail(f"missing contract file: {path}")
    try:
        return json.loads(path.read_text())
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        fail(f"invalid JSON in {name}: {exc}")


def declared_as_function(symbol):
    """True if the public header declares `symbol` as a function prototype.

    A plain ``\\bname\\s*\\(`` search is insufficient: it also matches macro
    definitions and mentions inside comments. Require a prototype terminated
    by a semicolon.
    """
    if not isinstance(symbol, str) or not symbol:
        return False
    pattern = re.compile(
        r"(?:^|[;{}])\s*[A-Za-z_][A-Za-z0-9_ \t*]*?\b"
        + re.escape(symbol)
        + r"\s*\([^;{}]*\)\s*(?:CPLUS_[A-Z_]+\s*\([^)]*\))?\s*;",
        re.MULTILINE,
    )
    return pattern.search(HEADER) is not None


def validate_c17(data):
    keys(data, ("schema_version", "functions"), "c17_contracts.json")
    if data["schema_version"] != 1:
        fail("c17_contracts.json: schema_version must be 1")
    functions = data["functions"]
    if not isinstance(functions, list) or not functions:
        fail("c17_contracts.json: functions must be a non-empty list")
    seen = set()
    required = (
        "name", "header", "classification", "source", "test", "host_backend",
        "contracts", "logging", "safety_limitations",
    )
    for index, fn in enumerate(functions):
        where = f"c17_contracts.json functions[{index}]"
        keys(fn, required, where)
        name = fn["name"]
        if not isinstance(name, str) or not name.startswith("cplus_"):
            fail(f"{where}: name must be a non-empty cplus_* symbol")
        if name in seen:
            fail(f"{where}: duplicate name '{name}'")
        seen.add(name)
        if fn["classification"] not in VALID_C17_CLASSIFICATIONS:
            fail(f"{where}: invalid classification '{fn['classification']}'")
        for field in ("header", "source", "test", "host_backend"):
            if not isinstance(fn[field], str) or not fn[field].strip():
                fail(f"{where}: '{field}' must be a non-empty string")
        for field in ("source", "test"):
            if not (ROOT / fn[field]).is_file():
                fail(f"{where}: {field} path does not exist: {fn[field]}")
        if not isinstance(fn["contracts"], list) or not fn["contracts"]:
            fail(f"{where}: contracts must be a non-empty list")
        if not isinstance(fn["safety_limitations"], list) or not fn["safety_limitations"]:
            fail(f"{where}: safety_limitations must be a non-empty list")
        logging = fn["logging"]
        keys(logging, ("enabled_by", "records", "tests"), f"{where}.logging")
        if not isinstance(logging["records"], list) or not logging["records"]:
            fail(f"{where}.logging: records must be a non-empty list")
        if not isinstance(logging["tests"], list) or not logging["tests"]:
            fail(f"{where}.logging: tests must be a non-empty list")
        if not declared_as_function(name):
            fail(f"{where}: {name} not declared as a function in cplus_libc.h")

    # Matrix row count must agree with the human-readable documentation.
    matrix = ROOT / "docs" / "libc" / "API_TEST_MATRIX.md"
    if matrix.is_file():
        rows = sum(
            1 for line in matrix.read_text().splitlines()
            if line.startswith("| cplus_")
        )
        if rows != len(functions):
            fail(f"c17_contracts.json: {len(functions)} functions but "
                 f"API_TEST_MATRIX.md has {rows} rows")
    return len(functions)


def main():
    loaded = {}
    for name in REQUIRED_FILES:
        loaded[name] = load(name)

    for name, version in EXPECTED_VERSIONS.items():
        data = loaded[name]
        key = "version"
        if data.get(key) != version:
            fail(f"{name}: {key} must be {version}, got {data.get(key)!r}")

    contracts = loaded["contracts.json"]
    if not isinstance(contracts.get("apis"), list) or not contracts["apis"]:
        fail("contracts.json: apis must be a non-empty list")
    for api in contracts["apis"]:
        keys(api, ("name", "header", "ownership", "bounds", "invalid", "errors"),
             "contracts.json api")
        if not isinstance(api["name"], str) or not api["name"]:
            fail("contracts.json: api name must be a non-empty string")

    owner = loaded["ownership_contracts.json"]
    if not isinstance(owner.get("functions"), list) or not owner["functions"]:
        fail("ownership_contracts.json: functions must be a non-empty list")
    for fn in owner["functions"]:
        keys(fn, ("name", "params", "return", "thread_safety"),
             "ownership_contracts.json function")

    libc_contracts = loaded["libc_contracts.json"]
    fdict = libc_contracts.get("functions")
    if not isinstance(fdict, dict) or not fdict:
        fail("libc_contracts.json: functions must be a non-empty object")
    for fname, spec in fdict.items():
        keys(spec, ("params", "ownership", "failure", "returnState"),
             f"libc_contracts.json functions['{fname}']")

    smap = loaded["symbol_map.json"]
    mappings = smap.get("mappings")
    if not isinstance(mappings, list) or not mappings:
        fail("symbol_map.json: mappings must be a non-empty list")
    for index, entry in enumerate(mappings):
        where = f"symbol_map.json mappings[{index}]"
        keys(entry, ("standard", "bundled_symbol", "safe_symbol", "header",
                     "classification"), where)
        if entry["classification"] not in VALID_CLASSIFICATIONS:
            fail(f"{where}: invalid classification '{entry['classification']}'")
        sym = entry["bundled_symbol"]
        if sym is not None and not declared_as_function(sym):
            fail(f"{where}: bundled_symbol {sym!r} not declared as a function "
                 "in cplus_libc.h")

    c17_count = validate_c17(loaded["c17_contracts.json"])
    print(f"contract validation: ok ({len(REQUIRED_FILES)} files validated, "
          f"{c17_count} C17 APIs)")


if __name__ == "__main__":
    main()
