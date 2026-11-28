#!/bin/sh
# Docs maintenance gate: the committed HTML API reference must match the
# generator output for the current headers. Bounded; POSIX sh.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
if ! timeout 120 python3 tools/gen_api_html.py --check >/dev/null 2>&1; then
    echo "DOC_CHECK FAIL html stale: run python3 tools/gen_api_html.py"
    exit 1
fi
echo "DOC_CHECK PASS html current"
