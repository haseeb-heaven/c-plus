#!/bin/sh
# ABI / source-compatibility evidence for the CHECKED ORDINARY-C LAYER.
#
# One unmodified legacy C source (original_c.c) is compiled three ways and run.
# The plain build uses /usr/bin/clang + system headers + system libc.  The safe
# build force-includes stdlib/safe/safe_c.h and links the real static archives.
# The disable build force-includes the same header with CPLUS_SAFE_C_DISABLE so
# it is declarations-only (no macro rewriting).  All three stdout streams must
# be byte-identical.  Layout/pointer sizes and unmangled exported symbol names
# are asserted independently.
#
# Ownership: writes ONLY under tests/abi/abi_safe_layer/out/.  Reads the two
# prebuilt archives but never rebuilds or edits any compiler/runtime file.
#
# SCOPE HONESTY: this is source/ABI-compatibility evidence.  Byte-identical
# output on bounded VALID inputs is NOT a safety claim and is NOT evidence that
# invalid inputs are rejected.  See README.md.
set -u

export PATH=/usr/bin:/bin
CC=/usr/bin/clang
NM=/usr/bin/nm
DIFF=/usr/bin/diff
SHA=/usr/bin/shasum
TIMEOUT=/usr/bin/timeout

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1
HERE=tests/abi/abi_safe_layer
OUT=$HERE/out
mkdir -p "$OUT"
LOG=$OUT/results.log
: > "$LOG"

PASS=0
FAIL=0

log() { printf '%s\n' "$*" >> "$LOG"; }
pass() { PASS=$((PASS + 1)); printf 'PASS %s\n' "$1"; log "PASS $1"; }
fail() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$1"; log "FAIL $1"; }

# step_cmd <label> <cmd...> : combined output -> LOG, exact command + rc logged.
step_cmd() {
    label=$1; shift
    log "CMD[$label] cwd=$ROOT: $*"
    "$@" >> "$LOG" 2>&1
    rc=$?
    log "RC[$label]=$rc"
    [ "$rc" -eq 0 ] && pass "$label" || fail "$label (rc=$rc)"
    return $rc
}

# capture_cmd <label> <outfile> <cmd...> : stdout -> outfile, stderr -> LOG.
capture_cmd() {
    label=$1; outfile=$2; shift 2
    log "CMD[$label] cwd=$ROOT: $* > $outfile"
    "$@" > "$outfile" 2>> "$LOG"
    rc=$?
    log "RC[$label]=$rc"
    [ "$rc" -eq 0 ] && pass "$label" || fail "$label (rc=$rc)"
    return $rc
}

# expect_present / expect_absent <label> <file> <grep -E pattern>
expect_present() {
    label=$1; file=$2; pat=$3
    log "CHECK[$label] (expect PRESENT): grep -E '$pat' $file"
    /usr/bin/grep -E "$pat" "$file" >/dev/null 2>&1
    rc=$?
    log "GREP_RC[$label]=$rc (0=present,1=absent)"
    [ "$rc" -eq 0 ] && pass "$label" || fail "$label"
}
expect_absent() {
    label=$1; file=$2; pat=$3
    log "CHECK[$label] (expect ABSENT): grep -E '$pat' $file"
    /usr/bin/grep -E "$pat" "$file" >/dev/null 2>&1
    rc=$?
    log "GREP_RC[$label]=$rc (0=present,1=absent)"
    [ "$rc" -eq 1 ] && pass "$label" || fail "$label"
}

log "=== abi_safe_layer : checked ordinary-C source/ABI compatibility ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "root=$ROOT"
log "PATH=$PATH"
log "arch=$(uname -m) os=$(uname -s)"
log "--- clang --version ---"
"$CC" --version >> "$LOG" 2>&1

# Prebuilt archives: record provenance, do not modify them.
log "--- archive provenance (read-only) ---"
ls -l "$ROOT/build/safe/libcplus_safecrt.a" "$ROOT/build/libc/libcplus_hardened.a" >> "$LOG" 2>&1
"$SHA" -a 256 "$ROOT/build/safe/libcplus_safecrt.a" "$ROOT/build/libc/libcplus_hardened.a" >> "$LOG" 2>&1
if [ ! -f "$ROOT/build/safe/libcplus_safecrt.a" ] || [ ! -f "$ROOT/build/libc/libcplus_hardened.a" ]; then
    echo "FATAL: prebuilt archives missing; this test reads (does not rebuild) them" >&2
    exit 2
