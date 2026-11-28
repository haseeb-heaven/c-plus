# Final consolidation: runtime-safety parity evidence (C+ checked layer vs safe Rust)

**Branch:** `agent/modern-stdlib`
**Document capture:** 2026-11-23 (aggregation only)
**Role:** This document **reads and consolidates** existing on-disk artifacts. It does **not**
re-execute any harness and does **not** synthesize, round, or promote a number beyond the
scope written in its source artifact. Every figure below is quoted from a named file listed
in §8. A missing artifact is reported as **PENDING**, never treated as a pass.

**File ownership:** `docs/security/RUNTIME_SAFETY_RUST_FINAL.md` is the only file
created/modified for this consolidation. No source, header, build file, test, or git index
entry was changed; no `git add` / `git commit` was run.

---

## 1. Headline summary (verbatim from the source artifacts)

| Campaign | Cases (as reported) | Matched | Different | Blocked | Runner exit | Source artifact |
|:--|--:|--:|--:|--:|--:|:--|
| Per-method memory-safety matrix | 283 | 277 | 0 | 13 | 0 (all 9 group drivers) | `benchmarks/safety_compare/per_method/RESULTS.md` |
| Rust safe-library examples (9 groups) | 376 (`cases`), 560 matched token lines | 560 | 0 (real mismatches) | 29 | **1** (`RESULT: FAIL`) | `benchmarks/rust_examples/RESULTS.md` |
| `stdc_compare` (ordinary C spellings) | 72 | 72 | 0 | 0 | 0 | `benchmarks/rust_examples/stdc_compare/NOTES.md` |
| 49 example-vs-Rust comparisons (10 groups) | 49 examples | 43 `MATCH` | 5 `DIFFERENT` | 1 | **1** (intentional) | `benchmarks/example_rust_compare/RESULTS.md` |
| Expanded CLI & game examples | 9 examples (20 fixed cases) | 9 `MATCH` | 0 | 0 | 0 | `benchmarks/example_rust_compare/expanded_cli_games/RESULTS.md` |
| Expanded database / engine / safety examples | 10 examples | 10 `MATCH` | 0 | 0 | 0 | `benchmarks/example_rust_compare/expanded_db_engine/RESULTS.md` |
| `more_games_a` (games_2 / games_3) | 6 examples (27 fixed cases) | 6 `MATCH` | 0 | 0 | 0 | `benchmarks/example_rust_compare/more_games_a/RESULTS.md` |

