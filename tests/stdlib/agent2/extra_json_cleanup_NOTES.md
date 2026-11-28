# `extra_json_cleanup.cp` — evidence notes

Branch: `agent/modern-stdlib`. Agent: agent2 (1 of 10 parallel agents).
Files owned for this task (and no others):

- `tests/stdlib/agent2/extra_json_cleanup.cp`
- `tests/stdlib/agent2/extra_json_cleanup_NOTES.md` (this file)

No headers, existing tests, build files or git index were changed. The build
artifacts produced by the commands below were written to the pre-approved temp directory
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode` (outside
the repository). Commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

## 1. Commands (exact)

`-I`, `-L` and `-l` were passed as **separate argv operands**, exactly as
required. Run order was STRICT/--sanitize=none FIRST, then default, then UBSan.
Each binary was executed under `timeout 30`.

```sh
TDIR=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode

# 1. STRICT, sanitizers off (run first)
./build/c+ build tests/stdlib/agent2/extra_json_cleanup.cp \
  --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o "$TDIR/extra_json_cleanup_strict_none"
timeout 30 "$TDIR/extra_json_cleanup_strict_none" \
  > "$TDIR/ejc_strict_none.stdout" 2> "$TDIR/ejc_strict_none.stderr"

# 2. Default mode (no --strict, no --sanitize)
./build/c+ build tests/stdlib/agent2/extra_json_cleanup.cp \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o "$TDIR/extra_json_cleanup_default"
timeout 30 "$TDIR/extra_json_cleanup_default" \
  > "$TDIR/ejc_default.stdout" 2> "$TDIR/ejc_default.stderr"

# 3. STRICT + UBSan
./build/c+ build tests/stdlib/agent2/extra_json_cleanup.cp \
  --strict --sanitize=undefined \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o "$TDIR/extra_json_cleanup_ubsan"
timeout 30 "$TDIR/extra_json_cleanup_ubsan" \
  > "$TDIR/ejc_ubsan.stdout" 2> "$TDIR/ejc_ubsan.stderr"
