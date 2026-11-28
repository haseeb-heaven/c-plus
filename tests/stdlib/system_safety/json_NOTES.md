# `json_*.cp` — Task 3 Phase 4 JSON robustness evidence notes

Branch: `agent/modern-stdlib`. Agent: Task 3 Phase 4 (1 of 10 parallel agents).
Date: 2026-11-23.

## 1. Scope and ownership

Audited `stdlib/json.hp` (yyjson-backed RAII JSON) for robustness only. Files
owned and created for this task (and **no others**):

- `tests/stdlib/system_safety/json_parse_edges.cp`
- `tests/stdlib/system_safety/json_numeric_edges.cp`
- `tests/stdlib/system_safety/json_lifecycle.cp`
- `tests/stdlib/system_safety/json_NOTES.md` (this file)

`stdlib/json.hp` was **not modified** — no defect was reproduced (see §6). No
compiler, runtime, build, or other library file was touched; no `git add` or
commit was run. Build artifacts were written to the pre-approved temp directory
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode` (outside
the repository). All commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

## 2. Coverage matrix

| Requirement | File | Checks |
| --- | --- | --- |
| Empty input | parse_edges | `jpe_empty_input`, `jpe_whitespace_input` |
| Malformed syntax | parse_edges | 18 `jpe_*` tokens: unclosed/extra-bracket objects & arrays, trailing commas, double comma, missing colon/comma, bad escape, bad `\\u` hex, trailing junk, single quotes, bare word, leading zero, `.5`, `1.`, lone `-` |
| Truncated input | parse_edges | `jpe_truncated_prefixes` — every proper prefix of a complete document |
| Deep nesting (bounded) | parse_edges | `jpe_deep_nesting_200` — 200 nested arrays; walks to innermost scalar |
| Large arrays/objects (<4096) | parse_edges | `jpe_large_array_2047`, `jpe_large_object_2047` |
| Duplicate keys | parse_edges | `jpe_duplicate_keys` — 3 duplicates; yyjson keeps all, `member()` returns first |
| Escaped strings | parse_edges | `jpe_escaped_string` — quote, backslash, `/`, backspace, form-feed, CR, LF, tab |
| Unicode escapes | parse_edges | `jpe_unicode_bmp` (U+0041/U+00E9/U+4F60/U+597D → UTF-8), `jpe_unicode_surrogate_pair` (U+1F600 → F0 9F 98 80) |
| Invalid numbers | numeric_edges | 9 `jne_*` tokens: `1e400`, `-1e400`, `NaN`, `Infinity`, `-Infinity`, `0x10`, `+1`, `1.2.3`, `1e`, `1_000` |
| Integer overflow boundaries | numeric_edges | `jne_int_max_ok`, `jne_int_min_ok`, `jne_int_max_plus_one`, `jne_int_min_minus_one`, `jne_i64_max_refused`, `jne_i64_min_refused`, `jne_u64_max_refused`, `jne_beyond_u64_refused`, `jne_huge_int_refused`, `jne_real_not_int` |
| Floating edge cases | numeric_edges | `jne_dbl_i64max`, `jne_dbl_i64min`, `jne_dbl_u64max`, `jne_dbl_subnormal_1e308`, `jne_dbl_min_subnormal`, `jne_dbl_max_finite`, `jne_dbl_1e308`, `jne_dbl_neg_zero` |
| Missing fields | lifecycle | `jlc_missing_field`, `jlc_missing_nested_field`, `jlc_null_key` |
| Wrong types | lifecycle | `jlc_int_on_string`, `jlc_double_on_bool`, `jlc_string_on_number`, `jlc_bool_on_array`, `jlc_bool_on_object`, `jlc_json_null_view`, `jlc_at_on_object`, `jlc_member_on_array`, `jlc_at_oob` |
| Repeated parse/write/drop cycles | lifecycle | `jlc_cycles_200_no_crash` (100 valid + 100 malformed), `jlc_valid_count_100`, `jlc_fail_count_100`, `jlc_builder_cycles_50` |
| Allocation failures | lifecycle | **SKIP** — `jlc_alloc_failure_hook` (see §5) |
| Malformed → controlled error, no crash | all | Every `*_bad`/malformed probe asserts `!valid() && failed_now() && category()==INVALID` (`+ code()==UNSUPPORTED` in parse_edges); all runs exit 0 with empty stderr and no sanitizer diagnostic |

## 3. Exact commands and results

`-I`, `-L` and `-l` were passed as **separate argv operands**, exactly as
required. STRICT / no-sanitizer was run **first**, then UBSan. Every run used
`timeout 30`. All three test sources are self-checking; each prints `<tag> <0|1>`
lines and a final `JSON_* <checks> <failures>` line, exiting 0 only when
`failures == 0`.

### 3.1 STRICT, sanitizers off (run first)

```sh
TDIR=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode

