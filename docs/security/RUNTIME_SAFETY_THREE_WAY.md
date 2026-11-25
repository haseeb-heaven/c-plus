# Three-way runtime-safety comparison: Rust / Fil-C / Trap-C

**Branch:** `agent/modern-stdlib`
**Document capture:** 2026-11-23 (aggregation only)
**Role:** This document **reads and consolidates** three sibling security documents and the
on-disk artifacts they cite. It does **not** re-execute a harness, compile a model, or
synthesize, round, or promote any number. Every figure below is quoted from a named sibling
document (§6). A missing artifact is reported **PENDING**, never treated as a pass.

**File ownership:** `docs/security/RUNTIME_SAFETY_THREE_WAY.md` is the only file
created/modified for this consolidation. No source, header, build file, test, or git index
entry was changed; no `git add` / `git commit` was run.

---

## 1. Headline status

| Approach | Available here? | Headline |
|:--|:--|:--|
| **Rust** | **YES** — standalone safe-Rust models (`no unsafe`, no FFI, no raw pointers, no external crates) plus seven comparison harnesses on disk | 0 real mismatches, 5 honest `DIFFERENT` divergences, 43 accounted `BLOCKED`, 2 nonzero exits by design — **not** equivalence, **not** 100% safety |
| **Fil-C** | **PARTIALLY** — one narrow technique (a metadata liveness/bounds *query*) adapted; no runtime, no capability model, no pointer representation | `ALLOC_DIAG 26 0` (26/26) in strict and UBSan; ABI-neutral; **no** Fil-C-level memory-safety claim |
| **Trap-C** | **NO** — no source, no repo, no license, no compiler/interpreter; **ZERO** code integrated | **NOT TESTABLE** — research-only; **no** pass, number, or comparison asserted |

---

## 2. The single comparison table