```

Build-time clang warnings are the driver's usual
`-lyyjson: linker input unused` / `-Lbuild/ext: argument unused during
compilation` notices (also emitted for every pre-existing json suite); linking
still succeeded and all binaries ran. No runtime stderr in any mode.

## 2. Per-mode results

| Mode | Build exit | Run exit (`timeout 30`) | stderr | Final line |
| --- | --- | --- | --- | --- |
| `--strict --sanitize=none` (first) | 0 | 0 | empty | `EXTRA_JSON_CLEANUP 26 0` |
| default | 0 | 0 | empty | `EXTRA_JSON_CLEANUP 26 0` |
| `--strict --sanitize=undefined` | 0 | 0 | empty | `EXTRA_JSON_CLEANUP 26 0` |

Exit 0 only when failures == 0 (`return xc_failures == 0 ? 0 : 1;`). No
`timeout` expiry, no sanitizer diagnostic, no `INVARIANT`-style output.
Stdout is byte-identical in all three modes:

```
shasum: 59880c347c54749146f022902ae371a89b024395  (27 lines)
```

`diff strict_none default` and `diff default ubsan` both empty.

## 3. Exact stdout (all three modes)

```
cyc_200_parse_write_drop 1
cyc_valid_count_100 1
cyc_fail_count_100 1
json_leak_telemetry 1
mal_extra_close_bracket 1
mal_trailing_junk 1
mal_trailing_comma_nested 1
mal_double_comma 1
mal_missing_colon 1
mal_missing_comma 1
dup_keys_parse_and_write 1
nul_strlen_truncation 1
nul_incomplete_prefix 1
nul_in_string_value 1
empty_object_root 1
empty_array_root 1
nested_empty_containers 1
nested_wrong_type_refusal 1
nested_correct_type_ok 1
builder_empty_write 1
builder_mutate_between_writes 1
doc_repeat_write_identical 1
postdrop_doc_view_refusal 1
postdrop_builder_refusal 1
view_use_after_free 1
json_alloc_failure_hook 1
EXTRA_JSON_CLEANUP 26 0
```

Every check line is exactly `<tag> <0|1>` (26 check lines + 1 final summary).
There are no extra annotation lines on stdout; SKIPs are explained here only.

## 4. SKIPs (a printed `1` here is a SKIP marker, NOT a pass claim)

1. **`json_leak_telemetry`** — SKIP. `stdlib/json.hp` passes `NULL` for every
   `yyjson_alc *` (`yyjson_read_opts`, `yyjson_read_file`, `yyjson_val_write`,
   `yyjson_mut_doc_new`, all `yyjson_mut_obj_add_*`, `yyjson_mut_write`), so all
   document/buffer allocations go through **yyjson's default allocator**, not
   the checked `stdlib_alloc`. `stdlib_alloc_fail_at/after` therefore cannot
   observe them and there is no allocation counter to read. Instead
   `cyc_200_parse_write_drop` runs **200 iterations** (100 valid
   parse → write ×2 → reparse → drop → free, 100 malformed parse-refusal → drop)
   with `cyc_valid_count_100` / `cyc_fail_count_100` asserting deterministic
   success/failure; each iteration explicitly `drop()`s and `json_free()`s
   everything. Completion under `timeout 30` with stable output is the only
   observable evidence available — it is **not** a leak-freedom proof.
2. **`view_use_after_free`** — SKIP. `json_view` borrows a `const yyjson_val*`
   with no owner reference; after `doc.drop()` a previously obtained view is
   dangling. That is a documented lifetime rule, not a refusal contract the
   API can enforce. Deliberately dereferencing such a view would be an
   intentional UAF, so it is not executed. `postdrop_doc_view_refusal` instead
   uses the safe path: call `doc.root()` **after** `drop()` (the header returns
   an invalid view when `document == NULL`) and verify all getters refuse,
   `size()==0`, all `is_*` false, `member()/at()` invalid and `write()==NULL`.
   Triple `drop()` idempotence is also asserted for both owning types.
3. **`json_alloc_failure_hook`** — SKIP. No allocation-failure coverage is
   claimed. yyjson's allocator is per-call and `json.hp` exposes no allocator
   setter, so no honest OOM injection is reachable through the public API. This
   matches the existing audit (`tests/stdlib/agent2_audit/module_audits/json.hp.md`
   §2 and the `json_alloc_failure_hook` SKIP in `deep_json_task5.cp`).

No defect was reproduced; no minimal repro exists to report.

## 5. Coverage mapping (checked behaviours)

| Scope item | Tag(s) | Observation on this host |
| --- | --- | --- |
| 200 repeated parse/write/drop cycles | `cyc_200_parse_write_drop`, `cyc_valid_count_100`, `cyc_fail_count_100` | 100 valid cycles produced the exact canonical dump twice and reparsed to `i==7`; 100 malformed cycles were refused INVALID/UNSUPPORTED; all buffers freed; 26/26 overall, exit 0 |
| malformed boundary variants | `mal_extra_close_bracket` `[1,2]]`, `mal_trailing_junk` `[1,2]x`, `mal_trailing_comma_nested` `{"o":[1,2,]}`, `mal_double_comma` `[1,,2]`, `mal_missing_colon` `{"a" 1}`, `mal_missing_comma` `{"a":1 "b":2}` | all rejected with INVALID/UNSUPPORTED and non-empty message (canonical `[1,2`, `{`, `{"a":1,}`, `[1,]` forms were already in `deep_json_task5.cp`; these are new variants) |
| duplicate keys | `dup_keys_parse_and_write` | `{"x":1,"x":2,"x":3}` parses, `size()==3`, `member("x")` returns first value 1, two writes byte-identical, dump reparses with `size()==3` |
| embedded NUL | `nul_strlen_truncation`, `nul_incomplete_prefix`, `nul_in_string_value` | **No explicit-length parse API exists** — `json::json` calls `strlen(text)`, so a byte array with a complete prefix plus trailing junk parses with only the prefix, and an incomplete prefix is INVALID; escaped `\\u0000` inside a string decodes to bytes `'a', 0x00, 'b'` via `get_string` (no length accessor, bytes inspected directly) |
| builder mutation between writes | `builder_empty_write`, `builder_mutate_between_writes` | zero-field builder writes `{}`; `{}` → `{"a":1}` → `{"a":1,"b":2}` → `{"a":1,"b":2,"a":9}`; each intermediate dump reparses with expected size/values; repeated writes byte-identical |
| repeated writes byte-identical | `builder_mutate_between_writes`, `doc_repeat_write_identical`, `dup_keys_parse_and_write` | `memcmp`/length equality on all paired dumps, including a nested document dump |
| post-drop view refusal | `postdrop_doc_view_refusal`, `postdrop_builder_refusal` | see SKIP 2; builder `write()==NULL` and all four `add_*` refuse after triple `drop()` |
| wrong-type getters on nested values | `nested_wrong_type_refusal`, `nested_correct_type_ok` | nested string→int, number→string, bool→double, array→bool, object→int and nested array element→string all STDLIB_FAIL with INVALID and untouched output sentinels; matching correct-type getters succeed |
| empty object / empty array | `empty_object_root`, `empty_array_root`, `nested_empty_containers` | `{}` / `[]` valid, `size()==0`, correct `is_*` predicates, write `{}` / `[]`; `{"e":[],"o":{}}` nested empties report correct predicates/size and refuse wrong-type getters |

Ground truth for the exact rejection/write behaviour of the malformed inputs,
the escaped-NUL decoding and the builder dump strings was first confirmed with
a direct yyjson probe compiled outside the repository; that probe is **not**
part of the suite and makes no claim about `json.hp` beyond what the tagged
checks assert.

## 6. Result

- Checks: **26**, failures: **0**, real defects reproduced: **0**, SKIPs: **3**.
- All three required modes (STRICT/--sanitize=none first, then default, then
  UBSan) built and ran cleanly under `timeout 30`.
- Files returned: the two paths listed at the top of this document.
