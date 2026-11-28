# Deep contract: `stdlib/error.hp` and `stdlib/vector.hp`

Per-family evidence for `tests/stdlib/deep_error_vector.cp`. Harness-level
rules, exact driver flags and the fatal/recoverable vocabulary live in
`docs/stdlib/DEEP_TESTS_HARNESS.md`; this document records only what this
family does, what was observed, and the reproduction commands.

Status at hand-off: **no confirmed defect** in either header for any scenario
below. Neither `stdlib/error.hp` nor `stdlib/vector.hp` was modified.

---

## 1. Scope and ownership

| Item | Path |
| --- | --- |
| Test | `tests/stdlib/deep_error_vector.cp` |
| Contract | `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md` (this file) |
| Headers under test | `stdlib/error.hp`, `stdlib/vector.hp` |
| Modified headers | **none** (no reproduced defect to fix) |

The test is self-checking, has no `.stdin` and no `.expected` file, and returns
`0` only when every check passes. Each record is
`<scenario> <0|1> <S|R|F>` where `S` = success path, `R` = recoverable failure,
`F` = fatal-class marker. The trailing `F` is **only a summary marker**; no
fatal scenario is executed in the test process (see §5).

---

## 2. Scenario inventory and classification

### `error.hp`

| ID | Scenario | Class | Asserted observable |
| --- | --- | --- | --- |
| E1 | message truncation | S | 255-byte message fits exactly (`strlen == 255`, exact `memcmp`); 256-byte and `(STDLIB_ERROR_SIZE + 32)`-byte inputs truncate to 255 bytes with exact prefix; `NULL` message yields `""`. Buffer always NUL-terminated. |
| E2 | repeated reset/set | S | `set` -> `clear` -> `set` -> 4x `error_reset`+`error_fail` -> `error_reset`; `failed_now()`, `ok()`, `code()`, `category()`, `what()` correct every time. A long message followed by a short one reads back exactly `"short"` (no stale tail). |
| E3 | NULL error pointer | R | `error_reset(NULL)`, `error_fail(NULL, ...)`, `error_fail_cat(NULL, ...)` return without crash; telemetry `api_failures >= 2` proves the note path still ran. |
| E4 | category correctness | S | Every exit code maps to the documented category (table in §3), callback sees the same category/code, `error_fail_cat` overrides correctly, reset returns `STDLIB_CATEGORY_OK`, all nine `stdlib_category_name()` strings plus the `"unknown"` fallback match, exactly 11 callback invocations. |
| E5 | status/exit-code mapping | S | `code()` returns the exact `cplus_exit_code` even when the category is overridden; reset restores `CPLUS_EXIT_OK`; lifecycle codes survive `error_fail`. |

### `vector.hp`

| ID | Scenario | Class | Asserted observable |
| --- | --- | --- | --- |
| V1 | growth boundaries | S | `0 -> 4 -> 8 -> 16` on the 1st/5th/9th `push_back`; `reserve` smaller than or equal to capacity is a no-op; `reserve(23)` then `reserve(24)` set capacity exactly (one-over); all nine element values preserved. |
| V2 | empty slices | S | `view()` on an empty vector: `size == 0`, `valid() == 0`, `stale() == 0`, `at_checked(0)` refuses; `sub(0,5)`, `sub(2,0)` and `sub(99,2)` on empty/out-of-range return empty slices; `sub(1,99)` clamps to 2 elements and reads 2,3. |
| V3 | stale slices | S | Slice stays fresh and readable across `push_back` **without** reallocation (size grows to 4, `at_checked(2)` refuses because the recorded extent is 2); becomes stale and unreadable at the `4 -> 8` reallocation; a `sub()`-slice inherits the same generation. A fresh slice goes stale after `pop_back`, `insert`, `erase`, `clear`, `copy_from`, `reserve`, `shrink_to_fit` and `drop`. |
| V4 | reserve/growth failure + recovery | R | `stdlib_alloc_fail_after(0)` makes `reserve(64)` and a capacity-full `push_back` return `-1`; size, capacity, element values, generation and slice freshness are unchanged; `alloc_failures == 1` exactly; `stdlib_alloc_fail_reset()` then the same operation succeeds with the expected new capacity and the old slice now stale. |
| V5 | get/pop on empty | R | `at(0) == 0`, `at_checked(0, &out) == -1` with `out` sentinel preserved, `pop_back(&out) == -1` and `pop_back(NULL) == -1` on a fresh and on a cleared vector; one successful pop returns the last value, the next pop refuses. |
| V6 | allocation-overflow shapes | R | `reserve(SIZE_MAX)` and `reserve(STDLIB_ALLOC_LIMIT/sizeof(int) + 1)` return `-1` with state unchanged and no allocation; `stdlib_calloc(SIZE_MAX, 2)`, `stdlib_calloc(SIZE_MAX/2 + 1, 2)` and `stdlib_alloc(SIZE_MAX)` all return `NULL`; the vector is still readable. No request near the 1 TiB limit is issued (host memory is never stressed). |
| V7 | allocator accounting | R | With telemetry reset, a reserve/grow/shrink/copy/drop sequence gives `alloc_count > 0` and `free_count == alloc_count` after all `drop()` calls — no leaked block, no double release. |

