#!/bin/sh
# Verify the modern C+ conversion of game_a against the original's recorded
# evidence. Run from the repository root. Prints:
#   CONVERT game_a <checks> <matched> <differed>
# Exits nonzero on any unmatched behaviour.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

SRC=external_projects/game_a/cplus/snake_modern.cp
BIN=external_projects/game_a/cplus/snake_modern
ORIG=external_projects/game_a/build/snake
REC=external_projects/game_a/build/run.stdout
EXP_SHA=234d359e90fb4b5c0253ad2134cdb5f8cacc8f33db8394b7f5ab9019d428d282

checks=0
matched=0
differed=0

record() {
    checks=$((checks + 1))
    if [ "$1" = "0" ]; then
        matched=$((matched + 1))
    else
        differed=$((differed + 1))
        echo "DIFFER: $2" >&2
    fi
}

# 1. Strict build (the exact supported build line from the task).
./build/c+ build "$SRC" --strict --sanitize=none -I. \
    -Istdlib/safe -Ilibc/install-include \
    -o "$BIN" -- build/safe/libcplus_safecrt.a \
    build/libc/libcplus_hardened.a -lm -pthread \
    > external_projects/game_a/cplus/build.stdout \
    2> external_projects/game_a/cplus/build.stderr
record $? "strict C+ build"

# 2. Deterministic fixed input 'q' from ORIGINAL_EVIDENCE.md, under timeout 30.
printf 'q' | timeout 30 "$BIN" \
    > external_projects/game_a/cplus/run.stdout \
    2> external_projects/game_a/cplus/run.stderr
run_rc=$?
record $run_rc "run exit code ($run_rc)"

# 3. Byte-for-byte stdout match against the original's recorded output.
cmp -s external_projects/game_a/cplus/run.stdout "$REC"
record $? "stdout byte-for-byte vs recorded run.stdout"

# 4. SHA-256 of stdout matches the value recorded in ORIGINAL_EVIDENCE.md.
got_sha=$(shasum -a 256 external_projects/game_a/cplus/run.stdout | awk '{print $1}')
[ "$got_sha" = "$EXP_SHA" ]
record $? "stdout SHA-256 ($got_sha)"

# 5. stderr empty, as in the original run.
[ ! -s external_projects/game_a/cplus/run.stderr ]
record $? "stderr empty"

# 6-9. Additional deterministic inputs (all quit before the first rendered
# frame, so the time-seeded food position cannot affect stdout). Compare the
# conversion directly against the original evidence binary.
for input in 'q' 'qq' 'wq' 'dwq' '\033[Aq'; do
    printf "$input" | timeout 30 "$ORIG" \
        > /tmp/game_a_orig_case.out 2>/dev/null
    orig_rc=$?
    printf "$input" | timeout 30 "$BIN" \
        > /tmp/game_a_mod_case.out 2>/dev/null
    mod_rc=$?
    cmp -s /tmp/game_a_orig_case.out /tmp/game_a_mod_case.out && \
        [ "$orig_rc" = "$mod_rc" ]
    record $? "input '$input' vs original binary"
done

# 10. ABI: main is still the externally visible entry point, with the static
# helpers preserved as local symbols (no change to the original API surface).
nm "$BIN" | grep -q ' T _main$'
record $? "main exported"
nm "$BIN" | grep -q ' t _raw_on$' && nm "$BIN" | grep -q ' t _raw_off$'
record $? "raw_on/raw_off remain file-local"

echo "CONVERT game_a $checks $matched $differed"
[ "$differed" -eq 0 ]
