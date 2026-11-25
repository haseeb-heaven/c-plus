# Deep Task-4 contract for `stdlib/string.hp` (uncovered cases only)

Scope: `stdlib/string.hp` only. This fixture covers **only** the Task-4 cases
that were absent from `tests/stdlib/deep_string_append.cp` (append/reserve/drop
success paths, fault injection, fatal forks) and `tests/stdlib/deep_string_view.cp`
(generation-based invalidation, fixed-window in-place append).

Fixture: `tests/stdlib/deep_string_task4.cp`.
`stdlib/string.hp`, `stdlib/error.hp`, `deep_string_append.cp` and
`deep_string_view.cp` were **not modified**. No `.expected` file is used (file
ownership is restricted); the exact stdout is pinned in section 5.

The fixture defines `STDLIB_ALLOC_LIMIT` to `4096` **before** including
`stdlib/error.hp`. That is the allocator's documented build-time override
(error.hp lines 57-67, `#ifndef` guarded), so every "limit" assertion uses a
4096-byte boundary and never allocates near the real 1 TiB limit.

## 1. Classification vocabulary

| Class | Meaning |
| --- | --- |
| **DOCUMENTED** | Stated verbatim or directly implied by a comment in `stdlib/string.hp` (or `stdlib/error.hp` for the allocator/telemetry helpers). |
| **UNDOCUMENTED** | Behaviour the header implements but does not promise in comments. Observed and pinned here as an implementation fact; not a contract guarantee a caller should rely on without this test. |
| **DEFECT** | A real mismatch between the header's documented contract and its behaviour. **None found.** |

Constants used here: `STDLIB_ALLOC_LIMIT=4096`, `STDLIB_OK=0`, `STDLIB_FAIL=-1`.

## 2. Coverage map (tags -> case) and DOCUMENTED / UNDOCUMENTED status

### 2.1 Constructor allocation failure and invalid-object reads (17 checks)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_ctor_allocfail_valid`, `_size`, `_cap`, `_cstr`, `_at0` | `stdlib_alloc_fail_at(1)` before `string t4_bad = string("abc")`: `valid()==0`, `size()==0`, `capacity()==0`, `c_str()==""` (non-NULL), `at(0)=='\0'` | **DOCUMENTED** (header lines 8-12, 146, 157; `at()` out-of-range guard line 152) |
| `t4_ctor_reinit_append`, `_reserve_pre`, `_reserve`, `_copy` | After reset, each of `append`, `reserve(n>0)`, `copy_from` re-initialises the failed object to `valid()==1` | **DOCUMENTED** (header lines 8-12 explicitly name all three) |
| `t4_invalid_valid`, `_equals_empty`, `_equals_nonempty`, `_compare_empty`, `_compare_nonempty`, `_find`, `_utf8`, `_drop_inert` | On an invalid object: `equals("")==1`, `compare("")==0`, `compare("x")==-1`, `find(anything)==-1`, `is_valid_utf8()==1`, `drop()` inert | `equals`/`compare` NULL behaviour and "behaves as empty for reads" are **DOCUMENTED** (lines 12, 14-16, 162-196); `find` on invalid and inert `drop` are **UNDOCUMENTED** implementation facts consistent with lines 200-206 and 385-392 |

Note: `string(NULL)`, `append(NULL)`, `equals(NULL)`, `compare(NULL)` are already
covered by `deep_string_append.cp` and are deliberately **not repeated** here.

### 2.2 `copy_from` (12 checks incl. telemetry)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_copy_null_rc`, `_null_unchanged` | `copy_from(NULL) == -1`; size/capacity/`c_str()` pointer/content unchanged | **UNDOCUMENTED** in string.hp (failure-atomicity is documented, but the NULL-source return is only established by `deep_string_append.cp`) |
| `t4_copy_self_rc`, `_self_unchanged` | `copy_from(&self) == 0`, state unchanged | **UNDOCUMENTED** (guard at line 301 has no comment) |
| `t4_copy_shorter_rc`, `_shorter_state`, `_longer_rc`, `_longer_state` | Overwrite non-empty with shorter and longer sources; capacity becomes `n+1` each time | contents/capacity result **UNDOCUMENTED**; failure-atomicity of the operation is **DOCUMENTED** (line 292) |
| `t4_copy_fail_rc`, `_fail_unchanged` | `stdlib_alloc_fail_at(1)` at the *single* allocation point: `-1` with size, capacity, `c_str()` pointer and content all unchanged | **DOCUMENTED** (line 292 "Failure-atomic") |
| `t4_copy_retry` | Retry after `stdlib_alloc_fail_reset()` succeeds | **UNDOCUMENTED** (reasonable, not promised) |
| `t4_copy_telemetry_activity`, `_balanced` | With telemetry enabled, allocations inside the measured window equal frees after explicit drops | **DOCUMENTED** for the allocator counters (`error.hp` lines 18-28, 127-161, 365-392, 517-524); the *string* copy/drop accounting pattern is **UNDOCUMENTED** but follows directly from `stdlib_alloc`/`stdlib_release` usage |

