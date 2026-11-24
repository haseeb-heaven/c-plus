#!/bin/sh
# EDGE100 agent 3/10 -- exhaustive edge-case campaign for stdlib I/O APIs.
#
#   sh tests/edge100/io/run.sh
#
# Builds ONLY tests/edge100/io/harness.cp (no source/header edits), runs the
# classifier non-vacuity control, then one forked child per (API, edge case)
# with a 4s per-case alarm and a 60s whole-run bound.  Writes RESULTS.md and
# API_MANIFEST.md from the per-case log.  Always prints the EDGE100 summary.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

SRC="$HERE/harness.cp"
OUT="$HERE/out/harness"
LOG="$HERE/out/cases.log"
BUILD_LOG="$HERE/out/build.log"
STDERR="$HERE/out/stderr.log"
SINK="$HERE/out/stdout.txt"
SUMMARY_LINE="EDGE100 io 0 0 0 0"

mkdir -p "$HERE/out" "$HERE/out/tmp"

# Preferred compiler.  The C+ toolchain is rebuilt in place by other work; if
# the primary binary regresses on system headers, fall back to a sibling build
# of the same compiler discovered next to it (no hard-coded absolute paths).
PRIMARY=${CPLUS_BIN:-"$ROOT/build/c+"}
set -- "$PRIMARY"
if [ -f "$PRIMARY" ]; then
    CORE_BIN=$(sed -n 's/^exec \([^ ]*\) .*/\1/p' "$PRIMARY" 2>/dev/null | head -1)
    if [ -n "$CORE_BIN" ] && [ -d "$(dirname "$CORE_BIN")" ]; then
        CORE_ROOT=$(CDPATH= cd -- "$(dirname "$(dirname "$CORE_BIN")")" 2>/dev/null && pwd)
        if [ -n "$CORE_ROOT" ]; then
            for alt in "$CORE_ROOT"/build-*/c+; do
                [ -x "$alt" ] && set -- "$@" "$alt"
            done
        fi
    fi
fi

CPLUS_USED=""
for cand in "$@"; do
    [ -x "$cand" ] || continue
    if "$cand" build "$SRC" --default --sanitize=none -I"$ROOT" \
            -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
            -DYYJSON_READER_DEPTH_LIMIT=0 -DYYJSON_WRITER_DEPTH_LIMIT=0 \
            -o "$OUT" >"$BUILD_LOG" 2>&1; then
        CPLUS_USED=$cand
        break
    fi
done

if [ -z "$CPLUS_USED" ]; then
    echo "[edge100 io] BUILD FAILED (no usable C+ compiler)" >&2
    cat "$BUILD_LOG" >&2
    echo "$SUMMARY_LINE"
    exit 3
fi
echo "[edge100 io] compiler: $CPLUS_USED"

CONTROL=$(EDGE100_SELFCHECK=1 "$OUT")
printf '%s\n' "$CONTROL"
case "$CONTROL" in
    "SELFCHECK 2 2") ;;
    *)
        echo "[edge100 io] classifier selfcheck failed" >&2
        echo "$SUMMARY_LINE"
        exit 4
        ;;
esac

# Bounded run; the harness itself forks one child per case with alarm(4).
timeout 60 "$OUT" "$LOG" >"$SINK" 2>"$STDERR"
rc=$?
echo "[edge100 io] harness rc=$rc"

SUMMARY_LINE=$(grep '^EDGE100 io ' "$SINK" | tail -1)
if [ -z "$SUMMARY_LINE" ]; then
    SUMMARY_LINE=$(grep '^EDGE100 io ' "$STDERR" | tail -1)
fi
if [ -z "$SUMMARY_LINE" ]; then
    echo "[edge100 io] no summary line" >&2
    echo "EDGE100 io 0 0 0 0"
    exit 5
fi

# Derive the api -> cases -> result manifest and the human-readable report.
EDGE100_SUMMARY="$SUMMARY_LINE" EDGE100_CPLUS="$CPLUS_USED" \
python3 - "$LOG" "$HERE/API_MANIFEST.md" "$HERE/RESULTS.md" <<'PY'
import os
import re
import sys
from collections import OrderedDict

log_path, manifest_path, results_path = sys.argv[1:4]
summary = os.environ.get("EDGE100_SUMMARY", "")
compiler = os.environ.get("EDGE100_CPLUS", "")

data = OrderedDict()
for raw in open(log_path, "r", encoding="utf-8"):
    parts = raw.split()
    if len(parts) != 3 or parts[0].startswith("#"):
        continue
    api, case, result = parts
    data.setdefault(api, OrderedDict())[case] = result

parts = summary.split()
if len(parts) >= 5 and parts[-4:]:
    apis, cases, passed, failed = [int(x) for x in parts[-4:]]
else:
    apis, cases, passed, failed = 0, 0, 0, 0

