#!/bin/sh
# Deterministic 10k-scenario fuzz driver for the checked format/conversion APIs.
#
# Build: /usr/bin/clang -std=c99, link BOTH archives (safecrt + hardened libc).
# Run:   forked subprocess under `timeout 600`; stdout (all printf_s output) is
#        redirected to a sink file, the harness's FUZZ10K summary is recovered
#        from that sink and re-printed here.
# Exit:  nonzero on any crash / hang / corruption / harness failure.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

CC=/usr/bin/clang
INC="-Istdlib/safe -Ilibc/install-include -Ilibc -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
ARCHIVES="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a"
CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Wno-format-nonliteral -Wno-format-security -Wno-unused-function"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/fuzz10k_format.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

BIN="$WORK/fuzz_format"
LOG="$HERE/scenarios.log"
DIGEST="$HERE/digest.txt"
SINK="$WORK/stdout_sink"
STDERR="$WORK/stderr"

echo "[fuzz10k/format] ROOT=$ROOT"
echo "[fuzz10k/format] CC=$CC  CFLAGS=$CFLAGS"
echo "[fuzz10k/format] archives: $ARCHIVES"
echo "[fuzz10k/format] seed=0x46555A5A3130304B (\"FUZZ100K\"), scenarios=10000, timeout=600s"

if ! $CC $CFLAGS $INC "$HERE/fuzz_format.c" $ARCHIVES -lm -pthread -o "$BIN" \
        >"$WORK/build.log" 2>&1; then
    echo "[fuzz10k/format] BUILD FAILED" >&2
    cat "$WORK/build.log" >&2
    exit 1
fi
echo "[fuzz10k/format] build ok: $BIN"

# The child runs in a forked subprocess; `timeout` is the outer watchdog.
timeout 600 "$BIN" "$LOG" >"$SINK" 2>"$STDERR"
rc=$?
echo "[fuzz10k/format] harness rc=$rc"
if [ -s "$STDERR" ]; then
    echo "[fuzz10k/format] stderr lines (expected SAFECRT violation notices): $(wc -l < "$STDERR" | tr -d ' ')"
    if [ "$rc" -ne 0 ]; then
        echo "[fuzz10k/format] --- stderr (tail) ---"
        tail -20 "$STDERR"
    fi
fi

SUMMARY=$(grep '^FUZZ10K format ' "$SINK" | tail -1)
if [ -z "$SUMMARY" ]; then
    echo "FUZZ10K format 0 1 1 0 0"
    echo "[fuzz10k/format] no summary line (rc=$rc)" >&2
    exit 1
fi

# Digest of the full deterministic scenario log.
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" | awk '{print $1}' > "$DIGEST"
elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$LOG" | awk '{print $NF}' > "$DIGEST"
else
    echo "[fuzz10k/format] no SHA-256 tool available" >&2
    exit 1
fi
echo "[fuzz10k/format] log lines: $(wc -l < "$LOG" | tr -d ' ')"
echo "[fuzz10k/format] sha256=$(cat "$DIGEST")"

# Re-emit the summary, folding an external timeout/signal into the counters.
set -- $SUMMARY  # FUZZ10K format <scenarios> <crashes> <hangs> <corruptions> <refusals>
sc=$3; cr=$4; hg=$5; co=$6; rf=$7
if [ "$rc" -eq 124 ]; then
    hg=1
elif [ "$rc" -ge 128 ]; then
    cr=1
fi
echo "FUZZ10K format $sc $cr $hg $co $rf"

[ "$cr" -eq 0 ] && [ "$hg" -eq 0 ] && [ "$co" -eq 0 ] && [ "$sc" -ge 10000 ]
