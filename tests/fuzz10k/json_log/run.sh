#!/usr/bin/env bash
# FUZZ10K json_log: build, run twice each, verify determinism, write evidence.
# All generated evidence stays in ./results (gitignored by the local .gitignore).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
MAIN_BIN="$HERE/results/fuzz10k_json_log"
COMP_BIN="$HERE/results/compiled_probe"
MAIN_BUILD_LOG="$HERE/results/build_main.log"
COMP_BUILD_LOG="$HERE/results/build_compiled.log"

OK=1
cd "$ROOT" || exit 2
mkdir -p "$HERE/results"
: > "$MAIN_BUILD_LOG"
: > "$COMP_BUILD_LOG"

echo "== build main (exact prescribed form) =="
echo "./build/c+ build tests/fuzz10k/json_log/fuzz10k_json_log.cp --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o $MAIN_BIN"
./build/c+ build tests/fuzz10k/json_log/fuzz10k_json_log.cp --strict --sanitize=none \
  -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$MAIN_BIN" \
  >>"$MAIN_BUILD_LOG" 2>&1
main_build_rc=$?
echo "main_build_exit=$main_build_rc"
[ "$main_build_rc" -eq 0 ] || OK=0

echo "== build compiled-symbol probe =="
echo "./build/c+ build tests/fuzz10k/json_log/compiled_probe.cp --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o $COMP_BIN -- build/safe/libcplus_safecrt.a"
./build/c+ build tests/fuzz10k/json_log/compiled_probe.cp --strict --sanitize=none \
  -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$COMP_BIN" \
  -- build/safe/libcplus_safecrt.a >>"$COMP_BUILD_LOG" 2>&1
comp_build_rc=$?
echo "compiled_build_exit=$comp_build_rc"
[ "$comp_build_rc" -eq 0 ] || OK=0

cd "$HERE" || exit 2
rm -f results/scenarios.log results/scenarios2.log results/scenarios.out \
      results/scenarios2.out results/compiled.out results/compiled2.out \
      results/run.stderr results/run2.stderr results/compiled.stderr \
      results/compiled2.stderr

echo "== main run 1 =="
echo "timeout 600 $MAIN_BIN results/scenarios.log > results/scenarios.out"
timeout 600 "$MAIN_BIN" results/scenarios.log \
  > results/scenarios.out 2> results/run.stderr
main_run1_rc=$?
echo "main_run1_exit=$main_run1_rc"
[ "$main_run1_rc" -eq 0 ] || OK=0

echo "== main run 2 (determinism) =="
echo "timeout 600 $MAIN_BIN results/scenarios2.log > results/scenarios2.out"
timeout 600 "$MAIN_BIN" results/scenarios2.log \
  > results/scenarios2.out 2> results/run2.stderr
main_run2_rc=$?
echo "main_run2_exit=$main_run2_rc"
[ "$main_run2_rc" -eq 0 ] || OK=0

echo "== compiled probe run 1 =="
echo "timeout 600 $COMP_BIN > results/compiled.out"
timeout 600 "$COMP_BIN" > results/compiled.out 2> results/compiled.stderr
comp_run1_rc=$?
echo "compiled_run1_exit=$comp_run1_rc"
[ "$comp_run1_rc" -eq 0 ] || OK=0

echo "== compiled probe run 2 (determinism) =="
echo "timeout 600 $COMP_BIN > results/compiled2.out"
timeout 600 "$COMP_BIN" > results/compiled2.out 2> results/compiled2.stderr
comp_run2_rc=$?
echo "compiled_run2_exit=$comp_run2_rc"
[ "$comp_run2_rc" -eq 0 ] || OK=0

echo "== coverage recomputation =="
echo "python3 coverage.py > results/coverage.txt"
python3 coverage.py > results/coverage.txt
cov_rc=$?
echo "coverage_exit=$cov_rc"
[ "$cov_rc" -eq 0 ] || OK=0

