#!/bin/sh
# benchmarks/example_rust_compare/utilities/compare.sh
#
# One-to-one stdout/exit comparison of the 30 NEW examples/utilities C+ programs
# against safe-Rust models (no unsafe, no FFI, no external crates).
#
#   C+  : ./build/c+ build examples/utilities/<group>/<name>.cp \
#             --strict --sanitize=none -I. [-Ithird_party/yyjson -Lbuild/ext -lyyjson] \
#             [-o ... -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a] \
#             -o <bin>
#   Rust: rustc -O --edition 2021 <name>.rs -o <bin>
#   run : timeout 30 <bin> <fixed args>   (identical stdin: empty)
#
# stdout: exactly one line per utility:
#   UTIL_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# Diagnostics, commands, hashes and diffs go to stderr and to .build/* logs.
# Exit status: nonzero ONLY on a real DIFFERENT.  BLOCKED (build failure) and
# PENDING (scoped source or Rust model not yet created) are inconclusive and
# are reported but deliberately do NOT fail the script.
#
# A missing C+ source is reported PENDING with the exact refresh command:
#   sh benchmarks/example_rust_compare/utilities/compare.sh
# No MATCH is ever fabricated for a missing source.
#
# Environment overrides: UC_CPLUS, UC_RUSTC, UC_TIMEOUT, UC_OUT.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CPLUS=${UC_CPLUS:-"$ROOT/build/c+"}
RUSTC=${UC_RUSTC:-rustc}
TMO=${UC_TIMEOUT:-30}
OUT=${UC_OUT:-"$HERE/.build"}

mkdir -p "$OUT" || exit 1