fi

INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
LIBS="$ROOT/build/safe/libcplus_safecrt.a $ROOT/build/libc/libcplus_hardened.a -lm -pthread"
CFLAGS="-O0 -fno-builtin -std=c11 -Wall -Wextra"

# ---------------------------------------------------------------------------
# 1. Compile the SAME source three ways.
# ---------------------------------------------------------------------------
step_cmd compile_plain "$CC" $CFLAGS "$HERE/original_c.c" -o "$OUT/plain"
step_cmd compile_safe "$CC" $CFLAGS -include stdlib/safe/safe_c.h $INC \
    "$HERE/original_c.c" $LIBS -o "$OUT/safe"
step_cmd compile_disable "$CC" $CFLAGS -DCPLUS_SAFE_C_DISABLE \
    -include stdlib/safe/safe_c.h $INC "$HERE/original_c.c" -o "$OUT/disable"

# ---------------------------------------------------------------------------
# 2. Run all three with the same argument; capture stdout byte-for-byte.
# ---------------------------------------------------------------------------
run_prog() {
    label=$1; bin=$2; outfile=$3
    log "CMD[$label] cwd=$ROOT: $TIMEOUT 30 $bin $OUT/io_roundtrip.bin > $outfile"
    "$TIMEOUT" 30 "$bin" "$OUT/io_roundtrip.bin" > "$outfile" 2> "$OUT/${label}.stderr"
    rc=$?
    log "RC[$label]=$rc"
    [ "$rc" -eq 0 ] && pass "$label" || fail "$label (rc=$rc)"
    return $rc
}
run_prog run_plain "$OUT/plain" "$OUT/plain.stdout"
run_prog run_safe "$OUT/safe" "$OUT/safe.stdout"
run_prog run_disable "$OUT/disable" "$OUT/disable.stdout"

log "--- plain.stdout ---"; cat "$OUT/plain.stdout" >> "$LOG" 2>&1
log "--- safe.stdout ---"; cat "$OUT/safe.stdout" >> "$LOG" 2>&1
log "--- disable.stdout ---"; cat "$OUT/disable.stdout" >> "$LOG" 2>&1

# ---------------------------------------------------------------------------
# 3. Byte-identical stdout: exact diff + SHA-256.
# ---------------------------------------------------------------------------
capture_cmd diff_plain_vs_safe "$OUT/diff_plain_vs_safe.txt" \
    "$DIFF" -u "$OUT/plain.stdout" "$OUT/safe.stdout"
capture_cmd diff_plain_vs_disable "$OUT/diff_plain_vs_disable.txt" \
    "$DIFF" -u "$OUT/plain.stdout" "$OUT/disable.stdout"

if [ ! -s "$OUT/diff_plain_vs_safe.txt" ]; then
    pass "stdout_plain_equals_safe (diff empty)"
else
    fail "stdout_plain_equals_safe (diff non-empty)"
fi
if [ ! -s "$OUT/diff_plain_vs_disable.txt" ]; then
    pass "stdout_plain_equals_disable (diff empty)"
else
    fail "stdout_plain_equals_disable (diff non-empty)"
fi

log "--- stdout SHA-256 ---"
"$SHA" -a 256 "$OUT/plain.stdout" "$OUT/safe.stdout" "$OUT/disable.stdout" \
    > "$OUT/stdout_sha256.txt" 2>> "$LOG"
cat "$OUT/stdout_sha256.txt" >> "$LOG"

expect_present summary_plain "$OUT/plain.stdout" '^ABI_SAFE_LAYER_RESULT ok$'
expect_present summary_safe "$OUT/safe.stdout" '^ABI_SAFE_LAYER_RESULT ok$'
expect_present summary_disable "$OUT/disable.stdout" '^ABI_SAFE_LAYER_RESULT ok$'

