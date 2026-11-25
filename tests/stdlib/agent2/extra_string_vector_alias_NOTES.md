# extra_string_vector_alias — remaining string/vector alias paths (agent 2)

## Owned files

- `tests/stdlib/agent2/extra_string_vector_alias.cp`
- `tests/stdlib/agent2/extra_string_vector_alias_NOTES.md` (this file)

No header, existing test, shared result artifact, or other agent's file was
modified. No `git add` / `git commit` was performed. The agent2 harness was run
only with `AGENT2_RESULTS` redirected to a private temp directory so that the
shared `tests/stdlib/agent2/results/` tree was not touched.

## What the single program covers

60 self-checking tags; every check prints `<tag> <0|1>`. Final line:
`EXTRA_STRING_VECTOR_ALIAS <checks> <failures>` (60, 0). Exit 0 only on zero
failures. All owned locals use unique `sva_`-prefixed names; randomness is a
fixed-seed LCG (`0xA2C1E3u`, `a=1664525`, `c=1013904223`), so stdout is
byte-for-byte deterministic.

| # | Assigned path | Check tags |
|---|---|---|
| A | string self-assignment via `copy_from(this)` documented no-op | `sva_str_self_copy_rc`, `_repeated`, `_identity`, `_content`, `_empty` |
| B | repeated append growth to a long string | `sva_str_append_growth_rc`, `_size`, `_valid`, `_terminator`, `_content`, `sva_str_self_append_growth`, `_content`, `sva_str_suffix_append` |
| C | embedded NUL via length-aware operations (SKIP for `string` storage) | `sva_nul_view_size`, `_view_utf8`, `_validator`, `sva_skip_str_nul_storage`, `sva_nul_view_equals_limited` |
| D | `string_view` at end / beyond end / after clear / after realloc | `sva_view_at_end_ptr`, `_size`, `sva_view_beyond_ptr`, `_size`, `sva_view_snapshot_setup`, `sva_view_clear_ptr_stable`, `_size_snapshot`, `_owner_empty`, `sva_view_realloc_rc`, `_moved`, `_ptr_stale`, `_size_snapshot`, `_old_generation_stale` |
| E | repeated `is_valid_utf8` on valid + invalid data | `sva_str_utf8_valid_repeat`, `_invalid_repeat`, `sva_view_utf8_valid_repeat`, `_invalid_repeat`, `sva_str_utf8_empty`, `sva_utf8_validator_edges` |
| F | vector self-assignment via `copy_from(this)` | `sva_vec_self_copy_setup`, `_rc`, `_shape`, `_first`, `_last`, `_repeated`, `_empty` |
| G | element preservation through 1000 fixed-seed push/pop ops | `sva_vec_pushpop_ops`, `_count`, `_final` |
| H | stale slice refusal after `clear()` / `erase()` | `sva_vec_slice_clear_fresh`, `_read`, `_stale`, `_refused`, `sva_vec_slice_erase_fresh`, `_rc`, `_stale`, `_refused` |
| I | `view()` on an empty vector | `sva_vec_empty_view_size`, `_valid`, `_read_refused`, `_sub`, `_stale_after_push` |

Notes on safety of the view/slice sections:

- Section D compares `data()` pointers and `size()` values only. The at-end view
  uses `c_str() + size()` (the terminator); the beyond-end view uses
  `c_str() + size() + 1` (one-past-the-array, never dereferenced). After
  `clear()` and `reserve(1024)`, the snapshot view's pointer/size are compared
  to the captured values; staleness is confirmed with allocator generation
  metadata (`stdlib_alloc_generation` / `stdlib_alloc_check_generation`), which
  does not dereference the stale pointer. `equals()` / `is_valid_utf8()` are
  never called on a stale view.
- Section H uses `slice::at_checked`, which refuses stale slices with `-1`.
- Both `copy_from(this)` calls are the documented no-ops; they return 0 and
  leave buffer pointer, size, capacity, and contents unchanged. This is a
  confirmed contract, not a defect.