| approach | available here? | what was actually tested | measured result | ABI impact | license status | honest limit |
|:--|:--|:--|:--|:--|:--|:--|
| **Rust** (safe-Rust models vs the C+ checked layer) | **YES.** Models are standalone `.rs` files, no `unsafe`/FFI/raw pointers/crates, built with `rustc 1.100.0 (0ef1bc386)`. Seven harness campaigns exist on disk. | **Outcome-token / stdout parity**, campaign by campaign (not additive): per-method memory-safety matrix (`per_method/RESULTS.md`); 9-group safe-library examples (`rust_examples/RESULTS.md`); 12-API ordinary-C spellings (`stdc_compare/NOTES.md`); 49 example-vs-Rust comparisons (`example_rust_compare/RESULTS.md`); and three expanded campaigns (`expanded_cli_games`, `expanded_db_engine`, `more_games_a`). Tokens = normalized words (`OK`, `REFUSED`, `NOTFOUND`, `CANARY_OK`); executable campaigns compare stdout bytes and usually exit status on fixed deterministic inputs. | `per_method`: **283 cases / 277 matched / 0 mismatched / 13 blocked**, all 9 drivers exit 0. `rust_examples`: **376 cases, 560 matched token lines, 0 real mismatches, 29 blocked, 2 `BAD_REPORT`**, runner exit **1** (`RESULT: FAIL`, for contract violations, not behavior). `stdc_compare`: **72 / 72 / 0**, exit 0. `example_rust_compare`: **49 examples → 43 `MATCH`, 5 `DIFFERENT`, 1 `BLOCKED`**, exit **1** (intentional). Expanded: **9/9**, **10/10**, **6/6** (27 fixed cases), all exit 0. | **None on the C+ ABI.** Rust models are separate comparison programs in a separate language/runtime; they do not link into or alter the C+ checked layer. The C+ side keeps its ordinary C ABI throughout. C+ runtime checks (errno/`errno` codes, bounded caches, canaries, fail-closed checkers) vs Rust compile-time ownership/borrow rules + checked returns are **different mechanisms**, not a shared ABI. | Rust models were authored for these campaigns; **no external crate** is used. No third-party source-redistribution obligation is introduced by the comparison beyond the Rust toolchain itself. | A `MATCH` means **observable outcome/stdout/exit parity on the tested inputs only** — not equivalent guarantees, semantics, diagnostics, memory layout, or correctness. C+ checks at **run time** and dangerous code still compiles/runs; Rust prevents null/`&mut` aliasing/double-owner at **compile time**. 5 real `DIFFERENT` rows (4 C-only safeclib handler stdout lines; 1 Rust panic (exit 101) vs C+ abort (exit 134)); 43 accounted blocked; `BOS_UNKNOWN` calls disable object-size checks; caches are finite heuristics. One host (macOS arm64), one toolchain pair, finite deterministic sets — **no equivalence and no 100%-safety claim**. |
| **Fil-C** (liveness/bounds query) | **PARTIALLY / technique only.** The contract `zhasvalidcap` / `zvalinbounds` was re-expressed as read-only `static inline` helpers (`stdlib_alloc_cap_state` / `hasvalidcap` / `cap_covers` / `cap_check`) over the **existing** `stdlib/error.hp` side table. Upstream BSD-2-Clause-style source was fetched and studied; the pointer representation was **NOT copied**. | `tests/stdlib/upstream_reuse/filc_diag.cp` — **26 checks** across `fd_live_state`, `fd_freed_state`, `fd_recycled_state`, `fd_bounds_edges`, `fd_invalid_refusal`; output `ALLOC_DIAG <checks> <failures>`. Re-run independently for this comparison. | `ALLOC_DIAG 26 0` (**26/26 pass**) in **strict/no-sanitizer** (BUILD_RC=0, RUN_RC=0, empty stderr) **and UBSan** via the env-only `CPLUS_INCLUDE` bridge; strict and UBSan stdout byte-identical (`cmp -s`, SHA-256 `1c9ca500…72bf305`). Regression guard `allocator_test 21 0` still passes under strict. | **NONE — ABI-neutral.** Ordinary `T *` stays one word; no fat/capability pointers, no InvisiCaps lower/intval pair, no pointer tagging, no custom ABI/mangling, no getter indirection, no GC/safepoints/free-singleton repointing, no new global symbol (only `static inline`). Fil-C's own ABI is incompatible with Yolo-C and needs whole-program compilation and ELF (no 32-bit support); this project builds 32-bit Windows x86 DLLs, so the representation was **REJECTED for port**. | Upstream is **BSD-2-Clause-style** (`filc/include/stdfil.h`, `filc/src/runtime.c`: © 2023–2026 Epic Games, Inc. and © 2026 Filip Pizlo; `libpas/LICENSE.txt` adds © 2018–2022 Apple Inc.), pinned at commit `a80e32804fd44d295deb1ce1b0098962022da0a3` on `deluge`. **Only the contract/technique was re-expressed; no Fil-C source was copied** → no redistribution obligation beyond documentation/attribution. | The query is a **point-in-time metadata snapshot under `meta_lock`, not a dereference-time check** — raw writes through an unchecked pointer remain unguarded. Freed vs never-allocated is indistinguishable by address alone (tombstone + zero generation, not Fil-C's free-singleton repointing). A generation token detects recycled addresses but is a **diagnostic hint, not an unforgeable capability**. Exact-base keying only. These fail-closed, read-only helpers are safe on stale pointers but establish no liveness/identity proof and do not trap on violation. **No Fil-C-level memory-safety claim.** |
| **Trap-C** | **NO.** No public repository, commit, tag, `LICENSE`/`COPYING`, source, header, test, compiler (`trapc`) or interpreter (`itrapc`). **Zero code integrated** — none can be. | **NOTHING.** No Trap-C program was compiled, run, or measured; no harness can be built. Only documentation was read: WG14 `n3423` (2025-01-07), trapc.org pages, and a third-party news report. All described mechanisms (traps, `testof`/RTTI extent queries, pointer nulling on overrun, no-op `free`/`delete`) are **author-published design intent**, not observed behavior. | **NONE — no pass, no number, no equivalence/parity/superiority result.** Every Trap-C row of a runtime-safety comparison is **NOT TESTABLE (no published source/toolchain)**. Implementation internals (metadata layout, check ordering, trap ABI, tag width, thread safety) are **[NOT OBSERVED]**. | **REJECTED.** Documented design is **one-way ABI compatible only** (Trap-C → C); Trap-C pointers carry hidden RTTI and managed lifetime, so raw C pointers are forbidden "mystery pointers," and `free`/`delete` become no-ops with trap-aware POSIX wrappers. This would break the ordinary thin-pointer representation and C signatures; it is compiler/language territory, not a header, and is rejected independently of the license question. | **UNDETERMINED.** Only published intent: "free open source software (FOSS)" / "nearing release as FOSS" — **no license published**, no SPDX identifier. **Do not** write MIT/BSD/Apache for Trap-C; the `$ echo "Open Source MIT" > LICENSE` string in `n3423` is a tutorial example for a user's *own* project, not Trap-C's license. | **Research-only documentation-based traceability**, not an implementation or validation of Trap-C. No runtime-safety property was verified or scored `PASS`/`FAIL`. Pins are stale as to release status (n3423 "sometime in 2025"; About page "Q1 2026"), yet source remains unpublished as of 2026-11-23 — trapc.org must be re-checked before any future reuse discussion. |

