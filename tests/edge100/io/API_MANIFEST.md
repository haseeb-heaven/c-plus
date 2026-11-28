# EDGE100 io -- API / case manifest

Every public API in `stdlib/io.hp`, `stdlib/file.hp`, `stdlib/fs.hp` and `stdlib/json.hp`, with its edge-case matrix and per-case result. Generated from `out/cases.log`.

Summary: `EDGE100 io 62 279 279 0`

| # | API | Header | Cases (case=result) | Overall |
|---|-----|--------|---------------------|---------|
| 1 | `io.println` | io.hp | valid=PASS | PASS |
| 2 | `io.stdlib_io_token` | io.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 3 | `io.read_line` | io.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 4 | `io.read_int` | io.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, malformed=PASS, truncated=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 5 | `io.read_long` | io.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, malformed=PASS, truncated=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 6 | `io.read_float` | io.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, malformed=PASS, truncated=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 7 | `io.read_double` | io.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, malformed=PASS, truncated=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 8 | `file.ctor` | file.hp | valid=PASS, null=PASS, zero=PASS, invalid_handle=PASS | PASS |
| 9 | `file.is_open` | file.hp | valid=PASS, null=PASS, invalid_handle=PASS, double_close=PASS | PASS |
| 10 | `file.native` | file.hp | valid=PASS, null=PASS, double_close=PASS | PASS |
| 11 | `file.read` | file.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, invalid_handle=PASS, double_close=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 12 | `file.read_line` | file.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, eof=PASS, invalid_handle=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 13 | `file.write` | file.hp | valid=PASS, null=PASS, zero=PASS, invalid_handle=PASS, double_close=PASS, crlf=PASS | PASS |
| 14 | `file.write_bytes` | file.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, invalid_handle=PASS, double_close=PASS | PASS |
| 15 | `file.seek` | file.hp | valid=PASS, zero=PASS, invalid_handle=PASS, double_close=PASS | PASS |
| 16 | `file.flush` | file.hp | valid=PASS, invalid_handle=PASS, double_close=PASS | PASS |
| 17 | `file.close` | file.hp | valid=PASS, null=PASS, invalid_handle=PASS, double_close=PASS | PASS |
| 18 | `file.drop` | file.hp | valid=PASS, double_close=PASS | PASS |
| 19 | `file_borrow` | file.hp | valid=PASS, null=PASS, double_close=PASS | PASS |
| 20 | `file_temp` | file.hp | valid=PASS, null=PASS, alloc_fail=PASS | PASS |
| 21 | `fs.exists` | fs.hp | valid=PASS, null=PASS, zero=PASS, malformed=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 22 | `fs.is_file` | fs.hp | valid=PASS, null=PASS, zero=PASS, malformed=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 23 | `fs.is_dir` | fs.hp | valid=PASS, null=PASS, zero=PASS, malformed=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 24 | `fs.size` | fs.hp | valid=PASS, null=PASS, zero=PASS, malformed=PASS | PASS |
| 25 | `fs.remove` | fs.hp | valid=PASS, null=PASS, zero=PASS, double_close=PASS, exact_fit=PASS | PASS |
| 26 | `fs.rename` | fs.hp | valid=PASS, null=PASS, zero=PASS, invalid_handle=PASS, exact_fit=PASS | PASS |
| 27 | `fs.mkdir` | fs.hp | valid=PASS, null=PASS, zero=PASS, double_close=PASS, exact_fit=PASS | PASS |
| 28 | `fs.mkdtemp` | fs.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, malformed=PASS, exact_fit=PASS | PASS |
| 29 | `json.json_input_within_budget` | json.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, malformed=PASS, depth_16384=PASS, depth_16385=PASS, crlf=PASS | PASS |
| 30 | `json.ctor` | json.hp | valid=PASS, null=PASS, zero=PASS, malformed=PASS, truncated=PASS, depth_16384=PASS, depth_16385=PASS, exact_fit=PASS, crlf=PASS | PASS |
| 31 | `json.valid` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 32 | `json.read_size` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 33 | `json.value_count` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 34 | `json.root` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 35 | `json.drop` | json.hp | valid=PASS, double_close=PASS | PASS |
| 36 | `json.parse_budget` | json.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, malformed=PASS, truncated=PASS, depth_16384=PASS, depth_16385=PASS, exact_fit=PASS, crlf=PASS, alloc_fail=PASS | PASS |
| 37 | `json.parse_file` | json.hp | valid=PASS, null=PASS, zero=PASS, malformed=PASS, truncated=PASS, depth_16384=PASS, depth_16385=PASS, exact_fit=PASS, crlf=PASS, alloc_fail=PASS | PASS |
| 38 | `json_view_of` | json.hp | valid=PASS, null=PASS | PASS |
| 39 | `json_close_stream` | json.hp | valid=PASS, null=PASS | PASS |
| 40 | `json_free` | json.hp | valid=PASS, null=PASS | PASS |
| 41 | `json_view.valid` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 42 | `json_view.is_object` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 43 | `json_view.is_array` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 44 | `json_view.is_number` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 45 | `json_view.is_string` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 46 | `json_view.is_bool` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 47 | `json_view.get_int` | json.hp | valid=PASS, null=PASS, zero=PASS, one_over=PASS, invalid_handle=PASS, malformed=PASS | PASS |
| 48 | `json_view.get_double` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 49 | `json_view.get_bool` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 50 | `json_view.get_string` | json.hp | valid=PASS, null=PASS, malformed=PASS | PASS |
| 51 | `json_view.size` | json.hp | valid=PASS, null=PASS, zero=PASS | PASS |
| 52 | `json_view.at` | json.hp | valid=PASS, zero=PASS, one_over=PASS, invalid_handle=PASS | PASS |
| 53 | `json_view.member` | json.hp | valid=PASS, null=PASS, invalid_handle=PASS | PASS |
| 54 | `json_view.write` | json.hp | valid=PASS, null=PASS | PASS |
| 55 | `json_builder.ctor` | json.hp | valid=PASS | PASS |
| 56 | `json_builder.valid` | json.hp | valid=PASS, double_close=PASS | PASS |
| 57 | `json_builder.add_int` | json.hp | valid=PASS, null=PASS, zero=PASS, invalid_handle=PASS | PASS |
| 58 | `json_builder.add_double` | json.hp | valid=PASS, null=PASS, zero=PASS, invalid_handle=PASS, malformed=PASS | PASS |
| 59 | `json_builder.add_bool` | json.hp | valid=PASS, null=PASS, invalid_handle=PASS | PASS |
| 60 | `json_builder.add_string` | json.hp | valid=PASS, null=PASS, zero=PASS, invalid_handle=PASS | PASS |
| 61 | `json_builder.write` | json.hp | valid=PASS, double_close=PASS | PASS |
| 62 | `json_builder.drop` | json.hp | valid=PASS, double_close=PASS | PASS |

## Case legend

| Case | Meaning |
|------|---------|
| `valid` | well-formed input / correct call |
| `null` | NULL pointer argument |
| `zero` | zero length / zero value / empty path |
| `one_over` | one byte short buffer, oversized token or SIZE_MAX length |
| `eof` | empty stream / end of file |
| `invalid_handle` | NULL or failed-open file handle / missing path |
| `double_close` | second close/drop/remove after the first succeeded |
| `malformed` | syntactically invalid JSON / invalid type / bad template |
| `truncated` | truncated JSON document |
| `depth_16384` | 16384 nested containers (inside the default budget) |
| `depth_16385` | 16385 nested containers (one over the default budget) |
| `exact_fit` | buffer or length exactly matches the payload |
| `crlf` | CR / CRLF line endings |
| `alloc_fail` | injected realloc/tmpfile failure or pre-allocation length guard |

APIs with no applicable case for a category omit it (for example `println` has no handle to invalidate). Allocation failure is injected into header-side `realloc`/`tmpfile` calls; yyjson-internal allocations are covered by the pre-allocation length/depth guards.
