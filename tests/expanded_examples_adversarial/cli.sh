#!/bin/sh
# Adversarial driver: examples/expanded/cli/{calculator,todo}.cp
set -u
. "$(dirname -- "$0")/common.sh" || exit 1
make_fixtures

BIG=$(cat "$FIX/oversized_10k.txt")
BIGD=$(cat "$FIX/oversized_10k_digits.txt")
SIZEMAX=18446744073709551615

build_one calculator examples/expanded/cli/calculator.cp || true
build_one todo       examples/expanded/cli/todo.cp       || true
build_one file_tools examples/expanded/cli/file_tools.cp || true
build_one text_tools examples/expanded/cli/text_tools.cp || true

if [ -x "$BIN/calculator" ]; then
    # -- calculator: REFUSAL tokens are "CALC ERROR ...", exit 1. ---------------
    ADV_STDIN_FILE="$FIX/empty.txt" \
        run_case calculator oversized_expr REFUSE "$BIN/calculator" "$BIG"
    run_case calculator size_max_number REFUSE "$BIN/calculator" "$SIZEMAX"
    run_case calculator empty_expr REFUSE "$BIN/calculator" ""
    run_case calculator whitespace_expr REFUSE "$BIN/calculator" "   "
    run_case calculator div_zero REFUSE "$BIN/calculator" "1/0"
    run_case calculator invalid_command REFUSE "$BIN/calculator" "foo"
    run_case calculator truncated_input REFUSE "$BIN/calculator" "(1+2"
    ADV_STDIN_FILE="$FIX/oversized_10k_digits.txt" \
        run_case calculator oversized_stdin REFUSE "$BIN/calculator"
    ADV_STDIN_FILE="$FIX/empty.txt" \
        run_case calculator no_input REFUSE "$BIN/calculator"
fi

if [ -x "$BIN/todo" ]; then
    # -- todo: REFUSAL tokens are "TODO error ...", exit 1. --------------------
    run_case todo oversized_title REFUSE "$BIN/todo" add 1 "$BIG"
    run_case todo oversized_command REFUSE "$BIN/todo" "$BIG"
    run_case todo size_max_id REFUSE "$BIN/todo" add "$SIZEMAX" x
    run_case todo empty_command REFUSE "$BIN/todo" ""
    run_case todo whitespace_id REFUSE "$BIN/todo" add "   " x
    run_case todo duplicate_key REFUSE "$BIN/todo" add 1 alpha -- add 1 beta
    run_case todo out_of_range_index REFUSE "$BIN/todo" done 999
    run_case todo invalid_command REFUSE "$BIN/todo" frobnicate
    run_case todo null_like_args REFUSE "$BIN/todo" add
    run_case todo truncated_path REFUSE "$BIN/todo" load /definitely/not/here
    # Sentinel: a hostile title must not corrupt the previously stored record.
    ADV_REQUIRE_TOKEN='TODO item 1 alpha' \
        run_case todo post_hostile_integrity REFUSE "$BIN/todo" \
            add 1 alpha -- add 2 "$BIG" -- list
fi

if [ -x "$BIN/file_tools" ]; then
    # file_tools refuses with "FILETOOLS error ..." and a nonzero exit.
    run_case file_tools one_over_file REFUSE "$BIN/file_tools" \
        size "$FIX/one_over_1k.bin"
    run_case file_tools copy_one_over REFUSE "$BIN/file_tools" \
        copy "$FIX/one_over_1k.bin" "$FIX/should_not_exist.bin"
    run_case file_tools cmp_one_over REFUSE "$BIN/file_tools" \
        cmp "$FIX/one_over_1k.bin" "$FIX/exact_1k.bin"
    run_case file_tools truncated_path REFUSE "$BIN/file_tools" \
        size /definitely/not/here
    run_case file_tools oversized_command REFUSE "$BIN/file_tools" "$BIG" x
    run_case file_tools size_max_path REFUSE "$BIN/file_tools" size "$SIZEMAX"
    run_case file_tools invalid_command REFUSE "$BIN/file_tools" frobnicate x
    run_case file_tools null_like_args REFUSE "$BIN/file_tools"
fi

if [ -x "$BIN/text_tools" ]; then
    # text_tools refuses with "TEXT error ..." and a nonzero exit.
    run_case text_tools missing_file REFUSE "$BIN/text_tools" \
        /definitely/not/here words
    run_case text_tools oversized_path REFUSE "$BIN/text_tools" "$BIG" words
    run_case text_tools size_max_path REFUSE "$BIN/text_tools" "$SIZEMAX" words
    run_case text_tools invalid_command REFUSE "$BIN/text_tools" \
        "$FIX/sample.txt" frobnicate
    run_case text_tools bad_operand_count REFUSE "$BIN/text_tools" \
        "$FIX/sample.txt" find
    run_case text_tools null_like_args REFUSE "$BIN/text_tools"
fi

subtotal cli
