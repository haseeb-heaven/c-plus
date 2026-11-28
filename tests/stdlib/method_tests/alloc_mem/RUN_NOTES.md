# METHOD_TEST alloc_mem — per-method public API invocation suite

Task: invoke **100% of public functions/methods, one by one**, for
`stdlib/alloc_policy.hp`, `stdlib/error.hp`, `stdlib/allocator.hp`, and
`stdlib/memory.hp`. Each method has a dedicated deterministic test function in
`all_methods.cp` that calls it with valid, bounded inputs and asserts the
observed result. Nothing is credited by name appearance in a comment,
declaration, or unrelated call: every manifest entry is invoked and checked.

## Deliverables

| File | Purpose |
| --- | --- |
| `all_methods.cp` | 87 dedicated test functions + `main()`; prints one `<header> <method> PASS\|FAIL` line per method and the final `METHOD_TEST alloc_mem <total> <passed> <failed> <skipped>` line. |
| `methods.txt` | Manifest, exactly 87 lines, one `<header> <method>` per line. |
| `RUN_NOTES.md` | This file: commands, evidence, per-method results, totals. |
| `bin/` | Build/runtime logs and both binaries (evidence only). |

## Exact commands (run from repo root)

Primary build and run (the required driver and flags, every run under `timeout 30`):

```sh
timeout 30 ./build/c+ build tests/stdlib/method_tests/alloc_mem/all_methods.cp \
    --strict --sanitize=none \
    -o tests/stdlib/method_tests/alloc_mem/bin/all_methods \
    > tests/stdlib/method_tests/alloc_mem/bin/build.log 2>&1
# BUILD_RC=0, build.log empty (zero errors, zero warnings)

timeout 30 tests/stdlib/method_tests/alloc_mem/bin/all_methods \
    > tests/stdlib/method_tests/alloc_mem/bin/run.out \
    2> tests/stdlib/method_tests/alloc_mem/bin/run.err
# RUN_RC=0, run.err empty
```

Separate UBSan attempt — **plain UBSan is BLOCKED by an external toolchain
defect** (evidence below). The documented env-only workaround permits it, so
the UBSan variant was run as additional evidence (not as the primary gate):

```sh
# Blocked variant:
timeout 30 ./build/c+ build tests/stdlib/method_tests/alloc_mem/all_methods.cp \
    --strict --sanitize=undefined -o .../bin/all_methods_ubsan
# BUILD_RC=1

# Working variant (documented in tests/stdlib/allocator/DIAG_NOTES.md):
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
timeout 30 ./build/c+ build tests/stdlib/method_tests/alloc_mem/all_methods.cp \
    --strict --sanitize=undefined \
    -o tests/stdlib/method_tests/alloc_mem/bin/all_methods_ubsan
# BUILD_RC=0, ubsan_workaround_build.log empty

timeout 30 .../bin/all_methods_ubsan \
    > .../bin/ubsan_run.out 2> .../bin/ubsan_run.err
# RUN_RC=0, ubsan_run.err empty (no UBSan diagnostics)
cmp -s .../bin/run.out .../bin/ubsan_run.out
# CMP_RC=0 (byte-identical stdout in both modes)
```

Blocker evidence (`bin/ubsan_plain_build.log`, first lines):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:144:23: error:
  use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:259:23: error:
  use of undeclared identifier 'CPLUS_EXIT_CONVERSION'
