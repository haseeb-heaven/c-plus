# Deep task-5 contract: `stdlib/json.hp`

Per-family evidence for `tests/stdlib/deep_json_task5.cp`. This suite covers
only the genuinely uncovered gaps left by `deep_json_types.cp` and
`deep_json_stress.cp`.

Status: **no defect found**. `stdlib/json.hp` was not modified.

## Scope and ownership

| Item | Path |
| --- | --- |
| Test | `tests/stdlib/deep_json_task5.cp` |
| Contract | `docs/stdlib/DEEP_JSON_TASK5_CONTRACT.md` (this file) |
| Header under test | `stdlib/json.hp` (untouched) |
| Link flags | `-Ithird_party/yyjson -Lbuild/ext -lyyjson` |

The test is self-checking, has no `.stdin`/`.expected` file, and returns `0`
only when every check passes. Each check prints `<tag> <0|1>`; the final line
is `JSON_TASK5 <checks> <failures>`. A documented SKIP prints `<tag> 1` followed
by `SKIP_REASON <tag> <reason>`.

## Scenarios

| Tag | What is asserted | Result |
| --- | --- | --- |
| `json_empty_input` | `json("")` invalid; `INVALID`/`UNSUPPORTED`, non-empty message `"input length is 0"` | pass |
| `json_whitespace_input` | whitespace-only invalid; `INVALID`/`UNSUPPORTED`, message `"input data is empty"` | pass |
| `json_truncated_object` | `{` → invalid, `"unexpected end of data"` | pass |
| `json_unmatched_bracket` | `[1,2` → invalid, `"unexpected end of data"` | pass |
| `json_trailing_comma_obj` | `{"a":1,}` → invalid, `"trailing comma is not allowed"` | pass |
| `json_trailing_comma_arr` | `[1,]` → invalid (same rejection) | pass |
| `json_bad_escape` | `{"a":"\q"}` → invalid, INVALID category | pass |
| `json_duplicate_keys` | `{"a":1,"a":2}` parses; `size()==2`; `member("a")` returns first value `1` | pass |
| `json_embedded_nul_complete_prefix` | `{"a":1}\0...` parses only the prefix (`strlen` semantics): valid, size 1, `a==1` | pass |
| `json_embedded_nul_incomplete_prefix` | `{"a":\0...` → INVALID parse failure | pass |
| `json_double_i64max` | `get_double` on `9223372036854775807` ≈ `2^63`, OK | pass |
| `json_double_i64min` | `get_double` on `-9223372036854775808` ≈ `−2^63`, OK | pass |
| `json_double_u64max` | `get_double` on `18446744073709551615` ≈ `2^64`, OK | pass |
| `json_double_denorm_min` | `1e-308` parses and round-trips as a positive subnormal double | pass |
| `json_double_denorm_neg` | `-1e-308` parses with a negative sign | pass |
| `json_double_1e308` | `1e308` parses as a finite large double | pass |
| `json_null_text` | `json((const char*)0)` → invalid doc, **no** error recorded (`ok()==1`, OK/OK) | pass |
| `json_null_err_and_write_args` | getters with `error*=0` work; wrong type with NULL err refuses; `write(NULL length)` returns a buffer; `json_free(NULL)` is a no-op | pass |
| `json_view_of_null` | `json_view_of(NULL)` → invalid view, all `is_*` false, `size()==0`, getters refuse with INVALID | pass |
| `json_builder_null_err_args` | all four `add_*` accept `error*=0`; `write(NULL length)` returns a buffer | pass |
| `json_dropped_doc_views` | triple `drop()` is idempotent; fresh `root()` and all derived views are invalid/refuse safely; `write` returns NULL | pass |
| `json_dropped_builder_triple` | triple `drop()`; every mutator refuses with NULL err too; `write` returns NULL | pass |
| `json_alloc_failure_hook` | **SKIP** — yyjson allocator is not reachable from `json.hp` | — |

### API observations (not defects)

