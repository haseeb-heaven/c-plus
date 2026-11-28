# Safety audit — `stdlib/json.hp`

Branch: `agent/modern-stdlib`. Module owner for this document: agent2.
Scope: the C+ RAII JSON facade in `stdlib/json.hp` and the four suites that
exercise it. This is a **documentation audit of evidence**, not a new claim of
memory safety: a passing test only shows that the asserted paths behaved as
asserted on this host/build, in these modes. It does not prove absence of bugs.

## Sources read (primary evidence)

| Item | Path |
| --- | --- |
| Header under audit | `stdlib/json.hp` |
| Error helpers used by the header | `stdlib/error.hp` (`error_reset` / `error_fail_cat` are NULL-safe, lines 526-620) |
| Typed conversion suite | `tests/stdlib/deep_json_types.cp` |
| Stress suite | `tests/stdlib/deep_json_stress.cp` |
| Stress fixture | `tests/stdlib/deep_json_stress.stdin` (5 lines, 1 malformed) |
| Task-5 gap suite | `tests/stdlib/deep_json_task5.cp` |
| Base regression suite | `tests/stdlib/json_test.cp` (referenced by contract; not re-read here) |
| Family contract | `docs/stdlib/DEEP_JSON_CONTRACT.md` |
| Task-5 contract | `docs/stdlib/DEEP_JSON_TASK5_CONTRACT.md` |
| Recorded run artifacts | `build/stdlib-deep/deep_json_task5_default.out`, `deep_json_task5_default.err`, `deep_json_task5_ubsan.out`, `deep_json_types_ubsan.out`, `deep_json_stress_default.out` |

Confirmed-defect count for this module: **0** (no `stdlib/json.hp` change is
warranted by any reproduced behaviour; see §6). SKIP count: **1** (`json_alloc_failure_hook`).

---

## 1. Public API surface (VERIFIED — read from `stdlib/json.hp`)

`class json_view` (borrowed value handle; no owner reference; lines 21-138):

| Member | Signature / behaviour |
| --- | --- |
| `valid()` | `value != NULL` |
| `is_object()`, `is_array()`, `is_number()`, `is_string()`, `is_bool()` | thin `yyjson_is_*` predicates |
| `get_int(int *out, error *err)` | accepts only `yyjson_is_int`; explicit INT_MIN/INT_MAX and uint→int range checks; RANGE/BOUNDS on overflow |
| `get_double(double *out, error *err)` | accepts any `yyjson_is_num` |
| `get_bool(int *out, error *err)` | accepts only `yyjson_is_bool` |
| `get_string(const char **out, error *err)` | borrowed C string via `yyjson_get_str` |
| `size()` | array/object element count, else 0 |
| `at(size_t index)` | `yyjson_arr_get` (returns invalid view for OOB/non-array/NULL) |
| `member(const char *key)` | NULL-guarded `yyjson_obj_get` (returns invalid view for NULL key/NULL value/non-object) |
| `write(size_t *length)` | `yyjson_val_write(..., YYJSON_WRITE_NOFLAG, length)`; NULL on invalid view |

`class json` (owning parsed document; lines 140-178): `json(const char *text,
error *err)` (uses `strlen(text)`), `valid()`, `root()`, `drop()` (idempotent).

Free functions (lines 180-204): `json_parse_file(const char *path, error *err)`,
`json_view_of(const yyjson_val *)`, `json_free(char *)` (plain `free`).

`class json_builder` (owning mutable document; lines 206-303): `json_builder()`,
`valid()`, `add_int`, `add_double` (rejects non-finite with RANGE/BOUNDS),
`add_bool`, `add_string` (copies via `yyjson_mut_obj_add_strcpy`), `write`,
`drop()` (idempotent).

---

## 2. Allocation paths and fault-injection reachability

Every allocation in this module goes through **yyjson's allocator**, because
`json.hp` passes `NULL` for every `yyjson_alc *` argument. `stdlib_alloc_fail_at/after`
hooks the checked `stdlib_alloc`, so it **cannot reach any of these sites**.

