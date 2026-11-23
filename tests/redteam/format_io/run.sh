#!/bin/sh
# REDTEAM agent 4/10 -- deterministic adversarial harness for checked format & I/O.
#
# Build: /usr/bin/clang -std=c99 -O1, link BOTH safecrt + hardened libc archives.
# Run:   6000 deterministic cases, each in a forked child; stdout redirected to
#        a sink file. The precise scenario log and summary are written directly
#        by the harness. Exit is always 0 (the harness is an attacker; findings
#        are reported in FINDINGS.md, not via a failing status).
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

CC=/usr/bin/clang
INC="-Istdlib/safe -Ilibc/install-include -Ilibc -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
ARCHIVES="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a"
CFLAGS="-std=c99 -O1 -g -w -Wno-format-nonliteral -Wno-format-security -Wno-unused-function"

RES="$HERE"
BIN="$RES/redteam_format_io.bin"
LOG="$RES/scenarios.log"
SUM="$RES/summary.txt"
DIGEST="$RES/digest.txt"
SINK="$RES/stdout_sink.txt"
STDERR="$RES/stderr.log"

echo "[redteam/format_io] ROOT=$ROOT"
echo "[redteam/format_io] CC=$CC  CFLAGS=$CFLAGS"
echo "[redteam/format_io] archives: $ARCHIVES"
echo "[redteam/format_io] seed=0x52454454464D4931, cases=6000, forked children"

if ! $CC $CFLAGS $INC "$HERE/redteam_format_io.c" $ARCHIVES -lm -pthread -o "$BIN" \
        >"$RES/build.log" 2>&1; then
    echo "[redteam/format_io] BUILD FAILED" >&2
    cat "$RES/build.log" >&2
    exit 2
fi
echo "[redteam/format_io] build ok"

# The harness forks children for every case; redirect its own stdout to a sink.
# The harness itself always exits 0 unless it cannot open outputs / fork fails.
"$BIN" "$LOG" "$SUM" >"$SINK" 2>"$STDERR"
rc=$?
echo "[redteam/format_io] harness rc=$rc"

SUMMARY=$(grep '^REDTEAM format_io ' "$SINK" | tail -1)
if [ -z "$SUMMARY" ]; then
    SUMMARY=$(grep '^REDTEAM format_io ' "$SUM" | tail -1)
fi
if [ -z "$SUMMARY" ]; then
    echo "REDTEAM format_io 0 0 0 0 0"
    echo "[redteam/format_io] no summary line (rc=$rc)" >&2
    exit 3
fi

if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" | awk '{print $1}' > "$DIGEST"
elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$LOG" | awk '{print $NF}' > "$DIGEST"
else
    echo "[redteam/format_io] no SHA-256 tool available" >&2
    exit 4
fi
echo "[redteam/format_io] log lines: $(wc -l < "$LOG" | tr -d ' ')"
echo "[redteam/format_io] sha256=$(cat "$DIGEST")"

if [ -s "$STDERR" ]; then
    echo "[redteam/format_io] stderr lines: $(wc -l < "$STDERR" | tr -d ' ')"
    grep -v '^\[CPLUS-SAFE\]' "$STDERR" | head -20 || true
fi

# Emit the canonical machine-readable summary line.
echo "$SUMMARY"
exit 0