./build/c+ build tests/stdlib/system_safety/json_parse_edges.cp \
  --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$TDIR/json_parse_edges_sn"
timeout 30 "$TDIR/json_parse_edges_sn" > "$TDIR/jpe_sn.out" 2> "$TDIR/jpe_sn.err"

./build/c+ build tests/stdlib/system_safety/json_numeric_edges.cp \
  --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$TDIR/json_numeric_edges_sn"
timeout 30 "$TDIR/json_numeric_edges_sn" > "$TDIR/jne_sn.out" 2> "$TDIR/jne_sn.err"

./build/c+ build tests/stdlib/system_safety/json_lifecycle.cp \
  --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$TDIR/json_lifecycle_sn"
timeout 30 "$TDIR/json_lifecycle_sn" > "$TDIR/jlc_sn.out" 2> "$TDIR/jlc_sn.err"
```

Build exit 0 for all three; run exit 0 for all three; stderr empty for all
three. Final lines: `JSON_PARSE_EDGES 28 0`, `JSON_NUMERIC_EDGES 28 0`,
`JSON_LIFECYCLE 18 0`.

### 3.2 STRICT + UBSan

A pre-existing, repository-wide toolchain blocker prevents *any* UBSan build
without an environment override (see §7). The no-file-edit workaround
documented in `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` (Option B) was
used, exporting the external header directory that already defines
`CPLUS_EXIT_ARITHMETIC`; no compiler/runtime source was changed.

```sh
export CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include

./build/c+ build tests/stdlib/system_safety/json_parse_edges.cp \
  --strict --sanitize=undefined \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$TDIR/json_parse_edges_ubsan"