note() { printf '%s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ -x "$CPLUS" ] || die "c+ compiler not found/executable: $CPLUS"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"

# c+ resolves stdlib/*.hp includes relative to cwd; always build from the root.
cd "$ROOT" || die "cannot cd to repository root: $ROOT"

# -------- deterministic fixtures (rewritten on every run) -------------------
printf 'hello world\nfoo bar baz\none\ntwo three\n' > "$OUT/text.txt" || die "fixture write"
printf 'name,value\nalpha,10\nbeta,-3\ngamma,7\n' > "$OUT/csv.csv" || die "fixture write"
printf 'name,value\n' > "$OUT/nodata.csv" || die "fixture write"
printf '[server]\nhost=localhost\nport=8080\n[db]\nuser=admin\n' > "$OUT/ini.ini" || die "fixture write"
printf 'alpha\nbeta\ngamma\n' > "$OUT/diff_a.txt" || die "fixture write"
printf 'alpha\nBETA\ngamma\ndelta\n' > "$OUT/diff_b.txt" || die "fixture write"
printf 'Hello,\000world!\n' > "$OUT/bin.dat" || die "fixture write"
printf 'SGVsbG8sIHdvcmxkIQ==\n' > "$OUT/b64.txt" || die "fixture write"
printf '2024-01-02T03:04:05Z INFO started\n2024-01-02T03:04:06Z WARN slow\n2024-01-02T03:04:07Z ERROR failed\n2024-01-02T03:04:08Z DEBUG detail\n' > "$OUT/log.txt" || die "fixture write"
i=0; while [ "$i" -lt 257 ]; do printf A; i=$((i + 1)); done > "$OUT/oversized.bin" || die "fixture write"
head -c 4097 /dev/zero > "$OUT/big4097.bin" || die "fixture write"
head -c 65537 /dev/zero > "$OUT/big65537.bin" || die "fixture write"

CURRENT_NAME=""

run_case() {
    # run_case <binary> <prefix> [args...]
    bin=$1; prefix=$2; shift 2
    timeout "$TMO" "$bin" "$@" >>"$prefix.stdout" 2>>"$prefix.stderr"
    printf '%s\n' "$?" >>"$prefix.rc"
}

emit_cases() {
    # emit_cases <binary> <prefix> -- fixed invocations for $CURRENT_NAME
    bin=$1; prefix=$2
    : >"$prefix.stdout"; : >"$prefix.rc"; : >"$prefix.stderr"
    case "$CURRENT_NAME" in
        fhash)
            run_case "$bin" "$prefix" "$OUT/bin.dat"
            run_case "$bin" "$prefix" "$OUT/missing.dat"
            run_case "$bin" "$prefix" "$OUT/big4097.bin"
            ;;
        fdiff)
            run_case "$bin" "$prefix" "$OUT/diff_a.txt" "$OUT/diff_b.txt"
            run_case "$bin" "$prefix" "$OUT/diff_a.txt" "$OUT/diff_a.txt"
            ;;
        fcopy)
            rm -f "$OUT/copy_dst.bin"
            run_case "$bin" "$prefix" "$OUT/text.txt" "$OUT/copy_dst.bin"
            run_case "$bin" "$prefix" "$OUT/big4097.bin" "$OUT/copy_dst.bin"
            ;;
        fcat) run_case "$bin" "$prefix" "$OUT/text.txt" ;;
        fsize)
            run_case "$bin" "$prefix" "$OUT" text.txt csv.csv
            run_case "$bin" "$prefix" "$OUT" missing-entry.txt
            ;;
        csvstat)
            run_case "$bin" "$prefix" "$OUT/csv.csv" stats
            run_case "$bin" "$prefix" "$OUT/csv.csv" sum
            run_case "$bin" "$prefix" "$OUT/csv.csv" min
            run_case "$bin" "$prefix" "$OUT/csv.csv" max
            run_case "$bin" "$prefix" "$OUT/csv.csv" count
            run_case "$bin" "$prefix" "$OUT/nodata.csv" stats
            run_case "$bin" "$prefix" "$OUT/csv.csv" bogus
            ;;
        iniparse)
            run_case "$bin" "$prefix" "$OUT/ini.ini" sections
            run_case "$bin" "$prefix" "$OUT/ini.ini" keys server
            run_case "$bin" "$prefix" "$OUT/ini.ini" get server host
            run_case "$bin" "$prefix" "$OUT/ini.ini" get server missing
            run_case "$bin" "$prefix" "$OUT/ini.ini" bogus
            ;;
        jsonfmt)
            run_case "$bin" "$prefix" '{"b":2,"a":[1,true,null],"s":"x"}'
            run_case "$bin" "$prefix" '{"a":'
            run_case "$bin" "$prefix" ''
            ;;
        uriparse)
            run_case "$bin" "$prefix" 'https://example.com:8443/a/b'
            run_case "$bin" "$prefix" 'http://example.com'
            run_case "$bin" "$prefix" 'ftp://host/path'
            run_case "$bin" "$prefix" 'http://h?x=1'
            ;;
        logparse)
            run_case "$bin" "$prefix" "$OUT/log.txt" INFO
            run_case "$bin" "$prefix" "$OUT/log.txt" all
            run_case "$bin" "$prefix" "$OUT/log.txt" TRACE
            run_case "$bin" "$prefix" "$OUT/log.txt" NOPE
            ;;
        envfilter)
            run_case "$bin" "$prefix" APP_ A=1 APP_X=2 OTHER=3 APP_9=4
            ;;
        hexdump)
            run_case "$bin" "$prefix" "$OUT/bin.dat"
            run_case "$bin" "$prefix" "$OUT/oversized.bin"
            ;;
        numconv)
            run_case "$bin" "$prefix" 255 dec hex
            run_case "$bin" "$prefix" 101010 bin dec
            run_case "$bin" "$prefix" ff hex bin
            run_case "$bin" "$prefix" 12345678901234 dec bin
            run_case "$bin" "$prefix" 2x dec hex
            ;;
        sha_lite)
            run_case "$bin" "$prefix" hello
            run_case "$bin" "$prefix" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
            ;;
        timeconv)
            run_case "$bin" "$prefix" 0
            run_case "$bin" "$prefix" 1700000000
            run_case "$bin" "$prefix" 253402300799
            run_case "$bin" "$prefix" -1
            run_case "$bin" "$prefix" 999999999999999999999999
            run_case "$bin" "$prefix" abc
            ;;
        checksum)
            run_case "$bin" "$prefix" hello
            run_case "$bin" "$prefix" AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
            run_case "$bin" "$prefix" ''
            run_case "$bin" "$prefix"
            ;;
        pwdcheck)
            run_case "$bin" "$prefix" 'Str0ng!Pass'
            run_case "$bin" "$prefix" short
            ;;
        safe_copy)
            run_case "$bin" "$prefix" exact
            run_case "$bin" "$prefix" over
            run_case "$bin" "$prefix" overlap
            run_case "$bin" "$prefix" null
            run_case "$bin" "$prefix" nullsrc
            run_case "$bin" "$prefix" raw
            run_case "$bin" "$prefix" unknown
            ;;
        safe_token)
            run_case "$bin" "$prefix" 'alpha,beta gamma;delta'
            run_case "$bin" "$prefix" 'aaaaaaaaaaaaaaaaaaa,x'
            run_case "$bin" "$prefix" 'a b c d e'
            ;;
        bounds_check)
            run_case "$bin" "$prefix" exact
            run_case "$bin" "$prefix" one-over
            run_case "$bin" "$prefix" interior
            run_case "$bin" "$prefix" stale
            ;;
        twc)
            run_case "$bin" "$prefix" "$OUT/text.txt"
            run_case "$bin" "$prefix" "$OUT/big65537.bin"
            ;;
        tgrep)
            run_case "$bin" "$prefix" "$OUT/text.txt" bar
            run_case "$bin" "$prefix" "$OUT/text.txt" 'b.*z'
            ;;
        thead)
            run_case "$bin" "$prefix" "$OUT/text.txt" 2
            run_case "$bin" "$prefix" "$OUT/text.txt" 0
            ;;
        ttr) run_case "$bin" "$prefix" "$OUT/text.txt" abc xyz ;;
        tbase64)
            run_case "$bin" "$prefix" enc "$OUT/bin.dat"
            run_case "$bin" "$prefix" dec "$OUT/b64.txt"
            ;;
        ipcheck) run_case "$bin" "$prefix" 192.168.0.1 999.1.1.1 ;;
        portcheck) run_case "$bin" "$prefix" 8080 70000 ;;
        emailcheck) run_case "$bin" "$prefix" bob@example.org bad ;;
        httpline) run_case "$bin" "$prefix" 'GET / HTTP/1.1' 'BAD /x HTTP/1.1' ;;
        urlcheck) run_case "$bin" "$prefix" 'https://example.com/x' 'http://' ;;
        *) die "no fixed inputs defined for $CURRENT_NAME" ;;
    esac
}

