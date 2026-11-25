#!/bin/sh
# PR32 libc+ portability: local host toolchain probe.
# POSIX sh only. Writes out/toolchain.txt and out/toolchain.status.
# Availability is checked with explicit if/else and reported; no success fallbacks.

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT="$HERE/out"
mkdir -p "$OUT"
F="$OUT/toolchain.txt"
: > "$F"

log() { printf '%s\n' "$*" >> "$F"; }

log "PR32_LIBC local host toolchain probe"
log "probe_utc: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "uname_a: $(uname -a 2>&1)"
log "uname_s: $(uname -s 2>&1)"
log "uname_m: $(uname -m 2>&1)"
log "uname_r: $(uname -r 2>&1)"

if command -v sw_vers >/dev/null 2>&1; then
    log "== sw_vers =="
    sw_vers >> "$F" 2>&1
else
    log "sw_vers: NOT FOUND (non-Apple host)"
fi
if [ -f /etc/os-release ]; then
    log "== /etc/os-release =="
    cat /etc/os-release >> "$F"
else
    log "/etc/os-release: NOT FOUND (Apple host)"
fi

log "== C compiler =="
CLANG=${CPLUS_CLANG:-clang}
if command -v "$CLANG" >/dev/null 2>&1; then
    CLANG_PATH=$(command -v "$CLANG")
    log "clang_path: $CLANG_PATH"
    "$CLANG_PATH" --version >> "$F" 2>&1
    log "clang_target: $("$CLANG_PATH" -print-target-triple 2>&1)"
    log "clang_resource_dir: $("$CLANG_PATH" -print-resource-dir 2>&1)"
    log "clang_probe: OK"
else
    log "clang_path: NOT FOUND ($CLANG)"
    log "clang_probe: NOT FOUND"
fi

log "== Default system cc (for comparison) =="
if command -v cc >/dev/null 2>&1; then
    CC_PATH=$(command -v cc)
    log "cc_path: $CC_PATH"
    "$CC_PATH" --version >> "$F" 2>&1
else
    log "cc_path: NOT FOUND"
fi

log "== binutils =="
if command -v nm >/dev/null 2>&1; then
    log "nm_path: $(command -v nm)"
    nm --version >> "$F" 2>&1
else
    log "nm_path: NOT FOUND"
fi
if command -v ar >/dev/null 2>&1; then
    log "ar_path: $(command -v ar)"
    ar --version >> "$F" 2>&1
else
    log "ar_path: NOT FOUND"
fi
if [ -f "$ROOT/build/libc/libcplus_hardened.a" ]; then
    log "== build/libc/libcplus_hardened.a present; first members =="
    ar -t "$ROOT/build/libc/libcplus_hardened.a" >> "$F" 2>&1 | head -5
else
    log "archive: build/libc/libcplus_hardened.a NOT FOUND"
fi

log "== build tools =="
if command -v cmake >/dev/null 2>&1; then
    log "cmake_path: $(command -v cmake)"
    cmake --version >> "$F" 2>&1
else
    log "cmake_path: NOT FOUND"
fi
if command -v ninja >/dev/null 2>&1; then
    log "ninja_path: $(command -v ninja)"
    ninja --version >> "$F" 2>&1
else
    log "ninja_path: NOT FOUND"
fi

log "== Apple SDK =="
if command -v xcrun >/dev/null 2>&1; then
    SDKROOT=$(xcrun --show-sdk-path 2>/dev/null)
    log "sdk_root: $SDKROOT"
    log "sdk_version: $(xcrun --show-sdk-version 2>&1)"
    printf '%s\n' "$SDKROOT" > "$OUT/sdkroot.txt"
else
    log "sdk_root: NOT FOUND (no xcrun; non-Apple host)"
    : > "$OUT/sdkroot.txt"
fi

log "== C+ driver =="
CPLUS_CHECK=${CPLUS_BIN:-}
if [ -z "$CPLUS_CHECK" ]; then
    if [ -x "$ROOT/build/c+" ]; then
        CPLUS_CHECK="$ROOT/build/c+"
    elif command -v c+ >/dev/null 2>&1; then
        CPLUS_CHECK=$(command -v c+)
    fi
fi
if [ -n "$CPLUS_CHECK" ] && { [ -x "$CPLUS_CHECK" ] || command -v "$CPLUS_CHECK" >/dev/null 2>&1; }; then
    log "cplus_driver: $CPLUS_CHECK"
    "$CPLUS_CHECK" --version >> "$F" 2>&1
else
    log "cplus_driver: NOT FOUND"
fi

log "== timeout utility (global bound for run.sh) =="
if command -v timeout >/dev/null 2>&1; then
    log "timeout_path: $(command -v timeout)"
else
    log "timeout_path: NOT FOUND (run.sh global bound cannot be enforced here)"
fi

log "== Windows cross compiler (compile-only probe) =="
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    log "mingw_path: $(command -v x86_64-w64-mingw32-gcc)"
    x86_64-w64-mingw32-gcc --version >> "$F" 2>&1
else
    log "mingw_path: NOT FOUND"
fi

log "== Sanitizer feature availability (from sanitizers.txt; probe runs first) =="
for s in asan ubsan tsan; do
    if [ -f "$OUT/$s.status" ]; then
        log "$s: $(sed -n '1p' "$OUT/$s.status")"
    else
        log "$s: NOT RUN (no status file)"
    fi
done

if grep -q '^clang_probe: OK$' "$F" && grep -q '^uname_a:' "$F" && [ -s "$F" ]; then
    printf 'PASS toolchain evidence captured (clang, arch, SDK, versions)\n' > "$OUT/toolchain.status"
else
    printf 'FAIL toolchain evidence incomplete (no usable clang or uname)\n' > "$OUT/toolchain.status"
fi