| Call site in `stdlib/json.hp` | Backend allocation | Reachable by `stdlib_alloc_fail_*`? |
| --- | --- | --- |
| `json::json` → `yyjson_read_opts(..., NULL, &info)` (line 153) | yyjson default allocator | **No** — SKIP |
| `json_parse_file` → `yyjson_read_file(..., NULL, &info)` (line 190) | yyjson default allocator | **No** — SKIP |
| `json_view::write` → `yyjson_val_write` (line 136) | yyjson default allocator | **No** — SKIP |
| `json_builder()` → `yyjson_mut_doc_new(NULL)` (line 211) | yyjson default allocator | **No** — SKIP |
| `add_int/add_double/add_bool/add_string` → `yyjson_mut_obj_add_*` (lines 231/251/266/282) | yyjson default allocator | **No** — SKIP |
| `json_builder::write` → `yyjson_mut_write` (line 293) | yyjson default allocator | **No** — SKIP |
| `json_free(char *text)` → `free(text)` (line 203) | libc `free` (matches yyjson's default `malloc`) | N/A — deallocation |

**SKIP (not a pass):** `json_alloc_failure_hook` in `tests/stdlib/deep_json_task5.cp`
(lines 258-265) is recorded as `SKIP_REASON ...` in
`build/stdlib-deep/deep_json_task5_default.out`. `deep_json_stress.cp` lines
206-231 apply `stdlib_alloc_fail_after(0)` but deliberately accept either a
successful operation or a well-formed recoverable error
(`s_fault_parse`, `s_fault_build`, `s_fault_reset`); it asserts status shape,
not OOM behaviour. The mismatch is a documented design characteristic
(`DEEP_JSON_CONTRACT.md` §3, `DEEP_JSON_TASK5_CONTRACT.md` "Allocation-failure
gap"), **not** evidence that allocation failure is handled. `json.hp` exposes
no allocator setter and yyjson's allocator is per-call, so no honest OOM
injection can be fabricated through the public API.

VERIFIED: deterministic failure coverage that *is* reachable is exercised —
builder argument/value refusals (`deep_json_types.cp` T5, `deep_json_task5.cp`
`json_builder_null_err_args`, `json_dropped_builder_triple`).

---

## 3. Input-validation matrix (all rows VERIFIED against the named suite)

| Input / condition | Observation | Evidence |
| --- | --- | --- |
| Empty string `""` | invalid doc, INVALID(1)/UNSUPPORTED(108), non-empty message `"input length is 0"` | `deep_json_task5.cp` `json_empty_input` |
| Whitespace only `"  \n\t  "` | invalid, INVALID/UNSUPPORTED, `"input data is empty"` | `json_whitespace_input` |
| Truncated object `{` | invalid, `"unexpected end of data"` | `json_truncated_object` |
| Unmatched bracket `[1,2` | invalid, same truncated-data error | `json_unmatched_bracket` |
| Trailing comma object `{"a":1,}` / array `[1,]` | both invalid | `json_trailing_comma_obj`, `json_trailing_comma_arr` |
| Bad escape `{"a":"\q"}` | invalid, INVALID | `json_bad_escape` |
| Duplicate keys `{"a":1,"a":2}` | **parses**; `size()==2`; `member("a")` returns **first** value `1` | `json_duplicate_keys` |
| Embedded NUL, complete prefix `{"a":1}\0...` | valid (input is `strlen`-truncated); `size()==1`, `a==1`, trailing bytes ignored | `json_embedded_nul_complete_prefix` |
| Embedded NUL, incomplete prefix `{"a":\0...` | INVALID parse failure | `json_embedded_nul_incomplete_prefix` |
| NULL text `json((const char*)0, &err)` | invalid doc, **no error recorded** (OK/OK, `err.ok()==1`) — deliberate; contrasts with `json_parse_file(NULL)` | `json_null_text`; `DEEP_JSON_TASK5_CONTRACT.md` "NULL text is silent" |
| `json_parse_file(NULL)` | invalid doc, INVALID category + **IO code (109)** — deliberate contract split, not a defect | `deep_json_types.cp` `t_null_path`; `DEEP_JSON_CONTRACT.md` §2.2 |
| `get_int` at INT_MIN/INT_MAX, `-1`, `0` | STDLIB_OK, exact value | `t_int_zero`, `t_int_min`, `t_int_max`, `t_int_negative` |
| `get_int` at INT_MAX+1 / INT_MIN−1 / i64min / i64max / u64max | STDLIB_FAIL, output sentinel untouched, RANGE(2)/BOUNDS(101), no wrap/truncation | `t_int_over_max`, `t_int_under_min`, `t_int_i64max/min`, `t_int_u64max` |
| Beyond uint64 (`9999...`) stored as real | STDLIB_FAIL, INVALID | `t_int_huge_real` |
| Integral reals `1.0`, `1e2`, `-0.0` | STDLIB_FAIL, INVALID/UNSUPPORTED — no implicit real→int | `t_real_not_int`, `t_exp_not_int`, `t_negzero_not_int` |
| Parse overflow `1e400` | recoverable parse failure, INVALID — not infinity | `t_dbl_inf_parse_fail` |
| `get_double` on i64min/max, u64max, `1e-308`, `-1e-308`, `1e308` | accepted; approximate with documented tolerances (precision loss above 2^53 is inherent) | `json_double_i64max` … `json_double_1e308` |
| Wrong types (string→int, bool→double, number→string, array/object→bool) | STDLIB_FAIL, INVALID/UNSUPPORTED, no dereference of mismatched payload | `t_getint_on_str`, `t_getdbl_on_bool`, `t_getstr_on_num`, `t_getbool_on_arr/obj` |
| NULL `out` pointers (all four getters) | STDLIB_FAIL, INVALID | `t_null_out_int/dbl/bool/str` |
| NULL `error *` on getters/builders, `write(NULL length)`, `json_free(NULL)` | all accepted as documented (error helpers NULL-safe); wrong type with NULL err still refuses | `json_null_err_and_write_args`, `json_builder_null_err_args` |
| `member(NULL)` / `member("absent")` / `member` on array | invalid view, `size()==0` | `t_null_key`, `t_missing_member`, `t_member_on_arr` |
| `at(3)` / `at(SIZE_MAX)` on 3-element array; `at(0)` on string/object/number/null | invalid view (no wild pointer) | `t_arr_oob`, `t_arr_oob_max`, `t_at_on_str/obj/num/null` |
| JSON `null` value | **valid** view, all `is_*` false, `size()==0`, every typed getter refuses INVALID | `t_json_null_view`, `json_view_of_null` |
| Post-drop document: `root()`, `at()`, `member()`, getters, `write()` | all invalid / refuse; triple `drop()` idempotent | `json_dropped_doc_views`, `t_invalid_doc_root`, `t_invalid_doc_write` |
| Post-drop builder: all four `add_*`, `write()` | all refuse (INVALID/UNSUPPORTED) even with NULL err; triple `drop()` idempotent | `t_dropped_add_int/dbl/bool/str`, `t_dropped_write`, `t_builder_double_drop`, `json_dropped_builder_triple` |
| Non-finite builder doubles (`NAN`, `INFINITY`) | STDLIB_FAIL, RANGE/BOUNDS | `t_add_nan`, `t_add_inf` |
| NULL builder key/value | STDLIB_FAIL, INVALID | `t_add_null_key`, `t_add_null_value` |
| Repeated `write()` (builder and document), escaping `\"` `\\` | byte-identical buffers/lengths; re-serialized dump round-trips | `t_builder_repeat_write`, `t_builder_escape`, `t_doc_repeat_write`, `t_roundtrip_write` |

---

## 4. Resource cleanup and lifetime (VERIFIED where noted)

- `json::drop()` calls `yyjson_doc_free` and NULLs the handle (line 172-177);
  `json_builder::drop()` calls `yyjson_mut_doc_free` and NULLs both handles
  (line 296-302). Both are explicitly idempotent/triple-drop tested
  (`json_dropped_doc_views`, `json_dropped_builder_triple`, `t_builder_double_drop`).
- Serialized buffers are released with `json_free()`/`free()` after every write
  in both suites; release of a NULL buffer is a no-op (`json_null_err_and_write_args`).
- `json_view` **borrows** from its owner and has no owner reference: after
  `doc.drop()` any derived view is dangling and must not be used. Suites use
  views only before `drop()`; the rule is documented, not exercised
  (`DEEP_JSON_CONTRACT.md` §2.4). **UNTESTED:** actual use-after-free or
  double-free is never executed.
- Owning `json` copy is rejected at compile time (`json b = a;` →
  `"resource 'b' cannot be copied from 'a'"`); verified once out-of-tree as a
  one-shot compile, not part of a suite (`DEEP_JSON_CONTRACT.md` §2.4).
- Bounded recursion/size in stress: `walk()` budget `JSON_DEEP_BUDGET=96`,
  `JSON_LINE_CAP=8192` via `fgets`, generated 900-element array, 64-level object,
  `alarm(20)` backstop (`deep_json_stress.cp` lines 27-28, 42-60, 133-145,
  148-204). No unbounded recursion is exercised.

---

## 5. Test counts and recorded results (VERIFIED from artifacts)

| Suite | Expected | Recorded artifact | Result |
| --- | --- | --- | --- |
| `deep_json_task5.cp` | `JSON_TASK5 23 0` (23 checks, 0 failures, 1 SKIP folded as a passing line) | `build/stdlib-deep/deep_json_task5_default.out`; also `deep_json_task5_ubsan.out` | `JSON_TASK5 23 0`, empty `.err` |
| `deep_json_types.cp` | `RESULT 65 0` (65 checks, 0 failed) | `build/stdlib-deep/deep_json_types_ubsan.out` | `RESULT 65 0` |
| `deep_json_stress.cp` | `RESULT 20 0` (20 checks incl. 5 stdin line groups, 0 failed) | `build/stdlib-deep/deep_json_stress_default.out` | `RESULT 20 0` |

Exactly: **JSON_TASK5 23**, **deep_json_types 65**, **stress 20**. The contracts
report pass in strict+no-sanitize, default, UBSan and ASan+UBSan modes on this
host (macOS/arm64); `deep_json_task5` artifact exists for default and UBSan.
No `.expected` diff is used; exit 0 + empty stderr is the contract. These
results are evidence of the asserted behaviours **only** — they are not a
memory-safety proof.

---

## 6. Confirmed defects with evidence

**Confirmed defect count: 0.** No reproduced behaviour contradicted
`stdlib/json.hp`'s documented API, and the header was not modified. The
contracts' finding tables (`DEEP_JSON_CONTRACT.md` §4) classify all five
observations as deliberate contracts/limitations, not fixable defects:

1. 65/23/20 assertion sets match intended behaviour — no truncation, no
   range acceptance, no wrong-type acceptance.
2. `json_parse_file(NULL)` pairs INVALID with IO code — deliberate.
3. yyjson allocator bypass — documented characteristic (SKIP above).
4. No object key enumeration, no string-length accessor, JSON null is a valid
   untyped view — documented API limitations.
5. Views dangle after owner drop; owning copies rejected at compile time —
   documented lifetime rule.

---

## 7. UNTESTED paths (explicitly not covered by any of the four suites)

1. **All yyjson allocation-failure paths** (parse, file parse, builder create,
   every `add_*`, every `write`) — SKIP; no reachable injection point (§2).
2. **Actual view use-after-owner-drop / double-free** — not executed; only the
   post-drop *refusal* contract is pinned. Fatal-safety behaviour is unrecorded.
3. **`json_parse_file` success/failure with real paths** — only the NULL path
   case is exercised. Valid file, missing file, permission-denied file,
   directory-as-file, and empty file are untested by these suites.
4. **NUL bytes inside JSON string values** (e.g. `"\u0000"`) — `get_string`
   returns a borrowed C string with no length accessor, so such strings cannot
   be measured; no test exists.
5. **Deeply nested arrays beyond the 10-level fixture** — stress uses a 10-level
   array fixture, a 64-level *object* chain and a flat 900-element array; the
   96-level walk budget is never approached.
6. **Fuzzing / arbitrary malformed JSON** — only the hand-picked malformed
   cases in §3 are covered.
7. **Builder state transitions** — mutation between two `write()` calls,
   duplicate keys added via the builder, very long keys/strings, and a
   zero-field builder write are untested.
8. **`get_double` on a beyond-uint64 stored real** and other rare numeric
   representations — not exercised.
9. **Custom yyjson allocator / `json_free` mismatch** — unreachable from the
   public API, and no test pairs `json_free` with a non-default allocator.
10. **Thread-safety / concurrent access** to a `json`, `json_view` or
    `json_builder` — no contract or test in this family.
11. **Non-macOS/Windows runtime behaviour** of the vendored yyjson path — all
    recorded evidence is from macOS/arm64.

**Bottom line:** 0 confirmed defects, 1 SKIP, and the untested list above.
Passing tests do not establish that `stdlib/json.hp` or yyjson are free of
memory-safety defects on unexercised paths.
