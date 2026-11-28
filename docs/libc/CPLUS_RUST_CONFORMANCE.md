# C+ / Rust conformance: honest safety-gate status

> **C+ is NOT Rust-equivalent.** Sanitizer detection (Clang UBSan, ASan, TSan) is
> **not a language guarantee**. A program that aborts because a sanitizer noticed
> undefined behaviour is still a program that contained undefined behaviour.
> Nothing in this document claims Rust-level memory, ownership, concurrency, or
> FFI safety for C+.

This document maps the twelve safety gates from the external assessment onto the
evidence that actually exists in this repository. It is a status record, not an
advertisement and not a proof. The machine-readable companion is
[`SAFETY_GATES.json`](SAFETY_GATES.json).

## How to read the statuses

- **PROVEN** — a dedicated in-repo test demonstrates the *whole* gate through a
  C+ mechanism (compiler diagnostic or checked library API), not through a
  sanitizer, and no uncovered path is silently assumed safe.
- **PARTIAL** — a real C+ mechanism exists for a defined subset (constant
  indices, checked `_s` entry points, explicit generation/liveness queries,
  caller-side annotation), but raw C paths, unknown-capacity cases, or
  interprocedural cases remain undefined behaviour.
- **NOT_PROVEN** — no dedicated C+ mechanism demonstrates the property; the
  evidence is sanitizer output, caller discipline, or documentation of a limit.

Rust's column describes **safe Rust** at the language level. Rust is marked
`PARTIAL` for integer overflow because debug builds panic on overflow while
release builds wrap by default unless overflow checks are enabled. Rust's
`unsafe` blocks and FFI are explicit escape hatches; they are not claimed to be
memory-safe by themselves.

## Gate-by-gate status

