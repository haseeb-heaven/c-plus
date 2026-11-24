#!/bin/sh
# RED-TEAM agent 2/20 -- string / vector / collections adversarial campaign.
#
#   sh tests/redblue20/red_containers/run.sh
#
# Builds the C+ harness from this directory only (no source or header edits),
# runs the non-vacuity control, runs 6000 deterministic one-case-per-fork
# attacks under a 60-second timeout, and digests scenarios.log.  Always exits
# 0 on a completed campaign: findings are reported in the RED20 summary line
# and in FINDINGS.md, not via the exit status.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT"

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
SRC="$HERE/harness.cp"
OUT="$HERE/out/harness"

if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler not found at $CPLUS" >&2
    exit 2
fi

mkdir -p "$HERE/out"
"$CPLUS" build "$SRC" --default --sanitize=none -I"$ROOT" -o "$OUT"

control=$(REDTEAM_SELFCHECK=1 "$OUT")
printf '%s\n' "$control"
case "$control" in
    "SELFCHECK 5 5") ;;
    *) echo "error: classifier selfcheck failed" >&2; exit 2 ;;
esac

if command -v timeout >/dev/null 2>&1; then
    summary=$(timeout 60 "$OUT")
else
    summary=$("$OUT")
fi
printf '%s\n' "$summary"

digest=$(shasum -a 256 "$HERE/scenarios.log" | awk '{print $1}')
printf '%s  %s\n' "$digest" "$HERE/scenarios.log" > "$HERE/scenarios.log.sha256"
printf 'SCENARIOS_SHA256 %s\n' "$digest"
exit 0