**These campaigns are NOT additive.** Several overlap in subject matter (the per-method
matrix and `rust_examples`/`stdc_compare` both cover the checked C libc layer; the four
expanded/game campaigns partly overlap with the 49-example aggregate's games). Summing them
would double-count. There is **no grand total** and no combined "pass" figure.

**Headline outcome, stated plainly:** across all seven campaigns there were **0 real
behavioral mismatches reported by the token-stream comparisons**, but there were **5 genuine
`DIFFERENT` observable divergences** (§4), **43 individually accounted `BLOCKED` items**
(13 + 29 + 1; the per-method 13 already includes the 6 unrepresentable `alloc` cases — see the
reconciliation note below), **2 `BAD_REPORT` groups**, and **two runners exited nonzero by
design**. These are finite, deterministic, one-host results. They are **not** a
Rust-equivalence claim and **not** a 100%-safety claim (§3, §7).

### Reconciliation notes (reported, not corrected)

1. **Per-method 283 / 277 / 13.** The 13 blocked items are heterogeneous: **7 are whole
   methods with `cases run = 0`** (excluded from the 283 cases), and **6 are individual `alloc`
   cases** that *are* included in `cases run` but cannot be represented in safe Rust. Hence
   `277 matched + 6 counted-but-unrepresentable alloc cases = 283 cases`, while the separate
   blocked total is `7 methods + 6 cases = 13`. The matrix document explicitly flags that an
   earlier headline of 262 was a transcription error and that 277 is the correct arithmetic
   sum of the per-group matched counts `(20, 42, 42, 21, 29, 42, 42, 18, 21)`.
2. **`rust_examples` 376 vs 560.** That aggregate's `cases` column is inconsistent across
   groups: `format` reports `cases=10, matched=58` and `json_log` reports `cases=6,
   matched=18` because those groups count *distinct case names* while `matched` counts
   *emitted token lines*. The runner emits an informational `WARN` for these; it is not a
   mismatch. Both figures are reproduced verbatim; this document neither recomputes nor
   silently adjusts them.
3. **`rust_examples` exit 1.** The runner failed because two groups (`safecrt_memstr`,
   `vendored_v`) violated the reporting contract by omitting the `<blocked>` field
   (`BAD_REPORT`), **not** because of a behavioral mismatch. Their `mismatched=0` fields are
   still recorded as zero.
4. **`example_rust_compare` exit 1.** Expected and intentional: the contract is "nonzero only
   on a real `DIFFERENT` or a build failure"; there are 5 `DIFFERENT` rows and 1 build-failure
   `BLOCKED`, so a zero exit would be a false pass.

---

## 2. Snapshot provenance

| Item | Value |
| --- | --- |
| Host | macOS `arm64` (`Darwin 27.2.0`) |
| Rust | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| C/C++ backend | Apple clang 16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0` |
| C+ driver | `./build/c+` → `C+ 0.1.0` (host Clang/LLVM backend) |
| Run wrapper | GNU-style `/usr/bin/timeout` (typically 30 s per group, 300 s hard bound in aggregate runners) |
| Checked archives | `build/safe/libcplus_safecrt.a`, `build/libc/libcplus_hardened.a` (symbol surfaces verified with `nm` at runtime by the harnesses) |

Rust models in all campaigns are standalone `.rs` files with **no `unsafe`, no FFI, no raw
pointers, and no external crates**. Cross-run archive SHA-256 drift is expected from
concurrent sibling rebuilds; each artifact's claim applies to its own run (see
`docs/abi/STDC_ABI_SAFETY_FINAL.md` §10).

---

## 3. What a `MATCH` means — explicit interpretation

A `MATCH` (called a "matched token" in the per-method and `rust_examples` campaigns) means
**only** one of the following, depending on the campaign:

- **Token campaigns** (`per_method`, `rust_examples`, `stdc_compare`): two independently
  written programs **printed the same normalized word** for the same `(method, case)` label —
  e.g. `OK`, `REFUSED`, `NOTFOUND`, `CANARY_OK`, `TRUNC`. Token normalization deliberately
  discards return-code families (safeclib `errno_t` codes, `errno` values, `NULL`, comparison
  results) and exact diagnostic text.
- **Executable campaigns** (`example_rust_compare` and the three expanded/game campaigns):
  the C+ executable and the safe-Rust model produced **identical observable output on the
  fixed deterministic inputs exercised** — stdout bytes, and in most cases stderr and/or
  process exit status. Several campaigns compare stdout + exit only and explicitly discard
  stderr (`more_games_a`); games are matched only on their deterministic cores (seed 12345),
  with interactive UI excluded; `macpulse` is matched only on a patched non-interactive
  tty-guard path.

`MATCH` **does NOT mean**:

- equivalent safety guarantees, equivalent semantics, or correctness;
- equality of mechanisms, diagnostics, failure modes, or memory layout;
- proof of functional, performance, ABI, or language equivalence;
- anything about untested inputs, longer runs, other hardware, or other concurrency schedules.

The source artifacts are unanimous on this. Representative language:

- "matching outcome TOKENS do NOT prove equivalent memory-safety guarantees" —
  `per_method/RESULTS.md`, warning banner.
- "Matching outcome tokens are NOT equivalent safety guarantees" — `rust_examples/RESULTS.md` §"Non-equivalence of outcome tokens".
- "MATCHING TOKENS ARE NOT EQUIVALENT GUARANTEES" — `stdc_compare/NOTES.md`.
- "MATCH means observable behavior parity for the tested inputs only — it is not a
  Rust-equivalence claim and not a 100%-safety claim" — `example_rust_compare/RESULTS.md`.
- "A `MATCH` here means only ... same stdout bytes and the same process exit codes" —
  `expanded_cli_games/RESULTS.md`, `expanded_db_engine/RESULTS.md`, `more_games_a/RESULTS.md`.

### Mechanism contrast (the central distinction)

- **C+ checked APIs are runtime-enforced.** They enforce via `errno_t`/`errno` return codes —
  e.g. `400` `ESNULLP`, `403` `ESLEMAX`, `404` `ESOVRLP`, `406` `ESNOSPC`, `22`
  `CPLUS_SAFE_EINVAL` — plus `NULL` returns, bounded live-allocation / recent-free /
  recent-close caches, canary bytes, fail-closed strict-checker rejection, and (in `format`) a
  fail-fast trap that was explicitly disabled so refusals stay observable. Dangerous code
  still **compiles and runs**; it is refused only when executed. Many checked entry points are
  called with `BOS_UNKNOWN`, which **disables the object-size check**; the metadata caches are
  finite (diagnosis/mitigation, not a proof of lifetime safety).
- **Safe Rust uses compile-time ownership/borrow rules plus checked returns.** Null
  references and overlapping `&mut` borrows are unrepresentable; a second owner/double drop
  cannot be expressed. The remaining cases use `Option`/`None`, `Result`/`Err`,
  `checked_mul`, `try_reserve_exact`, and panic-free bounds-checked `get`/`get_mut`. Several
  `REFUSED` rows are deliberate **models** of situations safe Rust cannot construct — not
  reproduced C handler/constraint machinery.

A token match shows that the same input class is *recognised* on both sides, not that the two
implementations provide the same guarantees. For example, the C side can be fooled by a
caller that lies about capacity, whereas that program is inexpressible in safe Rust; C
refusal tokens are modelled in Rust with guards/`Option`, and a C refusal token does not imply
the Rust code needed a run-time guard at all.

---

## 4. `DIFFERENT` cases — honest inventory

There are **5 `DIFFERENT` rows**, all in `benchmarks/example_rust_compare/RESULTS.md`
(out of 49 examples). No `DIFFERENT` row was reported in any other campaign.

| Example | Group | Observed divergence | Exact reason recorded |
|:--|:--|:--|:--|
| `strcpy_s` | `crt_string` | stdout differs; stderr identical; exit 0 vs 0 | **C-only safeclib violation-handler stdout line.** The C binary prints the process-global `ignore_handler_s` line to stdout; safe Rust models the same refusal with `Result::Err` / `Option::None` and prints nothing. |
| `strcat_s` | `crt_string` | stdout differs; stderr identical; exit 0 vs 0 | Same: C-only safeclib violation-handler line on stdout. |
| `strcmp_s` | `crt_string` | stdout differs; stderr identical; exit 0 vs 0 | Same: C-only safeclib violation-handler line on stdout. |
| `strnlen_s` | `crt_string` | stdout differs; stderr identical; exit 0 vs 0 | Same: C-only safeclib violation-handler line on stdout. |
| `B_bounds` | `demos_a_d` | stdout identical; **stderr differs; exit 134 vs 101** | Both stop before the unreachable line and print the same stdout, but the **C+ checked-index helper aborts** (stderr diagnostic, exit 134) while **Rust panics** (own diagnostic, exit 101). This is the "Rust panic message" divergence: the messages and the termination path differ even though the reachable output matches. |

Two honest qualifications recorded by the source artifact:

- The four `crt_string` `DIFFERENT` rows differ **only** by that single C-only
  `ignore_handler_s` stdout line. A supplementary **normalized** comparison that removes that
  one line reports `MATCH=6 DIFFERENT=0` for the group. The raw verdict remains `DIFFERENT`.
- `K_state_class` is direct counter-evidence to any equivalence claim: C+ strict mode rejects
  a construct that Rust accepts and runs (§5). These 5 `DIFFERENT` rows are genuine observable
  divergences, not harness errors.

The `rust_examples` aggregate reports **0 real mismatches**; its two `BAD_REPORT` groups are
reporting-contract violations, not behavioral `DIFFERENT` results. The per-method matrix,
`stdc_compare`, and all three expanded/game campaigns report **0 different/mismatched**.

---

## 5. `BLOCKED` cases — honest inventory with reasons

`BLOCKED` is neither a match nor a failure; it is reported separately and never counted as a
pass. No tokens were fabricated for any blocked item.

### 5.1 Per-method matrix — 13 blocked (source: `per_method/RESULTS.md` §"Blocked methods")

| Group | Blocked method / case(s) | Reason recorded | Mechanism class |
|:--|:--|:--|:--|
| `strquery` | `strlen_s` | No `cplus_sc_*` / `cplus_v_*` checked symbol in any archive; `strnlen_s` is the bounded API | Missing linkable checked symbol (whole method, 0 cases) |
| `search` | `qsort_s` | safeclib header macro expands to `_qsort_s_chk`, which has no definition in any archive | Missing linkable checked symbol (whole method, 0 cases) |
| `field` | `strisalphanumeric_s`, `strisascii_s`, `strisdigit_s`, `strishex_s` | Four `nm` greps empty (exit 1); no classification TU in `libc/src/safecrt_vendor/` | Missing linkable checked symbols (4 whole methods, 0 cases) |
| `io` | `tmpfile_s` | Only unchecked `cplus_tmpfile` exists in the hardened archive; no checked entry | Missing linkable checked symbol (whole method, 0 cases) |
| `alloc` | `realloc_s invalid`; `free_checked_s invalid`; `aligned_alloc_s valid / zero / bad_align / canary` | Scenario unrepresentable with safe APIs: **invalid/foreign pointers** cannot be constructed in safe Rust; **caller-chosen runtime alignment** cannot be expressed with `Vec`/`Box` | 6 individual cases (included in `cases run`), not a Rust check failure |

Each of these is re-derived at runtime by `nm` on the archives, so a later rebuild that adds
a symbol activates the guard-compiled test paths automatically.

### 5.2 Rust safe-library examples — 29 blocked (source: `rust_examples/RESULTS.md`)

| Group | Blocked count | Reason recorded |
|:--|--:|:--|
| `alloc` | 24 | Allocation cases without a safe-Rust counterpart (e.g. injected/foreign-pointer and capacity/twiddled scenarios); reported per group's own `out/` |
| `runtime_diag` | 2 | Diagnostics/global-state case not reproducible in safe Rust |
| `search` | 1 | Case not reproducible with safe reference idioms |
| `threading` | 2 | Threading/primitive case not reproducible without FFI/`unsafe` |
| **Total** | **29** | Reported, **never counted as passes** (`PASS_WITH_BLOCKS` is a partial result) |

Honest scope: the aggregate records the **per-group blocked counts** and the general mechanism
difference — Rust makes null pointers and out-of-range lengths unrepresentable, so C refusal
tokens are modelled with guards/`Option`, and the C archives' diagnostics, handlers and global
state are not reproduced by the Rust examples. The aggregate does **not itemize all 29 by
case name**; item-level detail lives in each group's own `out/` / `results/` directory. The
per-item enumeration of these 29 is therefore **PENDING further itemization** (see §8); no
per-item reason is invented here.

### 5.3 49-example aggregate — 1 blocked (source: `example_rust_compare/RESULTS.md`)

| Example | Reason recorded |
|:--|:--|
| `K_state_class` | **C+ strict build failed (rc=1):** `apps/legacy_demos/K_state_class.c:17:30: error: [CPLUS-ARITH] division by zero cannot be excluded`. The strict checker rejects the divide-by-zero guard at compile time, so **no strict C+ binary exists to compare**; the Rust source builds and runs. |

### 5.4 Withheld / externally blocked configurations (not scored as passes)

- **Sanitizer configurations.** In this checkout the C+ safe-mode / sanitized instrumented
  build is externally **BLOCKED** by the `CPLUS_EXIT_ARITHMETIC` toolchain/header mismatch:
  `c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier
  'CPLUS_EXIT_ARITHMETIC'`, while this checkout's `runtime/include/cplus/cplus.h` enum ends at
  `CPLUS_EXIT_IO = 109`. It is owned by `c-plus-core` and out of scope here; all green results
  use non-instrumented paths. A missing or blocked sanitizer configuration is **never counted
  as a pass**. Separately, ASan (and Leak/Thread/Memory/CFI) is unsupported by the selected
  backend and host Apple clang 16 ASan aborts at startup; no ASan pass is claimed. The only
  sanitizer evidence in the ABI artifact is a UBSan checked run with 0 runtime reports, which
  is not a substitute for ASan. (Sources: `per_method/RESULTS.md`,
  `docs/abi/STDC_ABI_SAFETY_FINAL.md` §11.)
- **Interactive / non-deterministic paths.** `macpulse` live interactive sampling is
  timing/hardware-dependent and not byte-compared (only the patched tty-guard path is
  matched); the four games in the 49-example aggregate and all expanded games are matched
  only on deterministic cores; `E_networking` normalizes inherently nondeterministic
  ephemeral ports and line ordering. These are scope exclusions, not passes.

`stdc_compare`, `expanded_cli_games`, `expanded_db_engine`, and `more_games_a` each report
**0 blocked** (and 0 pending) at their final runs.

---

## 6. Relationship to the ABI / static-analysis context

`docs/abi/STDC_ABI_SAFETY_FINAL.md` establishes the surrounding context and is quoted here for
completeness, not re-verified: the standard-C ABI is preserved at every measured layer
(source blobs, fingerprint MATCH with empty diff, 70/70 layouts, 24/24 consumer matrix,
installed SDK), and the opt-in checked layer converts tested hostile inputs into refusals
(`STDC_SAFETY 64 48 64 0`: 16 APIs × 4 cases, 48 refusals, 64/64 canaries intact, 0 failures;
raw-C control corrupted 13 canaries and crashed/aborted 23 times). It records the same Rust
token-match figure as §1 (`RUST_EXAMPLES stdc_compare 12 72 72 0 0`) and the same warning that
the broader Rust aggregate is not green (0 real mismatches but 29 BLOCKED, 2 `BAD_REPORT`,
exit 1). That document's own non-claims — no Rust equivalence, no 100% memory-safety proof, no
platform claim beyond macOS arm64 / Apple clang 16 / C+ 0.1.0, no ASan pass, unresolved
`CPLUS_EXIT_ARITHMETIC` external blocker — apply equally here.

---

## 7. Explicit non-claims

1. **No Rust-equivalence claim.** Identical tokens or bytes on a fixed case set are behavioral
   correspondences only. C+ runtime codes and bounded caches are not the same mechanism as
   Rust compile-time ownership/borrow rules plus checked returns.
2. **No 100%-safety claim and no general memory-safety proof.** `BOS_UNKNOWN` calls disable the
   object-size check; canaries cover only tested regions; several checked functions have no
   object-size validation; the C layer can be fooled by a lying capacity; caches are
   heuristics, not proofs of provenance, ownership, lifetime, or alias safety. Passing canaries
   on a finite hostile corpus is evidence for those cases only.
3. **No "all green" aggregate.** Two runners exit 1 (`rust_examples` reporting-contract
   failures; `example_rust_compare` intentional nonzero on 5 `DIFFERENT` + 1 build failure).
   `BLOCKED` is neither a pass nor a zero.
4. **No extrapolation.** All results are from one host, one toolchain pair
   (`rustc 1.100.0`, Apple clang 16), and finite deterministic case sets. macOS x86_64, Linux,
   Windows, other compilers/optimizers, unaffected inputs, interactive UI, and other
   concurrency schedules are untested.
5. **No live-game claim.** These are harness/build results, not evidence of live-game success.
6. **No sanitizer pass claimed** (see §5.4).

---

## 8. Artifact / provenance index and PENDING status

All eight artifacts assigned to this consolidation were present on disk and read directly:

| # | Artifact | Used for | Status |
|--:|:--|:--|:--|
| 1 | `benchmarks/safety_compare/per_method/RESULTS.md` | 283/277/0/13 matrix, 13-blocked inventory | **PRESENT — read** |
| 2 | `benchmarks/rust_examples/RESULTS.md` | 9 groups, 0 mismatches, 29 blocked, exit 1 | **PRESENT — read** |
| 3 | `benchmarks/rust_examples/stdc_compare/NOTES.md` | 12 APIs / 72 cases / 72 matched / 0 blocked, exit 0 | **PRESENT — read** |
| 4 | `benchmarks/example_rust_compare/RESULTS.md` | 49 examples, 43/5/1, exit 1, DIFFERENT inventory | **PRESENT — read** |
| 5 | `benchmarks/example_rust_compare/expanded_cli_games/RESULTS.md` | 9/9, exit 0 | **PRESENT — read** |
| 6 | `benchmarks/example_rust_compare/expanded_db_engine/RESULTS.md` | 10/10, exit 0 | **PRESENT — read** |
| 7 | `benchmarks/example_rust_compare/more_games_a/RESULTS.md` | 6/6 (27 fixed cases), exit 0 | **PRESENT — read** |
| 8 | `docs/abi/STDC_ABI_SAFETY_FINAL.md` | ABI/safety context, external blocker, non-claims | **PRESENT — read** |

**PENDING items (explicit, not hidden):**

- **Per-item enumeration of the 29 `rust_examples` blocked cases** — the aggregate reports only
  per-group counts (24/2/1/2); the case names are in each group's `out/` / `results/`
  directory, which was outside this consolidation's assigned artifacts. Marked **PENDING
  further itemization**; no per-item reason was fabricated (§5.2).
- **`benchmarks/rust_examples/stdc_compare/RESULTS.md`** — not present on disk at the ABI
  artifact's final capture (footnote ¹ of `STDC_ABI_SAFETY_FINAL.md`). The authoritative
  recorded run for this campaign is in `stdc_compare/NOTES.md` §"Actual verification run",
  which is the artifact assigned to this consolidation and is present. No `RESULTS.md`
  content is invented.

No other assigned artifact is missing. This document adds no new experimental result.

---

**Bottom line:** the seven campaigns consistently report **0 real mismatches** on their
finite deterministic case sets, with **5 honest `DIFFERENT` divergences** (four safeclib
handler-line stdout cases and one Rust-panic-vs-C+-abort case), **43 accounted blocked items**
across the campaigns (13 + 29 + 1; the per-method 13 includes the 6 unrepresentable `alloc`
cases), plus withheld sanitizer/interactive configurations, and **two nonzero runner exits by
design**. Every source insists on the same reading: `MATCH` is
observable outcome/stdout/exit parity **only**. C+ checked APIs enforce at run time with
`errno` codes and bounded metadata; safe Rust enforces at compile time with ownership/borrow
rules and checked returns. **No Rust-equivalence and no 100%-safety claim is made.**
