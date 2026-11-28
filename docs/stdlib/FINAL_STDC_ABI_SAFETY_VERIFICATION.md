# Final verification — standard-C ABI + safety proof

**Branch:** `agent/modern-stdlib`
**Scope:** final, read-only re-run of the seven standard-C ABI/safety suites that back the
"ordinary C ABI unchanged + additive caller-side safety" claim.
**Environment:** macOS arm64, Apple clang 16.0.0 (`clang-1600.0.26.6`), `/usr/bin/clang`.
Every command was run from the repository root with `CPLUS_CLANG=/usr/bin/clang` exported and
`SDKROOT` unset, under a 300-second timeout. Shell timeout was set to `300000 ms` for the batch.

**Strict file ownership:** this document is the only file created or modified for this task.
No source, header, build file, or git index entry was touched; no `git add` / `git commit` was
run. Suite-generated artifacts under `tests/**/out`, `tests/**/artifacts` and `build/` were
written by the suites themselves during a normal verification run, not hand-edited.

---

## 1. Suite results (exact final lines and exit codes)

| # | Suite / command | Final line printed | Exit | Interpretation |
|---|-----------------|--------------------|------|----------------|
| 1 | `sh tests/abi/stdc_abi/run.sh` | `stdc_abi: RESULT PASS -- ordinary C ABI unchanged by the safety layer` | **0** | PASS. Ordinary C symbol set preserved in plain, safe and disabled builds; no C++ mangling; added symbols are internal helpers only; runtime struct layout/offsets identical; machine code for `_abi_add`, `_abi_sum`, `_abi_fill` byte-identical. (`stdc_abi: 0 failed`.) |
| 2 | `sh tests/abi/stdc_mapping/verify.sh` | `SUMMARY: mapped-name pass=16 fail=0; total failures(incl. nm/iso)=0` | **0** | PASS. All 16 ordinary-spelling→checked-symbol mappings resolve in `libcplus_hardened.a`; per-translation-unit isolation confirmed (untouched TU → ordinary `_malloc`, header TU → `_malloc_s`, `CPLUS_SAFE_C_DISABLE` → ordinary, precompiled archive TU → ordinary, non-mapped `fopen`/`fclose`/`strlen`/`strnlen` unchanged). Both TUs linked together: `tu_isolation:ok`. |
| 3 | `sh tests/abi/stdc_precompiled/run.sh` | `stdc_precompiled: ALL CHECKS PASSED` | **0** | PASS. Precompiled `stdc_legacy.o` / `libstdc_legacy.a` hashes bit-identical before and after all links; plain vs safe stdout byte-identical (same SHA-256); negative case refuses with ESNOSPC 406 (exit 70) while the *unchanged* precompiled C library is still called. Confirms caller-side remap, no library rebuild. |
| 4 | `sh tests/abi/stdc_safety/run.sh` | `stdc_safety: 68 passed, 0 failed` | **0** | PASS. 16 valid cases succeed with canary intact; 48 hostile cases refused with canaries intact (64/64 canaries intact, 0 failures). Plain (unchecked) baseline: 13 canary corruptions, 23 crashes/aborts, 19 divergent hostile cases. |
| 5 | `sh tests/abi/stdc_consumers/run.sh` | `Generated report: tests/abi/stdc_consumers/RESULTS.md` (last result line: `RESULT: stdc_consumers: 24 passed, 0 failed (main); probes 1 OK, 1 unexpected`) | **0** | Main matrix PASS (24 compiled, 0 failed) for all 9 standard headers under C99 **and C++17** plus both additive overlays. **Caveat:** the out-of-matrix `cplus_libc_only` isolation probe compiled at rc=0 but was *expected to fail* → reported `UNEXPECTED`. See §3. |
| 6 | `sh tests/abi/stdc_sdk/run.sh` | `=== done; raw artifacts in tests/abi/stdc_sdk/out ===` | **0** | PASS, but note the script emits no single PASS/FAIL summary token — it ends with the artifacts notice. Internal checks: both required scripts exit 0 with `sdk consumer: ok` / `install smoke: ok`; installed static and shared consumers both `exit=0`, stdout `BYTE-IDENTICAL`; checked consumer stdout `BYTE-IDENTICAL` to plain; 40-byte-into-8-byte `memcpy` refused (exit 70, exact `ESNOSPC 406` diagnostic) while the plain build traps (exit 133); 0 C++-mangled-looking symbols in the static archive. |
| 7 | `sh benchmarks/rust_examples/stdc_compare/compare.sh` | `RUST_EXAMPLES stdc_compare 12 72 72 0 0` | **0** | PASS. 12 APIs (each `nm`-verified against a real checked symbol), 72 comparison cases, 72 token matches, 0 mismatches, 0 blocked. This is **stdout/exit token parity only** — see §2 statement 6. |

