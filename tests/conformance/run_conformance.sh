#!/usr/bin/env bash
# C+ vs Rust conformance runner.
#
# Usage: tests/conformance/run_conformance.sh
#   (set CONFORMANCE_MANIFEST to use an alternate manifest path)
#
# Reads tests/conformance/manifest.json (a JSON list, or an object with a
# "cases" list). Every entry must provide:
#   id, c_file, rust_file, expected_c, expected_rust
# and may optionally provide mechanism_c / mechanism_rust (default "none").
#
# For each case the C source is compiled with the system clang against the
# checked-in safe/hardened static libraries, and the Rust source with rustc.
# The observed outcome (accepted / runtime_reject / compile_reject) is compared
# with the manifest expectation. This runner reports what was *detected*; it
# does not prove Rust-equivalent safety.
#
# Exit status is 0 unless the manifest is invalid or a case fails to compile
# in either language when compilation was not the recorded expectation.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 1

OUT="$ROOT/build/conformance"
# Default manifest path; CONFORMANCE_MANIFEST is an optional override for CI.
MANIFEST=${CONFORMANCE_MANIFEST:-"tests/conformance/manifest.json"}

mkdir -p "$OUT" || exit 1

if [ ! -f "$MANIFEST" ]; then
  echo "error: conformance manifest not found at $ROOT/$MANIFEST" >&2
  exit 1
fi

# --- 1. Parse the manifest into a tab-separated plan (no hardcoded cases) --
if ! python3 - "$MANIFEST" >"$OUT/plan.tsv" <<'PY'
import json
import sys

path = sys.argv[1]
try:
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
except Exception as exc:
    sys.stderr.write("error: cannot parse manifest: %s\n" % exc)
    sys.exit(2)

if isinstance(doc, dict):
    cases = doc.get("cases")
elif isinstance(doc, list):
    cases = doc
else:
    cases = None

if not isinstance(cases, list):
    sys.stderr.write("error: manifest must be a JSON list or an object with a 'cases' list\n")
    sys.exit(2)


def clean(value):
    if value is None:
        return ""
    return str(value).replace("\t", " ").replace("\n", " ").replace("\r", " ").strip()


required = ("id", "c_file", "rust_file", "expected_c", "expected_rust")
rows = []
for index, case in enumerate(cases):
    if not isinstance(case, dict):
        sys.stderr.write("error: manifest case #%d is not an object\n" % index)
        sys.exit(2)
    values = {}
    for key in required:
        value = clean(case.get(key))
        if not value:
            sys.stderr.write("error: manifest case #%d is missing required field '%s'\n" % (index, key))
            sys.exit(2)
        values[key] = value
    values["mechanism_c"] = clean(case.get("mechanism_c")) or "none"
    values["mechanism_rust"] = clean(case.get("mechanism_rust")) or "none"
    rows.append(values)

if not rows:
    sys.stderr.write("warning: manifest contains no cases\n")

for row in rows:
    print("\t".join(
        row[key]
        for key in (
            "id", "c_file", "rust_file", "expected_c", "expected_rust",
            "mechanism_c", "mechanism_rust",
        )
    ))
PY
then
  echo "error: invalid conformance manifest ($MANIFEST); see messages above" >&2
  exit 1
fi

# Manifest fields may be relative to the repo root or to tests/conformance.
resolve_source() {
  candidate=$1
  if [ -f "$candidate" ]; then
    printf '%s' "$candidate"
  elif [ -f "tests/conformance/$candidate" ]; then
    printf '%s' "tests/conformance/$candidate"
  else
    printf '%s' "$candidate"
  fi
}

CFLAGS="-std=c99 -Wall -Wextra -I. -Istdlib/safe -Ilibc -Ilibc/install-include"
CFLAGS="$CFLAGS -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
CLIBS="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread"

: >"$OUT/results.tsv"
MATCHED=0
MISMATCHED=0
HARD_FAIL=0
GATES=0

