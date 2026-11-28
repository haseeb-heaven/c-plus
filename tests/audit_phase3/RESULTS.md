# PHASE 3 audit — exact test results

Agent B (audit_phase3), branch `agent/modern-stdlib`, commit `ab8cb51`.
All results below were produced by executing tests in this checkout; nothing is
inferred from prior reports or cached logs. No source/header files were edited.

## Protocol actually used

- Compiler: `./build/c+` (on-disk binary timestamp 2026-11-23 08:07:07).
- Build: `timeout -k 5 30 ./build/c+ build <src> --strict --sanitize=none -I. [extra libs] -o <bin>`
  (extra libs derived from includes, mirroring `tests/stdlib/run_stdlib_tests.sh`:
  yyjson for `stdlib/json.hp`, SDL3/libcurl/libuv/FFmpeg as needed by include).
- Run: `timeout -k 5 30 <bin>` with the sibling `.stdin` fixture when present,
  else `/dev/null`; stdout/stderr captured separately; `ulimit -c 0`.
- Verification: `cmp -s <name>.expected <stdout>` and
  `cmp -s <name>.stderr.expected <stderr>` where fixtures exist.
- Negative-control expectations (reject / runtime-trap) were derived from the
  authoritative regular runners (`cases.list`, `run_10.sh`, `cp_breaker.sh`).
- No `|| true`; every discovered test is classified with an exact reason.
- Machine-readable output: `tests/audit_phase3/work/results.tsv`;
  raw logs: `tests/audit_phase3/work/logs/`.

## Totals

| Metric | Count |
|---|---:|
| Discovered and executed | 114 |
| PASS | 102 |
| FAIL | 3 |
| BUILD_FAIL | 9 |
| TIMEOUT | 0 |
| BLOCKED | 0 |

Breakdown: deep 29, agent2 16, regressions 10, breaker_v3 25, stress10 10,
breaker_probe 24. Master runner exit status: **1** (real failures present).

## Non-PASS results (exact reasons)

| Group | Test | Status | Exact reason |
|---|---|---|---|
| deep | deep_collections_alloc | FAIL | runtime terminated by signal 6 (rc=134); stderr: `C+ error: index 3 is outside the bounds of 'pre' (length 3)` |
| regression | adv_containers | FAIL | runtime terminated by signal 6 (rc=134); stderr: `C+ error: index 40 is outside the bounds of 'present' (length 40)` |
| regression | adv_json_fs_net | FAIL | runtime exit 1; stderr: `C+ error: index 8 is outside the bounds of 'rb' (length 8)` |
| regression | adv_exact_fit | BUILD_FAIL | `adv_exact_fit.cp:59:13: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe` |
| regression | adv_long_input | BUILD_FAIL | `adv_long_input.cp:46:9: error: [CPLUS-FFI] pointer parameter 'ali_buf' has unverifiable extent; subscript cannot be proven safe` |
| breaker_v3 | 10_signal_outside_handler | BUILD_FAIL | `10_signal_outside_handler.cp:5:44: error: [CPLUS-FFI] pointer created from integer value has unverifiable provenance` |
| breaker_v3 | 14_resource_transfer | BUILD_FAIL | `14_resource_transfer.cp:9:68: error: use-after-move: use of moved-from resource 'a'` |
| breaker_v3 | 23_double_free_trap | BUILD_FAIL | `23_double_free_trap.cp:2:73: error: [CPLUS-OWNER] double free of 'p'` |
| breaker_v3 | 24_invalid_free_trap | BUILD_FAIL | `24_invalid_free_trap.cp:2:28: error: [CPLUS-OWNER] invalid free of address of non-heap object 'value' via cplus_release` |
| breaker_v3 | 25_freed_len_trap | BUILD_FAIL | `25_freed_len_trap.cp:5:3: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: liveness check on released pointer 'p'` |
| stress10 | 10_trap_double_free | BUILD_FAIL | `10_trap_double_free.cp:7:5: error: [CPLUS-OWNER] double free of 'value'` |
| breaker_probe | cplus_probe_double_release | BUILD_FAIL | `cplus_probe_double_release.cp:2:59: error: [CPLUS-OWNER] double free of 'p'` |

