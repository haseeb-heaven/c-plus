#!/bin/sh
# EDGE100 runtime edge-case suite.
#
#   sh tests/edge100/runtime/run.sh
#
# Builds harness.cp against THIS repo's runtime/include/cplus/cplus.h and
# runtime/cplus_runtime.c (the C+ driver in ../c-plus-core links a *different*
# runtime, so it is deliberately not used here).  Every case runs in its own
# forked child with an alarm(2) watchdog; the whole harness is bounded by
# `timeout 60`.  Also compiles a plain-C TU and a C++ TU against the same
# header/runtime to verify C/C++ interop of the cplus_* entry points.
#
# Findings are reported on stdout, not via the exit status (always 0 on a
# successful build+run), matching the other EDGE100/redteam harnesses.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT="$HERE/out"

CC=/usr/bin/clang
CXX=/usr/bin/clang++
INC="$ROOT/runtime/include"
RT="$ROOT/runtime/cplus_runtime.c"

mkdir -p "$OUT"

# --- build the fork-per-case harness ---------------------------------------
echo "--- build harness ---"
timeout 60 "$CC" -std=gnu17 -O1 -g -I"$INC" \
    -x c "$HERE/harness.cp" -x c "$RT" -pthread -o "$OUT/harness" \
    >"$OUT/harness.build.log" 2>&1
BUILD_RC=$?
cat "$OUT/harness.build.log"
if [ "$BUILD_RC" -ne 0 ]; then
    echo "FATAL: harness build failed (rc=$BUILD_RC)"
    exit 2
fi

# --- C/C++ interop: generate two small TUs in the owned out/ dir ------------
cat >"$OUT/interop_c.c" <<'EOF'
/* Plain C translation unit calling the C entry points directly. */
#include "cplus/cplus.h"
int c_side_alloc_check(void) {
    int *p = cplus_new(int);
    if (!p) return 1;
    if (*p != 0) return 2;
    if (cplus_alloc_bytes(p) != sizeof(int)) return 3;
    cplus_release(p);
    return 0;
}
void c_side_release(void *p) { cplus_release(p); }
EOF

cat >"$OUT/interop_cpp.cpp" <<'EOF'
/* C++ translation unit: exercises the extern "C" block of cplus/cplus.h. */
#include "cplus/cplus.h"
#include <cstdio>
extern "C" int c_side_alloc_check(void);
extern "C" void c_side_release(void *p);
int main() {
    if (c_side_alloc_check() != 0) return 1;
    void *p = cplus_alloc(32);              /* C++ -> C runtime entry point */
    if (!p) return 2;
    if (cplus_alloc_bytes(p) != 32) return 3;
    c_side_release(p);                      /* C++ hands managed ptr to C */
    void *q = cplus_zalloc(8);
    if (!q) return 4;
    cplus_release(q);
    cplus_scope s = {nullptr};
    (void)s;
    std::printf("INTEROP OK\n");
    return 0;
}
EOF

echo "--- build C/C++ interop ---"
timeout 60 "$CC" -std=gnu17 -I"$INC" -x c "$RT" -c -o "$OUT/rt.o" \
    >"$OUT/interop.build.log" 2>&1 || { cat "$OUT/interop.build.log"; echo "FATAL: runtime object build failed"; exit 2; }
timeout 60 "$CC" -std=gnu17 -I"$INC" -x c "$OUT/interop_c.c" -c -o "$OUT/interop_c.o" \
    >>"$OUT/interop.build.log" 2>&1 || { cat "$OUT/interop.build.log"; echo "FATAL: C TU build failed"; exit 2; }
timeout 60 "$CXX" -std=gnu++17 -I"$INC" -x c++ "$OUT/interop_cpp.cpp" -c -o "$OUT/interop_cpp.o" \
    >>"$OUT/interop.build.log" 2>&1 || { cat "$OUT/interop.build.log"; echo "FATAL: C++ TU build failed"; exit 2; }
timeout 60 "$CXX" "$OUT/interop_cpp.o" "$OUT/interop_c.o" "$OUT/rt.o" -pthread -o "$OUT/interop" \
    >>"$OUT/interop.build.log" 2>&1 || { cat "$OUT/interop.build.log"; echo "FATAL: interop link failed"; exit 2; }
cat "$OUT/interop.build.log"

INTEROP_OUT=$(timeout 10 "$OUT/interop" 2>"$OUT/interop.stderr")
INTEROP_RC=$?
if [ "$INTEROP_RC" -eq 0 ] && [ "$INTEROP_OUT" = "INTEROP OK" ]; then
    echo "EDGE100 interop PASS exit=0 output=INTEROP_OK"
else
    echo "EDGE100 interop FAIL exit=$INTEROP_RC output=<$INTEROP_OUT>"
    cat "$OUT/interop.stderr"
fi

# --- run the harness --------------------------------------------------------
echo "--- run harness ---"
EDGE100_OUT="$OUT" timeout 60 "$OUT/harness" -v \
    >"$OUT/run.stdout" 2>"$OUT/run.stderr"
RUN_RC=$?
if [ "$RUN_RC" -eq 124 ]; then
    echo "FATAL: harness watchdog/timeout (60s) exceeded"
    exit 3
fi

SUMMARY=$(grep '^EDGE100 runtime ' "$OUT/run.stdout" | tail -n 1)
echo "$SUMMARY"
echo "--- failing cases (if any) ---"
grep ' FAIL ' "$OUT/run.stdout" || echo "(none)"
echo "--- harness stderr (diagnostics from fatal cases are per-case files) ---"
wc -l "$OUT/run.stderr"

exit 0
