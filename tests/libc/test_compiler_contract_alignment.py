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

# The user's P0 API list (Agent 1 integration). REQUIRED_NAMES above is the
# original 29-name baseline; this set is the expanded P0 coverage and must be
# a subset of the names present in the JSON.
P0_REQUIRED_NAMES = {
    "memcpy", "memmove", "memset", "memcmp",
    "strlen", "strnlen", "strcpy", "strncpy", "strcat", "strncat",
    "strcmp", "strncmp", "snprintf", "vsnprintf",
    "malloc_s", "calloc_s", "realloc_s", "free_s", "aligned_alloc_s",
}

# The six entries added by the additive extension. Each must spell out all of
# the semantic aspects requested for Agent 1, not just satisfy the nine-key
# schema. (Names here are a subset of P0_REQUIRED_NAMES.)
EXTENDED_P0_NAMES = {
    "vsnprintf", "malloc_s", "calloc_s", "realloc_s", "free_s",
    "aligned_alloc_s",
}

# Required semantic aspect keys in the additive top-level contract_semantics
# map. The map documents where each aspect is encoded in every nine-key entry.
SEMANTIC_ASPECTS = {
    "argument_types", "read_write_behavior", "buffer_sizes",
    "source_dest_relationships", "nullability",
    "ownership_acquisition_release", "borrowed_args",
    "returned_pointer_lifetime", "allocation_behavior", "error_behavior",
    "reallocation_semantics",
}

# Substrings that must appear in an extended entry's serialized text for the
# non-structural aspects. Structural aspects (argument types, buffer sizes,
# nullability, ownership, error behavior) are checked directly on the fields.
EXTENDED_TEXT_MARKERS = {
    "read_write_behavior": ("writes", "reads", "allocates", "frees"),
    "source_dest_relationships": ("dest", "source", "src", "ptr", "source/destination"),
    "borrowed_args": ("borrow", "no source/destination", "ptr"),
    "returned_pointer_lifetime": (
        "owned by the caller", "caller-owned", "returned-pointer lifetime",
        "invalidated", "no allocation, reallocation",
    ),
    "allocation_behavior": ("allocat", "no allocation"),
    "reallocation_semantics": ("realloc", "invalidat"),
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


def check_extended_semantics(api, where):
    """Check that a newly added P0 entry spells out all requested aspects.

    Structural aspects are read from the typed fields; the remaining aspects
    are checked against the entry's serialized text so the evidence is
    machine-verifiable rather than implied by a field name alone.
    """
    text = json.dumps(api).lower()

    # argument_types: every param carries a non-empty C type.
    if not api["params"] or any(not p["type"].strip() for p in api["params"]):
        fail(f"{where}: extended entry needs typed params (argument_types)")
    # buffer_sizes: every param and the entry have an extent string.
    if not api["extent"].strip() or any(not p["extent"].strip() for p in api["params"]):
        fail(f"{where}: extended entry needs extents (buffer_sizes)")
    # nullability: every param declares a bool.
    if any(not isinstance(p["nullable"], bool) for p in api["params"]):
        fail(f"{where}: extended entry needs param nullability")
    # ownership_acquisition_release: ownership value + explicit transfer key.
    if not api["ownership"].strip() or "transfer" not in api:
        fail(f"{where}: extended entry needs ownership/transfer")
    # error_behavior: result code plus at least one failure condition.
    if not api["failure"]["result_code"].strip() or not api["failure"]["conditions"]:
        fail(f"{where}: extended entry needs failure result/conditions")

    for aspect, markers in EXTENDED_TEXT_MARKERS.items():
        if not any(marker.lower() in text for marker in markers):
            fail(f"{where}: extended entry missing {aspect} evidence")


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

    # Additive top-level fields introduced by the P0 integration. They document
    # where each required semantic aspect lives and which bundled symbol
    # implements every contract; they do not change the nine-key entry schema.
    semantics = data.get("contract_semantics")
    if not isinstance(semantics, dict):
        fail("contract_semantics: expected object")
    if semantics.get("additive") is not True:
        fail("contract_semantics.additive must be true")
    per_api_keys = semantics.get("per_api_keys")
    if not isinstance(per_api_keys, str) or not all(
            key in per_api_keys for key in API_KEYS):
        fail("contract_semantics.per_api_keys must list all nine API keys")
    aspects = semantics.get("aspects")
    if not isinstance(aspects, dict):
        fail("contract_semantics.aspects: expected object")
    missing_aspects = sorted(SEMANTIC_ASPECTS - set(aspects))
    if missing_aspects:
        fail(f"contract_semantics.aspects missing: {missing_aspects}")
    for aspect, description in aspects.items():
        if not isinstance(description, str) or not description.strip():
            fail(f"contract_semantics.aspects[{aspect}]: expected non-empty string")

    symbols = data.get("implementation_symbols")
    if not isinstance(symbols, dict) or not symbols:
        fail("implementation_symbols: expected a non-empty object")

    apis = data.get("apis")
    if not isinstance(apis, list) or not apis:
        fail("apis: expected a non-empty list")

    seen = set()
    apis_by_name = {}
    for index, api in enumerate(apis):
        where = f"apis[{index}]"
        exact_keys(api, API_KEYS, where)
        name = api["name"]
        if not isinstance(name, str) or not name.strip():
            fail(f"{where}.name: expected a non-empty string")
        if name in seen:
            fail(f"{where}: duplicate API name {name!r}")
        seen.add(name)
        apis_by_name[name] = api

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

    missing_p0 = sorted(P0_REQUIRED_NAMES - seen)
    if missing_p0:
        fail(f"missing P0 contract names: {missing_p0}")

    # implementation_symbols must resolve every contract to a concrete bundled
    # symbol. Ordinary C spellings map to distinct cplus_* symbols; the checked
    # allocators keep their own names (no cplus_ prefix in the safe layer).
    checked_allocators = {
        "malloc_s", "calloc_s", "realloc_s", "free_s", "aligned_alloc_s",
    }
    for name in sorted(seen):
        symbol = symbols.get(name)
        if not isinstance(symbol, str) or not symbol.strip():
            fail(f"implementation_symbols missing valid entry for {name!r}")
        if name in checked_allocators:
            if symbol != name:
                fail(f"implementation_symbols[{name}] must be {name!r}, got {symbol!r}")
        elif not symbol.startswith("cplus_"):
            fail(f"implementation_symbols[{name}] must be a cplus_* symbol, got {symbol!r}")
    extra_symbols = sorted(set(symbols) - seen)
    if extra_symbols:
        fail(f"implementation_symbols has unknown contract names: {extra_symbols}")

    # The six newly added P0 entries must carry explicit evidence for every
    # semantic aspect, not merely the nine schema keys.
    for name in sorted(EXTENDED_P0_NAMES):
        if name not in apis_by_name:
            fail(f"missing extended P0 contract: {name}")
        check_extended_semantics(apis_by_name[name], f"api {name!r}")

    print("compiler contract alignment: ok")


if __name__ == "__main__":
    main()
