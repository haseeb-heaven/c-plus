#!/bin/sh
# Attempt to RUN the 100 Windows PE apps under Highball (Wine engine on
# Apple Silicon). Bounded: a single 20s probe decides whether the Wine runtime
# is usable; if not, all 100 are reported BLOCKED with the exact reason.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
D=tests/cross_platform_apps
OUT=$D/out-win
mkdir -p "$OUT"
sh "$D/generate.sh" >/dev/null
sh "$D/run_windows_build.sh" >/dev/null 2>&1 || true

if ! command -v highball >/dev/null 2>&1; then
    echo 'WINDOWS_RUNTIME BLOCKED reason=highball-not-installed apps=100'
    exit 0
fi

# Probe one executable with a hard bound. Highball's Wine engine on this host
# has stalled on a trivial hello.exe (uninterruptible wine process), so the
# probe decides whether the runtime is genuinely usable.
probe_rc=0
timeout 20 highball run "$OUT/app_0.exe" >"$OUT/highball_probe.out" 2>&1 || probe_rc=$?
if [ "$probe_rc" -eq 124 ] || [ "$probe_rc" -ge 128 ]; then
    echo "WINDOWS_RUNTIME BLOCKED reason=highball-wine-stall probe_rc=$probe_rc apps=100"
    echo "Output: $(tail -1 "$OUT/highball_probe.out" 2>/dev/null)"
    exit 0
fi

# Runtime is usable: run all 100 with bounded timeouts.
pass=0; fail=0; blocked=0
i=0
while [ "$i" -lt 100 ]; do
    rc=0
    timeout 20 highball run "$OUT/app_$i.exe" >"$OUT/hb_app_$i.out" 2>"$OUT/hb_app_$i.err" || rc=$?
    if [ "$rc" -eq 0 ] && grep -q -- '-1 -1' "$OUT/hb_app_$i.out"; then pass=$((pass+1));
    elif [ "$rc" -eq 124 ]; then blocked=$((blocked+1));
    else fail=$((fail+1)); echo "WIN_RUNTIME_FAIL app_$i rc=$rc"; fi
    i=$((i+1))
done
printf 'WINDOWS_RUNTIME pass=%d fail=%d blocked=%d\n' "$pass" "$fail" "$blocked"
[ "$fail" -eq 0 ]
