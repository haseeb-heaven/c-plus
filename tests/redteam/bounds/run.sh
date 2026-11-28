#!/bin/sh
# RED-TEAM agent 3 -- bounds / lifetime metadata adversarial campaign.
#
#   sh tests/redteam/bounds/run.sh
#
# Builds the C+ harness from this directory only (no source/header edits),
# runs the non-vacuity control, runs 6000 deterministic one-case-per-fork
# attacks, and digests scenarios.log.  Always exits 0: findings are reported
# in the REDTEAM summary line and in FINDINGS.md, not via the exit status.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT"

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
SRC="$HERE/bounds_redteam.cp"
OUT="$HERE/out/bounds_redteam"

if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler not found at $CPLUS" >&2
    exit 2
fi

mkdir -p "$HERE/out"
"$CPLUS" build "$SRC" --strict --sanitize=none -I"$ROOT" -o "$OUT"

control=$(REDTEAM_SELFCHECK=1 "$OUT")
printf '%s\n' "$control"
case "$control" in
    "SELFCHECK 5 5") ;;
    *) echo "error: classifier selfcheck failed" >&2; exit 2 ;;
esac

"$OUT"
digest=$(shasum -a 256 "$HERE/scenarios.log" | awk '{print $1}')
printf '%s  %s\n' "$digest" "$HERE/scenarios.log" > "$HERE/scenarios.log.sha256"
printf 'SCENARIOS_SHA256 %s\n' "$digest"
exit 0