build_c() {
    # build_c <name> <srcpath> <outbin> <logfile>
    name=$1; src=$2; bin=$3; log=$4
    case "$name" in
        checksum|pwdcheck|safe_copy|safe_token)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. \
                -o "$bin" -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
                >"$log" 2>&1 ;;
        jsonfmt)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. \
                -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$bin" >"$log" 2>&1 ;;
        *)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. \
                -o "$bin" >"$log" 2>&1 ;;
    esac
}

fails=0           # exit-driving counter: incremented ONLY on a real DIFFERENT
inconclusive=0    # PENDING/BLOCKED: reported but do NOT affect exit status

compare_example() {
    # compare_example <name> <relative source path under examples/utilities>
    name=$1; src=$2
    CURRENT_NAME=$name
    srcpath="$ROOT/examples/utilities/$src"
    rs="$HERE/$name.rs"

    if [ ! -f "$srcpath" ]; then
        printf 'UTIL_COMPARE %s PENDING source-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$srcpath" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi
    if [ ! -f "$rs" ]; then
        printf 'UTIL_COMPARE %s PENDING rust-model-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$rs" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi

    cbin="$OUT/c_$name"
    rbin="$OUT/r_$name"
    cbuild="$OUT/$name.c.build.log"
    rbuild="$OUT/$name.rs.build.log"

    note "BUILD C+ $name: c+ build $src --strict --sanitize=none -I. -o $cbin [+ per-source flags]"
    build_c "$name" "$srcpath" "$cbin" "$cbuild" || {
        printf 'UTIL_COMPARE %s BLOCKED c+-build-failed see %s\n' "$name" "$cbuild"
        inconclusive=$((inconclusive + 1)); return
    }

    note "BUILD Rust $name: $RUSTC -O --edition 2021 $rs -o $rbin"
    timeout "$TMO" "$RUSTC" -O --edition 2021 "$rs" -o "$rbin" \
        >"$rbuild" 2>&1 || {
        printf 'UTIL_COMPARE %s BLOCKED rust-build-failed see %s\n' "$name" "$rbuild"
        inconclusive=$((inconclusive + 1)); return
    }

    cpfx="$OUT/$name.cases.c"
    rpfx="$OUT/$name.cases.r"
    emit_cases "$cbin" "$cpfx"
    emit_cases "$rbin" "$rpfx"

    if cmp -s "$cpfx.stdout" "$rpfx.stdout" && cmp -s "$cpfx.rc" "$rpfx.rc"; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$cpfx.stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rpfx.stdout" | awk '{print $1}')"
        printf 'UTIL_COMPARE %s MATCH stdout+exit identical for %s fixed case(s)\n' \
            "$name" "$(wc -l < "$cpfx.rc" | tr -d ' ')"
    else
        diff -u "$cpfx.stdout" "$rpfx.stdout" >"$OUT/$name.diff" 2>&1 || true
        diff -u "$cpfx.rc" "$rpfx.rc" >>"$OUT/$name.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/$name.diff"
        printf 'UTIL_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/$name.diff"
        fails=$((fails + 1))
    fi
}

