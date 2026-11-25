#!/bin/sh
# verify.sh -- INDEPENDENT verifier for PR #31/#32/#33 sibling artifacts.
#
# POSIX sh only. No Python, no `|| true`, no network, no source edits. All
# scratch files live in a TMPDIR directory removed by the trap. Re-running a
# sibling runner necessarily refreshes that sibling's own out/ directory;
# this script never edits sibling source, docs or workflow files itself.
#
# Prints exactly one summary line:
#   PR33_VERIFY <checks> <pass> <fail> <findings>

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
cd "$root" || exit 2

td=$(mktemp -d "${TMPDIR:-/tmp}/pr33-verify.XXXXXX") || exit 2
cleanup() { [ -d "$td" ] && rm -rf "$td"; return 0; }
trap cleanup 0 1 2 3 15

checks=0
pass=0
fail=0
findings=0

ok() { checks=$((checks + 1)); pass=$((pass + 1)); printf 'VERIFY PASS %s\n' "$1"; }
no() { checks=$((checks + 1)); fail=$((fail + 1)); printf 'VERIFY FAIL %s\n' "$1" >&2; }
finding() { findings=$((findings + 1)); printf 'VERIFY FINDING %s\n' "$1" >&2; }

run_bounded() {
    secs=$1
    shift
    if command -v timeout >/dev/null 2>&1; then
        timeout "$secs" "$@"
    elif command -v gtimeout >/dev/null 2>&1; then
        gtimeout "$secs" "$@"
    else
        "$@" &
        child=$!
        ( sleep "$secs"; kill "$child" 2>/dev/null ) >/dev/null 2>&1 &
        watcher=$!
        wait "$child"
        rc=$?
        kill "$watcher" 2>/dev/null
        wait "$watcher" 2>/dev/null
        return "$rc"
    fi
}

art() { printf '%-70s ' "$1"; if [ -f "$1" ]; then echo PRESENT; else echo MISSING; fi; }

# ---------------------------------------------------------------- 1. artifacts
artifacts="tests/pr31_abi/run.sh
docs/abi/PR31_LIBC_CROSS_MODULE.md
tests/pr32_portability/run.sh
docs/abi/PR32_LIBC_PORTABILITY.md
.github/workflows/libc-portability.yml
tests/pr33_integration/run.sh
tests/pr33_integration/check_report.sh
tests/pr33_integration/check_overlap.sh
tests/pr33_integration/run_combined_gate.sh
docs/abi/PR33_INTEGRATION_GATES.md"
missing=0
for f in $artifacts; do
    art "$f"
    [ -f "$f" ] || missing=$((missing + 1))
done
if [ "$missing" -eq 0 ]; then ok artifact_presence; else no artifact_presence; fi

# ------------------------------------------------- 2. independent re-runs (bounded)
# A bounded re-run is reported PASS when the runner emits its documented final
# line and exits with a recognised status (0 green, 1 fail-reported, 124 bound).
rerun() {
    label=$1
    regex=$2
    runner=$3
    out="$td/$label.out"
    s=$(date +%s)
    # Must exceed each runner's own global bound (pr32 uses 260s).
    run_bounded 300 sh "$runner" > "$out" 2>&1
    rc=$?
    e=$(date +%s)
    final=$(grep -E "$regex" "$out" | tail -1)
    printf 'VERIFY RUN %s exit=%s wall=%ss final=%s\n' "$label" "$rc" "$((e - s))" "$final"
    printf '%s' "$final" > "$td/$label.final"
    printf '%s' "$rc" > "$td/$label.rc"
    if printf '%s' "$final" | grep -Eq "$regex" && case "$rc" in 0|1|124) true ;; *) false ;; esac; then
        ok "rerun_$label"
    else
        no "rerun_$label"
    fi
}
rerun pr31 '^PR31_LIBC [0-9]+ [0-9]+ [0-9]+ [0-9]+$' tests/pr31_abi/run.sh
rerun pr32 '^PR32_LIBC [0-9]+ [0-9]+ [0-9]+ [0-9]+$' tests/pr32_portability/run.sh
rerun pr33 '^PR33_GATE [0-9]+ [0-9]+ [0-9]+ [0-9]+$' tests/pr33_integration/run.sh

# ------------------------------------------- 3. documented-vs-observed final lines
for p in pr31 pr32; do
    case "$p" in
        pr31) doc=docs/abi/PR31_LIBC_CROSS_MODULE.md; re='PR31_LIBC [0-9]+ [0-9]+ [0-9]+ [0-9]+' ;;
        pr32) doc=docs/abi/PR32_LIBC_PORTABILITY.md; re='PR32_LIBC [0-9]+ [0-9]+ [0-9]+ [0-9]+' ;;
    esac
    claim=$(grep -Eo "$re" "$doc" | tail -1)
    obs=$(cat "$td/$p.final")
    printf 'VERIFY CLAIM %s doc="%s" observed="%s"\n' "$p" "$claim" "$obs"
    if [ -n "$claim" ] && [ "$claim" = "$obs" ]; then ok "claim_match_$p"; else no "claim_match_$p"; fi