Telemetry note: the two owner strings are constructed **inside** the measured
window. Constructing them before `stdlib_telemetry_reset()` makes their buffers
get freed inside the window without a matching counted allocation, producing
`alloc=2, free=4`. That is a test-window bookkeeping issue, not a library leak.

### 2.3 Exact capacity boundaries (13 checks)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_cap_initial` | `string("abc")` -> `capacity()==4` | **UNDOCUMENTED** (small-string allocation `n+1` is in the implementation, not the comments) |
| `t4_cap_at_rc`, `_at_noop_seq`, `_at_noop_state` | `reserve(n)` with `n == capacity`: 0, `stdlib_alloc_seq` delta **0**, state unchanged | **UNDOCUMENTED** (the early return at line 211 is not commented as a no-allocation promise) |
| `t4_cap_plusone_rc`, `_plusone_seq`, `_plusone_state` | `reserve(capacity+1)`: exactly **one** allocation (seq delta 1), `capacity()==n` | **UNDOCUMENTED** (allocation count and exact-size growth are implementation details) |
| `t4_cap_fill_setup`, `_fill_rc`, `_fill_seq`, `_fill_state` | Append that makes `count+extra+1 == capacity`: 0, seq delta **0**, exact content | **UNDOCUMENTED** growth policy |
| `t4_cap_onemore_rc`, `_onemore_seq`, `_onemore_state` | Append needing one more byte: seq delta **1**, capacity doubles 8->16, exact content | **UNDOCUMENTED** growth/doubling policy |

`stdlib_alloc_seq()` (error.hp lines 247-251) counts every `stdlib_alloc`
*attempt* since the last reset/fail injection, so a delta of 0 is a direct
"no allocation happened" observation.

### 2.4 Repeated drop and use-after-drop (7 checks)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_drop3_state`, `_cstr_nonnull`, `_cstr_empty`, `_at0` | `drop()` three times: `valid()==0`, `size()==0`, `capacity()==0`, `c_str()` never NULL and `==""`, `at(0)=='\0'` | **DOCUMENTED** (lines 146, 157, 385-392); triple-drop inertness is **DOCUMENTED** by the `chars != NULL` guard |
| `t4_drop3_reinit_append`, `_reserve`, `_copy` | After three drops, append/reserve/copy_from all re-initialise successfully | **DOCUMENTED** (header lines 8-12) |

### 2.5 Integer overflow with reduced limit (7 checks)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_ovf_reserve_rc`, `_noalloc`, `_unchanged` | `reserve(LIMIT+1) == -1`, `stdlib_alloc_seq` delta **0**, state unchanged | **DOCUMENTED** (limit rejection is implemented at lines 213-217 and the `STDLIB_ALLOC_LIMIT` override is documented in error.hp; "no allocation attempt" is an implementation fact, so the seq assertion is **UNDOCUMENTED**) |
| `t4_ovf_source_built` | 2999-byte source string fits under 4096 | **UNDOCUMENTED** (arithmetic consequence of the override) |
| `t4_ovf_append_rc`, `_noalloc`, `_unchanged` | Append pushing `count+extra+1` past the limit: `-1`, seq delta **0**, byte-exact unchanged state | **DOCUMENTED** (lines 251-256 + line 8 failure-atomicity); seq delta 0 is **UNDOCUMENTED** |

No allocation near the real 1 TiB limit is attempted: the overflow branch is
reached at 4097 bytes with a 2999-byte source.