**Result: 7 of 7 suites exited 0. No suite exited nonzero.** Two honesty caveats (suites 5 and 6)
are recorded in §3; neither is a fabricated pass.

---

## 2. Explicit statements

1. **Ordinary C ABI is unchanged — fingerprint MATCH, byte-identical symbols/opcodes.**
   The live `stdc_abi/run.sh` run confirms: the six ordinary C symbols are present in plain,
   safe, and disabled builds (`T1`); no C++ mangling in any build (`T2`); no ordinary symbol
   removed (`T3`) and none added (`T4b`); struct size/offsets and pointer width identical
   (`T6`–`T8`); and `_abi_add`, `_abi_sum`, `_abi_fill` machine code byte-identical (`T9`).
   The full ABI fingerprint comparison is recorded in `tests/abi/stdc_abi/RESULTS.md`
   (Run 1b): `abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).`
   with an empty diff — i.e. 1235 fingerprint records match the checked-in baseline.
   *Evidence:* suite 1 output; `tests/abi/stdc_abi/RESULTS.md`.

2. **Source is unchanged since the integration baseline.**
   Recorded in `tests/abi/stdc_source/RESULTS.md` (this suite was **not** re-run here; it is
   cited as prior evidence, not a fresh pass). Against baseline `cbcea57` — the direct parent
   of the safety-integration series — all 7 core standard-C implementation files are
   byte-identical (same git blob id, empty diff); `libc/src/` and
   `libc/install-include/` show only additions (18 `A`/0 `M` and 19 `A`/0 `M`), i.e. the
   safety layer is additive. That document discloses concurrent `HEAD` movement
   (`ce336ab`→`f58c33a`) and re-verifies at both tips: the intervening commit touched only
   `tests/`, nothing under `libc/`, `runtime/`, or `stdlib/`.
   *Evidence:* `tests/abi/stdc_source/RESULTS.md`.

3. **The safety is a caller-translation-unit macro remap to checked APIs — no interposition.**
   `safe_c.h` force-included on the caller rewrites ordinary spellings at compile time; the
   library itself is untouched. Suite 2 shows per-TU isolation (a TU without the header emits
   ordinary `_malloc`; with the header emits `_malloc_s`; disabling macro or using the
   precompiled archive restores ordinary symbols). Suite 3 re-hashes the precompiled archive
   before and after every link — bit-identical — and states the conclusion verbatim:
   `caller-side -include safe_c.h is compile-time macro mapping; NO rebuild of the precompiled
   C library is needed; ABI unchanged.` Suite 1 confirms the ordinary symbols are not removed
   or replaced. Nothing hooks, overrides, or interposes on the platform libc symbols.
   *Evidence:* suites 1, 2, 3.

4. **Hostile inputs are refused with canaries intact.**
   Suite 4: 48 hostile cases (NULL, undersized, oversized, `SIZE_MAX`, format/printf, FILE and
   allocator abuse) all refused; 64/64 canaries intact; 0 failures. Suite 6 independently
   refuses a 40-byte `memcpy` into an 8-byte object with exit 70 and the exact
   `ESNOSPC 406` diagnostic while the plain build traps (exit 133). Suite 3 shows refusal
   happens before the "UNEXPECTED" line and the stale library is still called.
   *Evidence:* suites 3, 4, 6.

