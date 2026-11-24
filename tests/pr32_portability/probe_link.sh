#!/bin/sh
# PR32 libc+ portability: static + shared linking probes.
# Builds a tiny libc+ consumer against the checked-in static archive and,
# where possible, a shared library built from the same libc sources.
# POSIX sh only. Writes out/link.txt and four status files.

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT="$HERE/out"
mkdir -p "$OUT"
F="$OUT/link.txt"
: > "$F"
# Linux: libc+ atomic wrappers reference __atomic_is_lock_free from libatomic.
PLATFORM_LIBS=""
if [ "$(uname -s)" = Linux ]; then PLATFORM_LIBS="-latomic"; fi
log() { printf '%s\n' "$*" >> "$F"; }
status() { printf '%s\n' "$2" > "$OUT/$1.status"; }

CC=${CPLUS_CLANG:-clang}
CC_PATH=$(command -v "$CC" 2>/dev/null)
if [ -n "$CC_PATH" ]; then CC="$CC_PATH"; fi
SDKROOT=$(sed -n '1p' "$OUT/sdkroot.txt" 2>/dev/null)
if [ -z "$SDKROOT" ] && command -v xcrun >/dev/null 2>&1; then
    SDKROOT=$(xcrun --show-sdk-path 2>/dev/null)
fi
EXTRA=
if [ -n "$SDKROOT" ] && [ -d "$SDKROOT" ]; then EXTRA="-isysroot $SDKROOT"; fi
OS=$(uname -s 2>/dev/null)
ARCHIVE="$ROOT/build/libc/libcplus_hardened.a"

log "PR32_LIBC static/shared libc+ link probes"
log "probe_utc: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "compiler: $CC (sdkroot: ${SDKROOT:-NOT FOUND})"
log "os: $OS"
log "archive: $ARCHIVE"

