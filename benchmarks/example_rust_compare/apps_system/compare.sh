#!/bin/bash
# app/system example -> safe-Rust behavioral comparison.
# Prints: EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
# Only writes under benchmarks/example_rust_compare/apps_system/.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
BUILD="$HERE/build"
OUT="$HERE/out"
mkdir -p "$BUILD" "$OUT"

CLANG=/usr/bin/clang
CPLUS="$ROOT/build/c+"
TIMEOUT=/usr/bin/timeout
RUSTC=${RUSTC:-rustc}

CPUS=0
MATCHES=0

run_case() {
    # run_case <tag> <bin> <cwd> <input-file> <stdout-file> <stderr-file>
    local tag=$1 bin=$2 cwd=$3 input=$4 so=$5 se=$6
    ( cd "$cwd" && HOME="$PWD" NO_COLOR=1 "$TIMEOUT" 30 "$bin" < "$input" > "$so" 2> "$se" )
    echo $? > "$OUT/$tag.exit"
}

compare_streams() {
    # compare_streams <tag> ; returns 0 on byte-identical stdout+stderr+exit
    local tag=$1 rc=0
    diff -u "$OUT/$tag.orig.stdout" "$OUT/$tag.rust.stdout" > "$OUT/$tag.stdout.diff" || rc=1
    diff -u "$OUT/$tag.orig.stderr" "$OUT/$tag.rust.stderr" > "$OUT/$tag.stderr.diff" || rc=1
    if ! cmp -s "$OUT/$tag.orig.exit" "$OUT/$tag.rust.exit"; then rc=1; fi
    return $rc
}

echo "== toolchains =="
"$CLANG" --version | head -1
"$RUSTC" --version
"$CPLUS" --version 2>&1 | head -2

# ---------------------------------------------------------------- cgame ---
echo "== build cgame (original: clang) =="
$CLANG -std=c11 -O2 -o "$BUILD/orig_cgame" "$ROOT/examples/apps/cgame.c" \
    > "$OUT/cgame.orig.build.log" 2>&1
C_BUILD=$?
echo "== build cgame (Rust) =="
$RUSTC -O "$HERE/cgame.rs" -o "$BUILD/rust_cgame" > "$OUT/cgame.rust.build.log" 2>&1
R_BUILD=$?
CPUS=$((CPUS + 1))
if [ "$C_BUILD" -ne 0 ] || [ "$R_BUILD" -ne 0 ]; then
    echo "EXAMPLE_COMPARE cgame BLOCKED build failed (original=$C_BUILD rust=$R_BUILD)"
else
    C_OK=1
    for t in quit invalid reset levels_back libmenu quiz_wrong quiz_correct libex0; do
        case $t in
            quit)         printf 'q\n' > "$OUT/in.cgame.$t" ;;
            invalid)      printf 'x\nq\n' > "$OUT/in.cgame.$t" ;;
            reset)        printf 'r\nq\n' > "$OUT/in.cgame.$t" ;;
            levels_back)  printf '1\nb\nq\n' > "$OUT/in.cgame.$t" ;;
            libmenu)      printf '2\nb\nq\n' > "$OUT/in.cgame.$t" ;;
            quiz_wrong)   printf '1\n1\n\nwrong\nwrong\nwrong\nwrong\nb\nq\n' > "$OUT/in.cgame.$t" ;;
            quiz_correct) printf '1\n1\n\nb\nmain\nc\n3\ns\nb\nq\n' > "$OUT/in.cgame.$t" ;;
            libex0)       printf '2\n0\n\nq\nb\nq\n' > "$OUT/in.cgame.$t" ;;
        esac
        rm -rf "$BUILD/cg_o_$t" "$BUILD/cg_r_$t"
        mkdir -p "$BUILD/cg_o_$t" "$BUILD/cg_r_$t"
        run_case "cgame.$t.orig" "$BUILD/orig_cgame" "$BUILD/cg_o_$t" "$OUT/in.cgame.$t" \
            "$OUT/cgame.$t.orig.stdout" "$OUT/cgame.$t.orig.stderr"
        run_case "cgame.$t.rust" "$BUILD/rust_cgame" "$BUILD/cg_r_$t" "$OUT/in.cgame.$t" \
            "$OUT/cgame.$t.rust.stdout" "$OUT/cgame.$t.rust.stderr"
        if compare_streams "cgame.$t"; then echo "  cgame/$t: MATCH"; else echo "  cgame/$t: DIFFERENT"; C_OK=0; fi
    done
    if [ "$C_OK" -eq 1 ]; then
        MATCHES=$((MATCHES + 1))
        echo "EXAMPLE_COMPARE cgame MATCH 8/8 deterministic menu/quiz/sandbox paths byte-identical (stdout, stderr, exit); deeper level content not modeled"
    else
        echo "EXAMPLE_COMPARE cgame DIFFERENT see out/cgame.*.stdout.diff"
    fi
fi

# --------------------------------------------------------------- student ---
echo "== build student (original: c+ legacy-c, sanitize=none) =="
# NOTE: --strict/--default on this branch fail CPLUS-LIFETIME analysis on the
# legacy out-parameter code; --legacy-c is required. Exact logs in out/.
$CPLUS build "$ROOT/examples/apps/student.cp" -o "$BUILD/orig_students" \
    --legacy-c --sanitize=none > "$OUT/student.orig.build.log" 2>&1
C_BUILD=$?
echo "== build student (Rust) =="
$RUSTC -O "$HERE/student.rs" -o "$BUILD/rust_student" > "$OUT/student.rust.build.log" 2>&1
R_BUILD=$?
CPUS=$((CPUS + 1))
if [ "$C_BUILD" -ne 0 ] || [ "$R_BUILD" -ne 0 ]; then
    echo "EXAMPLE_COMPARE student BLOCKED build failed (original=$C_BUILD rust=$R_BUILD)"