5. **The C++17 header fix is ABI-neutral.**
   Commit `46e81e4` ("Fix C++17 compatibility in cplus_libc.h") replaced C-only `restrict` /
   `double complex` spellings with language-conditional, ABI-identical
   `CPLUS_RESTRICT` / `CPLUS_*_COMPLEX` macros. No layout, signature, or symbol name changes.
   Suite 5 compiles every header — including `cplus_libc.h` via its overlays — cleanly under
   `-std=c++17 -Wall -Wextra -pedantic` with 0 main-matrix failures, and the commit records
   `ABI fingerprint MATCH, libc 109/109`. The fix is a header-token change only, so it cannot
   alter the C ABI established in statement 1.
   *Evidence:* `git show 46e81e4 -- libc/install-include/cplus_libc.h`; suite 5.

6. **Matching Rust tokens are NOT equivalent guarantees.**
   Suite 7's `72/72` means the C checked layer and the safe-Rust examples printed the same
   `<api> <case> <token>` outputs and same exit codes on the probed cases. It does **not** prove
   equivalent safety: Rust's protections come from compile-time ownership/borrowing and bounds
   checks, whereas the C layer relies on runtime capacity checks + refusal in checked APIs,
   driven here by a caller macro. Token parity is output equivalence on a fixed case set, not
   a proof of identical memory-safety, type, or failure semantics. Non-matching behavior in
   other examples is separately documented as `MATCH`/`DIFFERENT`/`BLOCKED` in the
   example-vs-Rust reports with explicit non-equivalence caveats.
   *Evidence:* suite 7 (`compare.sh` header comment and output); commit `46e81e4` message.

---

## 3. Honest caveats (no fabricated passes)

- **Suite 5 — `cplus_libc_only` probe `UNEXPECTED`.** The probe is deliberately outside the
  main matrix (`tests/abi/stdc_consumers/RESULTS.md` §"Supplementary C++17 isolation probes").
  It was expected to fail (rc≠0) as root-cause isolation of a historical C++17 blocker, but it
  compiled at **rc=0**, so the harness reported `PROBE cplus_libc_only: UNEXPECTED` and
  `probes 1 OK, 1 unexpected`. The main 24-case matrix still reports `24 passed, 0 failed`.
  The most likely explanation is that the C++17 fix in `46e81e4` made the "expected-fail"
  probe stale; that is consistent with statement 5 but is an inference, not a re-run proof.
  This is reported as an unexpected probe, **not** counted as a pass or a suite failure.
- **Suite 6 — no summary token.** `stdc_sdk/run.sh` ends with an artifacts notice rather than
  an explicit PASS line. Its exit is 0 and every internal check is green (quoted in the table),
  but readers should not expect a `PASS`/`FAIL` final line from this script. One line in its
  output is a shell Trace/BPT trap notice (exit 133) from the **intentionally hostile plain
  consumer** — this is the expected UB baseline, not a suite failure.
- **Cited, not re-run here.** The fingerprint-MATCH comparison (statement 1, second half) and
  the source-integrity verdict (statement 2) live in recorded `RESULTS.md` files produced by
  other suites (`tools/abi_compare` and `tests/abi/stdc_source`). They were not re-executed in
  this seven-suite batch; they are presented as prior evidence with their source files named,
  not as fresh output of the commands listed in §1.

---

## 4. Bottom line

All seven mandated suites exited 0. They support the claim that the ordinary C ABI is
unchanged (fingerprint MATCH, byte-identical symbols and opcodes), the standard-C sources are
unchanged since the additive safety baseline, and the safety is a caller-TU compile-time macro
remap to checked APIs with hostile inputs refused and canaries intact. The C++17 header fix is
ABI-neutral. The Rust comparison shows output-token parity only and is explicitly **not** an
equivalence-of-guarantees result. The two caveats in §3 are disclosed and are not presented as
passes.
