#!/bin/sh
# RED-TEAM agent 10/20 -- ABI / source-integrity attack driver.
#
# Ownership: ONLY writes under tests/redblue20/red_abi/.  No source/header
# edits, no git add/commit.  All build artifacts land in ./artifacts/.
#
# Goal: empirically try to BREAK claims that the C ABI and C source surface are
# preserved on branch agent/modern-stdlib relative to cbcea57.  A "mismatch"
# here means an ABI fingerprint or exported-symbol difference; a "modified
# core file" is an M-status C file under libc/src/ or runtime/.
#
# Headline (printed last): RED20 abi <checks> <mismatches> <modified_core_files>

set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 2
OUT="$HERE/artifacts"
mkdir -p "$OUT"
BASE=cbcea57

CHECKS=0
MISMATCHES=0

run_check() {
    label=$1; shift
    CHECKS=$((CHECKS + 1))
    printf '\n===== CHECK %d: %s =====\n' "$CHECKS" "$label"
    printf 'CMD: %s\n' "$*" | tee "$OUT/$label.cmd"
    "$@" > "$OUT/$label.stdout" 2> "$OUT/$label.stderr"
    rc=$?
    printf 'RC=%s (stdout=%s stderr=%s)\n' \
        "$rc" "$OUT/$label.stdout" "$OUT/$label.stderr"
    printf '%s\n' "$rc" > "$OUT/$label.rc"
    return 0
}

# ---------------------------------------------------------------------------
# CHECK 1: ABI fingerprint compare against the committed baseline.
# ---------------------------------------------------------------------------
run_check fp_compare /bin/sh tools/abi_compare/run.sh compare \
    tests/abi/fingerprints/baseline.txt
if grep -q 'ABI fingerprint MISMATCH' "$OUT/fp_compare.stdout" 2>/dev/null; then
    MISMATCHES=$((MISMATCHES + 1))
    printf 'MISMATCH: ABI fingerprint differs from baseline\n'
fi

# ---------------------------------------------------------------------------
# CHECK 2: comprehensive ABI-compat suite (real C compiler + linker).
# ---------------------------------------------------------------------------
run_check abi_compat /bin/sh -c \
    'CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_compat.sh'
if grep -q '^FAIL ' "$OUT/abi_compat.stdout" 2>/dev/null; then
    printf 'NOTE: run_abi_compat.sh has failing assertions (see FINDINGS.md)\n'
fi

# ---------------------------------------------------------------------------
# CHECK 3: host-C vs C+ layout agreement.
# ---------------------------------------------------------------------------
run_check abi_layouts /bin/sh tests/abi/run_abi_layouts.sh
if [ "$(cat "$OUT/abi_layouts.rc")" != "0" ]; then
    # Diagnostic retry (not counted as a new check) with the restricted,
    # auditable PATH used by check 2, to classify the root cause.
    printf '\n===== DIAGNOSTIC RETRY: abi_layouts restricted PATH =====\n'
    env PATH=/usr/bin:/bin CPLUS_CLANG=/usr/bin/clang \
        sh tests/abi/run_abi_layouts.sh \
        > "$OUT/abi_layouts_retry.stdout" 2> "$OUT/abi_layouts_retry.stderr"
    printf 'RC=%s\n' "$?" > "$OUT/abi_layouts_retry.rc"
    tail -3 "$OUT/abi_layouts_retry.stdout"
fi

# ---------------------------------------------------------------------------
# CHECK 4: unified legacy-lab runner (54 labs).
# ---------------------------------------------------------------------------
run_check legacy_labs bash tests/abi/run_legacy_labs.sh
if grep -q '^FAIL ' "$OUT/legacy_labs.stdout" 2>/dev/null; then
    printf 'NOTE: run_legacy_labs.sh has failing labs (see FINDINGS.md)\n'
fi

# ---------------------------------------------------------------------------
# CHECK 5: core C source change audit (git diff cbcea57..HEAD).
# ---------------------------------------------------------------------------
CHECKS=$((CHECKS + 1))
printf '\n===== CHECK %d: modified core C files (git diff %s..HEAD) =====\n' \
    "$CHECKS" "$BASE"
git diff --name-status "$BASE"..HEAD -- libc/src/ runtime/ \
    > "$OUT/core_name_status.txt" 2>&1
git diff --diff-filter=M "$BASE"..HEAD -- libc/src/ runtime/ \
    > "$OUT/core_modified.diff" 2>&1