# ---------------------------------------------------------------------------
# 4. Pointer size + layout assertions (from the program's own stdout).
# ---------------------------------------------------------------------------
for f in plain.stdout safe.stdout disable.stdout; do
    expect_present "ptr8_$f" "$OUT/$f" '^PTR_SIZE 8$'
    expect_present "layout_voidptr_$f" "$OUT/$f" '^LAYOUT void_ptr 8 8$'
    expect_present "layout_abisample_$f" "$OUT/$f" '^LAYOUT abi_sample '
    expect_present "offset_abisample_p_$f" "$OUT/$f" '^OFFSET abi_sample.p 16$'
done

# ---------------------------------------------------------------------------
# 5. Declarations-only control: macro table + actual token rewriting.
# ---------------------------------------------------------------------------
capture_cmd macros_enabled "$OUT/macros_enabled.txt" \
    "$CC" -dM -E -include stdlib/safe/safe_c.h $INC "$HERE/empty.c"
capture_cmd macros_disabled "$OUT/macros_disabled.txt" \
    "$CC" -dM -E -DCPLUS_SAFE_C_DISABLE -include stdlib/safe/safe_c.h $INC "$HERE/empty.c"

# `clang -dM -E` normalises whitespace in macro parameter lists, so the table
# shows e.g. `calloc(n,sz)` rather than the source spelling `calloc(n, sz)`.
for macro in 'malloc\(sz\)' 'calloc\(n,sz\)' 'memcpy\(d,s,n\)' \
             'strcpy\(d,s\)' 'fread\(p,sz,n,f\)' 'printf\(\.\.\.\)'; do
    expect_present "macro_${macro}_enabled" "$OUT/macros_enabled.txt" "^#define ${macro}"
    expect_absent  "macro_${macro}_disabled" "$OUT/macros_disabled.txt" "^#define ${macro}"
done
expect_present macro_ENABLE_flag_enabled "$OUT/macros_enabled.txt" '^#define CPLUS_SAFE_C_ENABLE 1$'
expect_absent  macro_ENABLE_flag_disabled "$OUT/macros_disabled.txt" '^#define CPLUS_SAFE_C_ENABLE 1$'

capture_cmd pp_rewrite_enabled "$OUT/pp_rewrite_enabled.i" \
    "$CC" -E -P -include stdlib/safe/safe_c.h $INC "$HERE/rewrite_probe.c"
capture_cmd pp_rewrite_disabled "$OUT/pp_rewrite_disabled.i" \
    "$CC" -E -P -DCPLUS_SAFE_C_DISABLE -include stdlib/safe/safe_c.h $INC "$HERE/rewrite_probe.c"

expect_present pp_malloc_s_enabled "$OUT/pp_rewrite_enabled.i" 'malloc_s\(8\)'
expect_absent  pp_malloc_s_disabled "$OUT/pp_rewrite_disabled.i" 'malloc_s\(8\)'
expect_absent  pp_malloc_plain_enabled "$OUT/pp_rewrite_enabled.i" '[^_]malloc\(8\)'
expect_present pp_malloc_plain_disabled "$OUT/pp_rewrite_disabled.i" '[^_]malloc\(8\)'
# The header declares cplus_memcpy_s_auto in BOTH modes, so match the rewritten
# CALL expression (macro body), not the bare declaration.
expect_present pp_memcpy_wrapper_enabled "$OUT/pp_rewrite_enabled.i" 'cplus_memcpy_s_auto\(\(q\)'
expect_absent  pp_memcpy_wrapper_disabled "$OUT/pp_rewrite_disabled.i" 'cplus_memcpy_s_auto\(\(q\)'

# ---------------------------------------------------------------------------
# 6. Exported symbol names: present, UNMANGLED, in both real archives.
# ---------------------------------------------------------------------------
capture_cmd nm_safecrt "$OUT/nm_safecrt.txt" "$NM" -gU "$ROOT/build/safe/libcplus_safecrt.a"
capture_cmd nm_libc "$OUT/nm_libc.txt" "$NM" -gU "$ROOT/build/libc/libcplus_hardened.a"

while read -r key sym; do
    case "$key" in
        ''|'#'*) continue ;;
    esac
    case "$key" in
        safecrt) nf="$OUT/nm_safecrt.txt" ;;
        libc)    nf="$OUT/nm_libc.txt" ;;
        *) printf 'FAIL bad expected-symbol key: %s\n' "$key"; FAIL=$((FAIL+1)); continue ;;
    esac
    pat="^[0-9A-Fa-f]+ T _${sym}"'$'
    expect_present "nm_${key}_${sym}" "$nf" "$pat"