---

## 3. Observed contract facts

### Message truncation boundary (`STDLIB_ERROR_SIZE == 256`)

The copy loop in `error_fail_cat` stops when `i + 1 == STDLIB_ERROR_SIZE`, so
the payload capacity is **255 bytes** and `text[255]` is the terminator.
Observed:

| Input length | `strlen(what())` | Result |
| --- | --- | --- |
| 255 | 255 | exact, including all 255 `'A'` bytes (`memcmp` passed) |
| 256 | 255 | exact 255-byte prefix, NUL at index 255 |
| 288 | 255 | exact 255-byte prefix, NUL at index 255 |
| `NULL` | 0 | empty string |

No off-by-one write and no missing terminator was observed in default,
strict/no-sanitize or UBSan builds.

### Category mapping (`error_fail`)

| Exit code | Category | Observed |
| --- | --- | --- |
| `CPLUS_EXIT_IO` (109) | `STDLIB_CATEGORY_IO` | match |
| `CPLUS_EXIT_RESOURCE` (107) | `STDLIB_CATEGORY_MEMORY` | match |
| `CPLUS_EXIT_BOUNDS` (101) | `STDLIB_CATEGORY_BOUNDS` | match |
| `CPLUS_EXIT_OWNERSHIP` (106) | `STDLIB_CATEGORY_OWNERSHIP` | match |
| `CPLUS_EXIT_USE_AFTER_FREE` (103) | `STDLIB_CATEGORY_LIFECYCLE` | match |
| `CPLUS_EXIT_DOUBLE_FREE` (104) | `STDLIB_CATEGORY_LIFECYCLE` | match |
| `CPLUS_EXIT_NULL_POINTER` (102) | `STDLIB_CATEGORY_INVALID` (default arm) | match |
| `CPLUS_EXIT_TYPE_CONFUSION` (105) | `STDLIB_CATEGORY_INVALID` (default arm) | match |
| `CPLUS_EXIT_UNSUPPORTED` (108) | `STDLIB_CATEGORY_INVALID` (default arm) | match |
| `CPLUS_EXIT_OK` (0) | `STDLIB_CATEGORY_INVALID` (default arm) | match — see "Notes" |

`error_fail_cat` stores the caller's category verbatim (`RANGE` with a
`BOUNDS` status and `UTF8` with an `IO` status were both verified), and the
callback receives the same category/code pair that the error record stores.

### Notes (observations, not defects)

- `error_fail(..., CPLUS_EXIT_OK, ...)` marks the error failed with
  `STDLIB_CATEGORY_INVALID`. This is the documented default arm of the mapping
  switch; calling `error_fail` with an OK status is caller misuse and was not
  changed to pass a test.
- `stdlib_note()` counts `RANGE`, `IO`, `OK`, `INVALID` and unknown categories
  under `invalid_ops` while always incrementing `api_failures`; `MEMORY`,
  `BOUNDS`, `OWNERSHIP` and `LIFECYCLE` get their dedicated counters. E3/V4
  rely only on the documented `api_failures` and `alloc_failures` counters.
- `pop_back` on an empty vector and `insert` with `index > size()` refuse
  without bumping the generation (no shape change); a slice taken before
  those refusals stays fresh.

---

## 4. Exact commands and results

All commands were run from the repository root with the checked-in
`build/c+` driver. Driver flags are passed as **separate argv entries** (zsh
does not word-split an unquoted variable, so a single `$flags` string fails
with `clang: error: unsupported option '--strict --sanitize=none'`).