while IFS=$'\t' read -r id c_file rust_file expected_c expected_rust mechanism_c mechanism_rust; do
  [ -n "$id" ] || continue
  GATES=$((GATES + 1))

  num=$(printf '%s' "$id" | tr -cd '0-9')
  if [ -z "$num" ]; then
    num=$(printf '%s' "$id" | tr -c 'A-Za-z0-9' '_')
  fi

  c_file=$(resolve_source "$c_file")
  rust_file=$(resolve_source "$rust_file")

  if [ ! -f "$c_file" ]; then
    echo "warning: case $num C source missing: $c_file" >&2
  fi
  if [ ! -f "$rust_file" ]; then
    echo "warning: case $num Rust source missing: $rust_file" >&2
  fi

  c_exe="$OUT/case_${num}"
  rs_exe="$OUT/case_${num}_rs"
  c_log="$OUT/case_${num}.build.log"
  rs_log="$OUT/case_${num}_rs.build.log"
  c_stdout="$OUT/case_${num}.stdout"
  c_stderr="$OUT/case_${num}.stderr"
  rs_stdout="$OUT/case_${num}_rs.stdout"
  rs_stderr="$OUT/case_${num}_rs.stderr"

  # --- 2a. C case ---------------------------------------------------------
  # shellcheck disable=SC2086
  /usr/bin/clang $CFLAGS "$c_file" $CLIBS -o "$c_exe" >"$c_log" 2>&1
  c_compile_rc=$?
  c_run_rc="-"
  detected_c=""
  if [ "$c_compile_rc" -eq 0 ]; then
    timeout 10 "$c_exe" >"$c_stdout" 2>"$c_stderr"
    c_run_rc=$?
    if [ "$c_run_rc" -eq 0 ]; then
      actual_c="accepted"
    else
      actual_c="runtime_reject"
    fi
    if [ "$c_run_rc" -eq 124 ]; then
      echo "warning: case $num C run timed out after 10s" >&2
    fi
    if grep -qE 'runtime error:|AddressSanitizer' "$c_stderr"; then
      detected_c="UBSan/ASan"
    fi
  else
    actual_c="compile_reject"
    echo "warning: case $num C compile failed; see build/conformance/case_${num}.build.log" >&2
  fi

  # --- 2b. Rust case ------------------------------------------------------
  rustc --edition 2021 "$rust_file" -o "$rs_exe" >"$rs_log" 2>&1
  rs_compile_rc=$?
  rs_run_rc="-"
  detected_rust=""
  if [ "$rs_compile_rc" -eq 0 ]; then
    timeout 10 "$rs_exe" >"$rs_stdout" 2>"$rs_stderr"
    rs_run_rc=$?
    if [ "$rs_run_rc" -eq 0 ]; then
      actual_rust="accepted"
    else
      actual_rust="runtime_reject"
    fi
    if [ "$rs_run_rc" -eq 124 ]; then
      echo "warning: case $num Rust run timed out after 10s" >&2
    fi
    if grep -q 'panicked at' "$rs_stderr"; then
      detected_rust="panic"
    fi
  else
    actual_rust="compile_reject"
    echo "warning: case $num Rust compile failed; see build/conformance/case_${num}_rs.build.log" >&2
  fi

  # --- 3. Compare with the manifest and record ----------------------------
  if [ "$actual_c" = "$expected_c" ] && [ "$actual_rust" = "$expected_rust" ]; then
    verdict="MATCH"
    MATCHED=$((MATCHED + 1))
  else
    verdict="NOMATCH"
    MISMATCHED=$((MISMATCHED + 1))
  fi

  # An unexpected compile rejection is the only per-case hard failure.
  if [ "$actual_c" = "compile_reject" ] && [ "$expected_c" != "compile_reject" ]; then
    HARD_FAIL=1
    echo "error: case $num C failed to compile (expected '$expected_c')" >&2
  fi
  if [ "$actual_rust" = "compile_reject" ] && [ "$expected_rust" != "compile_reject" ]; then
    HARD_FAIL=1
    echo "error: case $num Rust failed to compile (expected '$expected_rust')" >&2
  fi

  c_display="$actual_c"
  [ -z "$detected_c" ] || c_display="${actual_c}[$detected_c]"
  rs_display="$actual_rust"
  [ -z "$detected_rust" ] || rs_display="${actual_rust}[$detected_rust]"

  printf 'case %s  C=%s (expected %s, %s)  Rust=%s (expected %s, %s)  %s\n' \
    "$num" "$c_display" "$expected_c" "$mechanism_c" \
    "$rs_display" "$expected_rust" "$mechanism_rust" "$verdict"

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$id" "$num" "$c_file" "$rust_file" \
    "$actual_c" "$expected_c" "$mechanism_c" "$detected_c" \
    "$actual_rust" "$expected_rust" "$mechanism_rust" "$detected_rust" \
    "$verdict" "$c_compile_rc" "$c_run_rc" "$rs_compile_rc" "$rs_run_rc" \
    >>"$OUT/results.tsv"
done <"$OUT/plan.tsv"

# --- 4. Machine-readable results from python3 -----------------------------
if ! python3 - "$OUT/results.tsv" "$OUT/results.json" <<'PY'
import json
import sys

source, destination = sys.argv[1], sys.argv[2]


def exit_code(value):
    return int(value) if value not in ("", "-") else None


cases = []
with open(source, "r", encoding="utf-8") as fh:
    for line in fh:
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split("\t")
        while len(parts) < 17:
            parts.append("")
        (cid, num, cfile, rfile, ac, ec, mc, dc,
         ar, er, mr, dr, verdict,
         c_compile_rc, c_run_rc, rs_compile_rc, rs_run_rc) = parts[:17]
        cases.append({
            "id": cid,
            "num": num,
            "c": {
                "file": cfile,
                "actual": ac,
                "expected": ec,
                "mechanism": mc,
                "detected_mechanism": dc,
                "compile_exit_code": exit_code(c_compile_rc),
                "run_exit_code": exit_code(c_run_rc),
            },
            "rust": {
                "file": rfile,
                "actual": ar,
                "expected": er,
                "mechanism": mr,
                "detected_mechanism": dr,
                "compile_exit_code": exit_code(rs_compile_rc),
                "run_exit_code": exit_code(rs_run_rc),
            },
            "status": verdict,
            "match": verdict == "MATCH",
        })

matched = sum(1 for case in cases if case["match"])
document = {
    "summary": {
        "gates": len(cases),
        "matched": matched,
        "mismatched": len(cases) - matched,
    },
    "notice": "conformance measures detected outcomes only; it does not prove Rust-equivalent safety.",
    "cases": cases,
}
with open(destination, "w", encoding="utf-8") as fh:
    json.dump(document, fh, indent=2)
    fh.write("\n")
PY
then
  echo "error: failed to write $OUT/results.json" >&2
  exit 1
fi

printf 'conformance: %d matched, %d mismatched (%d gates)\n' \
  "$MATCHED" "$MISMATCHED" "$GATES"
echo "results: $OUT/results.json"
echo "conformance measures detected outcomes only; it does not prove Rust-equivalent safety."

if [ "$HARD_FAIL" -ne 0 ]; then
  echo "error: one or more cases failed to compile without a matching expectation" >&2
  exit 1
fi
exit 0