done
# PR33 doc must NOT assert a numeric final line (it only documents the format).
if grep -Eq 'PR33_GATE [0-9]+ [0-9]+ [0-9]+ [0-9]+' docs/abi/PR33_INTEGRATION_GATES.md; then
    no pr33_no_numeric_claim
else
    ok pr33_no_numeric_claim
fi

# ------------------------------------------------ 4. claim / honesty audit
scripts="tests/pr31_abi/run.sh tests/pr32_portability/probe_sanitizers.sh \
tests/pr32_portability/probe_toolchain.sh tests/pr32_portability/probe_smoke.sh \
tests/pr32_portability/probe_link.sh tests/pr32_portability/run.sh \
tests/pr33_integration/check_report.sh tests/pr33_integration/check_overlap.sh \
tests/pr33_integration/run_combined_gate.sh tests/pr33_integration/run.sh"
bad_pattern=0
for f in $scripts; do
    if grep -nE '^[[:space:]]*[^#].*(\|\|[[:space:]]*true|(^|[^A-Za-z])python[0-9]?([[:space:]]|$))' "$f" > "$td/pat.out" 2>&1; then
        printf 'VERIFY PATTERN %s\n' "$(cat "$td/pat.out")"
        bad_pattern=$((bad_pattern + 1))
    fi
done
if [ "$bad_pattern" -eq 0 ]; then ok no_python_no_or_true; else no no_python_no_or_true; fi

# Machine-specific absolute paths in the claim-bearing text and runner scripts.
claim_files="docs/abi/PR31_LIBC_CROSS_MODULE.md docs/abi/PR32_LIBC_PORTABILITY.md \
docs/abi/PR33_INTEGRATION_GATES.md $scripts"
path_hits=0
for f in $claim_files; do
    if grep -nE '/Users/|/home/[A-Za-z0-9_.-]+/' "$f" > "$td/path.out" 2>&1; then
        printf 'VERIFY ABSPATH %s\n' "$(cat "$td/path.out")"
        path_hits=$((path_hits + 1))
        finding "hardcoded machine-specific absolute path in $f"
    fi
done
if [ "$path_hits" -eq 0 ]; then ok no_machine_paths; else no no_machine_paths; fi

# "committed under out/" vs. .gitignore reality.
if grep -q 'committed under' docs/abi/PR32_LIBC_PORTABILITY.md &&
   git check-ignore -q tests/pr32_portability/out/toolchain.txt 2>/dev/null; then
    finding "PR32 doc claims evidence is committed, but tests/pr32_portability/out/ is gitignored"
    no pr32_evidence_committed_claim
else
    ok pr32_evidence_committed_claim
fi

# A merge gate that exits 0 while reporting failed checks is not a gate.
lastline=$(grep -vE '^[[:space:]]*(#|$)' tests/pr32_portability/run.sh | tail -1)
if [ "$lastline" = 'exit 0' ]; then
    finding "PR32 runner ends with unconditional 'exit 0' despite FAIL statuses"
    no pr32_exit_on_fail
else
    ok pr32_exit_on_fail
fi

# The PR32 doc must not contradict its own tally (e.g. claim 0 FAIL in the
# final line while its results table still lists FAIL rows).
claim32=$(grep -Eo 'PR32_LIBC [0-9]+ [0-9]+ [0-9]+ [0-9]+' \
    docs/abi/PR32_LIBC_PORTABILITY.md | tail -1)
claim32_fail=$(printf '%s\n' "$claim32" | awk '{ print $4 }')
table_fails=$(grep -c '| FAIL |' docs/abi/PR32_LIBC_PORTABILITY.md 2>/dev/null || echo 0)
if [ -n "$claim32_fail" ] && [ "$claim32_fail" -eq 0 ] && [ "$table_fails" -gt 0 ]; then
    printf 'VERIFY CONTRADICTION pr32 doc final line fail=%s but table has %s FAIL rows\n' \
        "$claim32_fail" "$table_fails"
    finding "PR32 doc final line contradicts its own results table (0 FAIL vs $table_fails FAIL rows)"
    no pr32_doc_internal_consistency
else
    ok pr32_doc_internal_consistency
fi