```

The sibling `c-plus-core` runtime source uses exit codes absent from this
repo's `cplus.h`; pointing `CPLUS_INCLUDE` at the matching sibling header makes
UBSan build and run cleanly. No compiler/runtime/header file was edited by this
task. This is a toolchain issue only; it caused **no method skip**.

## Coverage accounting (exact)

`methods.txt` (87 lines) was cross-checked programmatically against the
declarations in the four headers and against the 87 printed result lines:

```
manifest count: 87
run method lines: 87
manifest == run (header, method order): True
all results PASS: True
alloc_policy.hp: declared=4  manifested=4
allocator.hp:    declared=24 manifested=24
memory.hp:       declared=17 manifested=17
error.hp:        declared=42 manifested=42
declared-but-not-manifested: []      (set_category spans two lines; counted)
```

Breakdown:

| Header | Public functions | Class methods (incl. ctors, `drop`) | Total |
| --- | ---: | ---: | ---: |
| `alloc_policy.hp` | 4 | 0 | 4 |
| `error.hp` | 33 | 9 (`error`) | 42 |
| `allocator.hp` | 20 | 4 (`alloc_fail_scope`) | 24 |
| `memory.hp` | 17 | 0 | 17 |
| **Total** | **74** | **13** | **87** |

Note on the existing coverage tool: `sh tests/stdlib/run_coverage.sh` reports
`allocator.hp 4/4` and `error.hp 9/9` (0 entries for `alloc_policy.hp` and
`memory.hp`) because it deliberately skips every `stdlib_*` name and counts only
class methods. This suite is stricter: it covers those 13 class methods **plus**
all 74 `stdlib_*`/`error_*` free functions, for 87 methods total.

## Determinism and safety

* No wall-clock, randomness, or unbounded sizes. All constants (e.g. 6*7==42,
  8*4==32, `SIZE_MAX` refusal cases) are fixed.
* All blocks are allocated with `stdlib_alloc`/`stdlib_calloc` and released.
  Interior/stale pointers are only passed to metadata queries or guarded
  operations that refuse them before touching memory; no stale pointer is
  dereferenced. Failure injection is reset in every scope.
* `error` objects are constructed with `error e = error();` — a plain
  `error e;` does **not** lower to a constructor call in this C+ toolchain
  (verified via `--emit-c`), so using plain declarations would leave the object
  uninitialised.
* Failed-refusal paths assert both the `STDLIB_FAIL` return and the recorded
  error category, and (where relevant) that bytes were not written.

## Per-method results (verbatim from `bin/run.out`)

```
alloc_policy.hp stdlib_count_size_overflow PASS
alloc_policy.hp stdlib_size_class_round_up PASS
alloc_policy.hp stdlib_size_class_words PASS
alloc_policy.hp stdlib_size_normalize_zero PASS
error.hp stdlib_tm_add PASS
error.hp stdlib_telemetry_set_enabled PASS
error.hp stdlib_telemetry_is_enabled PASS
error.hp stdlib_telemetry_reset PASS
error.hp stdlib_telemetry_snapshot PASS
error.hp stdlib_set_error_callback PASS
error.hp stdlib_category_name PASS
error.hp stdlib_note PASS
error.hp stdlib_alloc_fail_at PASS
error.hp stdlib_alloc_fail_after PASS
error.hp stdlib_alloc_fail_reset PASS
error.hp stdlib_alloc_seq PASS
error.hp stdlib_meta_acquire PASS
error.hp stdlib_meta_release_lock PASS
error.hp stdlib_meta_hash PASS
error.hp stdlib_meta_find PASS
error.hp stdlib_meta_record PASS
error.hp stdlib_meta_forget PASS
error.hp stdlib_alloc PASS
error.hp stdlib_calloc PASS
error.hp stdlib_realloc PASS
error.hp stdlib_safe_div PASS
error.hp stdlib_alloc_size PASS
error.hp stdlib_alloc_elem_size PASS
error.hp stdlib_alloc_length PASS
error.hp stdlib_alloc_generation PASS
error.hp stdlib_alloc_check_generation PASS
error.hp stdlib_alloc_query_length PASS
error.hp stdlib_transfer_ownership PASS
error.hp stdlib_release PASS
error.hp error::error PASS
error.hp error::ok PASS
error.hp error::failed_now PASS
error.hp error::code PASS
error.hp error::what PASS
error.hp error::category PASS
error.hp error::clear PASS
error.hp error::set PASS
error.hp error::set_category PASS
error.hp error_reset PASS
error.hp error_fail PASS
error.hp error_fail_cat PASS
allocator.hp stdlib_alloc_identity PASS
allocator.hp stdlib_alloc_id_equal PASS
allocator.hp stdlib_alloc_id_matches PASS
allocator.hp stdlib_alloc_view_bytes PASS
allocator.hp stdlib_alloc_view_elem_bytes PASS
allocator.hp stdlib_alloc_view_generation PASS
allocator.hp stdlib_alloc_view_live PASS
allocator.hp stdlib_alloc_view_count PASS
allocator.hp stdlib_alloc_view_remaining PASS
allocator.hp stdlib_alloc_view_unchanged PASS
allocator.hp stdlib_alloc_view_transfer_ok PASS
allocator.hp stdlib_alloc_cap_state PASS
allocator.hp stdlib_alloc_hasvalidcap PASS
allocator.hp stdlib_alloc_cap_covers PASS
allocator.hp stdlib_alloc_cap_check PASS
allocator.hp stdlib_alloc_tm_snapshot PASS
allocator.hp stdlib_alloc_tm_delta PASS
allocator.hp stdlib_alloc_tm_counter PASS
allocator.hp stdlib_alloc_fail_begin PASS
allocator.hp stdlib_alloc_fail_end PASS
allocator.hp alloc_fail_scope::alloc_fail_scope PASS
allocator.hp alloc_fail_scope::armed_now PASS
allocator.hp alloc_fail_scope::reset_now PASS
allocator.hp alloc_fail_scope::drop PASS
memory.hp stdlib_mem_capacity PASS
memory.hp stdlib_mem_count PASS
memory.hp stdlib_mem_token PASS
memory.hp stdlib_mem_check_token PASS
memory.hp stdlib_mem_check PASS
memory.hp stdlib_mem_span_ok PASS
memory.hp stdlib_mem_span_contains PASS
memory.hp stdlib_mem_bounds PASS
memory.hp stdlib_mem_key_current PASS
memory.hp stdlib_mem_query PASS
memory.hp stdlib_mem_check_range PASS
memory.hp stdlib_mem_copy PASS
memory.hp stdlib_mem_move PASS
memory.hp stdlib_mem_set PASS
memory.hp stdlib_mem_borrow PASS
memory.hp stdlib_mem_transfer PASS
memory.hp stdlib_mem_release PASS
METHOD_TEST alloc_mem 87 87 0 0
```

## Totals

* Total methods: **87**
* Passed: **87**
* Failed: **0**
* Skipped: **0**
* Final line: `METHOD_TEST alloc_mem 87 87 0 0`
* Exit status: 0 (the program returns 1 when `mt_failed > 0`).

**Explicit statement about untestable methods:** none. Every public
function/method declared in the four headers is invoked and its result
verified. The plain UBSan build is BLOCKED by the external
`CPLUS_EXIT_ARITHMETIC`/`CPLUS_EXIT_CONVERSION` mismatch, but that affects only
the sanitizer build configuration — with the documented `CPLUS_INCLUDE` env
workaround the same 87 methods also pass under UBSan with no diagnostics and
byte-identical stdout. No method is skipped for that reason.