---

## 3. What these approaches are (categorical distinction)

- **Rust — static guarantees.** Safe Rust makes null references, overlapping `&mut` borrows,
  and a second owner/double drop **unrepresentable** at compile time. Remaining cases use
  `Option`/`None`, `Result`/`Err`, `checked_mul`, `try_reserve_exact`, and panic-free
  bounds-checked `get`/`get_mut`. This is categorically different from C+ and does not
  transfer by copying checks.
- **Fil-C — narrow technique adaptation.** The reusable idea is only: *"is this pointer a
  valid, non-free allocation, and does `(offset,size)` fit?" is answered from metadata, never
  by dereferencing the pointer*, plus the overflow-safe subtract form `size <= upper - ptr`
  (re-expressed as `offset <= cap && size <= cap - offset`). No Fil-C runtime, capability
  model, fat pointer, or GC came across.
- **Trap-C — unusable.** No source to adapt and an ABI-incompatible one-way managed-pointer
  design; it is research-only and not a candidate for integration or comparison numbers.

---

## 4. Explicit conclusion

**C+ achieves runtime safety additively, through checked APIs and side-table metadata, while
preserving the ordinary C ABI.** Checked entry points return `errno_t`/`errno` codes, refuse
hostile inputs, and can be layered on without changing pointer representation, signatures, or
layout. This is the through-line of the Rust and Fil-C evidence:

- **Rust differs categorically.** Its guarantees are **static** (compile-time
  ownership/borrow rules), not an additive runtime layer. The seven campaigns show only that
  C+ and independently written safe-Rust models produce the same observable outputs on fixed
  deterministic inputs — 0 real mismatches — but with 5 genuine `DIFFERENT` divergences,
  43 accounted blocked items, and two deliberately nonzero runner exits. This is **behavioral
  correspondence, not equivalence**, and **not** a 100%-safety claim.
- **The Fil-C technique was adapted in a narrow form only** — a fail-closed, read-only,
  metadata-backed liveness/bounds query on the existing side table, verified at
  `ALLOC_DIAG 26 0`. It is ABI-neutral, copies no Fil-C code, and makes **no** Fil-C-level
  memory-safety claim.
- **Trap-C is unusable here** — no source/repo/license/toolchain, ZERO code integrated,
  NOT TESTABLE, research-only, and ABI-incompatible (one-way managed pointers).

**No equivalence, parity, superiority, or 100%-safety claim is made for any row.** The
campaigns are not additive and yield no grand total; all results are bounded, deterministic,
one-host (macOS arm64), one-toolchain-pair observations, with sanitizer and interactive paths
withheld rather than scored as passes.

---

## 5. Non-claims (apply to the whole document)

1. No Rust-equivalence, Fil-C-equivalence, or Trap-C-equivalence claim; no combined pass total.
2. No 100%-safety or general memory-safety proof for C+ (or any row).
3. No live-game or runtime claim from a build/harness result alone.
4. No assertion of Trap-C behavior, license, or internals; no SPDX identifier assigned.
5. No claim that Fil-C's pointer representation, custom ABI, GC, or capability model was ported.
6. No extrapolation beyond the cited artifacts' host/toolchain and finite deterministic inputs.

---

## 6. Sibling-document and artifact provenance

### 6.1 Sibling docs — status: **all PRESENT and read; none PENDING**

| Sibling doc | Status |
|:--|:--|
| `docs/security/RUNTIME_SAFETY_RUST_FINAL.md` | **PRESENT — read in full** |
| `docs/security/RUNTIME_SAFETY_FILC.md` | **PRESENT — read in full** |
| `docs/security/RUNTIME_SAFETY_TRAPC.md` | **PRESENT — read in full** |

Reproduction of this status check (run from the repo root):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
for f in \
  docs/security/RUNTIME_SAFETY_RUST_FINAL.md \
  docs/security/RUNTIME_SAFETY_FILC.md \
  docs/security/RUNTIME_SAFETY_TRAPC.md \
  docs/security/RUNTIME_SAFETY_THREE_WAY.md ; do
  [ -f "$f" ] && echo "PRESENT  $f" || echo "PENDING  $f"