# "Sanitizer passed" claims must be backed by on-disk status artifacts.
san_claim_bad=0
for s in asan tsan; do
    if grep -q "\`$s\` | PASS" docs/abi/PR32_LIBC_PORTABILITY.md 2>/dev/null &&
       ! grep -q '^PASS' "tests/pr32_portability/out/$s.status" 2>/dev/null; then
        printf 'VERIFY SANCLAIM %s status="%s"\n' "$s" \
            "$(cat "tests/pr32_portability/out/$s.status" 2>/dev/null)"
        san_claim_bad=$((san_claim_bad + 1))
    fi
done
if [ "$san_claim_bad" -eq 0 ]; then
    ok pr32_sanitizer_claims_backed
else
    finding "PR32 doc claims ASan/TSan PASS but current status artifacts do not say PASS"
    no pr32_sanitizer_claims_backed
fi

# Windows runtime must never be claimed as passing; unsupported targets labelled.
if grep -E 'Windows.{0,60}RUNTIME (PASS|SUCCESS|VERIFIED|SUPPORTED)|RUNTIME (PASS|SUCCESS|VERIFIED|SUPPORTED).{0,60}Windows' \
        .github/workflows/libc-portability.yml docs/abi/PR32_LIBC_PORTABILITY.md 2>/dev/null; then
    no windows_no_runtime_claim
else
    ok windows_no_runtime_claim
fi
if grep -q 'RUNTIME NOT TESTED' .github/workflows/libc-portability.yml &&
   grep -q 'EXPERIMENTAL' docs/abi/PR32_LIBC_PORTABILITY.md &&
   grep -q 'UNSUPPORTED' docs/abi/PR32_LIBC_PORTABILITY.md; then
    ok unsupported_labelled
else
    no unsupported_labelled
fi

# Sanitizer evidence artifacts exist (a "sanitizer passed" claim needs output).
san_missing=0
for f in toolchain.txt sanitizers.txt san_asan.run.log san_ubsan.run.log san_tsan.run.log; do
    [ -f "tests/pr32_portability/out/$f" ] || san_missing=$((san_missing + 1))
done
if [ "$san_missing" -eq 0 ]; then ok sanitizer_artifacts; else no sanitizer_artifacts; fi

# ------------------------------------------------------- 5. negative controls
cat > "$td/base.txt" <<EOF
SCOPE: negctl
DEPENDENCIES: none
COMMIT_SHA: 0123456789abcdef0123456789abcdef01234567
CHANGED_PATHS_BEGIN
libc/src/foo.c
CHANGED_PATHS_END
EXACT_COMMANDS_BEGIN
sh tests/run_tests.sh
EXACT_COMMANDS_END
TEST_OUTPUT_BEGIN
some output
TEST_OUTPUT_END
KNOWN_FAILURES: none
INDEPENDENT_REVIEW: PENDING
EOF
grep -v '^KNOWN_FAILURES' "$td/base.txt" > "$td/missing_field.txt"
sed '/TEST_OUTPUT_BEGIN/,/TEST_OUTPUT_END/d' "$td/base.txt" > "$td/missing_section.txt"
sed 's|libc/src/foo.c|/opt/absolute/foo.c|' "$td/base.txt" > "$td/abs_path.txt"

neg_expect() {
    label=$1
    needle=$2
    shift 2
    "$@" > "$td/$label.log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ] && { [ -z "$needle" ] || grep -q "$needle" "$td/$label.log"; }; then
        ok "negctl_$label"
    else
        no "negctl_$label"
    fi
}
neg_expect missing_field 'missing required field' \
    sh tests/pr33_integration/check_report.sh "$td/missing_field.txt"
neg_expect missing_section 'TEST_OUTPUT' \
    sh tests/pr33_integration/check_report.sh "$td/missing_section.txt"
neg_expect abs_path 'repo-relative' \
    sh tests/pr33_integration/check_report.sh "$td/abs_path.txt"

for base in base overlap; do
    [ "$base" = overlap ] && sed 's|libc/src/foo.c|libc/src/foo.c|' "$td/base.txt" > "$td/overlap.txt"
    awk '{ if ($0 ~ /^INDEPENDENT_REVIEW:/) { print "CLAIMS_BEGIN"; print "api:cplus_shared_thing"; print "CLAIMS_END" } print }' \
        "$td/$base.txt" > "$td/$base.claims.txt"
done
neg_expect overlap_and_claim 'WARNING DUPLICATE CLAIM' \
    sh tests/pr33_integration/check_overlap.sh "$td/base.claims.txt" "$td/overlap.claims.txt"
if grep -q 'WARNING OVERLAP path' "$td/overlap_and_claim.log"; then
    ok negctl_overlap_path_warning
else
    no negctl_overlap_path_warning
fi

printf 'PR33_VERIFY %d %d %d %d\n' "$checks" "$pass" "$fail" "$findings"
[ "$fail" -eq 0 ]
exit $?