echo "== compiled-artifact nm verification =="
bash "$HERE/verify_nm.sh" "$ROOT" > results/nm_evidence.txt 2>&1
nm_rc=$?
echo "nm_exit=$nm_rc"
[ "$nm_rc" -eq 0 ] || OK=0

echo "== digests =="
shasum -a 256 results/scenarios.out results/scenarios2.out \
  results/scenarios.log results/scenarios2.log \
  results/compiled.out results/compiled2.out > results/digests.txt
cat results/digests.txt

cmp -s results/scenarios.out results/scenarios2.out && main_det=1 || main_det=0
cmp -s results/compiled.out results/compiled2.out && comp_det=1 || comp_det=0

main_final=$(grep '^FUZZ10K json_log ' results/scenarios.out | head -1)
comp_final=$(grep '^COMPILED json_log ' results/compiled.out | head -1)
main_s=$(awk '$1=="S" {n++} END{print n+0}' results/scenarios.out)
comp_c=$(awk '$1=="C" {n++} END{print n+0}' results/compiled.out)
log_lines=$(wc -l < results/scenarios.log | tr -d ' ')
bad_main=$(grep -cE 'CRASH|HANG|CORRUPT|OOM|FAIL' results/scenarios.out || true)
bad_comp=$(grep -cE 'CRASH|HANG|CORRUPT|OOM|FAIL' results/compiled.out || true)
bad_sent_main=$(awk '$1=="S" && $5!="1" {n++} END{print n+0}' results/scenarios.out)
bad_sent_comp=$(awk '$1=="C" && $5!="1" {n++} END{print n+0}' results/compiled.out)

# Parse the final counters: FUZZ10K json_log <n> <crash> <hang> <corrupt> <refuse>
read -r tag label n crash hang corrupt refuse _rest <<EOF
$main_final
EOF
[ "${crash:-x}" = "0" ] && [ "${hang:-x}" = "0" ] && [ "${corrupt:-x}" = "0" ] || OK=0
[ "$n" = "10000" ] || OK=0
[ "${refuse:-0}" -gt 0 ] || OK=0

read -r ctag clabel cn ccrash chang ccorrupt crefuse _crest <<EOF
$comp_final
EOF
[ "${ccrash:-x}" = "0" ] && [ "${chang:-x}" = "0" ] && [ "${ccorrupt:-x}" = "0" ] || OK=0
[ "$cn" = "2048" ] || OK=0

[ "$main_det" -eq 1 ] || OK=0
[ "$comp_det" -eq 1 ] || OK=0
[ "$main_s" -eq 10000 ] || OK=0
[ "$comp_c" -eq 2048 ] || OK=0
[ "$log_lines" -eq 10000 ] || OK=0
[ "$bad_main" -eq 0 ] || OK=0
[ "$bad_comp" -eq 0 ] || OK=0
[ "$bad_sent_main" -eq 0 ] || OK=0
[ "$bad_sent_comp" -eq 0 ] || OK=0

{
  echo "FUZZ10K json_log run summary"
  echo "seed_main=0xF02A5EED1234ABCD seed_compiled=0xC0FFEE0DF02A5EED"
  echo "main_build_exit=$main_build_rc compiled_build_exit=$comp_build_rc"
  echo "main_run1_exit=$main_run1_rc main_run2_exit=$main_run2_rc"
  echo "compiled_run1_exit=$comp_run1_rc compiled_run2_exit=$comp_run2_rc"
  echo "main_deterministic=$main_det compiled_deterministic=$comp_det"
  echo "main_S_lines=$main_s compiled_C_lines=$comp_c scenarios_log_lines=$log_lines"
  echo "bad_main_lines=$bad_main bad_compiled_lines=$bad_comp"
  echo "bad_sentinel_main=$bad_sent_main bad_sentinel_compiled=$bad_sent_comp"
  echo "main_final=$main_final"
  echo "compiled_final=$comp_final"
  echo "coverage_exit=$cov_rc nm_exit=$nm_rc"
} > results/summary.txt

cat results/summary.txt
echo "RUN_ALL_OK=$OK"
[ "$OK" -eq 1 ]
