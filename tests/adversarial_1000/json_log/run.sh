#!/usr/bin/env bash
# ADV1000 json_log: build, run twice, verify determinism, write evidence.
# All generated evidence stays in ./results (gitignored by the local .gitignore).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BIN="$HERE/results/adv1000_json_log"
BUILD_LOG="$HERE/results/build.log"

cd "$ROOT" || exit 2
mkdir -p "$HERE/results"
: > "$BUILD_LOG"

echo "== build =="
echo "./build/c+ build $HERE/adv1000_json_log.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o $BIN"
./build/c+ build "$HERE/adv1000_json_log.cp" --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$BIN" >>"$BUILD_LOG" 2>&1
build_rc=$?
echo "build_exit=$build_rc (compiler diagnostics in results/build.log)"
if [ "$build_rc" -ne 0 ]; then
  cat "$BUILD_LOG"
  exit "$build_rc"
fi

cd "$HERE" || exit 2
rm -f results/scenarios.log results/scenarios.out results/scenarios2.log \
      results/scenarios2.out results/probe.log results/probe2.log \
      results/run.stderr results/run2.stderr

echo "== run 1 =="
echo "timeout 120 $BIN results/scenarios.log results/probe.log > results/scenarios.out"
timeout 120 "$BIN" results/scenarios.log results/probe.log \
  > results/scenarios.out 2> results/run.stderr
run1_rc=$?
echo "run1_exit=$run1_rc"

echo "== run 2 (determinism) =="
echo "timeout 120 $BIN results/scenarios2.log results/probe2.log > results/scenarios2.out"
timeout 120 "$BIN" results/scenarios2.log results/probe2.log \
  > results/scenarios2.out 2> results/run2.stderr
run2_rc=$?
echo "run2_exit=$run2_rc"

echo "== coverage recomputation =="
echo "python3 coverage.py > results/coverage.txt"
python3 coverage.py > results/coverage.txt
cov_rc=$?
echo "coverage_exit=$cov_rc"

echo "== compiled-artifact nm verification =="
bash "$HERE/verify_nm.sh" "$ROOT" > results/nm_evidence.txt 2>&1
nm_rc=$?
echo "nm_exit=$nm_rc"

echo "== digests =="
shasum -a 256 results/scenarios.out results/scenarios2.out \
  results/scenarios.log results/scenarios2.log > results/digests.txt
cat results/digests.txt

if cmp -s results/scenarios.out results/scenarios2.out; then
  deterministic=1
else
  deterministic=0
fi
s_lines=$(grep -c '^S ' results/scenarios.out)
final_lines=$(grep -c '^ADV1000 json_log 1000 ' results/scenarios.out)
fail_lines=$(grep -c ' FAIL ' results/scenarios.out)
bad_sent=$(awk '$1=="S" && $6!="1" {n++} END{print n+0}' results/scenarios.out)
log_lines=$(wc -l < results/scenarios.log | tr -d ' ')
final_line=$(grep '^ADV1000 json_log 1000 ' results/scenarios.out)

{
  echo "ADV1000 json_log run summary"
  echo "build_exit=$build_rc"
  echo "run1_exit=$run1_rc"
  echo "run2_exit=$run2_rc"
  echo "canonical_identical_across_runs=$deterministic"
  echo "S_lines=$s_lines"
  echo "final_lines=$final_lines"
  echo "FAIL_lines=$fail_lines"
  echo "sentinel_not_ok=$bad_sent"
  echo "scenarios_log_lines=$log_lines"
  echo "final=$final_line"
  echo "coverage_exit=$cov_rc"
  echo "nm_exit=$nm_rc"
} > results/summary.txt

cat results/summary.txt

ok=1
[ "$build_rc" -eq 0 ] || ok=0
[ "$run1_rc" -eq 0 ] || ok=0
[ "$run2_rc" -eq 0 ] || ok=0
[ "$cov_rc" -eq 0 ] || ok=0
[ "$nm_rc" -eq 0 ] || ok=0
[ "$deterministic" -eq 1 ] || ok=0
[ "$s_lines" -eq 1000 ] || ok=0
[ "$final_lines" -eq 1 ] || ok=0
[ "$fail_lines" -eq 0 ] || ok=0
[ "$bad_sent" -eq 0 ] || ok=0
[ "$log_lines" -eq 1000 ] || ok=0

echo "RUN_ALL_OK=$ok"
[ "$ok" -eq 1 ]