MOD_CORE=$(awk '$1 == "M" { c++ } END { print c+0 }' "$OUT/core_name_status.txt")
printf 'MODIFIED_CORE_FILES=%s\n' "$MOD_CORE"
cat "$OUT/core_name_status.txt"

# ---------------------------------------------------------------------------
# CHECK 6: precompiled C library linked with / without safe_c.h; symbol audit.
# ---------------------------------------------------------------------------
CHECKS=$((CHECKS + 1))
printf '\n===== CHECK %d: precompiled C library symbol invariance under safe_c.h =====\n' "$CHECKS"
mkdir -p "$OUT/precomp"
cat > "$OUT/precomp/probe_lib.h" <<'EOF'
#ifndef RED_PROBE_LIB_H
#define RED_PROBE_LIB_H
#include <stddef.h>
int red_probe_add(int, int);
size_t red_probe_copy(char *, const char *);
void *red_probe_alloc(size_t);
#endif
EOF
cat > "$OUT/precomp/probe_lib.c" <<'EOF'
#include <string.h>
#include <stdlib.h>
int red_probe_add(int a, int b) { return a + b; }
size_t red_probe_copy(char *d, const char *s) { strcpy(d, s); return strlen(d); }
void *red_probe_alloc(size_t n) { void *p = malloc(n); if (p) memset(p, 0, n); return p; }
EOF
cat > "$OUT/precomp/probe_app.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "probe_lib.h"
int main(void) {
    char buf[16];
    int r = red_probe_add(2, 3);
    size_t n = red_probe_copy(buf, "ok");
    void *p = red_probe_alloc(8);
    printf("r=%d n=%zu p=%d\n", r, n, p != 0);
    free(p);
    return 0;
}
EOF
P="$OUT/precomp"
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
/usr/bin/clang -O2 -std=c11 -c "$P/probe_lib.c" -o "$P/probe_lib.o"
/usr/bin/ar rcs "$P/libprobe.a" "$P/probe_lib.o"
/usr/bin/clang -O2 -std=c11 -I"$P" "$P/probe_app.c" "$P/libprobe.a" \
    -o "$P/app_plain"
/usr/bin/clang -O2 -std=c11 -I"$P" $INC -include stdlib/safe/safe_c.h \
    "$P/probe_app.c" "$P/libprobe.a" \
    "$ROOT/build/safe/libcplus_safecrt.a" "$ROOT/build/libc/libcplus_hardened.a" \
    -lm -pthread -o "$P/app_safe"
/usr/bin/nm -gU "$P/libprobe.a"   | grep 'red_probe' | awk '{print $2, $3}' \
    | sort > "$P/syms_lib.txt"
/usr/bin/nm -gU "$P/app_plain"    | grep 'red_probe' | awk '{print $2, $3}' \
    | sort > "$P/syms_plain.txt"
/usr/bin/nm -gU "$P/app_safe"     | grep 'red_probe' | awk '{print $2, $3}' \
    | sort > "$P/syms_safe.txt"
printf 'library symbols:\n'; cat "$P/syms_lib.txt"
printf 'plain-link library symbols:\n'; cat "$P/syms_plain.txt"
printf 'safe-link library symbols:\n'; cat "$P/syms_safe.txt"
if [ ! -s "$P/syms_lib.txt" ] || [ ! -s "$P/syms_plain.txt" ] || \
   [ ! -s "$P/syms_safe.txt" ]; then
    MISMATCHES=$((MISMATCHES + 1))
    printf 'SYMBOL CHECK: INCONCLUSIVE (empty nm symbol list -- see artifacts)\n'
elif /usr/bin/cmp -s "$P/syms_plain.txt" "$P/syms_safe.txt"; then
    printf 'SYMBOL CHECK: PASS (library symbols identical with/without safe_c.h)\n'
else
    MISMATCHES=$((MISMATCHES + 1))
    printf 'SYMBOL CHECK: MISMATCH\n'
    /usr/bin/diff -u "$P/syms_plain.txt" "$P/syms_safe.txt"
fi

# ---------------------------------------------------------------------------
printf '\n===== SUMMARY =====\n'
printf 'HEADLINE: RED20 abi %s %s %s\n' "$CHECKS" "$MISMATCHES" "$MOD_CORE"
printf 'checks=%s mismatches=%s modified_core_files=%s\n' \
    "$CHECKS" "$MISMATCHES" "$MOD_CORE"