| # | Gate | C+ status | Mechanism actually used | Evidence file(s) | Rust status | Gap description |
|---:|---|---|---|---|---|---|
| 1 | Static array bounds | **PARTIAL** | C+ front-end token-level `[CPLUS-BOUNDS]` / `[CPLUS-SAFE-BOUNDS]` rejects constant indices on same-function arrays; `__builtin_object_size(p,1)` gives object extents to checked copy APIs. | `tests/compile_fail/constant_oob.c`+`.err`, `tests/compile_fail/multidim_bounds.c`+`.err`, `tests/compile_fail/case13_dispatch_const.c`+`.err`, `tests/compile_fail/loop_oob.c`+`.err`, `docs/libc/KNOWN_COMPILER_GAPS.md` | PROVEN | Only constant/direct subscripts are rejected. No interprocedural, pointer-parameter, or heap-capacity analysis: `int *p = malloc(1); for (...) p[i]=...` compiles silently and corrupts the heap. |
| 2 | Runtime bounds | **PARTIAL** | Checked `_s` APIs and the ordinary-C mapping use tracked remaining capacity (including interior and one-past-end pointers) plus `__builtin_object_size(p,1)`; strict mode fails closed. Raw runtime-indexed subscripts are **only** caught by Clang UBSan. | `docs/libc/ORDINARY_C_SAFETY.md`, `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`, `tests/runtime_fail/bounds.stderr`, `tests/runtime_fail/case13_dispatch_runtime.stderr`, `tests/scenarios/cve_like/NOTES_1.md`, `tests/scenarios/cve_like/NOTES_4.md` | PROVEN | No C+ instrumentation for a runtime index on a raw array. UBSan detection is sanitizer evidence, not a C+ guarantee. Default mode preserves raw-C behaviour with a warning (not a refusal) for genuinely unknown capacity. |
| 3 | Heap alloc/dealloc safety | **PARTIAL** | Side-table metadata with generation counters, overflow-rejecting `calloc`/`realloc`, live-count telemetry, checked `free`. ASan intercepts the host `malloc` block but is verification only. | `docs/libc/C_BUG_CLASS_MATRIX.md` (rows 2, 6, 9, 28, 29), `docs/libc/ADVERSARIAL_FINDINGS.md`, `docs/libc/CONTRACT_STATUS.md`, `docs/libc/ASAN_AUDIT.md` §3.4–3.5, `tests/crt/adversarial/test_heap_oob_mitigation.c` | PROVEN | Raw `p[i]` on a tracked heap block is not bounds-checked; cross-allocator free and host-`free` bypass are UB; metadata is diagnostics, not provenance. ASan's OOB report is a sanitizer result. |
| 4 | Use-after-free | **PARTIAL** | Explicit `cplus_check_live`, `cplus_alloc_is_live`, generation checks, checked release, and caller-side re-derivation of aliases after `realloc`; fail-stop on detected stale handles. | `docs/libc/C_BUG_CLASS_MATRIX.md` (row 5), `tests/runtime_fail/classic_bug2_uaf.c`+`.stderr`+`.exit`, `tests/scenarios/cve_like/NOTES_2.md` (case 04), `tests/scenarios/cve_like/NOTES_3.md` (cases 06, 08), `docs/libc/CONTRACT_STATUS.md` §4 | PROVEN | A stale raw alias cannot be revoked, and a recycled address can defeat the bounded recent-free table. The dedicated test calls the check explicitly; it does not make arbitrary dereferences safe. |
| 5 | Double free | **PARTIAL** | `cplus_free_checked` / `free_checked_s` with an open-addressed recent-free cache refuse a repeated or invalid release; deterministic C+ runtime traps. | `docs/libc/C_BUG_CLASS_MATRIX.md` (row 6), `docs/libc/ADVERSARIAL_FINDINGS.md` (fixes 2, 4), `tests/runtime_fail/classic_bug5_double_free.c`+`.stderr`+`.exit`, `tests/runtime_fail/double_release.c`+`.stderr`+`.exit`, `docs/libc/CONTRACT_STATUS.md` §3 | PROVEN | Detection is best-effort: the cache is bounded (1024 entries) and collisions can misclassify. A raw `free(p)` twice remains UB, as does freeing with the wrong allocator. |
| 6 | Ownership / lifetime | **PARTIAL** | C+ resource/owner/borrow annotations with compile-time diagnostics (move, copy, escape, stack return), ownership-transfer and generation APIs, explicit caller discipline. | `tests/compile_fail/use_after_move.c`+`.err`, `tests/compile_fail/resource_copy.c`+`.err`, `tests/compile_fail/owned_not_resource.c`+`.err`, `tests/compile_fail/stack_return.c`+`.err`, `tests/compile_fail/borrow_escape.c`+`.err`, `tests/compile_fail/managed_escape.c`+`.err`, `docs/libc/KNOWN_COMPILER_GAPS.md` (stack-escape gap), `docs/libc/C_BUG_CLASS_MATRIX.md` (row 35), `tests/scenarios/cve_like/NOTES_3.md` | PROVEN | 547 of 1200 stack-escape scenarios are undiagnosed; there is no dedicated `[CPLUS-LIFETIME]` stack-escape check, no interprocedural lifetime analysis, and no borrow checker for raw C (explicitly out of scope, issue #22). |
| 7 | Null / dangling | **PARTIAL** | Compile-time null-dereference diagnostics for provable cases; guarded library entry points set `errno = EINVAL` and return defined failures; liveness/generation queries for tracked pointers. | `tests/compile_fail/null_deref.c`+`.err`, `tests/compile_fail/classic_bug6_null.c`+`.err`, `docs/libc/C_BUG_CLASS_MATRIX.md` (rows 4, 15), `docs/libc/CONTRACT_STATUS.md` §4, `tests/scenarios/cve_like/NOTES_3.md` | PROVEN | Unguarded ISO APIs (`strlen(NULL)`) intentionally keep UB; escaped raw pointers and compound-literal addresses cannot be revoked; null pointer parameters to a helper escape some checks (see compiler-gaps doc). |
| 8 | Data races | **PARTIAL** | One C+ compile-time diagnostic for unsynchronized writes to a shared global; atomics/mutexes inside allocator and logging metadata; caller-side mutex + stop flag in the safe scenario. | `tests/compile_fail/classic_bug24_race.c`+`.err`, `docs/libc/C_BUG_CLASS_MATRIX.md` (row 17), `tests/scenarios/runtime_13_threads.c`, `tests/scenarios/cve_like/NOTES_5.md` (case 15) | PROVEN | The cve-like race fix is caller-side; TSan output is sanitizer evidence. No language-level `Send`/`Sync`-style ownership of threads, and shared `FILE*`, `errno`, and `mbstate_t` remain the caller's synchronization responsibility. |
| 9 | Integer overflow | **PARTIAL** | `calloc`/`realloc` reject wrapping `n*size`; conversion wrappers saturate with `ERANGE`; C+ compile-time diagnostics for some shift counts; checked arithmetic guards in safe scenario variants. | `docs/libc/C_BUG_CLASS_MATRIX.md` (rows 9, 10, 11, 19, 20), `docs/libc/ADVERSARIAL_FINDINGS.md` (fix 9), `tests/runtime_fail/alloc_overflow.c`+`.exit`, `tests/runtime_fail/classic_bug9_overflow.stderr`, `tests/compile_fail/classic_bug19_shift.c`+`.err`, `tests/scenarios/cve_like/NOTES_1.md`, `tests/scenarios/cve_like/NOTES_2.md`, `tests/scenarios/cve_like/NOTES_4.md` | PARTIAL | Signed overflow, divide-by-zero and invalid shifts are documented `NOT FIXABLE BY LIBC`; the C+ runtime test for signed overflow is UBSan. Unsigned wrap is well-defined and is not flagged by the library itself. |
| 10 | Uninitialized memory | **PARTIAL** | Dedicated C+ compile-time diagnostics for locally provable uninitialized reads; corpora use deliberately hostile **but initialized** objects so wrappers define their behaviour. | `tests/compile_fail/uninitialized.c`+`.err`, `tests/compile_fail/classic_bug7_uninit.c`+`.err`, `docs/libc/C_BUG_CLASS_MATRIX.md` (row 14), `docs/libc/ADVERSARIAL_FINDINGS.md` §"Compile-time bug catalogs" | PROVEN | Reading truly uninitialized memory, or an uninitialized field of an otherwise initialized object, is caller UB. No language-level definite-assignment analysis for all paths; no library can detect an uninitialized read it did not perform. |
| 11 | Function-pointer / indirect-call validation | **PARTIAL** | C+ `[CPLUS-BOUNDS]` rejects a constant out-of-range index into a function-pointer table; caller-side `cmd < N` check in the safe variant. Runtime index detection is Clang UBSan only. | `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`, `tests/compile_fail/case13_dispatch_const.c`+`.err`, `tests/runtime_fail/case13_dispatch_runtime.c`+`.stderr`, `tests/scenarios/cve_like/NOTES_5.md` (case 13) | PROVEN | C+ inserts no runtime check for an index it cannot prove statically. There is no CFI, no indirect-call target validation, and no shadow stack: a corrupted table would still be followed. |
| 12 | FFI / unsafe boundaries | **NOT_PROVEN** | Explicit header opt-in for ordinary-C mapping, typed checked APIs, and machine-readable contracts with stated limitations. These are conventions and contracts, not a language-enforced boundary. | `docs/libc/ORDINARY_C_SAFETY.md` §"Honest limits", `docs/libc/CONTRACT_STATUS.md` §3–4, `docs/libc/ADVERSARIAL_FINDINGS.md` §"Documented as NOT FIXABLE BY LIBC", `tests/scenarios/cve_like/NOTES_1.md` … `NOTES_5.md` | PROVEN | No `unsafe` keyword or language-enforced FFI boundary; C+ cannot establish provenance or interior-object identity for arbitrary pointers, cannot stop cross-allocator frees, and cannot make a host call safe. Safe variants rely on caller-side validation. |

**Distribution:** 0 PROVEN, 11 PARTIAL, 1 NOT_PROVEN for C+; 11 PROVEN and 1
PARTIAL (integer overflow) for safe Rust.

## What would be required to claim equivalence

None of the following is claimed today. All four would have to hold, with
dedicated language-level tests, before any Rust-equivalence statement could be
made:

1. **Compile-time ownership, borrow, and lifetime analysis.** Interprocedural
   ownership propagation, move/copy rules, and a real stack-escape/lifetime
   check covering the 547 currently undiagnosed scenarios — not annotations that
   only work in a single function or on a checked API.
2. **Language-level bounds checks.** Constant indices *and* runtime indices,
   including heap extents, pointer parameters, interior pointers, and
   function-pointer dispatch. A check inserted by the C+ compiler itself, not a
   sanitizer runtime and not a caller-supplied capacity argument.
3. **A safe FFI boundary.** Language-enforced separation between safe and unsafe
   code, with pointer provenance and allocator identity tracked by the
   type system so arbitrary raw pointers cannot flow into safe code unchecked.
4. **No reliance on sanitizers.** UBSan/ASan/TSan are valuable verification
   tools, but today they supply the detection for runtime array bounds, signed
   overflow, some dispatch failures, and the baseline race evidence. Those
   paths must be enforced by C+ itself before equivalence can be discussed.

Additional prerequisites outside the four named above would include language-level
integer-overflow semantics in all build modes, definite initialization for every
field and path, and a concurrency model that makes data races a compile error
rather than a caller convention.

## Paired case suites measure outcomes, not guarantees

The paired case suite for this effort is `tests/conformance/` (vulnerable/safe
pairs consumed by its runner). In this branch the on-disk paired evidence is at
`tests/scenarios/cve_like/` with `case_NN.c` / `case_NN_safe.c` pairs and
`run_cve_like.sh`; the `tests/conformance/` location is the parallel-work target
and is not modified here.

Either way, such a runner measures **observed outcomes only**: exit codes,
stderr substrings, and sanitizer reports for a fixed scripted input. A pass means
the specific case behaved as expected on this host and toolchain. It does **not**
prove the absence of undefined behaviour, a language-level guarantee, or Rust
equivalence. The NOTES files in `tests/scenarios/cve_like/` classify most of the
cases as `NOT-FIXABLE-BY-LIBC` / caller-side fixes precisely for this reason.

## Environment caveats on the sanitizer evidence

- Apple clang 16 (`/usr/bin/clang`) has a broken ASan/TSan runtime on this host
  (`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 ...`). Baseline
  sanitizer reports in the cve_like notes use Homebrew clang 23.1.6 with an
  explicit `-isysroot`.
- ASan works with Homebrew clang 23 + the Command Line Tools SDK and intercepts
  the hardened allocators without source changes
  ([`ASAN_AUDIT.md`](ASAN_AUDIT.md)); that remains sanitizer evidence only.
- Several baseline cases are optimization-dependent: dead stores are removed at
  `-O1` (cases 01, 10, 12), so both `-O0` and `-O1` results are recorded in the
  NOTES files rather than a single universal "crash" claim.
