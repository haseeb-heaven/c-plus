#!/bin/sh
# PR32 libc+ portability: sanitizer feature probes.
# Compile AND run a trivial probe for each sanitizer on THIS host.
# POSIX sh only. Writes out/sanitizers.txt plus asan/ubsan/tsan status files.

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$HERE/out"
mkdir -p "$OUT"
F="$OUT/sanitizers.txt"
: > "$F"
log() { printf '%s\n' "$*" >> "$F"; }

CC=${CPLUS_CLANG:-clang}
CC_PATH=$(command -v "$CC" 2>/dev/null)
if [ -n "$CC_PATH" ]; then CC="$CC_PATH"; fi
SDKROOT=$(sed -n '1p' "$OUT/sdkroot.txt" 2>/dev/null)
if [ -z "$SDKROOT" ] && command -v xcrun >/dev/null 2>&1; then
    SDKROOT=$(xcrun --show-sdk-path 2>/dev/null)
fi

# Do NOT force -isysroot here: the sanitizer runtimes shipped with the
# compiler are tied to its own SDK. Overriding it (via -isysroot OR an inherited
# SDKROOT env var) breaks ASan/TSan interceptor installation on macOS arm64
# ("CHECK failed: sanitizer_malloc_mac.inc" / "loaded too late", exit 134).
unset SDKROOT
EXTRA=

log "PR32_LIBC sanitizer feature probes"
log "probe_utc: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "compiler_requested: $CC"
log "compiler_resolved: $CC_PATH"
log "sdkroot: ${SDKROOT:-NOT FOUND}"
log "extra_flags: ${EXTRA:-<none>}"
log "note: a sanitizer is recorded as PASS only when the probe both builds and runs here."

cat > "$OUT/san_probe.c" <<'SRC'
#include <stdio.h>
int main(void) {
    int a[2] = {1, 2};
    printf("san probe: %d\n", a[0] + a[1]);
    return 0;
}
SRC

for spec in 'asan address' 'ubsan undefined' 'tsan thread'; do
    set -- $spec
    name=$1
    flag=$2
    bl="$OUT/san_${name}.build.log"
    rl="$OUT/san_${name}.run.log"
    bin="$OUT/san_probe_$name"
    : > "$bl"
    : > "$rl"

    log ""
    log "=== $name: -fsanitize=$flag ==="
    if [ -n "$EXTRA" ]; then
        log "\$ $CC -O1 -g $EXTRA -fsanitize=$flag $OUT/san_probe.c -o $bin"
        $CC -O1 -g $EXTRA -fsanitize="$flag" "$OUT/san_probe.c" -o "$bin" > "$bl" 2>&1
    else
        log "\$ $CC -O1 -g -fsanitize=$flag $OUT/san_probe.c -o $bin"
        $CC -O1 -g -fsanitize="$flag" "$OUT/san_probe.c" -o "$bin" > "$bl" 2>&1
    fi
    brc=$?
    log "build_exit: $brc"
    cat "$bl" >> "$F"

    if [ "$brc" -eq 0 ]; then
        log "\$ timeout 10 $bin"
        if TMO=$(command -v timeout 2>/dev/null); then
            "$TMO" 10 "$bin" > "$rl" 2>&1
            rrc=$?
        else
            "$bin" > "$rl" 2>&1
            rrc=$?
        fi
        log "run_exit: $rrc"
        cat "$rl" >> "$F"
        if [ "$rrc" -eq 0 ]; then
            printf 'PASS compile+run succeeded with -fsanitize=%s on this host\n' "$flag" > "$OUT/$name.status"
        elif [ "$rrc" -eq 134 ] && grep -qi 'sanitizer.*CHECK failed\|Interceptors are not working\|loaded too late' "$rl"; then
            # Host sanitizer runtime is broken; this is NOT a libc+ defect.
            # Exact error is preserved in out/san_<name>.run.log.
            printf 'NOT_TESTED host sanitizer runtime blocked (-fsanitize=%s exit 134; error in out/san_%s.run.log)\n' "$flag" "$name" > "$OUT/$name.status"
        else
            printf 'FAIL probe built but run exited %d with -fsanitize=%s\n' "$rrc" "$flag" > "$OUT/$name.status"
        fi
    else
        log "build failed; exact compiler output is reproduced above."
        if grep -qi 'not supported\|unsupported\|unknown argument\|unrecognized\|invalid argument' "$bl"; then
            printf 'NOT_TESTED compiler rejects -fsanitize=%s on this host (unsupported)\n' "$flag" > "$OUT/$name.status"
        else
            printf 'FAIL probe did not build with -fsanitize=%s on this host\n' "$flag" > "$OUT/$name.status"
        fi
    fi
done
