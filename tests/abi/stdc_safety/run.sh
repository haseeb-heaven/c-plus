#!/bin/sh
# stdc_safety: hostile-input evidence for the checked ordinary-C layer.
#
# One C99 source (hostile.c) is compiled two ways:
#   checked  : clang -include stdlib/safe/safe_c.h + the two prebuilt archives.
#              Ordinary spellings are remapped to checked implementations.
#   plain    : clang, no force-include, no archives: raw ISO C / libSystem.
#              Every hostile case runs in a FORKED child, so a canary
#              corruption or crash is contained and only its exit status is
#              recorded by the supervisor.
#
# The checked binary must print one `stdc_safety <api> <case> OK|REFUSED <canary_ok>`
# line per case and a final `STDC_SAFETY <cases> <refused> <canary_ok> <failures>`.
# The plain control is never allowed to damage this shell or the checked build.
#
# Writes ONLY under tests/abi/stdc_safety/out/.  Reads (never rebuilds) the two
# static archives.  Every executed subcommand is under a 60-second timeout.
set -u

export PATH=/usr/bin:/bin
CC=/usr/bin/clang
NM=/usr/bin/nm
GREP=/usr/bin/grep
WC=/usr/bin/wc
TAIL=/usr/bin/tail
CP=/usr/bin/cp

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1
HERE=tests/abi/stdc_safety
OUT=$HERE/out
mkdir -p "$OUT"
LOG=$OUT/run.log
: > "$LOG"

SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"

PASS=0
FAIL=0
log() { printf '%s\n' "$*" >> "$LOG"; }
say() { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS + 1)); say "PASS $1"; log "PASS $1"; }
bad()  { FAIL=$((FAIL + 1)); say "FAIL $1"; log "FAIL $1"; }

# All child programs are wrapped in a hard 60-second timeout.
if [ -x /usr/bin/timeout ]; then
    run_t() { /usr/bin/timeout 60 "$@"; }
else
    run_t() { perl -e 'alarm 60; exec @ARGV' "$@"; }
fi

step() {  # step <label> <cmd...>
    label=$1; shift
    log "CMD[$label] cwd=$ROOT: $*"
    "$@" >> "$LOG" 2>&1
    rc=$?
    log "RC[$label]=$rc"
    if [ "$rc" -eq 0 ]; then ok "$label"; else bad "$label (rc=$rc)"; fi
    return $rc
}

say "=== stdc_safety : hostile inputs, unchanged ABI ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "root=$ROOT"
log "clang=$($CC --version 2>&1 | head -1)"
log "arch=$(uname -m) os=$(uname -s)"

# --- prerequisites (read-only archives) -----------------------------------
if [ ! -f "$SAFECRT" ] || [ ! -f "$HARDENED" ]; then
    say "FATAL: prebuilt archives missing: $SAFECRT / $HARDENED"
    exit 2
fi
/usr/bin/shasum -a 256 "$SAFECRT" "$HARDENED" > "$OUT/archive_sha256.txt" 2>> "$LOG"
log "archive_sha256:"; sed 's/^/  /' "$OUT/archive_sha256.txt" >> "$LOG"

INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CFLAGS="-O0 -fno-builtin -std=c99 -Wall -Wextra -Wno-format-nonliteral -Wno-fortify-source -Wno-builtin-memcpy-chk-size"
SRC="$HERE/hostile.c"

# --- 1. compile CHECKED (force-include remaps ordinary spellings) ----------
log "=== build checked ==="
step compile_checked "$CC" $CFLAGS $INC -include stdlib/safe/safe_c.h \
    "$SRC" "$SAFECRT" "$HARDENED" -lm -pthread -o "$OUT/checked"

# --- 2. compile PLAIN control (no safe_c.h, no archives) -------------------
log "=== build plain ==="
step compile_plain "$CC" $CFLAGS "$SRC" -o "$OUT/plain"

# --- 3. compile optional UBSan-instrumented CHECKED binary -----------------
log "=== build checked-ubsan (optional) ==="
UBSAN_OK=1
"$CC" $CFLAGS -fsanitize=undefined $INC -include stdlib/safe/safe_c.h \
    "$SRC" "$SAFECRT" "$HARDENED" -lm -pthread -o "$OUT/checked-ubsan" \
    >> "$LOG" 2>&1 || UBSAN_OK=0
