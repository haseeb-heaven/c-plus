#!/bin/sh
# Build and exercise the cli.hp calculator demo.
#
# Every case is checked against an exact expected stdout file and an exact
# exit code, with a 30s timeout per run.  No case is allowed to crash, hang,
# emit stderr or rely on the environment.  A Windows PE cross-compile check
# is compile-only: it never claims that Windows runtime behaviour was tested.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true
D=examples/cli_app
OUT=$D/out
mkdir -p "$OUT"
BIN=$OUT/cli_calc
LIBC=build/libc/libcplus_hardened.a

pass=0
fail=0
built=0

echo "== build: $D/cli_calc.cp (--default --no-werror --sanitize=none) =="
if timeout 180 ./build/c+ build "$D/cli_calc.cp" --default --no-werror --sanitize=none \
       -I. -o "$BIN" -- "$LIBC" > "$OUT/build.log" 2>&1; then
    built=1
else
    echo "BUILDFAIL cli_calc (see $OUT/build.log)"
    cat "$OUT/build.log"
    exit 1
fi
echo "BUILD ok"

# Fixed expected outputs. Generated here so this script is self-contained.
printf 'CLI no-command\n' > "$OUT/exp_empty"
printf 'CLI add 2 3 5\n' > "$OUT/exp_add"
printf 'CLI mul 6 7 42\n' > "$OUT/exp_mul"
cat > "$OUT/exp_help" <<'EOF'
Commands:
  add - add two integers (id=1)
  mul - multiply two integers (id=2)
  help - print help text (id=3)
Options:
  --verbose
  --tag=value
EOF
printf 'CLI refuse unknown command 7\n' > "$OUT/exp_unknown_cmd"
printf 'CLI refuse unknown option 6\n' > "$OUT/exp_unknown_opt"
printf 'CLI refuse too many arguments 8\n' > "$OUT/exp_too_many"
printf 'CLI refuse name or token too long 3\n' > "$OUT/exp_too_long"
printf 'CLI refuse invalid argument 2\n' > "$OUT/exp_invalid"
printf 'CLI bad-int\n' > "$OUT/exp_bad_int"
printf 'CLI args 1\n' > "$OUT/exp_few_args"
printf 'CLI overflow\n' > "$OUT/exp_overflow"
printf 'CLI add 4 5 9\nCLI verbose 1\n' > "$OUT/exp_verbose"
printf 'CLI mul 3 4 12\nCLI tag hello\n' > "$OUT/exp_tag"

# check <case-name> <expected-rc> <expected-file> [argv...]
check() {
    name=$1
    expected_rc=$2
    expected_file=$3
    shift 3
    rc=0
    timeout 30 "$BIN" "$@" > "$OUT/$name.out" 2> "$OUT/$name.err" || rc=$?
    if [ "$rc" -eq "$expected_rc" ] && \
       cmp -s "$OUT/$name.out" "$expected_file" && \
       [ ! -s "$OUT/$name.err" ]; then
        printf 'PASS %-16s rc=%s out=%s\n' "$name" "$rc" "$(cat "$OUT/$name.out" | tr '\n' '|')"
        pass=$((pass + 1))
    else
        printf 'FAIL %-16s rc=%s expected_rc=%s\n' "$name" "$rc" "$expected_rc"
        printf '  expected stdout: %s\n' "$(cat "$expected_file" | tr '\n' '|')"
        printf '  actual   stdout: %s\n' "$(cat "$OUT/$name.out" | tr '\n' '|')"
        printf '  actual   stderr: %s\n' "$(cat "$OUT/$name.err" | tr '\n' '|')"
        fail=$((fail + 1))
    fi
}

echo "== deterministic argv cases (timeout 30s each) =="
check empty          2 "$OUT/exp_empty"
check add            0 "$OUT/exp_add"       add 2 3
check mul            0 "$OUT/exp_mul"       mul 6 7
check help           0 "$OUT/exp_help"      help
check unknown_cmd    2 "$OUT/exp_unknown_cmd"  frobnicate
check unknown_opt    2 "$OUT/exp_unknown_opt"  add 1 2 --bogus
check too_many_args  2 "$OUT/exp_too_many"  add 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17
check too_long       2 "$OUT/exp_too_long"  "$(python3 -c 'print("A" * 10000, end="")')"
check empty_token    2 "$OUT/exp_invalid"  ""
check tag_no_value   2 "$OUT/exp_invalid"  --tag add 1 2
check verbose_eq     2 "$OUT/exp_invalid"  --verbose=x add 1 2
check negative_token 2 "$OUT/exp_unknown_opt" add -5 3
check bad_int        1 "$OUT/exp_bad_int"  add x 3
check few_args       1 "$OUT/exp_few_args" add 1
check overflow       1 "$OUT/exp_overflow" mul 1000000 1000000
check verbose        0 "$OUT/exp_verbose" --verbose add 4 5
check tag            0 "$OUT/exp_tag"     --tag=hello mul 3 4

# Windows PE cross-compile check. COMPILE ONLY. The demo source and stdlib/cli.hp
# are plain C, so the same source text is compiled to a PE/COFF object with the
# MinGW-w64 backend. Highball/Wine execution is BLOCKED here: no Windows runtime
# success is claimed or implied by this check.
wincross=skipped
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "== Windows PE cross-compile (compile-only, no runtime claim) =="
    if timeout 180 x86_64-w64-mingw32-gcc -x c -c -std=gnu17 \
           -I"$ROOT/runtime/include" -I"$ROOT" \
           "$D/cli_calc.cp" -o "$OUT/cli_calc.win.o" \
           > "$OUT/mingw.log" 2>&1; then
        wincross=ok
        echo "MINGW compile ok: $OUT/cli_calc.win.o"
        file "$OUT/cli_calc.win.o" | sed 's/^/  /'
    else
        wincross=fail
        echo "MINGW COMPILEFAIL (see $OUT/mingw.log)"
        tail -20 "$OUT/mingw.log"
    fi
else
    echo "== Windows PE cross-compile: x86_64-w64-mingw32-gcc unavailable; skipped =="
fi

printf 'CLI_APP_RESULT built=%d pass=%d fail=%d total=%d wincross=%s\n' \
    "$built" "$pass" "$fail" "$((pass + fail))" "$wincross"
[ "$built" -eq 1 ] && [ "$fail" -eq 0 ] && [ "$pass" -eq 17 ] && \
    [ "$wincross" != fail ]