timeout 30 "$TDIR/json_parse_edges_ubsan" > "$TDIR/json_parse_edges_ubsan.out" 2> "$TDIR/json_parse_edges_ubsan.err"
# ... identical build+run for json_numeric_edges and json_lifecycle ...
```

A first attempt **without** `CPLUS_INCLUDE` failed at build time for all three
files (and for pre-existing tests `json_test.cp` / `deep_json_task5.cp`) with:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

### 3.3 Result table

| Mode | File | Build exit | Run exit (`timeout 30`) | stderr | Final line | stdout sha1 |
| --- | --- | --- | --- | --- | --- | --- |
| strict / none (first) | json_parse_edges | 0 | 0 | empty | `JSON_PARSE_EDGES 28 0` | `97030084880e539d92bfe07396c1e186b0c75bdb` |
| strict / none (first) | json_numeric_edges | 0 | 0 | empty | `JSON_NUMERIC_EDGES 28 0` | `a22d0a29a6b4660e5f368b550bdb547e9b834351` |
| strict / none (first) | json_lifecycle | 0 | 0 | empty | `JSON_LIFECYCLE 18 0` | `dbbe5b723bc8b3be59bd983b72c4e63b58c7330b` |
| strict / UBSan | json_parse_edges | 0 | 0 | empty | `JSON_PARSE_EDGES 28 0` | `97030084880e539d92bfe07396c1e186b0c75bdb` |
| strict / UBSan | json_numeric_edges | 0 | 0 | empty | `JSON_NUMERIC_EDGES 28 0` | `a22d0a29a6b4660e5f368b550bdb547e9b834351` |
| strict / UBSan | json_lifecycle | 0 | 0 | empty | `JSON_LIFECYCLE 18 0` | `dbbe5b723bc8b3be59bd983b72c4e63b58c7330b` |

`diff` of strict-none vs UBSan stdout is **empty** for all three files. No
`timeout` expiry, no sanitizer diagnostic, no `INVARIANT`-style output. Build
logs contain only the driver's usual
`-lyyjson: linker input unused` / `-Lbuild/ext: argument unused during
compilation` notices (also emitted for every pre-existing JSON suite).

**Totals: 74 checks, 0 failures, 0 reproduced defects, 1 SKIP.**

## 4. Strict-mode compliance of the test sources

- No pointer-parameter subscripts: byte writes to caller buffers in
  `jpe_put` use pointer arithmetic (`*(out + *pos) = ch`), and bulk reads use
  `__builtin_memcpy` (truncated-prefix construction).
- No variable modulo or division: the 200-cycle valid/malformed alternation
  uses a boolean flag (`jlc_is_valid`), not `%`.
- Unique variable names: every local is prefixed per file (`jpe_`, `jne_`,
  `jlc_`) and named distinctly within its scope.
- Chained temporary methods are avoided: C+ lowers `view.at(i).valid()` to an
  invalid `json_view_at(...).valid()` expression, so returned views are bound
  to a local before `.valid()` is read. (This is a C+ lowering quirk, already
  recorded in `tests/stdlib/fuzz/F100_THREAD_JSON_NOTES.md`; it is not a
  `json.hp` defect.)

## 5. SKIPs (a printed `1` is a SKIP marker, NOT a pass claim)

1. **`jlc_alloc_failure_hook`** — SKIP. `stdlib/json.hp` passes `NULL` for
   every `yyjson_alc *` (`yyjson_read_opts`, `yyjson_read_file`,
   `yyjson_val_write`, `yyjson_mut_doc_new`, `yyjson_mut_obj_add_*`,
   `yyjson_mut_write`). yyjson's allocator is per-call with no global hook and
   `json.hp` exposes no allocator setter, so `stdlib_alloc_fail_at/after`
   cannot reach yyjson allocations and there is no counter to read. No honest
   OOM test can be fabricated here. The 200 document cycles and 50 builder
   cycles (§2) are the only available liveness evidence; they are **not** a
   leak-freedom or OOM-robustness proof.

## 6. Defects found / fixes applied

**None.** Every malformed, truncated, invalid-number, wrong-type and
out-of-range case produced a controlled `INVALID`/`UNSUPPORTED` (or
`RANGE`/`CPLUS_EXIT_BOUNDS` for out-of-range `get_int`) error with no crash,
and all valid edge inputs parsed, read, wrote and dropped cleanly. Because no
defect was reproduced, `stdlib/json.hp` was deliberately left unchanged.

One initial test expectation was corrected during development (this is a test
fix, not a library fix): a >uint64 integer literal such as
`9999999999999999999999999999999999999999` is parsed by yyjson as a `double`
(valid document), not rejected. The check was reframed as
`jne_huge_int_refused` (get_int must refuse the real value), matching the
behaviour already pinned by `tests/stdlib/deep_json_types.cp::t_int_huge_real`.

## 7. Environment note (no action taken)

UBSan builds in this workspace fail globally while compiling the external
runtime from `c-plus-core` against this repo's `runtime/include` header, which
lacks `CPLUS_EXIT_ARITHMETIC`; this affects pre-existing tests too. It is
owned by Agent 1 and documented in
`docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`. The UBSan results in §3 were
obtained using that document's environment-only Option B workaround
(`CPLUS_INCLUDE=.../c-plus-core/runtime/include`). No compiler or runtime file
was edited to obtain them.
