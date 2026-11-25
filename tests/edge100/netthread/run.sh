#!/bin/sh
# EDGE100 agent 4/10 -- edge-case coverage for net/http/thread/async/time.
#
#   sh tests/edge100/netthread/run.sh
#
# Builds harness.cp from this directory only (no source/header edits), creates
# the local file:// fixture used by the HTTP/file-I/O cases, then runs every
# case in its own forked child under a 60 s wall-clock bound.  Emits the
# machine-readable summary line:
#
#   EDGE100 netthread <apis> <cases> <passed> <failed>
#
# Exits 0 when the campaign completes; exits 2 only if no usable C+ compiler
# can build the harness (a finding is reported in the summary, not the rc).
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT"

SRC="$HERE/harness.cp"
OUT="$HERE/out/harness"
mkdir -p "$HERE/out"

# Local fixture.  The harness derives the absolute file:// URL from getcwd(),
# so no user- or machine-specific path is baked into the test source.
printf 'edge100-netthread-fixture\n' > "$HERE/out/fixture.txt"

# The repository's build/c+ wrapper may point at a compiler that another
# in-flight build is replacing; fall back to the installed `c+` on PATH.
build_with() {
    "$1" build "$SRC" --default --no-werror --sanitize=none -I"$ROOT" \
        -lcurl -I/opt/homebrew/include -L/opt/homebrew/lib -luv \
        -o "$OUT" >"$HERE/out/build.log" 2>&1
}

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
if ! build_with "$CPLUS"; then
    STABLE=$(command -v c+ 2>/dev/null || true)
    if [ -n "$STABLE" ] && [ "$STABLE" != "$CPLUS" ]; then
        echo "note: '$CPLUS' failed; retrying with '$STABLE'" >&2
        CPLUS="$STABLE"
    fi
    if ! build_with "$CPLUS"; then
        echo "error: cannot build harness with '$CPLUS' (see $HERE/out/build.log)" >&2
        echo "EDGE100 netthread 81 0 0 0"
        exit 2
    fi
fi

set +e
timeout 60 "$OUT" >"$HERE/out/stdout.log" 2>"$HERE/out/stderr.log"
RUN_RC=$?
set -e
cat "$HERE/out/stdout.log"
if [ "$RUN_RC" -ne 0 ]; then
    echo "error: harness rc=$RUN_RC (see $HERE/out/stderr.log)" >&2
fi

if [ -f "$HERE/out/scenarios.log" ]; then
    digest=$(shasum -a 256 "$HERE/out/scenarios.log" | awk '{print $1}')
    printf '%s  %s\n' "$digest" "$HERE/out/scenarios.log" > "$HERE/out/scenarios.log.sha256"
    printf 'SCENARIOS_SHA256 %s\n' "$digest"
fi
exit 0
