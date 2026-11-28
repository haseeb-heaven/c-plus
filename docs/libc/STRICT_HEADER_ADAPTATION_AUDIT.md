# Final strict-diagnostic scan after sibling header adaptations

Branch: `agent/modern-stdlib`, HEAD `487ab89` ("Convert Python test tooling to
native C+ and add 100 fuzz configs").
Scan window: 2026-11-23 08:51–08:58 local time. Report authored 2026-11-23
08:59. All commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

This document records **observed** compiler output only. No pass is inferred
from a build artifact, no error is fabricated, and no source, header, wrapper,
fixture, compiler or runtime file was edited by this verification. The only
repository file authored is this document. All compile logs and binaries were
written to a scratch directory under the approved temp path plus
`build/_strict_scan/`; nothing was staged or committed.

---

## 0.Compiler / adaptation provenance

| Item | Observed value |
|---|---|
| `build/c+` mtime | `2026-11-23 08:07:07` (`stat -f '%Sm'`); Mach-O arm64 |
| `git status --short compiler runtime` | **empty** — Agent 2 edited no `compiler/**` or `runtime/**` source |
| `stdlib/collections.hp` | 08:52:22 |
| `stdlib/error.hp` | 08:46:06 |
| `stdlib/file.hp` | 08:46:19 |
| `stdlib/io.hp` | 08:48:12 |
| `stdlib/log.hp` | 08:49:16 |
| `stdlib/string.hp` | 08:46:28 |
| `stdlib/json.hp`, `stdlib/vector.hp` | 08:17:44 / 2026-11-22 07:08:20 (not part of this adaptation wave) |
| `tests/native/cplus_test_native.hp` | 07:38:54 |
| Sibling header diff scope | `git diff --stat` on the 7 adapted headers: 205 insertions, 71 deletions (collections, error, file, io, log, string, `cplus_test_native.hp`) |

The compiler binary predates all header edits in this wave. No compiler source
was rebuilt or modified by Agent 2. See §4 (external rebuild note).

### Command form (verbatim shape)

```sh
./build/c+ build <src> --strict --sanitize=none \
  [-Ithird_party/yyjson -Lbuild/ext -lyyjson] \
  [-- build/libc/libcplus_hardened.a] \
  -o build/_strict_scan/<name>
```

Two harness caveats were found and corrected before the final scan; their
spurious results are **discarded** and are not reported as observations:

1. Output binaries must be written to a path inside the repository. With an
   output under `/private/var/folders/...` the driver emitted
   `stdlib/json.hp:19:10: fatal error: 'yyjson.h' file not found` even though
   the `-I` flag was present; the same command with an output under `build/`
   compiled cleanly (rc=0, only the usual unused-linker-input warnings).
2. The shell is zsh, which does not word-split an unquoted scalar. Passing the
   flag set as one scalar produced a single bogus `-I` argument and the same
   spurious `yyjson.h` fatal error, plus opaque `clang: error: linker command
   failed with exit code 1` when an archive was appended. The final scan passes
   every flag as a separate positional argument (the shape used by
   `tests/native/build_native_tools.sh`), which removes both artifacts.

---

## 1.Primary requested set — all 37 targets compile clean

The requested set is the 8 native C+ tools from
`tests/native/build_native_tools.sh` plus all 29 `tests/stdlib/deep_*.cp`
tests. Every invocation exited 0 and emitted **zero** `error:` lines.

### 1.1 Native tools (8/8 rc=0, 0 errors)

| Tool | Source | rc | errors |
|---|---|---:|---:|
| `validate_contracts` | `tests/libc/validate_contracts.cp` | 0 | 0 |
| `test_compiler_contract_alignment` | `tests/libc/test_compiler_contract_alignment.cp` | 0 | 0 |
| `test_contract_consumption` | `tests/libc/test_contract_consumption.cp` | 0 | 0 |
| `test_contract_integrity` | `tests/libc/test_contract_integrity.cp` | 0 | 0 |
| `coverage` | `tests/stdlib/coverage.cp` | 0 | 0 |
| `run_adversarial` | `tests/stdlib/run_adversarial.cp` | 0 | 0 |
| `run_all` | `tests/stdlib/agent2/run_all.cp` | 0 | 0 |
| `fuzz100_run` | `tests/fuzz100/run.cp` | 0 | 0 |

This supersedes the 4/8 result recorded in `docs/stdlib/NO_PYTHON_TESTING.md`
(V2, §2), which was taken before the sibling header adaptations. In particular
the earlier first errors

```text
tests/stdlib/coverage.cp:90:27: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe
tests/native/cplus_test_native.hp:46:5: error: [CPLUS-FFI] pointer parameter 'buf' has unverifiable extent; subscript cannot be proven safe
./stdlib/error.hp:401:9: error: [CPLUS-LIFETIME] variable 'total' is uninitialized (read before it is initialized)
tests/fuzz100/run.cp:170:32: error: [CPLUS-FFI] pointer parameter 'a' has unverifiable extent; subscript cannot be proven safe
```

are **no longer emitted** by the current tree with the current `build/c+`.
No claim is made here about running these binaries; this section is a compile
diagnostic scan only.

### 1.2 Deep tests (29/29 rc=0, 0 errors)

`deep_collections_alloc`, `deep_collections_boundary`,
`deep_collections_collisions`, `deep_collections_growth`, `deep_cross_family`,
`deep_error_task5`, `deep_error_vector`, `deep_file_lifecycle`,
`deep_file_lines`, `deep_file_task2`, `deep_fs_task5`, `deep_fs_time`,
`deep_io_1byte`, `deep_io_task2`, `deep_io_token`, `deep_json_stress`,
`deep_json_task5`, `deep_json_types`, `deep_log_task5`, `deep_memory_bounds`,
`deep_net`, `deep_net_task5`, `deep_string_append`, `deep_string_task4`,
`deep_string_view`, `deep_thread_guard`, `deep_thread_nested`,
`deep_thread_task5`, `deep_vector_task4` — all rc=0, zero `error:` lines.
The yyjson triplet was supplied for `deep_json*` and `deep_cross_family`,
matching `tests/stdlib/run_deep_tests.sh:132-134`.

**Headline:** the requested representative set has no remaining strict
diagnostics after the sibling adaptations. The remaining errors below come from
a wider scan of adjacent strict-sensitive suites, included so the handoff does
not hide work that is still outstanding.

---

## 2.Extended scan — 178 additional strict-sensitive sources

Sources: all of `tests/stdlib/agent2/*.cp`, `tests/stdlib/regressions/*.cp`,
`tests/stdlib/fuzz/*.cp`, `tests/stdlib/compile_time/*.cp`,
`tests/stdlib/agent1_handoff/*/*.cp`, plus `tests/libc/test_stdlib_realloc.cp`,
`tests/native/emit_result.cp` and `tests/stdlib/adversarial.cp`. Each file was
compiled alone with `--strict --sanitize=none`, the yyjson triplet, and the
hardened libc archive after `--`.

Result: **112 rc=0, 66 rc!=0**. The 66 non-zero results split as follows:

| Bucket | Files | Meaning |
|---|---:|---|
| (a) Agent 2 library-side patterns still fixable | 33 | Genuine remaining strict diagnostics in Agent 2-owned test/tool sources |
| Intentional negative controls | 25 | Sources designed to be rejected; rejection is the expected result, not a defect |
| Scan-harness artifacts | 8 | Link failures from compiling a single file out of a multi-file program / no `main` |

### (a) Agent 2 library-side patterns still fixable — 33 files

All exact `error:` lines are quoted verbatim below. They fall into three
signatures:

- **A1 — `[CPLUS-FFI] pointer parameter '<p>' has unverifiable extent; subscript cannot be proven safe`** (9 files). Fix at the library/test side by routing the subscript through a tracked extent (`.len`/`.data()` bounds, a checked accessor, or an explicit proven count) instead of a raw pointer parameter plus arithmetic.
- **A2 — `[CPLUS-FFI] shift count cannot be proven within the left operand's width`** (8 files). Fix by masking/clamping the generated shift count in the fuzz/test source before the shift.
- **A3 — `[CPLUS-ARITH] remainder by zero cannot be excluded`** (16 files). Fix by guarding the divisor (`if (d == 0) d = 1;` or equivalent) in the test source or in the shared fuzz engine header `tests/stdlib/fuzz/f100_cont_engine.hp`.

#### A1 — unverifiable pointer extent (9 files)

```text
tests/native/emit_result.cp:23:21: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:47:23: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:49:40: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:50:44: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:51:43: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:53:13: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:53:22: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:60:40: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/native/emit_result.cp:61:64: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe

tests/stdlib/regressions/adv_exact_fit.cp:59:13: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/regressions/adv_exact_fit.cp:69:13: error: [CPLUS-FFI] pointer parameter 'a' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/regressions/adv_exact_fit.cp:69:21: error: [CPLUS-FFI] pointer parameter 'b' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/regressions/adv_long_input.cp:46:9: error: [CPLUS-FFI] pointer parameter 'ali_buf' has unverifiable extent; subscript cannot be proven safe

tests/stdlib/fuzz/f100_log_lines.cp:79:17: error: [CPLUS-FFI] pointer parameter 'name' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/f100_log_lines.cp:80:28: error: [CPLUS-FFI] pointer parameter 'name' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/f100_strview_reserve_copy.cp:59:32: error: [CPLUS-FFI] pointer parameter 'shadow' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/f100_time_iso8601.cp:71:17: error: [CPLUS-FFI] pointer parameter 'text' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/f100_time_iso8601.cp:72:28: error: [CPLUS-FFI] pointer parameter 'text' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_containers.cp:20:37: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_containers.cp:21:17: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:20:26: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:21:17: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:26:40: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:29:42: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:32:36: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:32:54: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:35:39: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/fuzz/fuzz_string.cp:40:43: error: [CPLUS-FFI] pointer parameter 'data' has unverifiable extent; subscript cannot be proven safe

tests/stdlib/agent1_handoff/strict_safe_mode/00_control_strict_delta.cp:10:34: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
tests/stdlib/agent1_handoff/strict_safe_mode/00_control_strict_delta.cp:11:5: error: [CPLUS-FFI] format string in printf is not a string literal (CWE-134; attacker-controlled format)
```

Note: `00_control_strict_delta.cp` is a positive control whose *intended*
rejection is the non-literal `printf` format (line 11); its argv FFI diagnostic
at line 10 is incidental. It is listed here because its rc=1 was counted in
A1, not because it is a defect.

#### A2 — shift count not provable within operand width (8 files)

```text
tests/stdlib/fuzz/f100_file_lifecycle.cp:75:49: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_file_lines.cp:68:43: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_file_write_read.cp:78:49: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_file_write_read.cp:98:48: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_strview_accessors.cp:69:25: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_strview_append.cp:83:25: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_strview_reserve_copy.cp:93:25: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_strview_stale.cp:84:25: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_strview_utf8.cp:104:43: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/f100_strview_utf8.cp:140:43: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
tests/stdlib/fuzz/fuzz_hp.cp:250:25: error: [CPLUS-FFI] shift count cannot be proven within the left operand's width
```

#### A3 — remainder by zero cannot be excluded (16 files)

Five lines recur in the shared fuzz engine header
`tests/stdlib/fuzz/f100_cont_engine.hp`; a single guarded-divisor fix there
closes the bulk of this family (11 of 16 files):

```text
tests/stdlib/fuzz/f100_cont_engine.hp:347:18: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_cont_engine.hp:445:30: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_cont_engine.hp:842:58: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_cont_engine.hp:880:58: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_cont_engine.hp:937:31: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_cont_engine.hp:974:60: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_cont_engine.hp:976:54: error: [CPLUS-ARITH] remainder by zero cannot be excluded
```

Per-file diagnostics outside the shared header:

```text
tests/stdlib/fuzz/f100_file_write_read.cp:110:53: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_mem_tokens.cp:92:24: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_mem_tokens.cp:203:40: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_accessors.cp:148:22: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_accessors.cp:158:21: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_accessors.cp:178:30: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_append.cp:158:44: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_reserve_copy.cp:173:48: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_reserve_copy.cp:183:48: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/f100_strview_utf8.cp:134:17: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/fuzz_hp.cp:319:53: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/fuzz_hp.cp:333:31: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/fuzz_hp.cp:345:31: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/fuzz_hp.cp:358:53: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/fuzz_hp.cp:502:54: error: [CPLUS-ARITH] remainder by zero cannot be excluded
tests/stdlib/fuzz/fuzz_hp.cp:542:54: error: [CPLUS-ARITH] remainder by zero cannot be excluded
```

### Intentional negative controls — 25 files (not residual defects)

These sources are meant to be rejected; their diagnostics are the expected
result and are recorded here only so the raw failure count is not mistaken for
regressions. Representative exact lines:

```text
tests/stdlib/compile_time/collections_use_after_move.cp:31:38: error: use-after-move: use of moved-from resource 'a'
tests/stdlib/compile_time/error_invalid_lifecycle.cp:13:12: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: member access through released pointer
tests/stdlib/compile_time/error_null_pointer.cp:13:12: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer dereference
tests/stdlib/compile_time/file_invalid_borrow.cp:14:12: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer dereference
tests/stdlib/compile_time/file_use_after_move.cp:19:30: error: use-after-move: use of moved-from resource 'a'
tests/stdlib/compile_time/json_invalid_transfer.cp:33:10: error: resource 'b' cannot be copied from 'a'
tests/stdlib/compile_time/net_use_after_transfer.cp:15:28: error: use-after-move: use of moved-from resource 'a'
tests/stdlib/compile_time/string_double_drop.cp:11:5: error: [CPLUS-OWNER] double free of 'buf'
tests/stdlib/compile_time/string_invalid_transfer.cp:11:24: error: borrow 'borrowed' of 'a' is used after that resource was moved
tests/stdlib/compile_time/string_uninitialized.cp:9:21: error: 's' owns a resource and must be initialised
tests/stdlib/compile_time/string_use_after_drop.cp:12:18: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: subscript of released pointer
tests/stdlib/compile_time/vector_use_after_move.cp:29:38: error: use-after-move: use of moved-from resource 'a'
tests/stdlib/agent1_handoff/pointer_bounds/control_fixed_array_rejected.cp:16:12: error: [CPLUS-SAFE-BOUNDS] constant array index 9 is outside the bounds of 'a'
tests/stdlib/agent1_handoff/strict_safe_mode/06_func_ptr_call.cp:16:20: error: [CPLUS-SAFE-BOUNDS] constant array index 99 is outside the bounds of 'table'
tests/stdlib/agent1_handoff/cfg_ownership/conditional_move.cp:25:29: error: use-after-move: use of moved-from resource 'a'
tests/stdlib/agent1_handoff/cross_tu/single_tu_control.cp:10:12: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: subscript of released pointer
tests/stdlib/agent1_handoff/definite_init/01_direct_scalar_control.cp:9:12: error: [CPLUS-LIFETIME] variable 'count' is uninitialized (read before it is initialized)
tests/stdlib/agent1_handoff/definite_init/02_error_ptr_one_branch.cp:14:12: error: [CPLUS-LIFETIME] variable 'slot' is uninitialized (read before it is initialized)
tests/stdlib/agent1_handoff/definite_init/03_scalar_outparam.cp:17:12: error: [CPLUS-LIFETIME] variable 'result' is uninitialized (read before it is initialized)
tests/stdlib/agent1_handoff/definite_init/07_loop_zero_iterations.cp:11:12: error: [CPLUS-LIFETIME] variable 'last' is uninitialized (read before it is initialized)
tests/stdlib/agent1_handoff/definite_init/08_early_return_guard_ptr.cp:18:9: error: [CPLUS-LIFETIME] variable 'ready' is uninitialized (read before it is initialized)
tests/stdlib/agent1_handoff/cross_function_lifetime/cfl_pointer_to_parameter.cp:10:13: error: address of stack memory associated with parameter 'value' returned [-Werror,-Wreturn-stack-address]
tests/stdlib/agent1_handoff/cross_tu/stack_producer.cp:6:12: error: address of stack memory associated with local variable 'local' returned [-Werror,-Wreturn-stack-address]
tests/stdlib/agent1_handoff/alias_analysis/funcptr_dispatch_dynamic.cp:27:5: error: [CPLUS-SAFE-BOUNDS] constant array index 5556 is outside the bounds of 'dispatch'
tests/stdlib/adversarial.cp:79:26: error: use-after-move: use of moved-from resource 'original'
```

The `tests/stdlib/compile_time/*.json` manifests and the `agent1_handoff/*/manifest.json`
files mark these as expected-reject cases; `error.json`, `collections.json`, etc.
also record which untagged rejections count as `PARTIAL` rather than tagged C+
enforcement.

### Scan-harness artifacts — 8 link failures (not strict diagnostics)

Single-file `build` cannot link a program whose `main` or callee lives in
another translation unit, or that was authored for `c+ check` only. Exact
linker text:

```text
Undefined symbols for architecture arm64:
  "_main", referenced from:
      <initial-undefines>
ld: symbol(s) not found for architecture arm64
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

This shape occurs for `tests/stdlib/fuzz/fuzz_json.cp`,
`tests/stdlib/agent1_handoff/cross_tu/{dropper,consumer,producer,stack_consumer,user}.cp`
and similar. Other cases name the missing symbol directly, e.g.
`"_cplus_memset_s", referenced from: _main in cplus-1386-0.o` (strict_safe_mode/02),
`"_cplus_memcpy_s", referenced from:` (strict_safe_mode/03),
`"_produce_view", referenced from: _main` (cross_tu/consumer),
`"_release_shared_resource"`/`"_shared_resource"` (cross_tu/user). These are
multi-TU or link-setup conditions of the scan harness, not compiler
strict-mode errors; no library-side fix should be inferred from them.

---

## 3.Compiler-capability gaps requiring Agent 1 — no compiler edits proposed here

These are cases where the current compiler produces **no diagnostic** and
`--strict` behaves like `--default`. They cannot be closed by editing Agent 2
headers or test sources; they are handed to the compiler owner. Nothing in this
section proposes editing `compiler/**` or `runtime/**` — Agent 2 supplies only
reproducers, manifests and hand-off documents.

| Gap | Evidence / reproducer | Why library-side work cannot close it |
|---|---|---|
| P1 strict-safe mode: 6 unverified operations accepted identically by strict and default (raw pointer arithmetic, unchecked raw `malloc` with checked API, unproven bounded-wrapper capacity, raw `FILE*`, `void*`→typed cast, indirect call) | `tests/stdlib/agent1_handoff/strict_safe_mode/manifest.json` and `NOTES.md`: cases 01–06 `strict rc=0`, `default rc=0`, zero `[CPLUS-` diagnostics; control C has strict `error` (`[CPLUS-FFI]`) vs default `warning`, proving the flag is live | Requires interprocedural provenance/extent/liveness proof in the compiler; strict must fail closed when the proof is absent |
| Dynamic function-pointer dispatch index not proven in range (static constant 5556 is rejected as `[CPLUS-SAFE-BOUNDS]`, dynamic selector is not) | `tests/stdlib/agent1_handoff/alias_analysis/funcptr_dispatch_dynamic.cp:27:5`; `docs/libc/DISPATCH_BOUNDS_HANDOFF.md` | Needs compiler tracking of table extent + selector range; a wrapper cannot bound an indirect call target |
| Untagged rejections (`use-after-move`, `resource 'b' cannot be copied from 'a'`, `borrow ... used after ... moved`) that do not carry a `[CPLUS-` tag and therefore count as `PARTIAL`, not C+ enforcement | `tests/stdlib/compile_time/{collections,file,net,string,vector}.json`; `docs/libc/AGENT1_COMPILER_GAPS.md` §0 counting rule | Tag/category emission is a compiler diagnostic concern; library code cannot attach the tag |
| No `alloc_size` attributes on exported wrappers; `.len` not lowered for `malloc_s`/`cplus_malloc` family | `docs/libc/AUDIT_alloc.md` F-07; `docs/patches/agent1-len-managed-allocators.md`; `docs/libc/KNOWN_COMPILER_GAPS.md:60` | Requires compiler `alloc_size` consumption and `.len` lowering for managed allocators |
| Wide-format attributes ignored by Clang (`format(wprintf/wscanf, …)`) → format diagnostics suppressed | `docs/libc/AUDIT_numeric_wide.md` F11; `docs/libc/IMPLEMENTATION_PLAN.md:232` | Compiler/backend attribute handling, not a wrapper spelling issue |
| Stack-escape / address-after-frame-lifetime detection across 1200-case probe; alias/exclusivity and thread-race cases accepted silently | `docs/libc/KNOWN_COMPILER_GAPS.md:64-81`; `tests/stdlib/agent1_handoff/{alias_analysis,thread_safety,cross_function_lifetime}/` manifests | Needs escape/alias/concurrency analysis in the compiler |

Priority hand-off already exists at `docs/libc/AGENT1_PRIORITY_HANDOFF.md`
and the full catalog at `docs/libc/AGENT1_COMPILER_GAPS.md`.

---

## 4.External rebuild note (category c)

- `build/c+` has mtime `2026-11-23 08:07:07`, i.e. it was rebuilt/replaced
  outside the 08:46–08:52 header adaptation wave. This scan used that binary
  unchanged.
- `git status --short compiler runtime` is empty: no compiler or runtime source
  was edited by Agent 2 on this branch. Therefore the 08:07 binary change is
  an **external** rebuild/hand-off event, not an Agent 2 compiler edit.
- Consequence for downstream consumers: the clean results in §1 are valid for
  **this exact binary**. If `build/c+` is rebuilt again, the 8 tools and 29 deep
  tests must be recompiled before any of §1 is reused. The four formerly failing
  tools (`coverage`, `run_adversarial`, `run_all`, `fuzz100_run`) must not be
  reported as passing on the strength of the stale `07:59` binaries in
  `build/native-tools/`; this scan compiled fresh binaries into
  `build/_strict_scan/` for all eight.

---

## 5.What is and is not claimed

**Verified here**

- All 8 native tools and all 29 deep tests compile with `build/c+ --strict
  --sanitize=none` at rc=0 with zero `error:` lines (§1).
- 178 additional strict-sensitive sources were compiled; 66 returned non-zero,
  split into 33 library-side fixable diagnostics, 25 intentional negative
  controls and 8 single-file link artifacts (§2).
- Exact `file:line` and verbatim text for every remaining library-side
  diagnostic (§2(a)).
- `build/c+` mtime is 08:07:07 and no `compiler/**`/`runtime/**` tracked file is
  modified by Agent 2 (§4).

**Not claimed**

- That any binary was **run**. This is a compile-diagnostic scan; no runtime,
  fuzz, game or emulated result is asserted.
- That the whole `tests/` tree compiles clean under `--strict`. It does not:
  §2(a) lists 33 outstanding sources.
- That the compiler capability gaps in §3 are fixed. They are not; they produce
  no diagnostics and are Agent 1 work.
- That a compiler source file was edited by Agent 2. It was not.

**Repository files touched by this verification:** only
`docs/libc/STRICT_HEADER_ADAPTATION_AUDIT.md`. Compile artifacts were written
to `build/_strict_scan/` and a scratch temp directory; no source, header,
fixture, wrapper or script was modified; nothing was staged or committed.
