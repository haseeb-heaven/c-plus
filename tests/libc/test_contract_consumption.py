#!/usr/bin/env python3
"""Prove which CPLUS-SAFE-* libc contract categories the compiler actually consumes.

This is a *reporting regression test*, not a schema validator.  The schema-only
check already lives in ``test_compiler_contract_alignment.py``; this script asks
the harder question: on branch ``agent/modern-stdlib`` is there any *tagged
compiler output* that corresponds to a diagnostic declared in
``libc/contracts/compiler_libc_contracts.json``?

Method (all four steps are performed on every run):

1. Load ``compiler_libc_contracts.json`` and report counts by diagnostic
   category and by derived API family, with exact denominators.
2. For every contract category, scan ``tests/stdlib/compile_time/*.json`` and
   ``tests/stdlib/compile_time/*.cp`` plus every file in ``tests/compile_fail/``
   for a test that literally references the category tag (e.g. ``[CPLUS-SAFE-BOUNDS]``).
3. Run ``./build/c+ check --strict`` on up to five representative unsafe
   snippets, one per declared diagnostic category, and record the *actual*
   output.  A contract is only said to be "compiler consumed" when the exact
   tag appears in real compiler output.  Rejection under a different tag
   (e.g. ``[CPLUS-OWNER]``) is recorded but is NOT consumption of that contract.
4. Emit one machine-readable JSON document on stdout with exact denominators.

No compiler, runtime, contract, or corpus file is modified.  Snippets are
created in a TemporaryDirectory and removed afterwards.

Exit status:
  * default: 0 only when the observed evidence matches FROZEN_BASELINE, else 1
    (a drift in real compiler behaviour is a signal to re-review the docs).
  * ``--report-only``: 0 whenever the report was produced successfully.
"""
import argparse
import collections
import json
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "libc" / "contracts" / "compiler_libc_contracts.json"
COMPILER = ROOT / "build" / "c+"
EXPECTED_COMPILER_CONTRACT = ROOT / "compiler" / "Analysis" / "libc_contracts.json"
COMPILER_ANALYSIS_DIR = ROOT / "compiler" / "Analysis"
TEST_ROOTS = (ROOT / "tests" / "stdlib" / "compile_time", ROOT / "tests" / "compile_fail")
SCAN_SUFFIXES = {".json", ".cp", ".c", ".h", ".hp", ".err", ".txt", ".md", ".log"}

TAG_RE = re.compile(r"\[CPLUS-SAFE-[A-Z]+\]")
ANY_TAG_RE = re.compile(r"\[CPLUS-[A-Z][A-Z0-9_-]*\]")

# The five diagnostic categories requested by Agent 1.  Each probe is an unsafe
# C snippet whose violating construct is derived from the named contract entry.
# ``source_api`` is used to look up the exact declared diagnostic string in the
# live contract, so the report never invents contract text.
PROBES = (
    {
        "category": "[CPLUS-SAFE-FREE]",
        "source_api": "free",
        "snippet": "free_double_release.c",
        "code": (
            "#include <stdlib.h>\n"
            "int main(void) {\n"
            "    int *p = (int *)malloc(sizeof(int));\n"
            "    if (!p) return 1;\n"
            "    free(p);\n"
            "    free(p);\n"
            "    return 0;\n"
            "}\n"
        ),
    },
    {
        "category": "[CPLUS-SAFE-LIFETIME]",
        "source_api": "free",
        "snippet": "use_after_free.c",
        "code": (
            "#include <stdlib.h>\n"
            "int main(void) {\n"
            "    int *p = (int *)malloc(sizeof(int));\n"
            "    if (!p) return 1;\n"
            "    free(p);\n"
            "    *p = 7;\n"
            "    return 0;\n"
            "}\n"
        ),
    },
    {
        "category": "[CPLUS-SAFE-BOUNDS]",
        "source_api": "memcpy",
        "snippet": "constant_out_of_bounds.c",
        "code": (
            "int main(void) {\n"
            "    int a[3] = {1, 2, 3};\n"
            "    a[5] = 99;\n"
            "    return a[5];\n"
            "}\n"
        ),
    },
    {
        "category": "[CPLUS-SAFE-CRT]",
        "source_api": "strcpy",
        "snippet": "string_copy_overflow.c",
        "code": (
            "#include <string.h>\n"
            "int main(void) {\n"
            "    char dst[4];\n"
            "    strcpy(dst, \"overflow-long\");\n"
            "    return 0;\n"
            "}\n"
        ),
    },
    {
        "category": "[CPLUS-SAFE-REALLOC]",
        "source_api": "realloc",
        "snippet": "realloc_stale_alias.c",
        "code": (
            "#include <stdlib.h>\n"
            "int main(void) {\n"
            "    int *p = (int *)malloc(sizeof(int) * 4);\n"
            "    if (!p) return 1;\n"
            "    int *q = (int *)realloc(p, sizeof(int) * 8);\n"
            "    if (!q) return 1;\n"
            "    p[0] = 9;\n"
            "    free(q);\n"
            "    return 0;\n"
            "}\n"
        ),
    },
)