log "ubsan_build_available=$UBSAN_OK"
if [ "$UBSAN_OK" -eq 1 ]; then ok build_checked_ubsan; else say "SKIP build_checked_ubsan"; fi

# --- 4. run CHECKED under timeout -----------------------------------------
log "=== run checked ==="
log "CMD[run_checked] cwd=$ROOT: timeout 60 $OUT/checked $OUT"
run_t "$OUT/checked" "$OUT" > "$OUT/checked.stdout" 2> "$OUT/checked.stderr"
CHK_RC=$?
log "RC[run_checked]=$CHK_RC"
[ "$CHK_RC" -eq 0 ] && ok run_checked || bad "run_checked (rc=$CHK_RC)"

CASES=$($GREP -c '^stdc_safety ' "$OUT/checked.stdout" || true)
OKS=$($GREP -c '^stdc_safety .* OK 1$' "$OUT/checked.stdout" || true)
REFUSED=$($GREP -c '^stdc_safety .* REFUSED 1$' "$OUT/checked.stdout" || true)
BADCAN=$($GREP -c '^stdc_safety .* 0$' "$OUT/checked.stdout" || true)
FINAL=$($TAIL -n 1 "$OUT/checked.stdout")
log "checked: cases=$CASES ok=$OKS refused=$REFUSED bad_canary=$BADCAN final=[$FINAL]"

[ "$CASES" -eq 64 ]    && ok checked_case_count || bad "checked_case_count ($CASES)"
[ "$OKS" -eq 16 ]      && ok checked_valid_count || bad "checked_valid_count ($OKS)"
[ "$REFUSED" -eq 48 ]  && ok checked_refused_count || bad "checked_refused_count ($REFUSED)"
[ "$BADCAN" -eq 0 ]    && ok checked_all_canaries_intact || bad "checked_all_canaries_intact ($BADCAN)"
[ "$FINAL" = "STDC_SAFETY 64 48 64 0" ] && ok checked_final_summary || bad "checked_final_summary ($FINAL)"

# every one of the 16 APIs must contribute one REFUSED case per hostile case
for api in memcpy memmove memset strcpy strncpy strcat strncat printf fprintf fopen fread fwrite malloc calloc realloc free; do
    n=$($GREP -c "^stdc_safety $api .* REFUSED 1$" "$OUT/checked.stdout" || true)
    [ "$n" -eq 3 ] && ok "checked_refusals_$api" || bad "checked_refusals_$api ($n)"
done

# --- 5. run PLAIN control (separate subprocess; hostiles isolated by fork) --
log "=== run plain (subprocess only) ==="
log "CMD[run_plain] cwd=$ROOT: timeout 60 $OUT/plain $OUT"
run_t "$OUT/plain" "$OUT" > "$OUT/plain.stdout" 2> "$OUT/plain.stderr"
PLAIN_RC=$?
log "RC[run_plain]=$PLAIN_RC"
[ "$PLAIN_RC" -eq 0 ] && ok plain_supervisor_exit || bad "plain_supervisor_exit (rc=$PLAIN_RC)"

PCASES=$($GREP -c '^plain ' "$OUT/plain.stdout" || true)
PCORRUPT=$($GREP -c 'canary=0' "$OUT/plain.stdout" || true)
PCRASH=$($GREP -c 'CRASH signal=' "$OUT/plain.stdout" || true)
PDIFF=$($GREP -c 'DIFF$' "$OUT/plain.stdout" || true)
log "plain: case_lines=$PCASES canary_corrupted=$PCORRUPT crashes=$PCRASH diffs=$PDIFF"
[ "$PCASES" -ge 64 ] && ok plain_case_lines || bad "plain_case_lines ($PCASES)"
[ "$PCORRUPT" -gt 0 ] && ok plain_canary_corruption || bad "plain_canary_corruption ($PCORRUPT)"
[ "$PCRASH" -gt 0 ] && ok plain_crashes || bad "plain_crashes ($PCRASH)"
[ "$PDIFF" -gt 0 ] && ok plain_divergence || bad "plain_divergence ($PDIFF)"