## Exact commands (run from the repository root)

`ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
`TMPD` is a private directory under the approved temp root. Flags are written
out explicitly (a zsh loop with `$FLAGS` does not word-split and would pass
`"--strict --sanitize=none"` as one clang argument).

STRICT `--sanitize=none` (run FIRST):

```sh
timeout 30 "$ROOT/build/c+" build \
  "$ROOT/tests/stdlib/agent2/extra_string_vector_alias.cp" \
  --strict --sanitize=none -o "$TMPD/sva_strict_none"
timeout 30 "$TMPD/sva_strict_none" > "$TMPD/strict_none.out" 2> "$TMPD/strict_none.err"
```

Default:

```sh
timeout 30 "$ROOT/build/c+" build \
  "$ROOT/tests/stdlib/agent2/extra_string_vector_alias.cp" \
  -o "$TMPD/sva_default"
timeout 30 "$TMPD/sva_default" > "$TMPD/default.out" 2> "$TMPD/default.err"
```

UBSan:

```sh
timeout 30 "$ROOT/build/c+" build \
  "$ROOT/tests/stdlib/agent2/extra_string_vector_alias.cp" \
  --strict --sanitize=undefined -o "$TMPD/sva_ubsan"
timeout 30 "$TMPD/sva_ubsan" > "$TMPD/ubsan.out" 2> "$TMPD/ubsan.err"
```

Harness confirmation (private results dir; no shared artifacts touched):

```sh
AGENT2_RESULTS="$TMPD/results" \
  sh tests/stdlib/agent2/run_all.sh --test extra_string_vector_alias --clean
```

## Per-mode results

| Mode | Build rc | Run rc | Final stdout line | stderr | Verdict |
|------|----------|--------|-------------------|--------|---------|
| STRICT `--sanitize=none` (FIRST) | 0 | 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` | empty (0 bytes) | PASS |
| default | 0 | 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` | empty (0 bytes) | PASS |
| STRICT `--sanitize=undefined` (UBSan) | 0 | 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` | empty (0 bytes) | PASS |

- All three stdout files are byte-identical (`diff` clean, 61 lines); all three
  build logs and all three stderr files are empty.
- Two consecutive STRICT/none runs produced identical stdout (determinism).
- Harness result (`AGENT2_RESULTS` override): `PASS=1 FAIL=0 BUILD_FAILURE=0
  TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=1`, harness exit 0; the harness-captured
  stdout equals the manual stdout byte-for-byte.
- No build or run hit the 30 s timeout.

## Exact stdout (all three modes, byte-identical)

```
sva_str_self_copy_rc 1
sva_str_self_copy_repeated 1
sva_str_self_copy_identity 1
sva_str_self_copy_content 1
sva_str_self_copy_empty 1
sva_str_append_growth_rc 1
sva_str_append_growth_size 1
sva_str_append_growth_valid 1
sva_str_append_growth_terminator 1
sva_str_append_growth_content 1
sva_str_self_append_growth 1
sva_str_self_append_content 1
sva_str_suffix_append 1
sva_nul_view_size 1
sva_nul_view_utf8 1
sva_nul_validator 1
sva_skip_str_nul_storage 1
sva_nul_view_equals_limited 1
sva_view_at_end_ptr 1
sva_view_at_end_size 1
sva_view_beyond_ptr 1
sva_view_beyond_size 1
sva_view_snapshot_setup 1
sva_view_clear_ptr_stable 1
sva_view_clear_size_snapshot 1
sva_view_clear_owner_empty 1
sva_view_realloc_rc 1
sva_view_realloc_moved 1
sva_view_realloc_ptr_stale 1
sva_view_realloc_size_snapshot 1
sva_view_realloc_old_generation_stale 1
sva_str_utf8_valid_repeat 1
sva_str_utf8_invalid_repeat 1
sva_view_utf8_valid_repeat 1
sva_view_utf8_invalid_repeat 1
sva_str_utf8_empty 1
sva_utf8_validator_edges 1
sva_vec_self_copy_setup 1
sva_vec_self_copy_rc 1
sva_vec_self_copy_shape 1
sva_vec_self_copy_first 1
sva_vec_self_copy_last 1
sva_vec_self_copy_repeated 1
sva_vec_self_copy_empty 1
sva_vec_pushpop_ops 1
sva_vec_pushpop_count 1
sva_vec_pushpop_final 1
sva_vec_slice_clear_fresh 1
sva_vec_slice_clear_read 1
sva_vec_slice_clear_stale 1
sva_vec_slice_clear_refused 1
sva_vec_slice_erase_fresh 1
sva_vec_slice_erase_rc 1
sva_vec_slice_erase_stale 1
sva_vec_slice_erase_refused 1
sva_vec_empty_view_size 1
sva_vec_empty_view_valid 1
sva_vec_empty_view_read_refused 1
sva_vec_empty_view_sub 1
sva_vec_empty_view_stale_after_push 1
EXTRA_STRING_VECTOR_ALIAS 60 0
```

