#!/bin/sh
# Build and run the per-method string/vector API suite.
# Prints one "<header> <method> PASS|FAIL" line per method, then a single
# aggregate summary line.  Exits non-zero on any real failure.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
cd "$ROOT"

CPLUS=${CPLUS_BIN:-./build/c+}
FLAGS="--default --no-werror --sanitize=none"
TMP=${TMPDIR:-/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode}

rc=0
: > "$TMP/str_vec_methods.out"
for name in string_methods vector_methods; do
    if ! "$CPLUS" build "tests/stdlib/method_tests/str_vec/$name.cp" $FLAGS \
            -o "tests/stdlib/method_tests/str_vec/$name" >"$TMP/$name.build.log" 2>&1; then
        echo "BUILD_FAIL $name" >&2
        cat "$TMP/$name.build.log" >&2
        rc=1
        continue
    fi
    if ! timeout 30 "tests/stdlib/method_tests/str_vec/$name" \
            >>"$TMP/str_vec_methods.out" 2>&1; then
        rc=1
    fi
done

cat "$TMP/str_vec_methods.out"

# Aggregate from the per-binary summaries (24 + 23 methods).
total=0
passed=0
failed=0
skipped=0
# Use awk for a portable single-pass sum.
read -r total passed failed skipped <<EOF
$(grep '^METHOD_TEST ' "$TMP/str_vec_methods.out" | \
  awk '{t+=$3; p+=$4; f+=$5; s+=$6} END {print t, p, f, s}')
EOF
total=${total:-0}; passed=${passed:-0}; failed=${failed:-0}; skipped=${skipped:-0}

echo "METHOD_TEST str_vec $total $passed $failed $skipped"

# 47 public entries per tests/stdlib/run_coverage.sh (24 string + 23 vector).
if [ "$total" -ne 47 ] || [ "$passed" -ne 47 ] || \
   [ "$failed" -ne 0 ] || [ "$skipped" -ne 0 ]; then
    exit 1
fi
exit "$rc"
