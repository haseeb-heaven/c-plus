#!/bin/sh
# tools/verify_toolchain.sh -- portable C+ toolchain version report + probes.
#
# Prints the host identity and exact toolchain versions (clang, llvm-config,
# cmake, ninja, python3, uname), then probes:
#   * baseline C compile and run              (hard requirement)
#   * each requested sanitizer, compile+run   (optional, recorded skip)
#   * static archive link and run             (hard requirement)
#   * shared library link and run             (hard requirement)
#
# An unsupported sanitizer is NEVER silently skipped: it is printed as
# "UNSUPPORTED (skip=record)" with the compiler's reason. Hard failures call
# fail() and exit non-zero. Errors are handled explicitly; there are no
# error-swallowing fallbacks and no failed command is ever masked.
#
# Usage: CC=clang SANITIZERS=address,undefined sh tools/verify_toolchain.sh
set -eu

fail() {
  printf 'VERIFY FAIL: %s\n' "$*" >&2
  exit 1
}

printf '== host ==\n'
uname -a
printf 'uname -m: %s\n' "$(uname -m)"
if [ -r /etc/os-release ]; then
  # shellcheck disable=SC1091
  . /etc/os-release
  printf 'os-release: %s %s\n' "${NAME:-unknown}" "${VERSION_ID:-unknown}"
fi

printf '== toolchain versions ==\n'
CC_BIN=${CC:-}
if [ -z "$CC_BIN" ]; then
  for candidate in clang cc gcc; do
    if command -v "$candidate" >/dev/null 2>&1; then
      CC_BIN=$candidate
      break
    fi
  done
fi
[ -n "$CC_BIN" ] || fail "no C compiler found; set CC"
command -v "$CC_BIN" >/dev/null 2>&1 || fail "CC=$CC_BIN is not on PATH"
printf 'C compiler (CC): %s -> %s\n' "$CC_BIN" "$(command -v "$CC_BIN")"
"$CC_BIN" --version

# llvm-config may be version-suffixed or keg-only. Try PATH, LLVM_PREFIX, then
# Homebrew's LLVM prefix. A missing llvm-config is recorded, not fatal: the
# exact clang version printed above is the LLVM toolchain version.
LLVM_CONFIG=${LLVM_CONFIG:-}
if [ -z "$LLVM_CONFIG" ] && command -v llvm-config >/dev/null 2>&1; then
  LLVM_CONFIG=$(command -v llvm-config)
fi
if [ -z "$LLVM_CONFIG" ] && [ -n "${LLVM_PREFIX:-}" ] && [ -x "${LLVM_PREFIX}/bin/llvm-config" ]; then
  LLVM_CONFIG="${LLVM_PREFIX}/bin/llvm-config"
fi
if [ -z "$LLVM_CONFIG" ] && command -v brew >/dev/null 2>&1; then
  if brew_prefix=$(brew --prefix llvm 2>/dev/null); then
    if [ -x "$brew_prefix/bin/llvm-config" ]; then
      LLVM_CONFIG="$brew_prefix/bin/llvm-config"
    fi
  fi
fi
if [ -n "$LLVM_CONFIG" ]; then
  printf 'llvm-config: %s -> ' "$LLVM_CONFIG"
  "$LLVM_CONFIG" --version
else
  printf 'llvm-config: NOT FOUND (clang version above is the LLVM toolchain version)\n'
fi

for tool in cmake ninja python3; do
  if command -v "$tool" >/dev/null 2>&1; then
    printf '%s: %s\n' "$tool" "$(command -v "$tool")"
    "$tool" --version 2>&1 | sed -n '1p'
  else
    fail "required tool missing from PATH: $tool"
  fi
done
if command -v ar >/dev/null 2>&1; then
  printf 'ar: %s\n' "$(command -v ar)"
else
  fail "required tool missing from PATH: ar"
fi

