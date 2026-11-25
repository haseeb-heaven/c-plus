#!/bin/sh
# RED-TEAM agent 3/20 -- I/O, file, fs, JSON attack campaign.
#
#   sh tests/redblue20/red_io/run.sh
#
# Builds ONLY tests/redblue20/red_io/harness.cp (no source/header edits),
# runs the classifier non-vacuity control, then 3600 deterministic cases in
# one forked child each with a 4s alarm watchdog.  A bounded temp directory
# (tests/redblue20/red_io/out/tmp) holds all case files.  Always exits 0:
# findings are reported in the RED20 summary line and in FINDINGS.md.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
SRC="$HERE/harness.cp"
OUT="$HERE/out/harness"
LOG="$HERE/out/scenarios.log"
BUILD_LOG="$HERE/out/build.log"
STDERR="$HERE/out/stderr.log"
SINK="$HERE/out/stdout.txt"

if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler not found at $CPLUS" >&2
    echo "RED20 io 0 0 0 0 0"
    exit 2
fi

mkdir -p "$HERE/out" "$HERE/out/tmp"

# JSON cases include stdlib/json.hp, which wraps vendored yyjson.
if ! "$CPLUS" build "$SRC" --default --no-werror --sanitize=none -I"$ROOT" \
        -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
        -o "$OUT" >"$BUILD_LOG" 2>&1; then
    echo "[red_io] BUILD FAILED" >&2
    cat "$BUILD_LOG" >&2
    echo "RED20 io 0 0 0 0 0"
    exit 3
fi

CONTROL=$(REDTEAM_SELFCHECK=1 "$OUT")
printf '%s\n' "$CONTROL"
case "$CONTROL" in
    "SELFCHECK 5 5") ;;
    *)
        echo "[red_io] classifier selfcheck failed" >&2
        echo "RED20 io 0 0 0 0 0"
        exit 4
        ;;
esac

# The attacker harness always exits 0 unless it cannot create its log.
timeout 60 "$OUT" "$LOG" >"$SINK" 2>"$STDERR"
rc=$?
echo "[red_io] harness rc=$rc"

SUMMARY=$(grep '^RED20 io ' "$SINK" | tail -1)
if [ -z "$SUMMARY" ]; then
    SUMMARY=$(grep '^RED20 io ' "$STDERR" | tail -1)
fi
if [ -z "$SUMMARY" ]; then
    echo "RED20 io 0 0 0 0 0"
    echo "[red_io] no summary line" >&2
    exit 5
fi

if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" | awk '{print $1}' > "$HERE/out/scenarios.log.sha256"
elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$LOG" | awk '{print $NF}' > "$HERE/out/scenarios.log.sha256"
fi
echo "[red_io] log lines: $(wc -l < "$LOG" | tr -d ' ')"
echo "[red_io] sha256=$(cat "$HERE/out/scenarios.log.sha256" 2>/dev/null)"

# Bounded directory: every case file is unlinked by its child; remove empties.
find "$HERE/out/tmp" -type f -name 'case_*' -delete 2>/dev/null || true

echo "$SUMMARY"
exit 0