# Last verified against the real ./build/c+ on branch agent/modern-stdlib.
# Changing this requires re-running the probes and updating the documentation.
FROZEN_BASELINE = {
    "compiler_contract_file_present": False,
    "compiler_available": True,
    "category_verdicts": {
        "[CPLUS-SAFE-BOUNDS]": "compiler_consumption",
        "[CPLUS-SAFE-LIFETIME]": "compiler_consumption",
        "[CPLUS-SAFE-FREE]": "schema_alignment_only",
        "[CPLUS-SAFE-CRT]": "schema_alignment_only",
        "[CPLUS-SAFE-REALLOC]": "schema_alignment_only",
    },
}


def rel(path):
    return path.relative_to(ROOT).as_posix()


def load_contract():
    if not CONTRACT.is_file():
        raise SystemExit("missing contract file: " + str(CONTRACT))
    with CONTRACT.open(encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict) or not isinstance(data.get("apis"), list):
        raise SystemExit("unexpected contract shape in " + str(CONTRACT))
    return data


def family_of(name):
    """Derived, best-effort functional family (not a field in the contract)."""
    base = name[:-2] if name.endswith("_s") else name
    if "alloc" in base:
        return "allocation"
    if base in {"memcpy", "memmove", "memset", "memcmp", "memccpy"}:
        return "memory"
    if base.startswith("str") or "strcpy" in base or "strcat" in base:
        return "string"
    if "printf" in base or "scanf" in base:
        return "format"
    if base in {"fopen", "fclose", "fread", "fwrite", "fflush", "fseek"}:
        return "stream"
    if base.endswith("_s"):
        return "checked_variant"
    return "other"


def analyze_contract(data):
    apis = data["apis"]
    by_category = collections.defaultdict(lambda: {"apis": set(), "diagnostic_count": 0})
    untagged = []
    with_diagnostics = []
    for api in apis:
        diags = api.get("diagnostics") or []
        if diags:
            with_diagnostics.append(api["name"])
        for diag in diags:
            match = TAG_RE.search(diag)
            if not match:
                untagged.append({"api": api["name"], "diagnostic": diag})
                continue
            tag = match.group()
            by_category[tag]["apis"].add(api["name"])
            by_category[tag]["diagnostic_count"] += 1

    categories = {}
    for tag in sorted(by_category):
        categories[tag] = {
            "api_count": len(by_category[tag]["apis"]),
            "diagnostic_count": by_category[tag]["diagnostic_count"],
            "api_names": sorted(by_category[tag]["apis"]),
        }

    families = collections.Counter(family_of(api["name"]) for api in apis)
    return {
        "contract_apis": len(apis),
        "apis_with_diagnostics": len(with_diagnostics),
        "apis_without_diagnostics": len(apis) - len(with_diagnostics),
        "apis_with_diagnostics_names": sorted(with_diagnostics),
        "diagnostic_strings_total": sum(len(api.get("diagnostics") or []) for api in apis),
        "diagnostic_categories": len(categories),
        "untagged_diagnostic_strings": untagged,
        "by_category": categories,
        "by_family_derived": {key: families[key] for key in sorted(families)},
    }