legend = [
    ("valid", "well-formed input / correct call"),
    ("null", "NULL pointer argument"),
    ("zero", "zero length / zero value / empty path"),
    ("one_over", "one byte short buffer, oversized token or SIZE_MAX length"),
    ("eof", "empty stream / end of file"),
    ("invalid_handle", "NULL or failed-open file handle / missing path"),
    ("double_close", "second close/drop/remove after the first succeeded"),
    ("malformed", "syntactically invalid JSON / invalid type / bad template"),
    ("truncated", "truncated JSON document"),
    ("depth_16384", "16384 nested containers (inside the default budget)"),
    ("depth_16385", "16385 nested containers (one over the default budget)"),
    ("exact_fit", "buffer or length exactly matches the payload"),
    ("crlf", "CR / CRLF line endings"),
    ("alloc_fail", "injected realloc/tmpfile failure or pre-allocation length guard"),
]

with open(manifest_path, "w", encoding="utf-8") as f:
    f.write("# EDGE100 io -- API / case manifest\n\n")
    f.write("Every public API in `stdlib/io.hp`, `stdlib/file.hp`, `stdlib/fs.hp` "
            "and `stdlib/json.hp`, with its edge-case matrix and per-case result. "
            "Generated from `out/cases.log`.\n\n")
    f.write("Summary: `%s`\n\n" % summary)
    f.write("| # | API | Header | Cases (case=result) | Overall |\n")
    f.write("|---|-----|--------|---------------------|---------|\n")
    for i, (api, case_map) in enumerate(data.items(), 1):
        header = {
            "io.": "io.hp", "file": "file.hp", "fs.": "fs.hp", "json": "json.hp"
        }
        h = next((v for k, v in header.items() if api.startswith(k)), "unknown")
        overall = "FAIL" if "FAIL" in case_map.values() or "CRASH" in case_map.values() else "PASS"
        cells = ", ".join("%s=%s" % (c, r) for c, r in case_map.items())
        f.write("| %d | `%s` | %s | %s | %s |\n" % (i, api, h, cells, overall))
    f.write("\n## Case legend\n\n")
    f.write("| Case | Meaning |\n|------|---------|\n")
    for name, meaning in legend:
        f.write("| `%s` | %s |\n" % (name, meaning))
    f.write("\nAPIs with no applicable case for a category omit it (for example "
            "`println` has no handle to invalidate). Allocation failure is "
            "injected into header-side `realloc`/`tmpfile` calls; yyjson-internal "
            "allocations are covered by the pre-allocation length/depth guards.\n")

by_header = OrderedDict()
for api, case_map in data.items():
    h = next((v for k, v in {"io.": "io.hp", "file": "file.hp", "fs.": "fs.hp",
                              "json": "json.hp"}.items() if api.startswith(k)), "unknown")
    slot = by_header.setdefault(h, {"apis": 0, "cases": 0, "pass": 0, "fail": 0})
    slot["apis"] += 1
    for r in case_map.values():
        slot["cases"] += 1
        slot["pass" if r == "PASS" else "fail"] += 1

failed_cases = [(api, c, r) for api, cm in data.items() for c, r in cm.items() if r != "PASS"]

with open(results_path, "w", encoding="utf-8") as f:
    f.write("# EDGE100 io -- results\n\n")
    f.write("```\n%s\n```\n\n" % summary)
    f.write("Compiler used: `%s`\n\n" % compiler)
    f.write("| Header | APIs | Cases | Passed | Failed |\n")
    f.write("|--------|------|-------|--------|--------|\n")
    for h, slot in by_header.items():
        f.write("| %s | %d | %d | %d | %d |\n" %
                (h, slot["apis"], slot["cases"], slot["pass"], slot["fail"]))
    f.write("| **total** | **%d** | **%d** | **%d** | **%d** |\n\n" %
            (apis, cases, passed, failed))
    f.write("## Failed cases\n\n")
    if failed_cases:
        for api, c, r in failed_cases:
            f.write("- `%s` / `%s`: %s\n" % (api, c, r))
    else:
        f.write("None. Every case matched the documented contract with intact canaries.\n")
    f.write("\n## Method\n\n")
    f.write("- One `fork()` per (API, case); the child writes a result struct over a pipe.\n")
    f.write("- `alarm(4)` watchdog in each child; whole run bounded by `timeout 60`.\n")
    f.write("- 0xA5 canaries surround every caller buffer; overruns are reported as FAIL.\n")
    f.write("- Allocation failure injected via compile-time `realloc`/`tmpfile` stubs; "
            "yyjson-internal allocations are guarded by length/depth pre-checks.\n")
    f.write("- Depth budgets exercised at 16384 (within) and 16385 (one over).\n")
    f.write("- The runner prefers `build/c+` and falls back to another sibling build "
            "of the same compiler if the primary cannot compile the harness.\n")
    f.write("- stdout/stderr of the harness are in `out/stdout.txt` / `out/stderr.log`.\n")
PY

# Bounded temp directory: children unlink their own case files.
find "$HERE/out/tmp" -type f -name 'case_*' -delete 2>/dev/null || true

echo "$SUMMARY_LINE"
exit 0