- **No explicit-length API.** `json(text, err)` uses `strlen(text)`. An embedded
  NUL byte therefore truncates the input at that byte. A complete prefix parses
  successfully and trailing bytes are ignored; an incomplete prefix is a normal
  INVALID parse failure. The task's "NUL byte in input (use explicit length API
  if available)" reduces to this documented behaviour because no such API
  exists in `json.hp`.
- **Duplicate keys.** yyjson keeps both object entries (`size()==2`) and
  `yyjson_obj_get()` returns the first, so `member("a")` yields `1`.
- **NULL text is silent.** The `json(const char*, error*)` constructor resets
  `err` and records no failure for a NULL source (the file helper fills it in).
  This differs from `json_parse_file(NULL)`, which records INVALID/IO (already
  pinned in `deep_json_types.cp`).
- **Number accept/refuse.** `get_int` range refusal for INT64_MIN/MAX and
  UINT64_MAX is already in `deep_json_types.cp` T1 and is not duplicated. This
  suite pins only the `get_double` view, which accepts all boundary numbers
  including subnormals and `1e308`. `1e400` remains a recoverable *parse*
  failure (covered by T1).
- **Allocation-failure gap.** `json.hp` passes `NULL` for the `yyjson_alc *`
  argument to `yyjson_read_opts()` and `yyjson_mut_doc_new()`. yyjson's
  allocator is per-call (no global override) and `json.hp` exposes no setter,
  so `stdlib_alloc_fail_at/after` cannot reach its allocations. No OOM test is
  fabricated; see `DEEP_JSON_CONTRACT.md` §3.

## Exact stdout (byte-identical in all build modes)

```
json_empty_input 1
json_whitespace_input 1
json_truncated_object 1
json_unmatched_bracket 1
json_trailing_comma_obj 1
json_trailing_comma_arr 1
json_bad_escape 1
json_duplicate_keys 1
json_embedded_nul_complete_prefix 1
json_embedded_nul_incomplete_prefix 1
json_double_i64max 1
json_double_i64min 1
json_double_u64max 1
json_double_denorm_min 1
json_double_denorm_neg 1
json_double_1e308 1
json_null_text 1
json_null_err_and_write_args 1
json_view_of_null 1
json_builder_null_err_args 1
json_dropped_doc_views 1
json_dropped_builder_triple 1
json_alloc_failure_hook 1
SKIP_REASON json_alloc_failure_hook json.hp passes a NULL yyjson_alc to yyjson_read_opts() and yyjson_mut_doc_new(); yyjson's allocator is per-call with no global hook and json.hp exposes no allocator setter. stdlib_alloc_fail_at/after therefore cannot reach yyjson allocations, so no honest OOM test can be fabricated here (see DEEP_JSON_CONTRACT.md section 3).
JSON_TASK5 23 0
```

stderr is empty; exit code is `0`.

## Skips

- `json_alloc_failure_hook` — no allocator hook in `json.hp`/yyjson reachable
  through the public API; fabricating a malloc-failure test would be dishonest.

## Verification commands

From the repository root (checked-in `build/c+` driver):

```sh
./build/c+ build --strict --sanitize=none tests/stdlib/deep_json_task5.cp \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/stdlib-tests/deep_json_task5.strict_none
timeout 30 ./build/stdlib-tests/deep_json_task5.strict_none < /dev/null

./build/c+ build tests/stdlib/deep_json_task5.cp \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/stdlib-tests/deep_json_task5.default
timeout 30 ./build/stdlib-tests/deep_json_task5.default < /dev/null

./build/c+ build --sanitize=undefined tests/stdlib/deep_json_task5.cp \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/stdlib-tests/deep_json_task5.ubsan
timeout 30 ./build/stdlib-tests/deep_json_task5.ubsan < /dev/null
```

All three modes: build rc 0, run rc 0, stdout as above, stderr empty.
No sanitizer report and no C+ runtime fatal in any mode.
