#!/usr/bin/env python3
"""Validate machine-readable contract schemas and bundled symbol declarations."""
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
)
VALID_CLASSIFICATIONS = {
    "IMPLEMENTED",
    "VENDORED",
    "HOST-DELEGATED",
    "PARTIAL",
    "UNSUPPORTED",
}


def main():
    for name in REQUIRED_FILES:
        path = CONTRACTS / name
        if not path.is_file():
            sys.exit(f"missing contract file: {path}")
        try:
            data = json.loads(path.read_text())
        except json.JSONDecodeError as exc:
            sys.exit(f"invalid JSON in {path}: {exc}")

        if name == "contracts.json":
            assert data.get("version") == 1 and isinstance(data.get("apis"), list)
            for api in data["apis"]:
                for k in ("name", "header", "ownership", "bounds", "invalid", "errors"):
                    assert k in api, f"{name}: missing {k} in {api}"
        elif name == "ownership_contracts.json":
            assert data.get("version") == 1 and isinstance(data.get("functions"), list)
            for fn in data["functions"]:
                for k in ("name", "params", "return", "thread_safety"):
                    assert k in fn, f"{name}: missing {k} in {fn}"
        elif name == "libc_contracts.json":
            assert data.get("version") == 2 and isinstance(data.get("functions"), dict)
            for fname, spec in data["functions"].items():
                for k in ("params", "ownership", "failure", "returnState"):
                    assert k in spec, f"{name}: {fname} missing {k}"
        elif name == "symbol_map.json":
            assert data.get("version") == 1 and isinstance(data.get("mappings"), list)
            for entry in data["mappings"]:
                for k in ("standard", "bundled_symbol", "safe_symbol", "header", "classification"):
                    assert k in entry, f"{name}: missing {k} in {entry}"
                if entry["classification"] not in VALID_CLASSIFICATIONS:
                    sys.exit(f"invalid classification in {name}: {entry}")
                sym = entry["bundled_symbol"]
                if sym is not None:
                    if not re.search(r"\b" + re.escape(sym) + r"\s*\(", HEADER):
                        sys.exit(f"{name}: bundled_symbol {sym} not declared in cplus_libc.h")

    print("contract validation: ok (4 files validated)")


if __name__ == "__main__":
    main()
