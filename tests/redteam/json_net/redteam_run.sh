#!/bin/sh
# RED-TEAM agent 5 orchestrator: build + run both adversarial harnesses, then
# concatenate their per-case logs into results/scenarios.log, hash it, and
# emit the combined summary line:
#   REDTEAM json_net <cases> <refused> <crashes> <corrupt> <unexpected_ok>
#
# All artifacts live under tests/redteam/json_net/results/ (gitignored).
# Build time bounds: 180 s each. Run time bound: 600 s each.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
HERE=tests/redteam/json_net
R=$HERE/results
mkdir -p "$R"

echo "RT5: build C+ harness (json/net/http)"
timeout 180 ./build/c+ build "$HERE/redteam_json_net.cp" \
  --default --sanitize=none -lcurl -I. -Ithird_party/yyjson \
  -Lbuild/ext -lyyjson -o "$R/redteam_json_net" \
  >"$R/build_cplus.log" 2>&1
echo "RT5: build C harness (26 vendored cplus_v_*)"
timeout 180 /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -Wno-unused-parameter \
  "$HERE/redteam_vendored.c" \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -lm -pthread -o "$R/redteam_vendored" \
  >"$R/build_vendored.log" 2>&1

echo "RT5: run C+ harness"
timeout 600 "$R/redteam_json_net" "$R/scenarios_cplus.log" \
  >"$R/cplus.stdout" 2>"$R/cplus.stderr"
CPLUS=$(awk '$1=="REDTEAM_PART"{print $2,$3,$4,$5,$6,$7}' "$R/cplus.stdout")
echo "RT5: run vendored harness"
timeout 600 "$R/redteam_vendored" "$R/scenarios_vendored.log" \
  >"$R/vendored.stdout" 2>"$R/vendored.stderr"
VEND=$(awk '$1=="REDTEAM_PART"{print $2,$3,$4,$5,$6,$7}' "$R/vendored.stdout")

# Build combined log: preserve each part's lines under one file.
cat "$R/scenarios_cplus.log" "$R/scenarios_vendored.log" > "$R/scenarios.log"
shasum -a 256 "$R/scenarios.log" > "$R/digest.sha256"
FNV_CPLUS=$(awk '$1=="REDTEAM_DIGEST_CPLUS"{print $2}' "$R/cplus.stdout")
FNV_VEND=$(awk '$1=="REDTEAM_DIGEST_VENDORED"{print $2}' "$R/vendored.stdout")
printf 'PART cplus %s\nPART vendored %s\nFNV cplus %s\nFNV vendored %s\n' \
  "$CPLUS" "$VEND" "$FNV_CPLUS" "$FNV_VEND" > "$R/part_results.txt"

# Sum the 5 numeric columns (cases refused crashes corrupt unexpected_ok).
read -r NC RC CC KC UC <<EOF
$(awk '
  $1=="REDTEAM_PART" { c+=$3; r+=$4; x+=$5; k+=$6; u+=$7 }
  END { print c, r, x, k, u }
' "$R/cplus.stdout" "$R/vendored.stdout")
EOF

echo "RT5 stdout:"
cat "$R/cplus.stdout" "$R/vendored.stdout"
echo "RT5 stderr:"
cat "$R/cplus.stderr" "$R/vendored.stderr" 2>/dev/null || true
echo "RT5 combined log: $R/scenarios.log ($(wc -l < "$R/scenarios.log") lines)"
echo "RT5 digest: $(cat "$R/digest.sha256")"

echo "REDTEAM json_net $NC $RC $CC $KC $UC"
exit 0