### 2.6 `substr` / field boundaries (17 checks)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_sub_null_out`, `t4_field_null_out`, `t4_sub_zero_outsize`, `t4_field_zero_outsize` | NULL out or `out_size==0` -> `-1` | **UNDOCUMENTED** (guards at lines 328 and 358 have no comments; already partly noted in `DEEP_STRING_CONTRACT.md`) |
| `t4_sub_start_gt_size` | `start > size()` -> `-1` | **UNDOCUMENTED** (guard at line 330) |
| `t4_sub_start_eq_size` | `start == size()` -> 0, empty output | **UNDOCUMENTED** (boundary consequence of the implementation) |
| `t4_sub_clamp` | `n > available` clamps to available (`",b,c"`) | **DOCUMENTED** ("copy up to `n` characters", line 325) |
| `t4_sub_exact` | Exact fit `n+1 == out_size` (`"a,b,c"` with `out_size==6`) | **DOCUMENTED** ("always terminated", line 325) |
| `t4_sub_truncate` | `out_size==3` copies at most 2 bytes: `"a,"` | **UNDOCUMENTED** (exact truncation to `out_size-1` is an implementation rule; note it is the first two bytes of `"a,b,c"`, i.e. `"a,"`) |
| `t4_field_sep_first_count`, `_first_0`, `_first_1`, `t4_field_sep_last_count`, `_last_0`, `_last_1` | Separator at first/last position produces an empty leading/trailing field; `count_fields()==2` | **DOCUMENTED** ("separator-delimited fields", lines 343-353) |
| `t4_field_index_oor` | Index beyond the field count -> `-1` | **UNDOCUMENTED** (fall-through at line 375) |
| `t4_field_truncate` | Field output truncated to `out_size-1` | **UNDOCUMENTED** (same rule as `substr` truncation) |

### 2.7 `string_view` (13 checks)

| Tag(s) | Case | Status |
| --- | --- | --- |
| `t4_view_size`, `_data_identity`, `_equals`, `_utf8_valid_bytes` | Accessors and exact content on a live view; `is_valid_utf8()==1` over explicit 5-byte UTF-8 array | **DOCUMENTED** by the class role "borrowed view ... valid while its owner lives" (line 79) and the UTF-8 validator contract (lines 26-29). Per-method comments are absent, so accessor return semantics are self-evident rather than spelled out. |
| `t4_view_equals_null`, `t4_view_empty_equals_null` | `equals(NULL)==0` on populated and empty views | **UNDOCUMENTED** in the view class; consistent with `string::equals(NULL)` (line 16) |
| `t4_view_utf8_invalid_bytes` | `is_valid_utf8()==0` over explicit `C3 28` byte array | **DOCUMENTED** (lines 26-29) |
| `t4_view_stale_reserve_rc`, `_pointer_moved`, `_data_unchanged`, `_size_unchanged` | Capture `view.data()`, force reallocation with `reserve(64)`, assert `view.data() != owner.c_str()`, old pointer and `size()` snapshot unchanged; **never call `equals()`/`is_valid_utf8()` on the stale view** | **DOCUMENTED** lifetime rule (line 79) + caller rule in `DEEP_STRING_CONTRACT.md` §"string_view lifetime". The *pointer-inequality* observation itself is **UNDOCUMENTED** as an API promise. |

**No generation tracking.** `string_view` stores only `(const char *chars,
size_t count)` (lines 80-81). It has no owner handle, generation counter or
`drop()`. `to_view()` is a snapshot; a reallocating `reserve()`/`append()` or a
`drop()` invalidates the borrowed pointer, and detecting that is entirely the
**caller's responsibility**. This fixture therefore compares pointer/size
values only and never dereferences the stale view. (`deep_string_view.cp`
additionally probes staleness via the allocator's metadata generation; this
fixture deliberately does not, to keep the sections independent.)

## 3. Test-side issues found and resolved (NOT library defects)

1. **C+ name-based method resolution.** Reusing the identifier `t4_bad` for a
   `string_view` in section 2.7 made the compiler infer `t4_bad.c_str()` in
   section 2.1 as `int`, producing four `[CPLUS-FFI] incompatible integer to
   pointer conversion` build errors at lines 68/73/80/88. Renaming the view
   variable to `t4_badview` fixed the build in all three modes. This is a C+
   lowering quirk with duplicate owned-variable names across functions, not a
   `string.hp` defect.
2. **Telemetry window** (section 2.2): fixed as described above.
3. **`substr` expectation** (section 2.6): the correct truncation of
   `"a,b,c"` into a 3-byte output is `"a,"`, not `"ab"`; the fixture's
   expectation was corrected. No header change was needed.

## 4. Verification commands and exact results

Requested order: **STRICT with sanitizers disabled FIRST**, then default, then
UBSan. Every build and every run is wrapped in `timeout 30`. Flags are separate
argv operands (quoting them as one string makes clang reject the option).

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/task4
mkdir -p "$OUT"

# 1) STRICT, sanitizers disabled
timeout 30 build/c+ build tests/stdlib/deep_string_task4.cp \
  --strict --sanitize=none -o "$OUT/task4_strict_none"
timeout 30 "$OUT/task4_strict_none"

# 2) default
timeout 30 build/c+ build tests/stdlib/deep_string_task4.cp \
  -o "$OUT/task4_default"
timeout 30 "$OUT/task4_default"

# 3) UBSan
timeout 30 build/c+ build tests/stdlib/deep_string_task4.cp \
  --strict --sanitize=undefined -o "$OUT/task4_ubsan"