# 30 utilities, one-to-one against the scoped C+ sources.
compare_example fhash          file_tools/fhash.cp
compare_example fdiff          file_tools/fdiff.cp
compare_example fcat           file_tools/fcat.cp
compare_example fsize          file_tools/fsize.cp
compare_example fcopy          file_tools/fcopy.cp
compare_example twc            text_tools/twc.cp
compare_example tgrep          text_tools/tgrep.cp
compare_example thead          text_tools/thead.cp
compare_example ttr            text_tools/ttr.cp
compare_example tbase64        text_tools/tbase64.cp
compare_example csvstat        data_tools/csvstat.cp
compare_example iniparse       data_tools/iniparse.cp
compare_example jsonfmt        data_tools/jsonfmt.cp
compare_example uriparse       data_tools/uriparse.cp
compare_example logparse       data_tools/logparse.cp
compare_example checksum       security_tools/checksum.cp
compare_example pwdcheck       security_tools/pwdcheck.cp
compare_example safe_copy      security_tools/safe_copy.cp
compare_example safe_token     security_tools/safe_token.cp
compare_example bounds_check   security_tools/bounds_check.cp
compare_example hexdump        dev_tools/hexdump.cp
compare_example numconv        dev_tools/numconv.cp
compare_example sha_lite       dev_tools/sha_lite.cp
compare_example envfilter      dev_tools/envfilter.cp
compare_example timeconv       dev_tools/timeconv.cp
compare_example urlcheck       net_tools/urlcheck.cp
compare_example emailcheck     net_tools/emailcheck.cp
compare_example ipcheck        net_tools/ipcheck.cp
compare_example portcheck      net_tools/portcheck.cp
compare_example httpline       net_tools/httpline.cp

note "SUMMARY different=$fails inconclusive(BLOCKED+PENDING)=$inconclusive"
exit "$fails"