Note on the 9 BUILD_FAIL rows: these sources are written as *runtime-trap*
negative controls for the default-mode runners (they are expected to build and
then trap at runtime). Under the mandated strict build, the compiler's static
checks (`CPLUS-FFI`, `CPLUS-OWNER`, `CPLUS-LIFETIME`, use-after-move) reject them
at compile time. I could **not** confirm they still pass in default mode because
default mode is currently broken for every test (defect D2 below). They are
therefore reported honestly as BUILD_FAIL under the specified protocol, not as
runtime passes.

The 3 FAIL rows are runtime bound-check aborts/errors on code whose checked-in
`.expected` fixture records success. For `deep_collections_alloc`, the cached
strict-none log `tests/stdlib/reports/logs/strict-none/deep_collections_alloc.stdout`
(timestamp 2026-11-23 07:36:05) shows a full successful run; the current compiler
binary (08:07:07) aborts. That is a real, newly introduced behavioral regression,
not a fixture error.

## Full per-test table (114 rows)

| deep | deep_collections_alloc | FAIL | runtime terminated by signal 6 (rc=134); stderr: C+ error: index 3 is outside the bounds of 'pre' (length 3) |
| deep | deep_collections_boundary | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_collections_collisions | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_collections_growth | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_cross_family | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_error_task5 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_error_vector | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_file_lifecycle | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_file_lines | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_file_task2 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_fs_task5 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_fs_time | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_io_1byte | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_io_task2 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_io_token | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_json_stress | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_json_task5 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_json_types | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_log_task5 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_memory_bounds | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_net | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_net_task5 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_string_append | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_string_task4 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_string_view | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_thread_guard | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_thread_nested | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_thread_task5 | PASS | exit 0; stdout/stderr match .expected fixtures |
| deep | deep_vector_task4 | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | abi_safety_alloc_lifecycle | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | abi_safety_containers_borrow | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | abi_safety_injection_mem | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| agent2 | abi_safety_meta_integrity | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | abi_safety_safecrt | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | cross_module | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_collections_clear | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_error_metadata | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_file_binary | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_fs_edges | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_io_whitespace | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_json_cleanup | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_log_edges | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_string_vector_alias | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_thread_condvar | PASS | exit 0; stdout/stderr match .expected fixtures |
| agent2 | extra_time_boundaries | PASS | exit 0; stdout/stderr match .expected fixtures |
| regression | adv_alloc_failure | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| regression | adv_containers | FAIL | runtime terminated by signal 6 (rc=134); stderr: C+ error: index 40 is outside the bounds of 'present' (length 40) |
| regression | adv_exact_fit | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/regressions/adv_exact_fit.cp:59:13: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe |
| regression | adv_integer_bounds | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| regression | adv_json_fs_net | FAIL | runtime exit 1; stderr: C+ error: index 8 is outside the bounds of 'rb' (length 8) |
| regression | adv_lifecycle | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| regression | adv_long_input | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/regressions/adv_long_input.cp:46:9: error: [CPLUS-FFI] pointer parameter 'ali_buf' has unverifiable extent; subscript cannot be proven s |
| regression | adv_null_zero | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| regression | adv_stale_views | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| regression | adv_thread_contention | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_v3 | 01_raii_loop_exits | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 02_defer_return_order | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 03_this_nested_methods | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 04_class_destruct_order | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 05_managed_churn | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 06_vector_realloc_stress | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 07_string_nested_cleanup | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 08_three_dimensional_bounds | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 09_fread_used_result | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 10_signal_outside_handler | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stress/cplus_breaker_v3/10_signal_outside_handler.cp:5:44: error: [CPLUS-FFI] pointer created from integer value has unverifiable provenance |
| breaker_v3 | 11_bitfields_and_macros | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 12_new_octal_if_decl | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 13_range_countof | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 14_resource_transfer | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stress/cplus_breaker_v3/14_resource_transfer.cp:9:68: error: use-after-move: use of moved-from resource 'a' |
| breaker_v3 | 15_defer_and_managed_return | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 16_stdlib_file_raii | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 17_stdlib_thread_guard | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 18_c_aliasing_valid | PASS | exit 0; stdout/stderr match .expected fixtures |
| breaker_v3 | 19_resource_copy_should_fail | PASS | expected compile-time rejection (build rc=1) |
| breaker_v3 | 20_managed_owner_escape_should_fail | PASS | expected compile-time rejection (build rc=1) |
| breaker_v3 | 21_array_oob_should_fail | PASS | expected compile-time rejection (build rc=1) |
| breaker_v3 | 22_countof_pointer_should_fail | PASS | expected compile-time rejection (build rc=1) |
| breaker_v3 | 23_double_free_trap | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stress/cplus_breaker_v3/23_double_free_trap.cp:2:73: error: [CPLUS-OWNER] double free of 'p' |
| breaker_v3 | 24_invalid_free_trap | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stress/cplus_breaker_v3/24_invalid_free_trap.cp:2:28: error: [CPLUS-OWNER] invalid free of address of non-heap object 'value' via cplus_release |
| breaker_v3 | 25_freed_len_trap | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stress/cplus_breaker_v3/25_freed_len_trap.cp:5:3: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: liveness check on released poin |
| stress10 | 01_class_this_destructor | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 02_resource_early_return | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 03_managed_len | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 04_stdlib_string_vector | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 05_range_countof_auto | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 06_real_c_compatibility | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 07_defer_nested_returns | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| stress10 | 08_reject_resource_copy | PASS | expected compile-time rejection (build rc=1) |
| stress10 | 09_reject_borrow_escape | PASS | expected compile-time rejection (build rc=1) |
| stress10 | 10_trap_double_free | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stress/cplus_stress_10/10_trap_double_free.cp:7:5: error: [CPLUS-OWNER] double free of 'value' |
| breaker_probe | cplus_probe_bad_constant_oob | PASS | expected compile-time rejection (build rc=1) |
| breaker_probe | cplus_probe_bad_countof_pointer | PASS | expected compile-time rejection (build rc=1) |
| breaker_probe | cplus_probe_bad_loop_oob | PASS | expected compile-time rejection (build rc=1) |
| breaker_probe | cplus_probe_bad_managed_escape | PASS | expected compile-time rejection (build rc=1) |
| breaker_probe | cplus_probe_bad_null_deref | PASS | expected compile-time rejection (build rc=1) |
| breaker_probe | cplus_probe_bad_resource_copy | PASS | expected compile-time rejection (build rc=1) |
| breaker_probe | cplus_probe_class_this | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_default_arguments | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_defer_return | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_double_release | BUILD_FAIL | build rc=1; /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/breaker/cplus_probe_double_release.cp:2:59: error: [CPLUS-OWNER] double free of 'p' |
| breaker_probe | cplus_probe_explicit_bounds | PASS | expected runtime trap (exit 101) |
| breaker_probe | cplus_probe_if_decl | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_managed_len | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_octal_rotation | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_raii_order | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_range_auto_len | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_resource_early_return | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_stdlib_raii | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_valid_bitfield | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_valid_fread_store | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_valid_macro | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_valid_multidim | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_valid_printf | PASS | exit 0 (no byte-exact fixture; exit-code only) |
| breaker_probe | cplus_probe_valid_user_print | PASS | exit 0 (no byte-exact fixture; exit-code only) |