done < "$HERE/expected_symbols.txt"

expect_absent nm_no_cxx_mangling_safecrt "$OUT/nm_safecrt.txt" ' T __Z'
expect_absent nm_no_cxx_mangling_libc "$OUT/nm_libc.txt" ' T __Z'

# ---------------------------------------------------------------------------
# 7. Executable call targets: plain C names vs the checked C wrappers, all
#    unmangled.  The static archives are linked INTO the executables, so the
#    wrapper symbols appear as DEFINED text symbols (`nm -gU`), while the
#    libSystem calls left for the dynamic linker appear in `nm -u`.  (On this
#    Darwin nm, `nm -u` prints bare symbol names with no "U" column.)
#
#    strlen/strnlen/fopen/fclose are intentionally NOT remapped by safe_c.h,
#    so they appear as libSystem calls in BOTH builds: direct evidence that
#    enabling the layer changes only the mapped names.
# ---------------------------------------------------------------------------
capture_cmd nm_defined_plain "$OUT/nm_defined_plain.txt" "$NM" -gU "$OUT/plain"
capture_cmd nm_defined_safe "$OUT/nm_defined_safe.txt" "$NM" -gU "$OUT/safe"
capture_cmd nm_defined_disable "$OUT/nm_defined_disable.txt" "$NM" -gU "$OUT/disable"
capture_cmd nm_undef_plain "$OUT/nm_undef_plain.txt" "$NM" -u "$OUT/plain"
capture_cmd nm_undef_safe "$OUT/nm_undef_safe.txt" "$NM" -u "$OUT/safe"
capture_cmd nm_undef_disable "$OUT/nm_undef_disable.txt" "$NM" -u "$OUT/disable"

# The checked wrappers are linked into the SAFE executable as plain, unmangled
# C symbols, and are absent from the plain and declarations-only executables.
for sym in malloc_s calloc_s realloc_s free_s cplus_memcpy_s_auto \
           cplus_memmove_s_auto cplus_memset_s_auto cplus_memcmp_s_auto \
           cplus_strcpy_s_auto cplus_strncpy_s_auto cplus_strcat_s_auto \
           cplus_strncat_s_auto cplus_printf cplus_fread cplus_fwrite; do
    pat="^[0-9A-Fa-f]+ T _${sym}"'$'
    expect_present "safe_defines_${sym}" "$OUT/nm_defined_safe.txt" "$pat"
    expect_absent  "plain_has_no_${sym}" "$OUT/nm_defined_plain.txt" "$pat"
    expect_absent  "disable_has_no_${sym}" "$OUT/nm_defined_disable.txt" "$pat"
done

# libSystem calls still unresolved in the PLAIN executable (ordinary C names;
# Apple clang emits the _chk fortified spellings for the memcpy/strcpy family).
for sym in malloc calloc realloc free memcmp printf fread fwrite \
           __memcpy_chk __memmove_chk __memset_chk __strcpy_chk \
           __strncpy_chk __strcat_chk __strncat_chk; do
    expect_present "plain_calls_${sym}" "$OUT/nm_undef_plain.txt" "^_${sym}"'$'
done

# Names safe_c.h does not touch remain ordinary libSystem calls in both builds.
for sym in strlen strnlen fopen fclose; do
    expect_present "unremapped_plain_${sym}" "$OUT/nm_undef_plain.txt" "^_${sym}"'$'
    expect_present "unremapped_safe_${sym}" "$OUT/nm_undef_safe.txt" "^_${sym}"'$'
    expect_present "unremapped_disable_${sym}" "$OUT/nm_undef_disable.txt" "^_${sym}"'$'
done

for f in nm_defined_plain.txt nm_defined_safe.txt nm_defined_disable.txt; do
    expect_absent "nm_exec_no_cxx_mangling_$f" "$OUT/$f" ' T __Z'
done

# ---------------------------------------------------------------------------
printf 'abi_safe_layer: %d passed, %d failed\n' "$PASS" "$FAIL"
log "RESULT: abi_safe_layer: $PASS passed, $FAIL failed"
log "Full command/exit-code/stdout transcript: $LOG"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
