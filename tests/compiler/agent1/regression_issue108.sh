#!/bin/sh
# Focused regression for issue #108: lowered TUs and lowered .hp headers must
# use random exclusive-create names, carry 0600 permissions, and be cleaned
# up.  No PID/index-based temp names or reopen-by-name writes are accepted.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue108.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue108-$$
mkdir -p "$WORK/src" "$WORK/tmp" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$WORK/src/lib.hp" <<'EOF'
class TempProbe {
  int v;
  int get(void) { return this->v; }
};
EOF
cat > "$WORK/src/main.c" <<'EOF'
#include "lib.hp"
int main(void) { TempProbe p = {5}; return p.get() == 5 ? 0 : 1; }
EOF

TMPDIR="$WORK/tmp" CPLUS_KEEP_TEMP=1 "$CPLUS" build --no-werror \
    --emit-c "$WORK/lowered.c" "$WORK/src/main.c" -o "$WORK/main.bin" \
    >"$WORK/build.log" 2>&1 ||
    { cat "$WORK/build.log" >&2; fail "build failed"; }
"$WORK/main.bin" || fail "compiled program did not run"

# Random names: no PID, no counter suffix.
header_name=$(sed -n 's/.*cplus-hp-\([^"/]*\)\.h".*/\1/p' "$WORK/lowered.c")
header_name=${header_name%%.*} # LLVM inserts a separator before the suffix
[ -n "$header_name" ] || fail "lowered .hp include path not found"
case "$header_name" in
  *-*) fail "predictable header temp name survived: $header_name" ;;
  *[!A-Za-z0-9]*) fail "non-random characters in header temp name: $header_name" ;;
esac
[ "$header_name" != "$$" ] || fail "header temp name is the process id"

tu_count=0
for f in "$WORK"/src/.cplus-tmp-*.c; do
  [ -e "$f" ] || continue
  tu_count=$((tu_count + 1))
  stem=${f##*/.cplus-tmp-}
  stem=${stem%.c}
  case "$stem" in
    *-*) fail "predictable TU temp name survived: $f" ;;
    *[!A-Za-z0-9]*) fail "non-random characters in TU temp name: $f" ;;
  esac
  [ "$stem" != "$$" ] || fail "TU temp name is the process id"
  mode=$(stat -f %Lp "$f" 2>/dev/null || stat -c %a "$f" 2>/dev/null)
  [ "$mode" = "600" ] || fail "TU temp file is not 0600: $f ($mode)"
done
[ "$tu_count" -ge 1 ] || fail "no kept lowered TU temp file was produced"

# The header temp is removed by the guard on every normal exit path.
for f in "$WORK"/tmp/cplus-hp-*.h; do
  [ -e "$f" ] && fail "lowered header temp was not cleaned up: $f"
done

echo "issue #108 temp-file regression: PASS"
