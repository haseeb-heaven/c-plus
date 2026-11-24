#!/bin/sh
# =============================================================================
# proof/real_apps_cross_platform/run.sh
#
# Self-contained verifier for the cross-platform proof folder.  It verifies
# artifacts only; it never builds or modifies them.  Pure POSIX sh, bounded,
# restricted PATH, no interpreter dependency, no unconditional-success
# escape hatch, no machine-specific absolute paths (everything is relative to
# this script's directory).
#
# Verifies:
#   1. binaries/macos/SHA256SUMS.txt and binaries/windows/SHA256SUMS.txt
#   2. screenshots/SHA256SUMS.txt (integrity of the rendered terminal images)
#   3. file(1) types: 8 Mach-O arm64 + 8 PE32+ console + 2 PE32+ DLL
#      (only libwinpthread-1.dll and libgcc_s_seh-1.dll are required; the
#       larger libstdc++-6.dll was tested and found unnecessary)
#   4. exact final marker + exit-0 header in all 16 captured transcripts
#   5. deterministic RE-RUNS: cli_app "add 2 3" and games3d on BOTH platforms
#      (Windows via `highball run -v`, MinGW DLLs alongside the exe)
#   6. SVG well-formedness via xmllint when available (existence otherwise)
#
# Overall bound: OVERALL_LIMIT seconds.  Every external command is passed a
# timeout capped by the remaining wall-clock budget.  Outcome is one line:
#   CROSS_PROOF <checks> <pass> <fail>
# Exit status is nonzero if and only if fail > 0.
# =============================================================================
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
cd "$HERE" || exit 1

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH

OVERALL_LIMIT=900
DEADLINE=$(( $(date +%s) + OVERALL_LIMIT ))
TMPF="$HERE/.cross_proof.$$.err"
trap 'rm -f "$TMPF"' 0 1 2 3 15

checks=0
pass=0
fail=0

ok() {
    checks=$((checks + 1))
    pass=$((pass + 1))
    printf 'PASS %s\n' "$1"
}

bad() {
    checks=$((checks + 1))
    fail=$((fail + 1))
    printf 'FAIL %s %s\n' "$1" "$2"
}

# Seconds left in the global budget (never negative).
remaining() {
    left=$((DEADLINE - $(date +%s)))
    if [ "$left" -gt 0 ]; then
        printf '%s\n' "$left"
    else
        printf '0\n'
    fi
}

# run_in <dir> <seconds> <command> [args...] -- bounded run from <dir>.
run_in() {
    dir=$1
    cap=$2
    shift 2
    left=$(remaining)
    if [ "$left" -le 0 ]; then
        return 124
    fi
    if [ "$cap" -gt "$left" ]; then
        cap=$left
    fi
    (cd "$dir" && timeout "$cap" "$@")
}

APPS='math3d scene3d mesh3d widgets cli games3d gui_app cli_app'

# expected_marker <platform> <app> -- exact final line captured in the proof.
expected_marker() {
    case "$1/$2" in
        macos/math3d|windows/math3d)     printf 'MATH3D 36 0' ;;
        macos/scene3d|windows/scene3d)   printf 'SCENE3D 79 0' ;;
        macos/mesh3d|windows/mesh3d)     printf 'drop 1 1' ;;
        macos/widgets|windows/widgets)   printf 'WIDGETS 144 0' ;;
        macos/cli|windows/cli)           printf 'CLI_TEST 27 27 0' ;;
        macos/games3d|windows/games3d)   printf 'SCENE_DONE status=OK nodes=7 meshes=3' ;;
        macos/gui_app)                   printf 'GUI_RESULT describe:0,0,0 OK' ;;
        windows/gui_app)                 printf 'GUI_RESULT describe:0,0,0 REFUSED 108' ;;
        macos/cli_app|windows/cli_app)   printf 'CLI add 2 3 5' ;;
    esac
}

printf 'CROSS_PROOF verifier: overall bound %ss, PATH=%s\n' "$OVERALL_LIMIT" "$PATH"

