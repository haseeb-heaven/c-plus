#!/usr/bin/env python3
"""Standalone alignment test for the Agent 1 compiler contract schema.

Loads libc/contracts/compiler_libc_contracts.json and checks that it matches the
key set, API names and CPLUS-SAFE-* diagnostic categories requested by Agent 1
in issue #22. Mirrors the explicit-check style of validate_contracts.py: bare
``assert`` is avoided so ``python3 -O`` cannot silently disable the test.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
CONTRACT = ROOT / "libc" / "contracts" / "compiler_libc_contracts.json"

API_KEYS = {
    "name", "params", "ownership", "transfer", "extent", "effects",
    "failure", "diagnostics", "dynamic",
}
PARAM_KEYS = {"name", "type", "nullable", "extent"}
FAILURE_KEYS = {"result_code", "conditions"}
DYNAMIC_KEYS = {"checks", "limitations"}

REQUIRED_NAMES = {
    "malloc", "calloc", "realloc", "free",
    "memcpy", "memmove", "memset", "memcmp",
    "strcpy", "strncpy", "strcat", "strncat", "strlen", "strnlen",
    "strcmp", "strncmp", "strchr", "strstr",
    "printf", "fprintf", "snprintf", "sprintf", "scanf", "fscanf", "sscanf",
    "fopen", "fclose", "fread", "fwrite",
}

# Exactly the five categories from Agent 1's issue #22 comment.
ALLOWED_DIAGNOSTICS = {
    "[CPLUS-SAFE-FREE] double free of <var>",
    "[CPLUS-SAFE-LIFETIME] use-after-free",
    "[CPLUS-SAFE-BOUNDS] copy of N bytes exceeds destination <var>",
    "[CPLUS-SAFE-CRT] checked string copy needs N bytes but capacity is M",
    "[CPLUS-SAFE-REALLOC] realloc warnings",
}


def fail(message):
    sys.exit("compiler contract alignment FAILED: " + message)


def exact_keys(obj, expected, where):
    if not isinstance(obj, dict):
        fail(f"{where}: expected object")
    actual = set(obj)
    if actual != expected:
        fail(f"{where}: key mismatch, missing={sorted(expected - actual)}, "
             f"extra={sorted(actual - expected)}")


def non_empty_str_list(value, where):
    if not isinstance(value, list) or not value:
        fail(f"{where}: expected a non-empty list")
    for item in value:
        if not isinstance(item, str) or not item.strip():
            fail(f"{where}: expected non-empty strings")


def main():
    if not CONTRACT.is_file():
        fail(f"missing contract file: {CONTRACT}")
    try:
        data = json.loads(CONTRACT.read_text())
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        fail(f"invalid JSON in {CONTRACT.name}: {exc}")

    if not isinstance(data, dict):
        fail("top level: expected object")
    if data.get("schema_version") != 1:
        fail(f"schema_version must be 1, got {data.get('schema_version')!r}")
    if data.get("source") != "Agent 2 hardened libc":
        fail(f"source must be 'Agent 2 hardened libc', got {data.get('source')!r}")
    if not isinstance(data.get("note"), str) or not data["note"].strip():
        fail("note: expected a non-empty string")
    apis = data.get("apis")
    if not isinstance(apis, list) or not apis:
        fail("apis: expected a non-empty list")

    seen = set()
    for index, api in enumerate(apis):
        where = f"apis[{index}]"
        exact_keys(api, API_KEYS, where)
        name = api["name"]
        if not isinstance(name, str) or not name.strip():
            fail(f"{where}.name: expected a non-empty string")
        if name in seen:
            fail(f"{where}: duplicate API name {name!r}")
        seen.add(name)

        if not isinstance(api["params"], list):
            fail(f"{where}.params: expected a list")
        for pindex, param in enumerate(api["params"]):
            pwhere = f"{where}.params[{pindex}]"
            exact_keys(param, PARAM_KEYS, pwhere)
            if not isinstance(param["name"], str) or not param["name"].strip():
                fail(f"{pwhere}.name: expected a non-empty string")
            if not isinstance(param["type"], str) or not param["type"].strip():
                fail(f"{pwhere}.type: expected a non-empty string")
            if not isinstance(param["nullable"], bool):
                fail(f"{pwhere}.nullable: expected a bool")
            if not isinstance(param["extent"], str) or not param["extent"].strip():
                fail(f"{pwhere}.extent: expected a non-empty string")

        if not isinstance(api["ownership"], str) or not api["ownership"].strip():
            fail(f"{where}.ownership: expected a non-empty string")
        if api["transfer"] is not None and not isinstance(api["transfer"], str):
            fail(f"{where}.transfer: expected a string or null")
        if not isinstance(api["extent"], str) or not api["extent"].strip():
            fail(f"{where}.extent: expected a non-empty string")
        non_empty_str_list(api["effects"], f"{where}.effects")

        exact_keys(api["failure"], FAILURE_KEYS, f"{where}.failure")
        if not isinstance(api["failure"]["result_code"], str) or \
                not api["failure"]["result_code"].strip():
            fail(f"{where}.failure.result_code: expected a non-empty string")
        non_empty_str_list(api["failure"]["conditions"],
                           f"{where}.failure.conditions")

        if not isinstance(api["diagnostics"], list):
            fail(f"{where}.diagnostics: expected a list")
        for diagnostic in api["diagnostics"]:
            if diagnostic not in ALLOWED_DIAGNOSTICS:
                fail(f"{where}.diagnostics: unknown category {diagnostic!r}")

        exact_keys(api["dynamic"], DYNAMIC_KEYS, f"{where}.dynamic")
        # Honesty guard: every entry must state at least one real runtime check
        # (or an explicit 'no runtime guard' marker) and a limitations list.
        non_empty_str_list(api["dynamic"]["checks"], f"{where}.dynamic.checks")
        if not isinstance(api["dynamic"]["limitations"], list):
            fail(f"{where}.dynamic.limitations: expected a list")

    missing = sorted(REQUIRED_NAMES - seen)
    if missing:
        fail(f"missing required API names: {missing}")

    print("compiler contract alignment: ok")


if __name__ == "__main__":
    main()