## Runner defects found

**D1 — `tests/stdlib/run_deep_tests.sh` misclassifies runtime aborts as timeouts/skips.**
Its `one()` treats any run exit `>= 128` as TIMEOUT and calls `add_skip`, so a
SIGABRT (134) from a real bounds failure is reported as a *skipped timeout*, not
a FAIL. Cross-check run (`tests/audit_phase3/work/crosscheck_deep.log`):
`deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)` —
the single "skipped" invocation is `deep_collections_alloc (strict-none)`, which
my runner correctly classifies as FAIL (rc=134, bounds error). Also, the build
branch uses the same `>=128` rule for compiler signals.

**D2 — Default-mode builds are broken across the whole repository (cross-repo header mismatch).**
`./build/c+ build <any>.cp` (no `--strict`) fails while compiling the
instrumented runtime:

```
cplus_runtime.c:179:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
cplus_runtime.c:186:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
cplus_runtime.c:294:23: error: use of undeclared identifier 'CPLUS_EXIT_CONVERSION'; did you mean 'CPLUS_EXIT_OWNERSHIP'?
cplus_runtime.c:347:21: error: use of undeclared identifier 'CPLUS_EXIT_CONVERSION'
error: failed to compile instrumented C+ runtime
```

The sibling core runtime `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`
uses `CPLUS_EXIT_ARITHMETIC` and `CPLUS_EXIT_CONVERSION`, but this checkout's
`runtime/include/cplus/cplus.h` defines only `CPLUS_EXIT_OK=0`, `BOUNDS=101`,
`NULL_POINTER=102`, `USE_AFTER_FREE=103`, `DOUBLE_FREE=104`,
`TYPE_CONFUSION=105`, `OWNERSHIP=106`, `RESOURCE=107`, `UNSUPPORTED=108`,
`IO=109`. Verified impact: `bash tests/stress/cplus_stress_10/run_10.sh`
exits 1 with `SUMMARY: 2 passed, 8 failed (10 total)`; eight failures are exactly
`build rejected ... failed to compile instrumented C+ runtime`. This breaks
every default-mode runner: `tests/run_tests.sh`, `tests/stdlib/run_stdlib_tests.sh`,
`tests/cp_breaker.sh`, `tests/stress/cplus_breaker_v3/run.sh`,
`tests/stress/cplus_stress_10/run_10.sh`. In the canonical deep runner, all 29
`default` and all 29 `ubsan` invocations fail to build for this reason.

