#!/bin/sh
# RED-TEAM agent 4/20 -- networking / threading / async / checked-sync campaign.
#
#   sh tests/redblue20/red_netthread/run.sh
#
# Builds the harness from this directory only (no source/header edits), runs
# the classifier non-vacuity control, then 3000 deterministic one-case-per-fork
# attacks under a 60 s wall-clock bound.  Always exits 0: findings are reported
# in the RED20 summary line and in FINDINGS.md, not via the exit status.
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
timeout 180 "$CPLUS" build "$SRC" --default --no-werror --sanitize=none -I"$ROOT" \
    -lcurl -I/opt/homebrew/include -L/opt/homebrew/lib -luv \
    -L"$ROOT/build/libc" -lcplus_hardened \
    -o "$OUT" >"$HERE/out/build.log" 2>&1

control=$(REDTEAM_SELFCHECK=1 "$OUT")
printf '%s\n' "$control"
case "$control" in
    "SELFCHECK 5 5") ;;
    *) echo "error: classifier selfcheck failed" >&2; exit 2 ;;
esac

set +e
timeout 60 "$OUT" >"$HERE/out/stdout.log" 2>"$HERE/out/stderr.log"
RUN_RC=$?
set -e
cat "$HERE/out/stdout.log"
if [ "$RUN_RC" -ne 0 ]; then
    echo "error: harness run rc=$RUN_RC (see $HERE/out/stderr.log)" >&2
fi

if [ -f "$HERE/out/scenarios.log" ]; then
    digest=$(shasum -a 256 "$HERE/out/scenarios.log" | awk '{print $1}')
    printf '%s  %s\n' "$digest" "$HERE/out/scenarios.log" > "$HERE/out/scenarios.log.sha256"
    printf 'SCENARIOS_SHA256 %s\n' "$digest"
fi
exit 0