done
```

At capture time all four paths resolve, so **no sibling doc is marked PENDING**. If a future
capture reports `PENDING`, restore the missing doc before relying on that row's figures.

### 6.2 Source artifacts quoted (verified on disk at capture)

| Artifact | Used for | Status |
|:--|:--|:--|
| `benchmarks/safety_compare/per_method/RESULTS.md` | 283 / 277 / 0 / 13 matrix | **PRESENT — spot-checked** |
| `benchmarks/rust_examples/RESULTS.md` | 29 blocked, 0 real mismatches, exit 1 | **PRESENT — spot-checked** |
| `benchmarks/rust_examples/stdc_compare/NOTES.md` | 72 / 72 / 0, exit 0 | **PRESENT** |
| `benchmarks/example_rust_compare/RESULTS.md` | 49 = 43 `MATCH` / 5 `DIFFERENT` / 1 `BLOCKED` | **PRESENT — spot-checked** |
| `benchmarks/example_rust_compare/expanded_cli_games/RESULTS.md` | 9 / 9 | **PRESENT — spot-checked** |
| `benchmarks/example_rust_compare/expanded_db_engine/RESULTS.md` | 10 / 10 | **PRESENT — spot-checked** |
| `benchmarks/example_rust_compare/more_games_a/RESULTS.md` | 6 / 6 (27 fixed cases) | **PRESENT — spot-checked** |
| `docs/abi/STDC_ABI_SAFETY_FINAL.md` | ABI/safety context, external blocker | **PRESENT** |
| `docs/stdlib/upstream_study/fil_c.md`, `docs/stdlib/upstream-reuse.md` §10 | Fil-C upstream/license/integration record | **PRESENT** |
| `stdlib/allocator.hp`, `stdlib/error.hp` | Actual C+ liveness/bounds query adaptation + side table | **PRESENT** |
| `tests/stdlib/upstream_reuse/filc_diag.cp`, `filc_diag_NOTES.md` | `ALLOC_DIAG 26 0`, pins, license | **PRESENT** |
| WG14 `n3423`, trapc.org pages, third-party report | Trap-C documented design intent only | **External docs; no source** |

**PENDING items (explicit, not hidden):**

- **`benchmarks/rust_examples/stdc_compare/RESULTS.md`** — absent on disk at this capture; the
  authoritative recorded run is in `stdc_compare/NOTES.md`, which is present. No `RESULTS.md`
  content is invented. (Also noted in `RUNTIME_SAFETY_RUST_FINAL.md` §8.)
- **Per-item enumeration of the 29 `rust_examples` blocked cases** — the aggregate reports only
  per-group counts (24/2/1/2); case names live in each group's `out/` / `results/`, outside
  this consolidation. Marked **PENDING further itemization**; no per-item reason fabricated.
- **Any Trap-C runtime number** — permanently PENDING/NOT TESTABLE: no source or toolchain
  exists to produce one.

No other assigned artifact is missing. This document adds no new experimental result.

## FINAL UPDATE (2026-11-24) — modern allocators, 100% coverage, honest toolchain status

New evidence added after the consolidation above:

| Check | Command | Result |
|---|---|---|
| Rust vs C+ modern allocators | `sh benchmarks/rust_examples/compare_modern.sh` | `MODERN_COMPARE MATCH` (`cpp_failures=0 rust_ok=1 vm_tokens=1 slot_tokens=1`) |
| C+ Goose-inspired model | `tests/stdlib/scoped_memory.cp` | `SCOPED_MEMORY 0` (VM on-demand commit, RAII scope free, stable slot handles, stale 103 / double-free 104 refusal) |
| Fil-C technique (adapted side-table liveness/bounds only) | `tests/stdlib/upstream_reuse/filc_diag.cp` | `ALLOC_DIAG 26 0` — 26/26 pass |
| Fil-C full runtime | — | **NOT TESTABLE**: `filc`/`filc-cc` toolchain not installed; no Fil-C source/runtime vendored; only the contract-level technique was adapted |
| Trap-C | — | **NOT TESTABLE**: no source, repo, license, or `trapc`/`itrapc` toolchain (`which` found none); design-intent docs only |
| Full stdlib API coverage | `sh tests/stdlib/run_coverage.sh` | 416/416 callable APIs, all 24 headers 100.0% |
| Deep / methods / libc / CRT / ABI | runners | 28 PASS/0 FAIL (default), 518/518, 109/109, 86/86, fingerprint MATCH |

The comparison remains **outcome-token parity only**: C+ enforces at runtime with
`error*`/generation/side tables; Rust models the same observations with safe
`Vec`/`VecDeque` and ownership — no Rust-equivalence, no full memory-safety,
and no Fil-C/Trap-C runtime claim. `docs/stdlib/SCOPED_MEMORY_INSPIRATION.md`
records that Goose (Apache-2.0) contributed design ideas only, no code.