cat > "$OUT/consumer.c" <<'SRC'
/* glibc: feature macros must precede ALL system headers; assert.h pulls in
 * features.h and would otherwise hide the POSIX types cplus_libc.h declares. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include <assert.h>
#include <string.h>
#include "cplus_libc.h"
int main(void) {
    char *p = (char *)cplus_malloc(16);
    assert(p != NULL);
    assert(cplus_alloc_size(p) == 16);
    cplus_memcpy(p, "sdk", 4);
    assert(cplus_strcmp(p, "sdk") == 0);
    assert(cplus_iswalpha(L'A'));
    assert(cplus_strtoimax("123", NULL, 10) == 123);
    cplus_free(p);
    return 0;
}
SRC

# ---------------------------------------------------------------- STATIC ---
log ""
log "=== STATIC: tiny consumer vs build/libc/libcplus_hardened.a ==="
if [ ! -f "$ARCHIVE" ]; then
    log "archive missing; attempting \$ sh tests/libc/build_libc.sh"
    if TMO=$(command -v timeout 2>/dev/null); then
        "$TMO" 25 sh "$ROOT/tests/libc/build_libc.sh" >> "$F" 2>&1
        log "build_libc.sh exit: $?"
    else
        sh "$ROOT/tests/libc/build_libc.sh" >> "$F" 2>&1
        log "build_libc.sh exit: $?"
    fi
fi

STATIC_OK=0
if [ -f "$ARCHIVE" ]; then
    # Attempt 1: exactly what build scripts use (no explicit sysroot). This is
    # the version-sensitive step: Homebrew clang on this host expects an SDK
    # that is not installed, so the failure text is kept as evidence.
    PLAIN_BIN="$OUT/consumer_static_plain"
    log ""
    log "--- static attempt 1: plain \$CC (no -isysroot) ---"
    log "\$ $CC -std=c99 -Wall -Wextra -I$ROOT/libc/install-include $OUT/consumer.c $ARCHIVE -lm -pthread -o $PLAIN_BIN"
    $CC -std=c99 -Wall -Wextra -I"$ROOT/libc/install-include" \
        "$OUT/consumer.c" "$ARCHIVE" -lm -pthread $PLATFORM_LIBS -o "$PLAIN_BIN" >> "$F" 2>&1
    prc=$?
    log "build_exit: $prc"
    if [ "$prc" -eq 0 ]; then
        "$PLAIN_BIN" >> "$F" 2>&1
        log "run_exit: $?"
        STATIC_OK=1
    fi

    # Attempt 2: same command with the installed SDK sysroot (macOS only).
    if [ -n "$EXTRA" ] && [ "$STATIC_OK" -eq 0 ]; then
        FIX_BIN="$OUT/consumer_static"
        log ""
        log "--- static attempt 2: \$CC with detected SDK sysroot ---"
        log "\$ $CC -std=c99 -Wall -Wextra $EXTRA -I$ROOT/libc/install-include $OUT/consumer.c $ARCHIVE -lm -pthread -o $FIX_BIN"
        $CC -std=c99 -Wall -Wextra $EXTRA -I"$ROOT/libc/install-include" \
            "$OUT/consumer.c" "$ARCHIVE" -lm -pthread $PLATFORM_LIBS -o "$FIX_BIN" >> "$F" 2>&1
        frc=$?
        log "build_exit: $frc"
        if [ "$frc" -eq 0 ]; then
            "$FIX_BIN" >> "$F" 2>&1
            rrc=$?
            log "run_exit: $rrc"
            if [ "$rrc" -eq 0 ]; then STATIC_OK=1; fi
        fi
    fi
else
    log "archive NOT FOUND; static link not tested"
fi
if [ "$STATIC_OK" -eq 1 ]; then
    status link_static "PASS tiny consumer links and runs against the static archive"
else
    status link_static "FAIL static archive consumer did not build and run; see link.txt"
fi

# ------------------------------------------------------- SHARED FROM SRC ---
log ""
log "=== SHARED SOURCES: build .dylib/.so directly from libc sources ==="
SRCLIST="$OUT/libc_sources.txt"
awk '/^set\(LIBC_SOURCES/{f=1;next} /^\)/{if(f){f=0}} f && NF {print $1}' \
    "$ROOT/libc/CMakeLists.txt" > "$SRCLIST"
OBJDIR="$OUT/shared_obj"
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"
SO_FAIL=0
SHARED_BUILT=
if [ "$OS" = Darwin ]; then SHARED_EXT=dylib; SHARED_FLAG=-dynamiclib; else SHARED_EXT=so; SHARED_FLAG=-shared; fi
SHARED_OUT="$OUT/libcplus_hardened.$SHARED_EXT"
log "source_count: $(wc -l < "$SRCLIST" | tr -d ' ')"
log "\$ $CC -O1 -fPIC $EXTRA -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/libc/include -c <each source>"
while IFS= read -r s; do
    b="$OBJDIR/$(printf '%s' "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    if [ -n "$EXTRA" ]; then
        $CC -O1 -fPIC $EXTRA -I"$ROOT/libc" -I"$ROOT/libc/install-include" \
            -I"$ROOT/libc/include" -c "$ROOT/libc/$s" -o "$b" >> "$F" 2>&1
    else
        $CC -O1 -fPIC -I"$ROOT/libc" -I"$ROOT/libc/install-include" \
            -I"$ROOT/libc/include" -c "$ROOT/libc/$s" -o "$b" >> "$F" 2>&1
    fi
    if [ $? -ne 0 ]; then
        SO_FAIL=1
        log "compile FAILED for $s"
        break
    fi
done < "$SRCLIST"
log "compile_fail: $SO_FAIL"
if [ "$SO_FAIL" -eq 0 ]; then
    log "\$ $CC $SHARED_FLAG $EXTRA $OBJDIR/*.o -lm -pthread -o $SHARED_OUT"
    if [ -n "$EXTRA" ]; then
        $CC $SHARED_FLAG $EXTRA "$OBJDIR"/*.o -lm -pthread -o "$SHARED_OUT" >> "$F" 2>&1
    else
        $CC $SHARED_FLAG "$OBJDIR"/*.o -lm -pthread -o "$SHARED_OUT" >> "$F" 2>&1
    fi
    slrc=$?
    log "link_exit: $slrc"
    if [ "$slrc" -eq 0 ] && [ -f "$SHARED_OUT" ]; then
        SHARED_BUILT="$SHARED_OUT"
    fi
fi
if [ -n "$SHARED_BUILT" ]; then
    status link_shared_sources "PASS shared library built from sources with $CC"
else
    status link_shared_sources "FAIL shared library from sources did not link; see link.txt"
fi

# ------------------------------------------------------ SHARED CONSUMER ----
log ""
log "=== SHARED CONSUMER: link tiny consumer against an existing shared lib ==="
if [ -z "$SHARED_BUILT" ]; then
    # The CMake install produced by install_smoke.sh links the shared target
    # with the default system cc; reuse it as the shared-link evidence.
    for cand in \
        "$ROOT/build/libc-install/lib/libcplus_hardened_libc.$SHARED_EXT" \
        "$ROOT/build/libc-cmake/libcplus_hardened_libc.$SHARED_EXT" \
        "$ROOT/build/libc-cmake/libcplus_hardened_libc.dylib" \
        "$ROOT/build/compat/cmake/libcplus_hardened_libc.dylib"; do
        if [ -f "$cand" ]; then SHARED_BUILT="$cand"; break; fi
    done
fi
if [ -n "$SHARED_BUILT" ] && [ -f "$SHARED_BUILT" ]; then
    SHARED_DIR=$(dirname "$SHARED_BUILT")
    log "shared_library: $SHARED_BUILT"
    log "note: source build with $CC failed above; this library was built by the CMake install path."
    SHARED_BIN="$OUT/consumer_shared"
    log "\$ $CC -std=c99 -Wall -Wextra $EXTRA -I$ROOT/libc/install-include $OUT/consumer.c $SHARED_BUILT -Wl,-rpath,$SHARED_DIR -lm -pthread -o $SHARED_BIN"
    if [ -n "$EXTRA" ]; then
        $CC -std=c99 -Wall -Wextra $EXTRA -I"$ROOT/libc/install-include" \
            "$OUT/consumer.c" "$SHARED_BUILT" -Wl,-rpath,"$SHARED_DIR" \
            -lm -pthread $PLATFORM_LIBS -o "$SHARED_BIN" >> "$F" 2>&1
    else
        $CC -std=c99 -Wall -Wextra -I"$ROOT/libc/install-include" \
            "$OUT/consumer.c" "$SHARED_BUILT" -Wl,-rpath,"$SHARED_DIR" \
            -lm -pthread $PLATFORM_LIBS -o "$SHARED_BIN" >> "$F" 2>&1
    fi
    clrc=$?
    log "build_exit: $clrc"
    if [ "$clrc" -eq 0 ]; then
        "$SHARED_BIN" >> "$F" 2>&1
        crrc=$?
        log "run_exit: $crrc"
        if [ "$crrc" -eq 0 ]; then
            status link_shared_consumer "PASS consumer links+runs against installed shared library"
        else
            status link_shared_consumer "FAIL shared consumer ran with exit $crrc; see link.txt"
        fi
    else
        status link_shared_consumer "FAIL shared consumer did not build; see link.txt"
    fi
else
    log "no shared library available; shared consumer link NOT TESTED"
    status link_shared_consumer "NOT_TESTED no shared library available on this host"
fi

# ------------------------------------------------------------- MINGW PE ----
log ""
log "=== WINDOWS PE: compile-only probe via mingw (NO runtime claim) ==="
MINGW=$(command -v x86_64-w64-mingw32-gcc 2>/dev/null)
if [ -n "$MINGW" ]; then
    log "mingw: $MINGW"
    "$MINGW" --version >> "$F" 2>&1
    printf 'int main(void) { return 0; }\n' > "$OUT/mingw_trivial.c"
    log "\$ $MINGW -O2 -std=c99 $OUT/mingw_trivial.c -o $OUT/mingw_trivial.exe"
    "$MINGW" -O2 -std=c99 "$OUT/mingw_trivial.c" -o "$OUT/mingw_trivial.exe" >> "$F" 2>&1
    mrc=$?
    log "compile_exit: $mrc"
    log "runtime: NOT TESTED (Windows PE execution blocked here: Highball/Wine stalls)"
    # Experimental detail only: do not promote this to a supported target.
    log ""
    log "--- experimental: libc+ consumer headers under mingw (informational) ---"
    log "\$ $MINGW -std=c99 -I$ROOT/libc/install-include $OUT/consumer.c -o $OUT/mingw_consumer.exe"
    "$MINGW" -std=c99 -I"$ROOT/libc/install-include" "$OUT/consumer.c" \
        -o "$OUT/mingw_consumer.exe" >> "$F" 2>&1
    log "experimental_libc_consumer_compile_exit: $?"
    if [ "$mrc" -eq 0 ]; then
        status mingw_compile "PASS compile-only PE probe (runtime NOT TESTED; EXPERIMENTAL target)"
    else
        status mingw_compile "FAIL mingw PE compile probe exited $mrc"
    fi
else
    log "x86_64-w64-mingw32-gcc NOT FOUND; Windows compile-only probe NOT TESTED"
    status mingw_compile "NOT_TESTED x86_64-w64-mingw32-gcc NOT FOUND on this host"
fi
