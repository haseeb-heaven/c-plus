#!/bin/bash
# Reproducible conversion check for external_projects/cli_app (xdump).
# Builds xdump_modern.cp in strict mode, replays the eight fixed-input cases
# from ORIGINAL_EVIDENCE.md under `timeout 30`, and diffs stdout/stderr/exit
# against the original binary's recorded captures in build/runs/.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
CLIAPP="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$CLIAPP/../.." && pwd)"
SRC="$HERE/xdump_modern.cp"
BIN="$HERE/xdump_modern"
REF="$CLIAPP/build/runs"
FIXTURE="$CLIAPP/build/fixtures/sample.bin"
RES="$HERE/results"
mkdir -p "$RES"

cd "$ROOT" || exit 1

# Exact required build command form.
./build/c+ build "$SRC" --strict --sanitize=none -I. -o "$BIN" \
    > "$RES/build.stdout" 2> "$RES/build.stderr"
build_rc=$?

# Run one case. argv[0] is normalized to "./build/xdump" (the original's
# argv[0] in ORIGINAL_EVIDENCE.md) because getopt prints it in -Z diagnostics.
run_case() {
    name="$1"; shift
    stdin_file="$1"; shift
    (
        cd "$CLIAPP" || exit 1
        if [ "$stdin_file" = "-" ]; then
            timeout 30 bash -c 'exec -a ./build/xdump "$0" "$@"' "$BIN" "$@" \
                > "$RES/$name.stdout" 2> "$RES/$name.stderr"
        else
            timeout 30 bash -c 'exec -a ./build/xdump "$0" "$@"' "$BIN" "$@" \
                < "$stdin_file" > "$RES/$name.stdout" 2> "$RES/$name.stderr"
        fi
    )
    echo $? > "$RES/$name.exit"
}

run_case version          -                 -v
run_case help             -                 -h
run_case file_default     -                 build/fixtures/sample.bin
run_case file_width8      -                 -w 8 -g 4 build/fixtures/sample.bin
run_case file_skip_n      -                 -s 32 -n 16 build/fixtures/sample.bin
run_case stdin_default    "$FIXTURE"
run_case bad_flag         -                 -Z
run_case missing_file     -                 /definitely/not/a/file

checks=0; matched=0; differed=0
compare() {
    name="$1"
    for stream in stdout stderr exit; do
        checks=$((checks + 1))
        if diff -q "$REF/$name.$stream" "$RES/$name.$stream" >/dev/null 2>&1; then
            matched=$((matched + 1))
        else
            differed=$((differed + 1))
            echo "DIFF: $name.$stream" >&2
            diff -u "$REF/$name.$stream" "$RES/$name.$stream" >&2 || true
        fi
    done
}

for c in version help file_default file_width8 file_skip_n \
         stdin_default bad_flag missing_file; do
    compare "$c"
done

echo "CONVERT cli_app $checks $matched $differed"
[ "$build_rc" -eq 0 ] && [ "$differed" -eq 0 ]
