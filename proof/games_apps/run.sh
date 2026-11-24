#!/bin/sh
# Reproducible verifier for the games/apps proof binaries.
# POSIX sh, bounded, no Python, no `|| true`. Prints PROOF_GAMES <pass> <fail>.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$HERE" || exit 2
B=binaries
T=transcripts
pass=0; fail=0

ok()   { printf 'PROOF_CHECK PASS %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf 'PROOF_CHECK FAIL %s\n' "$1"; fail=$((fail+1)); }

# 1. hashes
if command -v shasum >/dev/null 2>&1; then
    (cd "$B" && shasum -a 256 -c SHA256SUMS.txt) >/tmp/proof_hash.out 2>&1
else
    (cd "$B" && sha256sum -c SHA256SUMS.txt) >/tmp/proof_hash.out 2>&1
fi
[ $? -eq 0 ] && ok binary_hashes || { bad binary_hashes; cat /tmp/proof_hash.out; }

# 2. binaries are executable Mach-O arm64 (informational; file is optional)
for f in scene3d_demo gui_app cli_calc; do
    [ -x "$B/$f" ] && ok "executable_$f" || bad "executable_$f"
done

# 3. re-run each binary and check exact markers + exit codes
run_case() {
    label=$1; marker=$2; want=$3; shift 3
    out=$(timeout 30 "$@" 2>&1); rc=$?
    printf '%s\n' "$out" >> "$T/verify.latest.txt"
    if [ "$rc" -eq "$want" ] && printf '%s\n' "$out" | grep -qF "$marker"; then
        ok "$label"
    else
        bad "$label"
        printf '  rc=%s want=%s output=' "$rc" "$want"
        printf '%s\n' "$out" | head -2
    fi
}

: > "$T/verify.latest.txt"
run_case games3d 'SCENE_DONE status=OK nodes=7 meshes=3' 0 "$B/scene3d_demo"
SDL_VIDEODRIVER=dummy run_case gui_describe 'GUI_RESULT describe:0,0,0 OK' 0 env SDL_VIDEODRIVER=dummy "$B/gui_app" describe 0
run_case gui_refuse 'GUI_RESULT describe:999,0,0 REFUSED' 0 "$B/gui_app" describe 999
run_case cli_add 'CLI add 2 3 5' 0 "$B/cli_calc" add 2 3
run_case cli_mul 'CLI mul 6 7 42' 0 "$B/cli_calc" mul 6 7
run_case cli_refuse 'CLI refuse unknown command 7' 2 "$B/cli_calc" bogus

printf 'PROOF_GAMES %d %d\n' "$((pass+fail))" "$fail"
[ "$fail" -eq 0 ]