## Documented SKIP: embedded NUL in `string` storage

Not skipped silently: the length-aware API was exercised, and the unsupported
storage path is confirmed by a passing check (`sva_skip_str_nul_storage`).

Supported and tested:

- `string_view("a\0b\0c", 5)` keeps `size() == 5` and `is_valid_utf8() == 1`
  because both use the explicit byte count.
- `stdlib_utf8_validate("a\0b\0c", 5) == 1` (NUL is a valid code point).
- `string_view::equals` is still NUL-terminator-based on its argument, so
  `equals("a\0b") == 0` for the 5-byte window (documented limitation).

Not supported (SKIP — no length-aware `string` storage/constructor/append API
exists in `stdlib/string.hp`; every length source is `strlen`). Minimal repro:

```c++
string s = string("a\0b");   // strlen stops at the NUL -> s.size() == 1
s.append("\0c");             // strlen("")==0 -> append is a no-op
// s.equals("a\0b") can never hold: equals() compares only strlen("a\0b")==1 byte
```

The test asserts the first line's observable consequence (`size() == 1`,
`equals("a") == 1`) rather than claiming support. No header was changed to add
a length-aware overload.

## Reproduced defects

**No defect in the stdlib string/vector headers or the C+ runtime was
reproduced by this test.** All 60 assigned-path checks pass in all three modes.

The following are test-authoring / lowering findings (fixed in the test only;
minimal repros included so they are not reintroduced). They are not library
defects and required no header change:

1. **Method call on a temporary `slice` is rejected in STRICT mode.**
   Minimal repro:
   ```c++
   vector v = vector();
   if (v.view().size() == 0) { }   // error: no member named 'size' in 'struct slice'
   ```
   The lowering rewrites the temporary to a plain C `vector_view(&v)` struct,
   whose generated C type has no member methods. Fix: bind the view to a named
   local first (`slice s = v.view(); if (s.size() == 0) {}`). Applied to
   `sva_empty_self_slice` and `sva_empty_sub`.

2. **STRICT bounds checker rejects `arr[n - 1]` even under an explicit
   `if (n > 0)` guard.** Minimal repro:
   ```c++
   int a[4]; int n = 1;
   if (n > 0) a[n - 1] = 7;   // [CPLUS-BOUNDS] negative array index on 'a'
   ```
   Fix used in the test: form a `size_t` index, then decrement inside the
   guard (`size_t i = (size_t)n; if (n > 0) { i--; a[i] = 7; }`).

3. **Test-side expectation bug (draft only, corrected).** The first draft used
   `(r & 1u)` for the LCG push/pop decision; the low bit of this LCG alternates,
   leaving a 500/500 push/pop split and an empty final stack. It also asserted
   `pops + final_stack_size == 1000`, which confuses remaining stack size with
   push count. Corrected to `(r & 3u)` (750 pushes / 250 pops, final size 500)
   and to explicit `pushes + pops == 1000 && pushes == pops + final_size`
   counters. These were failures of the test's own oracle, not of the vector.
