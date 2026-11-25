#!/bin/sh
# Build and run the 10,000-scenario forked offline net/http fuzz harness.
#
#   sh tests/fuzz10k/net_http/run.sh
#
# All artifacts go to tests/fuzz10k/net_http/results/ (gitignored). The script
# fixes the repository root so it works from any directory. Build uses the
# required `--default --sanitize=none -lcurl` mode (NOTES.md explains why
# `--strict` is impossible with the uneditable http.hp divide-by-zero false
# positive). External wall-clock bounds: 180 s build, 600 s run.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
R=tests/fuzz10k/net_http/results
mkdir -p "$R"
SRC=tests/fuzz10k/net_http/fuzz10k_net_http.cp
BIN="$R/fuzz10k_net_http"

echo "FUZZ10K BUILD: $SRC --default --sanitize=none -lcurl"
timeout 180 ./build/c+ build "$SRC" --default --sanitize=none -lcurl -o "$BIN" \
    >"$R/build.log" 2>&1
echo "FUZZ10K BUILD_RC=0 (diagnostics in $R/build.log)"

set +e
timeout 600 "$BIN" >"$R/stdout.log" 2>"$R/stderr.log"
RUN_RC=$?
set -e
echo "FUZZ10K RUN_RC=$RUN_RC"

shasum -a 256 "$R/scenarios.log" >"$R/digest.sha256" 2>/dev/null || true
echo "FUZZ10K digest: $(cat "$R/digest.sha256")"
echo "FUZZ10K stdout:"
cat "$R/stdout.log"
echo "FUZZ10K stderr:"
cat "$R/stderr.log"
exit "$RUN_RC"