else
    S_OK=1
    printf '1\n1\nAlice\n20\n95.5\n2\n3\n1\n4\n1\nAlicia\n21\n88.25\n5\n1\n9\n0\n' > "$OUT/in.student.crud"
    printf '1\n1\nBob\n30\n88.25\n1\n2\nCarol\n25\n0.5\n0\n2\n0\n' > "$OUT/in.student.persist"
    for t in crud persist; do
        rm -rf "$BUILD/st_o_$t" "$BUILD/st_r_$t"
        mkdir -p "$BUILD/st_o_$t" "$BUILD/st_r_$t"
        run_case "student.$t.orig" "$BUILD/orig_students" "$BUILD/st_o_$t" "$OUT/in.student.$t" \
            "$OUT/student.$t.orig.stdout" "$OUT/student.$t.orig.stderr"
        run_case "student.$t.rust" "$BUILD/rust_student" "$BUILD/st_r_$t" "$OUT/in.student.$t" \
            "$OUT/student.$t.rust.stdout" "$OUT/student.$t.rust.stderr"
        if compare_streams "student.$t"; then
            if cmp -s "$BUILD/st_o_$t/students.db" "$BUILD/st_r_$t/students.db"; then
                echo "  student/$t: MATCH (stdout, stderr, exit, students.db)"
            else
                echo "  student/$t: DIFFERENT (students.db)"; S_OK=0
            fi
        else
            echo "  student/$t: DIFFERENT"; S_OK=0
        fi
    done
    if [ "$S_OK" -eq 1 ]; then
        MATCHES=$((MATCHES + 1))
        echo "EXAMPLE_COMPARE student MATCH 2/2 deterministic CRUD+persistence paths byte-identical (stdout, stderr, exit, students.db)"
    else
        echo "EXAMPLE_COMPARE student DIFFERENT see out/student.*.stdout.diff"
    fi
fi

# --------------------------------------------------------------- macpulse ---
echo "== build macpulse (original unpatched, --strict --sanitize=none) =="
$CPLUS build "$ROOT/examples/system/macpulse.cp" -o "$BUILD/orig_macpulse_unpatched" \
    --strict --sanitize=none > "$OUT/macpulse.unpatched.build.log" 2>&1
MAC_UNPATCHED=$?
echo "  unpatched original build exit: $MAC_UNPATCHED (see out/macpulse.unpatched.build.log)"
# Minimal, documented compatibility patch (no examples/ edit): old vector API
# push/len -> push_back/size, and rename a C89-scope-colliding loop variable.
ln -sfn ../../../stdlib "$HERE/stdlib"
cp "$ROOT/examples/system/macpulse.cp" "$HERE/macpulse_patched.cp"
perl -pi -e 's/v->len\(\)/v->size()/g; s/\.push\(/.push_back(/g;' "$HERE/macpulse_patched.cp"
perl -0pi -e 's/for \(int i = 0; i < this->proc_count; \+\+i\)\n            this->top\[i\] = entries\[i\];/for (int j = 0; j < this->proc_count; ++j)\n            this->top[j] = entries[j];/' "$HERE/macpulse_patched.cp"
echo "== build macpulse (minimally patched copy, legacy-c) =="
( cd "$HERE" && "$CPLUS" build macpulse_patched.cp -o "$BUILD/orig_macpulse" \
    --legacy-c --sanitize=none ) > "$OUT/macpulse.orig.build.log" 2>&1
C_BUILD=$?
echo "== build macpulse (Rust) =="
$RUSTC -O "$HERE/macpulse.rs" -o "$BUILD/rust_macpulse" > "$OUT/macpulse.rust.build.log" 2>&1
R_BUILD=$?
CPUS=$((CPUS + 1))
if [ "$R_BUILD" -ne 0 ]; then
    echo "EXAMPLE_COMPARE macpulse BLOCKED rust build failed"
elif [ "$C_BUILD" -ne 0 ]; then
    echo "EXAMPLE_COMPARE macpulse BLOCKED original source does not compile on this branch (see out/macpulse.unpatched.build.log and out/macpulse.orig.build.log); live interactive path requires TTY+Mach/libproc"
else
    : > "$OUT/in.macpulse.empty"
    rm -rf "$BUILD/mac_o" "$BUILD/mac_r"
    mkdir -p "$BUILD/mac_o" "$BUILD/mac_r"
    run_case "macpulse.orig" "$BUILD/orig_macpulse" "$BUILD/mac_o" "$OUT/in.macpulse.empty" \
        "$OUT/macpulse.orig.stdout" "$OUT/macpulse.orig.stderr"
    run_case "macpulse.rust" "$BUILD/rust_macpulse" "$BUILD/mac_r" "$OUT/in.macpulse.empty" \
        "$OUT/macpulse.rust.stdout" "$OUT/macpulse.rust.stderr"
    # Non-interactive guard is the only hardware-independent observable path.
    if compare_streams "macpulse" && [ "$(cat "$OUT/macpulse.orig.exit")" = "1" ]; then
        MATCHES=$((MATCHES + 1))
        echo "EXAMPLE_COMPARE macpulse MATCH non-interactive tty-guard path byte-identical (stderr + exit 1) via minimally patched copy; live interactive sampling is timing/hardware-dependent and BLOCKED for byte comparison (Rust --model is deterministic)"
    else
        echo "EXAMPLE_COMPARE macpulse DIFFERENT see out/macpulse.*.diff"
    fi
fi

echo
echo "TOTAL: $MATCHES/$CPUS examples matched on the tested deterministic paths"