timeout 30 "$OUT/task4_ubsan"
```

| Mode | Build rc / stderr bytes | Run rc / stderr bytes | Final line |
| --- | --- | --- | --- |
| strict-none (run first) | 0 / 0 | 0 / 0 | `STRING_TASK4 86 0` |
| default | 0 / 0 | 0 / 0 | `STRING_TASK4 86 0` |
| ubsan | 0 / 0 | 0 / 0 | `STRING_TASK4 86 0` |

No timeouts, no sanitizer diagnostics, no fatal exits, and no stderr output in
any mode. The 86 tag lines are byte-identical across all three modes.

Harness integration (also green):

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_BIN=$PWD/build/c+ \
  sh tests/stdlib/run_deep_tests.sh deep_string_task4
```

```
ok       deep_string_task4 (default): STRING_TASK4 86 0
ok       deep_string_task4 (strict-none): STRING_TASK4 86 0
ok       deep_string_task4 (ubsan): STRING_TASK4 86 0
...
deep stdlib tests: 3 passed, 0 failed, 0 skipped (1 file(s) x 3 modes)
```

## 5. Exact expected stdout (all three modes identical)

```
t4_ctor_allocfail_valid 1
t4_ctor_allocfail_size 1
t4_ctor_allocfail_cap 1
t4_ctor_allocfail_cstr 1
t4_ctor_allocfail_at0 1
t4_ctor_reinit_append 1
t4_ctor_reinit_reserve_pre 1
t4_ctor_reinit_reserve 1
t4_ctor_reinit_copy 1
t4_invalid_valid 1
t4_invalid_equals_empty 1
t4_invalid_equals_nonempty 1
t4_invalid_compare_empty 1
t4_invalid_compare_nonempty 1
t4_invalid_find 1
t4_invalid_utf8 1
t4_invalid_drop_inert 1
t4_copy_null_rc 1
t4_copy_null_unchanged 1
t4_copy_self_rc 1
t4_copy_self_unchanged 1
t4_copy_shorter_rc 1
t4_copy_shorter_state 1
t4_copy_longer_rc 1
t4_copy_longer_state 1
t4_copy_fail_rc 1
t4_copy_fail_unchanged 1
t4_copy_retry 1
t4_copy_telemetry_activity 1
t4_copy_telemetry_balanced 1
t4_cap_initial 1
t4_cap_at_rc 1
t4_cap_at_noop_seq 1
t4_cap_at_noop_state 1
t4_cap_plusone_rc 1
t4_cap_plusone_seq 1
t4_cap_plusone_state 1
t4_cap_fill_setup 1
t4_cap_fill_rc 1
t4_cap_fill_seq 1
t4_cap_fill_state 1
t4_cap_onemore_rc 1
t4_cap_onemore_seq 1
t4_cap_onemore_state 1
t4_drop3_state 1
t4_drop3_cstr_nonnull 1
t4_drop3_cstr_empty 1
t4_drop3_at0 1
t4_drop3_reinit_append 1
t4_drop3_reinit_reserve 1
t4_drop3_reinit_copy 1
t4_ovf_reserve_rc 1
t4_ovf_reserve_noalloc 1
t4_ovf_reserve_unchanged 1
t4_ovf_source_built 1
t4_ovf_append_rc 1
t4_ovf_append_noalloc 1
t4_ovf_append_unchanged 1
t4_sub_null_out 1
t4_field_null_out 1
t4_sub_zero_outsize 1
t4_field_zero_outsize 1
t4_sub_start_gt_size 1
t4_sub_start_eq_size 1
t4_sub_clamp 1
t4_sub_exact 1
t4_sub_truncate 1
t4_field_sep_first_count 1
t4_field_sep_first_0 1
t4_field_sep_first_1 1
t4_field_sep_last_count 1
t4_field_sep_last_0 1
t4_field_sep_last_1 1
t4_field_index_oor 1
t4_field_truncate 1
t4_view_size 1
t4_view_data_identity 1
t4_view_equals 1
t4_view_equals_null 1
t4_view_empty_equals_null 1
t4_view_utf8_valid_bytes 1
t4_view_utf8_invalid_bytes 1
t4_view_stale_reserve_rc 1
t4_view_stale_pointer_moved 1
t4_view_stale_data_unchanged 1
t4_view_stale_size_unchanged 1
STRING_TASK4 86 0
```

86 checks, 0 failures. Exit code **0**; **zero bytes on stderr**.

## 6. Defects found in `string.hp`

**None.** Every Task-4 case passes against the unchanged header in all three
modes. The three issues encountered were all test-side (section 3) and were
fixed without touching `stdlib/string.hp` or `stdlib/error.hp`.
