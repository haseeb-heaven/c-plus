#!/bin/sh
# Adversarial driver: examples/expanded/database/{kv_store,table_query}.cp
set -u
. "$(dirname -- "$0")/common.sh" || exit 1
make_fixtures

BIG=$(cat "$FIX/oversized_10k.txt")
BADUTF=$(cat "$FIX/invalid_utf8.txt")
SIZEMAX=18446744073709551615

build_one kv_store    examples/expanded/database/kv_store.cp    || true
build_one table_query examples/expanded/database/table_query.cp || true

# These two examples deliberately return 0 on a handled refusal; their
# contract is the explicit "REFUSED" token (KV_SUMMARY / TQ_SUMMARY counters).
if [ -x "$BIN/kv_store" ]; then
    run_case kv_store oversized_key REFUSE "$BIN/kv_store" insert "$BIG" v
    run_case kv_store size_max_value REFUSE "$BIN/kv_store" insert k "$SIZEMAX"
    run_case kv_store empty_key REFUSE "$BIN/kv_store" insert "" v
    run_case kv_store invalid_utf8_key REFUSE "$BIN/kv_store" insert "$BADUTF" v
    run_case kv_store duplicate_key REFUSE "$BIN/kv_store" insert k v insert k w
    run_case kv_store missing_operand REFUSE "$BIN/kv_store" insert k
    run_case kv_store invalid_command REFUSE "$BIN/kv_store" frobnicate
    run_case kv_store out_of_range_key REFUSE "$BIN/kv_store" get absent
    run_case kv_store null_like_operand REFUSE "$BIN/kv_store" delete
    ADV_STDIN_FILE="$FIX/oversized_10k.txt" \
        run_case kv_store oversized_stdin REFUSE "$BIN/kv_store"
    # Sentinel: a hostile insert must leave the first record intact.
    ADV_REQUIRE_TOKEN='KV item 0 k v' \
        run_case kv_store post_hostile_integrity REFUSE "$BIN/kv_store" \
            insert k v insert "$BIG" v list
fi

if [ -x "$BIN/table_query" ]; then
    run_case table_query oversized_field REFUSE "$BIN/table_query" eq "$BIG" 10
    run_case table_query size_max_predicate REFUSE "$BIN/table_query" eq grp "$SIZEMAX"
    run_case table_query empty_field REFUSE "$BIN/table_query" eq "" 10
    run_case table_query invalid_command REFUSE "$BIN/table_query" frobnicate
    run_case table_query duplicate_id REFUSE "$BIN/table_query" row 1 10 100 row 1 10 100
    run_case table_query out_of_range_index REFUSE "$BIN/table_query" eq amt "$SIZEMAX"
    run_case table_query null_like_operand REFUSE "$BIN/table_query" row 1 10
    run_case table_query invalid_utf8_field REFUSE "$BIN/table_query" eq "$BADUTF" 10
    # Sentinel: duplicate-id refusal must not corrupt the committed row.
    ADV_REQUIRE_TOKEN='TQ count grp 10 1' \
        run_case table_query post_hostile_integrity REFUSE "$BIN/table_query" \
            row 1 10 100 row 1 10 100 count grp 10
fi

subtotal database