# --- 1+2. SHA-256 integrity -------------------------------------------------
for pair in 'binaries/macos:sha256-macos' \
            'binaries/windows:sha256-windows' \
            'screenshots:sha256-screenshots'; do
    dir=${pair%%:*}
    label=${pair##*:}
    if [ ! -f "$dir/SHA256SUMS.txt" ]; then
        bad "$label" 'SHA256SUMS.txt missing'
        continue
    fi
    out=$(cd "$dir" && shasum -a 256 -c SHA256SUMS.txt 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        ok "$label"
    else
        detail=$(printf '%s\n' "$out" | grep -i FAILED | head -1)
        bad "$label" "rc=$rc $detail"
    fi
done

# --- 3. file(1) types -------------------------------------------------------
for app in $APPS; do
    desc=$(file -b "binaries/macos/$app" 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ] && printf '%s\n' "$desc" | grep -qF 'Mach-O 64-bit executable arm64'; then
        ok "type-macos-$app"
    else
        bad "type-macos-$app" "$desc"
    fi
done

for app in $APPS; do
    desc=$(file -b "binaries/windows/$app.exe" 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ] && printf '%s\n' "$desc" | grep -qF 'PE32+ executable (console) x86-64'; then
        ok "type-windows-$app.exe"
    else
        bad "type-windows-$app.exe" "$desc"
    fi
done

for dll in libgcc_s_seh-1.dll libwinpthread-1.dll; do
    desc=$(file -b "binaries/windows/$dll" 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ] && printf '%s\n' "$desc" | grep -qF 'PE32+ executable (DLL)'; then
        ok "type-windows-$dll"
    else
        bad "type-windows-$dll" "$desc"
    fi
done

# --- 4. captured transcripts: exact final marker + exit-0 header -----------
for plat in macos windows; do
    for app in $APPS; do
        f="transcripts/$plat/$app.txt"
        expected=$(expected_marker "$plat" "$app")
        if [ ! -f "$f" ]; then
            bad "transcript-$plat-$app" 'transcript missing'
            continue
        fi
        if ! grep -qF "$expected" "$f"; then
            bad "transcript-$plat-$app" "expected marker '$expected' absent"
            continue
        fi
        if grep -qE '^# (EXIT|Exit code): 0' "$f"; then
            ok "transcript-$plat-$app"
        else
            bad "transcript-$plat-$app" 'no exit-0 header'
        fi
    done
done

# gui_app is captured twice on Windows: default (no args -> REFUSED 108) and
# the invocation that matches macOS (`describe 0` -> OK).  Check the matching
# one too so the two platforms can be compared like-for-like.
f='transcripts/windows/gui_app_describe0.txt'
if [ ! -f "$f" ]; then
    bad 'transcript-windows-gui_app_describe0' 'transcript missing'
elif grep -qF 'GUI_RESULT describe:0,0,0 OK' "$f" \
     && grep -qE '^# Exit code: 0' "$f"; then
    ok 'transcript-windows-gui_app_describe0'
else
    bad 'transcript-windows-gui_app_describe0' 'marker or exit-0 header absent'
fi

# --- 5. deterministic re-runs ----------------------------------------------
# macOS cli_app: exact one-line stdout, empty stderr, exit 0.
: > "$TMPF"
out=$(run_in 'binaries/macos' 30 ./cli_app add 2 3 2>"$TMPF")
rc=$?
if [ "$rc" -eq 0 ] && [ ! -s "$TMPF" ] && [ "$out" = 'CLI add 2 3 5' ]; then
    ok 'rerun-macos-cli_app'
else
    bad 'rerun-macos-cli_app' "rc=$rc stdout=[$out]"
fi

# macOS games3d: final marker + self-checksum, empty stderr, exit 0.
: > "$TMPF"
out=$(run_in 'binaries/macos' 30 ./games3d 2>"$TMPF")
rc=$?
if [ "$rc" -eq 0 ] && [ ! -s "$TMPF" ] \
   && printf '%s\n' "$out" | grep -qF 'SCENE_DONE status=OK nodes=7 meshes=3' \
   && printf '%s\n' "$out" | grep -qF 'CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40'; then
    ok 'rerun-macos-games3d'
else
    bad 'rerun-macos-games3d' "rc=$rc"
fi

# Windows: locate the Highball CLI without a hard-coded user path.  An
# explicit HIGHBALL environment variable wins; then PATH; then the standard
# per-user install location resolved through $HOME.
HB=${HIGHBALL:-}
if [ -z "$HB" ]; then
    HB=$(command -v highball 2>/dev/null)
fi
if [ -z "$HB" ] && [ -x "$HOME/.local/bin/highball" ]; then
    HB="$HOME/.local/bin/highball"
fi

if [ -z "$HB" ]; then
    bad 'rerun-windows-cli_app.exe' 'highball CLI not found; set HIGHBALL to its path'
    bad 'rerun-windows-games3d.exe' 'highball CLI not found; set HIGHBALL to its path'
else
    # verbose (-v) is required here; MinGW DLLs sit next to the .exe in cwd.
    : > "$TMPF"
    out=$(run_in 'binaries/windows' 240 "$HB" run -v ./cli_app.exe add 2 3 2>"$TMPF")
    rc=$?
    if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -qF 'CLI add 2 3 5'; then
        ok 'rerun-windows-cli_app.exe'
    else
        bad 'rerun-windows-cli_app.exe' "rc=$rc"
    fi

    : > "$TMPF"
    out=$(run_in 'binaries/windows' 240 "$HB" run -v ./games3d.exe 2>"$TMPF")
    rc=$?
    if [ "$rc" -eq 0 ] \
       && printf '%s\n' "$out" | grep -qF 'SCENE_DONE status=OK nodes=7 meshes=3' \
       && printf '%s\n' "$out" | grep -qF 'CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40'; then
        ok 'rerun-windows-games3d.exe'
    else
        bad 'rerun-windows-games3d.exe' "rc=$rc"
    fi
fi

# --- 6. SVG existence + optional XML well-formedness -----------------------
HAVE_XML=0
if command -v xmllint >/dev/null 2>&1; then
    HAVE_XML=1
fi
for plat in macos windows; do
    for app in $APPS; do
        f="screenshots/$app-$plat.svg"
        if [ ! -f "$f" ]; then
            bad "svg-$app-$plat" 'SVG missing'
            continue
        fi
        if [ "$HAVE_XML" -eq 1 ]; then
            msg=$(xmllint --noout "$f" 2>&1)
            rc=$?
            if [ "$rc" -eq 0 ]; then
                ok "svg-$app-$plat"
            else
                bad "svg-$app-$plat" "xmllint rc=$rc $msg"
            fi
        else
            ok "svg-present-$app-$plat (xmllint unavailable)"
        fi
    done
done

printf 'CROSS_PROOF %d %d %d\n' "$checks" "$pass" "$fail"
if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