def declared_diagnostic(apis, api_name, tag):
    for api in apis:
        if api.get("name") != api_name:
            continue
        for diag in api.get("diagnostics") or []:
            if tag in diag:
                return diag
    return None


def scan_test_references():
    """Map each exact CPLUS-SAFE tag to test files that literally contain it."""
    texts = []
    for root in TEST_ROOTS:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix.lower() in SCAN_SUFFIXES:
                try:
                    texts.append((path, path.read_text(encoding="utf-8", errors="replace")))
                except OSError:
                    continue
    return texts


def compiler_state():
    analysis_entries = sorted(p.name for p in COMPILER_ANALYSIS_DIR.iterdir()) if COMPILER_ANALYSIS_DIR.is_dir() else []
    return {
        "expected_path": rel(EXPECTED_COMPILER_CONTRACT),
        "present": EXPECTED_COMPILER_CONTRACT.is_file(),
        "analysis_dir_entries": analysis_entries,
        "compiler_binary": rel(COMPILER),
        "compiler_available": COMPILER.is_file(),
    }


def run_probe(probe, apis, tmp_dir):
    tag = probe["category"]
    declared = declared_diagnostic(apis, probe["source_api"], tag)
    result = {
        "category": tag,
        "source_api": probe["source_api"],
        "declared_diagnostic": declared,
        "declared_diagnostic_found": declared is not None,
        "snippet_file": probe["snippet"],
        "probe_kind": probe["snippet"].replace(".c", ""),
        "returncode": None,
        "tagged_diagnostics": [],
        "tagged_lines": [],
        "expected_tag_emitted": False,
        "status": "not_run",
    }
    if not COMPILER.is_file():
        result["status"] = "compiler_unavailable"
        return result

    snippet = tmp_dir / probe["snippet"]
    snippet.write_text(probe["code"], encoding="utf-8")
    try:
        completed = subprocess.run(
            [str(COMPILER), "check", "--strict", str(snippet)],
            cwd=str(ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            timeout=90,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        result["status"] = "compiler_error"
        result["error"] = str(exc)
        return result

    output = completed.stdout or ""
    tags = []
    for line in output.splitlines():
        for found in ANY_TAG_RE.findall(line):
            if found not in tags:
                tags.append(found)
    tagged_lines = [line.strip() for line in output.splitlines() if ANY_TAG_RE.search(line)]
    result.update({
        "returncode": completed.returncode,
        "tagged_diagnostics": tags,
        "tagged_lines": tagged_lines[:8],
        "expected_tag_emitted": tag in tags,
        "rejected": completed.returncode != 0,
        "status": "run",
    })
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-only", action="store_true",
                        help="print the report and exit 0 without checking FROZEN_BASELINE")
    args = parser.parse_args()

    data = load_contract()
    apis = data["apis"]
    contract_report = analyze_contract(data)
    test_texts = scan_test_references()
    compiler = compiler_state()

    refs_by_tag = {}
    for tag in contract_report["by_category"]:
        refs_by_tag[tag] = sorted(rel(path) for path, text in test_texts if tag in text)

    with tempfile.TemporaryDirectory(prefix="cplus-contract-consumption-") as tmp_name:
        tmp_dir = pathlib.Path(tmp_name)
        probes = [run_probe(probe, apis, tmp_dir) for probe in PROBES]

        probe_by_tag = {}
        for probe in probes:
            tag = probe["category"]
            previous = probe_by_tag.get(tag)
            if previous is None or (probe["expected_tag_emitted"] and not previous["expected_tag_emitted"]):
                probe_by_tag[tag] = probe

        categories = {}
        for tag, info in contract_report["by_category"].items():
            probe = probe_by_tag.get(tag)
            emitted = bool(probe and probe["expected_tag_emitted"])
            referenced = bool(refs_by_tag.get(tag))
            if emitted:
                verdict = "compiler_consumption"
            elif probe is not None or referenced:
                verdict = "schema_alignment_only"
            else:
                verdict = "no_probe"
            categories[tag] = {
                **info,
                "test_files_referencing_tag": refs_by_tag.get(tag, []),
                "test_reference_exists": referenced,
                "compiler_probe": probe,
                "compiler_tagged_output": emitted,
                "verdict": verdict,
            }

        per_api = []
        for api in apis:
            api_categories = []
            for diag in api.get("diagnostics") or []:
                match = TAG_RE.search(diag)
                if match and match.group() not in api_categories:
                    api_categories.append(match.group())
            if not api_categories:
                continue
            emitted = [tag for tag in api_categories if categories[tag]["compiler_tagged_output"]]
            missing = [tag for tag in api_categories if not categories[tag]["compiler_tagged_output"]]
            rollup = "mixed" if emitted and missing else ("compiler_consumption" if emitted else "schema_alignment_only")
            per_api.append({
                "api": api["name"],
                "categories": sorted(api_categories),
                "categories_with_tagged_output": sorted(emitted),
                "categories_without_tagged_output": sorted(missing),
                "rollup": rollup,
            })

        verdict_counter = collections.Counter(entry["verdict"] for entry in categories.values())
        rollup_counter = collections.Counter(entry["rollup"] for entry in per_api)
        total_categories = len(categories)
        probes_run = sum(1 for p in probes if p["status"] == "run")
        report = {
            "schema_version": 1,
            "purpose": "evidence of compiler consumption of libc diagnostic contracts",
            "branch_context": {
                "branch": "agent/modern-stdlib",
                "compiler_contract_file": compiler,
                "interpretation": (
                    "compiler/Analysis/libc_contracts.json is absent on this branch, so the only "
                    "direct evidence of compiler consumption is tagged output from ./build/c+ "
                    "check --strict. Strings inside the compiler binary are not evidence."
                ),
            },
            "contract_counts": contract_report,
            "categories": categories,
            "per_api_diagnostics_note": (
                "Per-API rollups project category-level probe evidence onto APIs that declare "
                "the same tag. They do NOT prove that the compiler loaded a per-API contract "
                "entry; compiler/Analysis/libc_contracts.json is absent on this branch."
            ),
            "per_api_diagnostics": per_api,
            "probes": probes,
            "headline": {
                "contract_apis": contract_report["contract_apis"],
                "apis_with_diagnostics": f"{contract_report['apis_with_diagnostics']}/{contract_report['contract_apis']}",
                "apis_without_diagnostics": f"{contract_report['apis_without_diagnostics']}/{contract_report['contract_apis']}",
                "diagnostic_categories": total_categories,
                "categories_with_compiler_consumption_evidence": f"{verdict_counter['compiler_consumption']}/{total_categories}",
                "categories_schema_alignment_only": f"{verdict_counter['schema_alignment_only']}/{total_categories}",
                "categories_without_probe": f"{verdict_counter['no_probe']}/{total_categories}",
                "apis_with_any_tagged_output": f"{sum(1 for e in per_api if e['rollup'] in ('compiler_consumption', 'mixed'))}/{len(per_api)}",
                "apis_schema_alignment_only": f"{rollup_counter['schema_alignment_only']}/{len(per_api)}",
                "apis_mixed_evidence": f"{rollup_counter['mixed']}/{len(per_api)}",
                "probes_run": f"{probes_run}/{len(PROBES)}",
                "probes_with_expected_tag": f"{sum(1 for p in probes if p['expected_tag_emitted'])}/{len(PROBES)}",
                "probes_rejected_under_any_tag": f"{sum(1 for p in probes if p.get('rejected'))}/{len(PROBES)}",
            },
        }

    json.dump(report, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")

    if args.report_only:
        return 0

    mismatches = []
    if compiler["present"] != FROZEN_BASELINE["compiler_contract_file_present"]:
        mismatches.append("compiler_contract_file_present")
    if compiler["compiler_available"] != FROZEN_BASELINE["compiler_available"]:
        mismatches.append("compiler_available")
    for tag, expected in FROZEN_BASELINE["category_verdicts"].items():
        current = categories.get(tag, {}).get("verdict")
        if current != expected:
            mismatches.append(f"{tag}: expected {expected}, observed {current}")
    if mismatches:
        sys.stderr.write("contract consumption baseline drift: " + "; ".join(mismatches) + "\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