```sh
# default (Safe mode; UBSan already on via backend args)
./build/c+ build tests/stdlib/deep_error_vector.cp \
  -o build/stdlib-tests/deep_error_vector.default
./build/stdlib-tests/deep_error_vector.default < /dev/null

# strict, sanitizers explicitly off
./build/c+ build tests/stdlib/deep_error_vector.cp \
  --strict --sanitize=none \
  -o build/stdlib-tests/deep_error_vector.strict_none
./build/stdlib-tests/deep_error_vector.strict_none < /dev/null

# strict + explicit UBSan
./build/c+ build tests/stdlib/deep_error_vector.cp \
  --strict --sanitize=undefined \
  -o build/stdlib-tests/deep_error_vector.ubsan
./build/stdlib-tests/deep_error_vector.ubsan < /dev/null

# through the harness (expects "ok deep_error_vector", exit 0)
tests/stdlib/run_stdlib_tests.sh deep_error_vector
```

Result summary:

| Mode | Build rc | Run rc | stderr | Result |
| --- | --- | --- | --- | --- |
| default | 0 | 0 | empty | PASS |
| `--strict --sanitize=none` | 0 | 0 | empty | PASS |
| `--strict --sanitize=undefined` | 0 | 0 | empty | PASS |
| harness `deep_error_vector` | — | — | — | `ok deep_error_vector`, 0 failed |

`--sanitize=address` is rejected by this host's driver probe
(`error: sanitizer 'address' is not supported by the selected backend`), so
ASan verification is delegated to the Linux CI path described in
`DEEP_TESTS_HARNESS.md` §3.3; it is not required by this family.

### Exact stdout (byte-identical in all three modes)

```
E1_truncation 1 S
E2_reset_set 1 S
E3_null_handle 1 R
E4_categories 1 S
E5_status_map 1 S
V1_growth 1 S
V2_empty_slices 1 S
V3_stale_slices 1 S
V4_oom_recovery 1 R
V5_empty_get_pop 1 R
V6_overflow_shapes 1 R
V7_alloc_balance 1 R
DEEP_ERROR_VECTOR 1 F
```

---

## 5. Fatal-safety classification

No fatal scenario is executed in this test. A trailing `F` record is the
summary marker (`DEEP_ERROR_VECTOR 1 F`), not a fatal invocation; the harness
inventory records the same classification.

Fatals that are *nearby but deliberately excluded*:

- Calling an `error` method through a null `error *` (e.g. `p->clear()`)
  dereferences the receiver and would be a runtime `CPLUS_EXIT_NULL_POINTER`.
  The public free helpers (`error_reset`, `error_fail`, `error_fail_cat`) are
  the supported null-safe path and are exercised by E3.
- Reading a slice that outlives its owning `vector` object, or dereferencing
  a slice that points into a released buffer, is a lifetime error the header
  explicitly says it cannot detect (`vector.hp` header comment). V3 checks
  only `stale()`/`valid()`/`at_checked()` refusals while the owner is alive.
- Passing a non-stdlib pointer to `stdlib_release`, or a non-NUL-terminated
  buffer to `error_fail`, violates caller contracts and would end in a runtime
  ownership/UBSan fatal. Neither is performed.

Because none of the above is executed, the test proves the recoverable and
success contracts only; fatal exact-exit-code verification belongs to
`tests/runtime_fail/` and the forked-child pattern in
`deep_string_append.cp`.

---

## 6. Findings, fixes and counts

| Item | Count / value |
| --- | --- |
| Scenario checks executed per run | 12 (5 error, 7 vector) plus 1 summary record |
| Fully passing records per run | 13 |
| Confirmed defects | **0** |
| Header files modified | **0** |
| Build/run modes verified | 3 (+ harness invocation) |
| Runs with sanitizer diagnostics or C+ runtime fatal | 0 |

**Finding F-EV-0 (no defect).** Reproduced every requested boundary with the
test above in default, strict/no-sanitize and UBSan modes. Every observed
status, category, exit code, capacity, size, element value, slice state and
telemetry counter matched the documented contract. There was no truncation
off-by-one, no missing NUL, no incorrect category mapping, no stale-slice
false-positive, no failed-mutation state change and no allocator leak. Per the
"fix confirmed defects only" rule, no behaviour in `error.hp` or `vector.hp`
was changed.

No behaviour was relaxed or added merely to make a test pass; the test file
and this document are the only artefacts produced by this task.