printf '== feature probes ==\n'
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cplus-toolchain.XXXXXX") || fail "mktemp failed"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cat > "$TMP/probe.c" <<'EOF'
int main(void) { return 0; }
EOF

if "$CC_BIN" -std=c11 "$TMP/probe.c" -o "$TMP/probe"; then
  if "$TMP/probe"; then
    printf 'PROBE baseline-c: SUPPORTED (compiled and ran)\n'
  else
    fail "baseline C probe ran with a non-zero exit status"
  fi
else
  fail "baseline C compile failed with CC=$CC_BIN"
fi

# Optional features: compile+run decides. Unsupported is recorded with the
# first lines of the compiler/runtime diagnostic; it is never a silent skip.
SANITIZERS=${SANITIZERS:-address,undefined}
for san in $(printf '%s\n' "$SANITIZERS" | tr ',' ' '); do
  [ -n "$san" ] || continue
  log="$TMP/san-$san.log"
  if "$CC_BIN" -std=c11 -fsanitize="$san" "$TMP/probe.c" -o "$TMP/san-$san" >"$log" 2>&1; then
    if "$TMP/san-$san" >"$log.run" 2>&1; then
      printf 'PROBE sanitizer=%s: SUPPORTED (compiled and ran)\n' "$san"
    else
      printf 'PROBE sanitizer=%s: UNSUPPORTED (skip=record) runtime failed:\n' "$san"
      sed -n '1,8p' "$log.run"
    fi
  else
    printf 'PROBE sanitizer=%s: UNSUPPORTED (skip=record) compile/link failed:\n' "$san"
    sed -n '1,8p' "$log"
  fi
done

cat > "$TMP/lib.c" <<'EOF'
int cplus_toolchain_probe_add(int a, int b) { return a + b; }
EOF
cat > "$TMP/main.c" <<'EOF'
#include <stdio.h>
int cplus_toolchain_probe_add(int, int);
int main(void) {
  if (cplus_toolchain_probe_add(2, 3) != 5) return 1;
  printf("link=5\n");
  return 0;
}
EOF

"$CC_BIN" -std=c11 -c "$TMP/lib.c" -o "$TMP/lib.o" || fail "static probe: helper compile failed"
ar rcs "$TMP/libprobe.a" "$TMP/lib.o" || fail "static probe: ar failed"
"$CC_BIN" -std=c11 "$TMP/main.c" -L"$TMP" -lprobe -o "$TMP/static-main" || fail "static probe: link failed"
out=$("$TMP/static-main") || fail "static probe: run failed"
[ "$out" = "link=5" ] || fail "static probe: unexpected output: $out"
printf 'PROBE static-library-link: SUPPORTED (linked and ran: %s)\n' "$out"

case "$(uname -s)" in
  Darwin) SHARED_FLAG=-dynamiclib; SHARED_EXT=dylib; LOADER_VAR=DYLD_LIBRARY_PATH ;;
  *) SHARED_FLAG=-shared; SHARED_EXT=so; LOADER_VAR=LD_LIBRARY_PATH ;;
esac
"$CC_BIN" -std=c11 -fPIC "$SHARED_FLAG" "$TMP/lib.c" -o "$TMP/libprobe.$SHARED_EXT" || fail "shared probe: helper build failed"
"$CC_BIN" -std=c11 "$TMP/main.c" -L"$TMP" -lprobe -o "$TMP/shared-main" || fail "shared probe: link failed"
if [ "$LOADER_VAR" = "DYLD_LIBRARY_PATH" ]; then
  out=$(DYLD_LIBRARY_PATH="$TMP${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" "$TMP/shared-main") || fail "shared probe: run failed"
else
  out=$(LD_LIBRARY_PATH="$TMP${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$TMP/shared-main") || fail "shared probe: run failed"
fi
[ "$out" = "link=5" ] || fail "shared probe: unexpected output: $out"
printf 'PROBE shared-library-link: SUPPORTED (linked and ran: %s)\n' "$out"

printf 'TOOLCHAIN VERIFY OK\n'
