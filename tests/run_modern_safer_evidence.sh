#!/bin/sh
# One-command end-to-end evidence for every game/app proof and the measured
# scoped-memory-style bump benchmark. POSIX sh, bounded, no Python, no || true.
# Prints: GAMEAPP_EVIDENCE <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
cases=0; pass=0; fail=0
for suite in apps/run_all.sh proof/games_apps/run.sh \
             proof/real_apps_cross_platform/run.sh proof/modern_c_ports/run.sh \
             apps/games3d/run.sh apps/gui_app/run.sh \
             apps/cli_app/run.sh benchmarks/scoped_bump/run.sh \
             benchmarks/scoped_apps/run.sh \
             tests/stdlib/upstream_reuse/run_memory_safety.sh \
             tests/docs/run_doc_check.sh tests/runtime/run_print_test.sh \
             proof/games_gui_nuklear/run.sh \
             tests/stdlib/scoped_mem/run.sh \
             tests/stdlib/no_heap/run.sh \
             proof/all_apps_games_screenshots/run.sh; do
    cases=$((cases + 1))
    if timeout 1800 sh "$suite" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1)); echo "GAMEAPP_EVIDENCE FAIL $suite"
    fi
done
echo "GAMEAPP_EVIDENCE $cases $pass $fail"
[ "$fail" -eq 0 ]