**D3 — No single runner covers the whole suite, and several suites have no runner at all.**
`tests/run_all_evidence.sh` and `tools/run_final_evidence.sh` omit breaker/stress,
`tests/stdlib/regressions`, `tests/stdlib/agent1_handoff`, `tests/adversarial_1000`,
`tests/fuzz10k`, redteam, defense, security, system_safety, method_tests,
upstream_reuse, allocator and known_gaps. `tests/stdlib/run_all.sh` omits
`run_deep_tests.sh`, `agent2/run_all.sh` and `run_adversarial.sh`. In particular
`tests/stdlib/agent1_handoff` has 52 `.cp` files and 10 `manifest.json` files but
no runner consumes those manifests (only `tests/conformance/run_conformance.sh`
reads a *different* manifest). Those tests are never run automatically.

**D4 — Unbounded runners.** `tests/stdlib/run_stdlib_tests.sh`,
`tests/cp_breaker.sh`, `tests/stress/cplus_breaker_v3/run.sh` and
`tests/stress/cplus_stress_10/run_10.sh` invoke the compiler and test binaries
with no `timeout`. A hanging test hangs the suite. `run_stdlib_tests.sh:pkg_libs()`
also contains `pkg-config ... || true`, a failure-suppressing no-op for dependency
probing.

**D5 — Silent state dependence.** `tests/stdlib/agent2/run_all.sh` and
`tests/stdlib/run_adversarial.sh` `exec` prebuilt binaries in `build/native-tools/`.
If those binaries are stale or absent, results reflect old code (or fail to
start) rather than the current sources.

## Cross-checks performed

- Canonical `sh tests/stdlib/run_deep_tests.sh` → rc=1, `28 passed, 58 failed,
  1 skipped`; matches this audit's 28 strict-none deep passes + 1 real abort.
- Canonical `bash tests/stress/cplus_stress_10/run_10.sh` → rc=1, `2 passed,
  8 failed`; confirms D2 independently of my runner.
- Default-mode spot-check of all 12 non-PASS sources → all 12 fail to build with
  the same D2 diagnostic; no default-mode result could be used to excuse a
  strict-mode failure.

## Files

- `tests/audit_phase3/run_all.sh` — master runner (reproduces this table).
- `tests/audit_phase3/RESULTS.md` — this file.
- `tests/audit_phase3/TEST_DISCOVERY.md` — discovery and coverage mapping.
- `tests/audit_phase3/work/results.tsv` — machine-readable per-test results.
- `tests/audit_phase3/work/logs/` — build/run logs for every executed test.