# --- 6. run UBSan checked binary ------------------------------------------
if [ "$UBSAN_OK" -eq 1 ]; then
    log "=== run checked-ubsan ==="
    run_t "$OUT/checked-ubsan" "$OUT" > "$OUT/ubsan.stdout" 2> "$OUT/ubsan.stderr"
    UB_RC=$?
    log "RC[run_ubsan]=$UB_RC"
    UB_FINAL=$($TAIL -n 1 "$OUT/ubsan.stdout")
    UB_ERRS=$($GREP -c 'runtime error' "$OUT/ubsan.stderr" || true)
    log "ubsan: final=[$UB_FINAL] runtime_errors=$UB_ERRS"
    [ "$UB_RC" -eq 0 ] && ok run_checked_ubsan || bad "run_checked_ubsan (rc=$UB_RC)"
    [ "$UB_FINAL" = "STDC_SAFETY 64 48 64 0" ] && ok ubsan_final_summary || bad "ubsan_final_summary ($UB_FINAL)"
    [ "$UB_ERRS" -eq 0 ] && ok ubsan_no_runtime_errors || bad "ubsan_no_runtime_errors ($UB_ERRS)"
fi

# --- 7. ABI evidence: plain, unmangled exported symbols --------------------
log "=== ABI symbol audit (read-only) ==="
"$NM" -gU "$SAFECRT" > "$OUT/nm_safecrt.txt" 2>> "$LOG"
"$NM" -gU "$HARDENED" > "$OUT/nm_libc.txt" 2>> "$LOG"
"$NM" -gU "$OUT/checked" > "$OUT/nm_checked.txt" 2>> "$LOG"
"$NM" -gU "$OUT/plain" > "$OUT/nm_plain.txt" 2>> "$LOG"
ABI_FAIL=0
for sym in malloc_s calloc_s realloc_s free_s free_checked_s cplus_memcpy_s_auto \
           cplus_memmove_s_auto cplus_memset_s_auto cplus_strcpy_s_auto \
           cplus_strncpy_s_auto cplus_strcat_s_auto cplus_strncat_s_auto \
           cplus_fread_s cplus_fwrite_s cplus_fopen_s cplus_printf_s \
           cplus_fprintf_s; do
    if $GREP -Eq "^[0-9A-Fa-f]+ T _${sym}$" "$OUT/nm_safecrt.txt" || \
       $GREP -Eq "^[0-9A-Fa-f]+ T _${sym}$" "$OUT/nm_libc.txt"; then
        PASS=$((PASS + 1)); log "PASS abi_symbol_$sym"
    else
        bad "abi_symbol_$sym"; ABI_FAIL=1
    fi
    # must appear in the checked executable, never as a C++ mangled name
    if $GREP -Eq "^[0-9A-Fa-f]+ T _${sym}$" "$OUT/nm_checked.txt"; then
        PASS=$((PASS + 1)); log "PASS abi_linked_$sym"
    else
        bad "abi_linked_$sym"; ABI_FAIL=1
    fi
done
if $GREP -Eq ' T __Z' "$OUT/nm_checked.txt"; then bad abi_no_cxx_mangling; else ok abi_no_cxx_mangling; fi

# --- 8. contrast table on stdout ------------------------------------------
say ""
say "plain-vs-checked contrast (from the captured stdout files):"
say "  api          checked(valid/null/small/huge)   plain hostile outcomes"
for api in memcpy memmove memset strcpy strncpy strcat strncat printf fprintf fopen fread fwrite malloc calloc realloc free; do
    c=$($GREP "^stdc_safety $api " "$OUT/checked.stdout" | /usr/bin/sed -e 's/^stdc_safety [^ ]* [^ ]* //' | /usr/bin/tr '\n' ',')
    p=$($GREP "^plain $api " "$OUT/plain.stdout" | /usr/bin/sed -n -e 's/^plain [^ ]* \([^ ]*\) .*\(canary=[01]\) .*/\1:\2/p' | /usr/bin/tr '\n' ',')
    say "  $(printf '%-12s' "$api") $c  <-  $p"
done
say ""
say "Checked: $OKS valid successes, $REFUSED refusals, all 64 canaries intact, 0 failures."
say "Plain:   $PCORRUPT canary corruptions, $PCRASH crashes/aborts, $PDIFF divergent hostile cases."
say "Full command/exit/stdout transcript: $OUT/run.log"
say "stdc_safety: $PASS passed, $FAIL failed"
log "RESULT: stdc_safety: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
