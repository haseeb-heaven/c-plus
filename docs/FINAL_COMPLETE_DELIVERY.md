# Final consolidated delivery report — branch `agent/modern-stdlib`

**Capture:** 2026-11-23 · **Role:** synthesis/aggregation only. Every number below is
quoted from a named, on-disk authoritative artifact (§0). Nothing was re-executed
for this report; nothing was rounded, promoted, or invented. A missing artifact is
reported **PENDING**, never treated as a pass. This document is the only file
created/modified by this agent — no source, header, build file, test, or git index
entry was changed; no `git add` / `git commit` was run.

**Evidence labels:** **[LANDED]** = committed on `agent/modern-stdlib` ·
**[HANDOFF]** = supplied by the coordinating agent (cross-checked against landed
commit messages / captured runner output where available) · **[PENDING]** = not
present as a runnable artifact · **[DISCREPANCY]** = two authoritative artifacts
record different values at different capture times; both are disclosed.

**Host/toolchain provenance (common):** macOS `arm64` (Darwin 27.2.0 / 27.2.1),
Apple clang 16.0.0 (`clang-1600.0.26.6`, `CPLUS_CLANG=/usr/bin/clang`), `C+ 0.1.0`,
`rustc 1.100.0 (0ef1bc386 2026-11-09)`. Source-integrity baseline `cbcea57`. The
default Homebrew clang 23.1.6 has a broken `MacOSX27.sdk`; all green runs pin
`/usr/bin/clang`.

---

## 0. Authoritative artifacts read

| Artifact | Supplies |
| --- | --- |
| `docs/FINAL_EXPANDED_DELIVERY.md` | predecessor consolidated report: ABI, API closure, testing, verdicts |
| `docs/abi/STDC_ABI_SAFETY_FINAL.md` | 8-group ABI/source/mapping/archive/safety/consumer/SDK evidence |
| `docs/abi/FINAL_ABI_AUDIT.md` | serial final ABI re-run (1235 MATCH, 69/0, 70/70, 54/54) |
| `docs/abi/ABI_SAFER_LIBS_FINAL.md` | safer-lib additive symbol inventory (38 `cplus_sc_*`, 26 `cplus_v_*`) |
| `docs/abi/ABI_ORIGINAL_C_FINAL.md` | original-C ABI; discloses the 19-added/1-modified header observation |
| `docs/libc/MISSING_API_CLOSURE.md` | 27-API closure ledger (26 RESOLVED / 1 BLOCKED-BY-HOST) |
| `docs/security/RUNTIME_SAFETY_THREE_WAY.md` | Rust / Fil-C / Trap-C comparison |
| `docs/security/RUNTIME_HOSTILE_EVIDENCE.md` | consolidated hostile-input ledger + non-claims |
| `tests/stdlib/method_tests/RESULTS.md` | 518/518 per-method tests |
| `tests/adversarial_1000/RESULTS.md` | 8000 scenarios / 0 failures |
| `tests/fuzz10k/RESULTS.md` + `FUZZ_ANALYSIS.md` | 97,250 scenarios / 0 crashes/hangs/corruptions |
| `benchmarks/example_rust_compare/RESULTS.md` | 49-example aggregate (43/5/1) |
| `benchmarks/example_rust_compare/MORE_EXAMPLES.md` | 25 new comparisons / 105 cases |
| `benchmarks/example_rust_compare/MODERN_SECURE_CATALOGUE.md` | 8 modern GUI + secure apps |
| `benchmarks/example_rust_compare/utilities/RESULTS.md` | 30 utilities vs safe Rust |
| `apps/legacy_examples/utilities/RESULTS.md` | 6/6 utility runners PASS |
| `external_projects/{cli_app,game_a,game_b}/ORIGINAL_EVIDENCE.md` + `game_a/cplus/CONVERSION.md` | 3 real upstream projects + 1 C+ conversion |
| `docs/security/RED_BLUE_BATTLE_FINAL.md` | **[PENDING]** — not on disk |
| `tests/c_standards/RESULTS.md`, `STD_SUPPORT_MATRIX.md` | **[PENDING]** — directories exist (`c89/c99/c11/c17/c23`), result docs absent |

**Compile-time vs runtime separation (applies throughout).** Compile-only results
(fingerprint, `nm`, `_Static_assert`, source blob IDs, consumer compiles, signature
probes) are **not** counted as behavioral passes. Runtime token matches are
**not** proofs of a safety guarantee. `skipped` is never added to `passed`;
`BLOCKED` / `PENDING` are neither matches nor failures. Campaigns use different
units (scenarios, cases, checks, transcripts) and are **not** summed into a grand
behavioral total.

---

## 1. Standard C ABI + source preservation

### 1a. Compile-time / static evidence

| Check | Exact result |
| --- | --- |
| ABI fingerprint | **MATCH**, empty diff, **1235 records** (V 4 / L 89 / C 27 / F 12 / S 1103) |
| ABI compatibility | `abi_compat: 69 passed, 0 failed` (incl. `T5_no_cxx_mangling` on all 3 archives) |
| ABI layouts | `abi_layouts: 70/70 layouts match` |
| `stdc_abi` | `22 PASS, 0 FAIL`; 6 ordinary C symbols in plain/safe/disabled; `plain minus safe = 0` |
| Source integrity | **7/7 core files UNCHANGED** (equal git blob IDs vs `cbcea57`); `libc/src/` **18 ADDED / 0 MODIFIED / 0 DELETED / 0 RENAMED** (2375 insertions); `libc/install-include/cplus*.h` **19 ADDED** |
| Checked-layer mapping | 16/16 ordinary spellings; all checked targets `nm`-verified as defined `T`/`D`/`B`; no interposition (`nm -u` TU-isolation) |
| C/C++ consumer matrix | **24 PASS, 0 FAIL** (9 headers × C99/C++17 + 2 overlays + combined TU) |
| Installed SDK symbol audit | **27/27** implemented/vendored mappings as defined `T` symbols in static + dylib; **0** C++-mangled symbols |
| Static API coverage | `headers=24 covered=411 total=414; worst 87.0%` — **411/414, not 100%** |
| Host capability probes | `FE_TIESAWAY` absent (exact `-fsyntax-only` probe exit 1); `_Float16` present (`__FLT16_MANT_DIG__ == 11`) |

Static assertions in all three builds (`plain`/`safe`/`disabled`) assert
`sizeof(void*)==8`, `sizeof(size_t)==8`, `sizeof(abi_sample)==40`, `_Alignof==8`,
exact field offsets and exact types of the five external functions. Mechanism is
**caller-side preprocessor remapping in the including translation unit only — not
symbol interposition**.

> **[DISCREPANCY — source integrity, headers]** `FINAL_EXPANDED_DELIVERY` and
> `STDC_ABI_SAFETY_FINAL` record **19 ADDED / 0 MODIFIED** headers at their capture.
> `ABI_ORIGINAL_C_FINAL` (an earlier capture) records **19 ADDED / 1 MODIFIED**
> (`cplus_libc.h`, commit `46e81e4`, the source-spelling-only C++17 fix) and its
> `stdc_source/run.sh` therefore exited 1 on the strict "additions-only" check.
> The invariant common to both is: **7/7 core C files byte-identical since
> `cbcea57`, 18 added / 0 modified sources, and the C ABI unchanged**. The edit is
> compile-time macro spellings (`CPLUS_RESTRICT` / `CPLUS_*_COMPLEX`) only. Honest
> source-integrity scope: *unchanged since baseline `cbcea57`*, **not** "never
> modified in project history" (pre-baseline hardening commits are disclosed).

### 1b. Runtime / dynamic evidence

- **Opcode identity:** `objdump` bytes for `_abi_add`, `_abi_sum`, `_abi_fill`
  byte-identical in plain/safe/disabled (direct AAPCS64 evidence on arm64).
- **Precompiled archive:** saved `libstdc_legacy.a` SHA-256 `e6f00c99…467403`
  identical at baseline/PRE-LINK/POST-LINK; plain and safe callers emit
  byte-identical stdout (SHA-256 `2919b20d…98ae7d64`); overflow caller refused
  with exit 70 and `ESNOSPC 406`.
- **Legacy labs:** `total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0`
  (bash 5.3; system bash 3.2 lacks associative arrays and is disclosed as a failed attempt).
- **Installed SDK runtime:** plain static/plain shared/checked-valid/checked-hostile
  all emit the same 42-byte stdout (SHA-256 `d5d86a4a…0f94b4`); hostile `memcpy`
  into `char[8]` refused exit 70; plain build on same input dies SIGTRAP (133).
- **External real-world projects (source + ABI preserved):** `xdump` (MIT, pinned
  `97fdfbd`, plain C99 compile exit 0, 8 run cases), Corg-Labs `snake` (MIT, pinned
  `b6bc040`, byte-identical source, `nm` shows only `T _main`), `mevdschee/2048.c`
  (MIT, pinned `90343eb`, byte-identical source, self-test 13/13). One conversion
  (`game_a/cplus/snake_modern.cp`) passed `CONVERT game_a 12 12 0` with byte-identical
  stdout/exit to the original. **Original sources under `external_projects/*/original/`
  were never edited; ABI is whatever each original exports.**

---

## 2. Safe libraries integrated (all **[LANDED]**)

### 2a. Vendored safeclib — 22 functions

- Commit `de79021` vendored 10 safeclib group sources + headers; commit `ce018ca`
  integrated upstream safecrt safe-C-runtime wrappers.
- Symbol inventory (`ABI_SAFER_LIBS_FINAL`): `cplus_v_*` **26 defined exports =
  22 vendored `_chk` helpers + 4 public** (`cplus_v_memccpy_s`, `cplus_v_memchr_s`,
  `cplus_v_strlcat`, `cplus_v_strlcpy`); `cplus_sc_*` **38 = 36 core + 2 upstream**
  additions; plus the checked ordinary `_s` spellings. The **22 vendored safe
  functions** are the `cplus_v__* _chk` group; the wider `cplus_upstream_*` family
  and the 22-function `stdlib/safe/safe_alloc.h` extension are also additive.
- Deterministic fuzz harnesses (`a11de7f`); `stdlib/safe/safe_c.h` remaps 16
  ordinary spellings to checked entry points compiled in the including TU.

### 2b. C23 / ISO missing-API closure — 26 of 27 resolved

From `docs/libc/MISSING_API_CLOSURE.md` (denominator `iso_c_inventory.json`, 948 APIs):

| Metric | Count |
| --- | ---: |
| APIs classified `MISSING` | **27** |
| **RESOLVED** (source changed + test executed) | **26** |
| **BLOCKED-BY-HOST** (`FE_TIESAWAY`) | **1** |
| Still MISSING / PENDING | **0** |

- **23 C23 math/temporal/Unicode APIs:** `c23_nextup: 84 0`, `c23_arith: 83 0`,
  `c23_hypot16: 357 0`, `c23_time_unicode: 102 0`.
- **3 closure APIs:** `timespec_getres`, `mbrtoc8`, `c8rtomb` (from-standard originals;
  host `uchar.h` absent).
- **`FE_TIESAWAY`** genuinely absent from both clang versions and the SDK; the
  compat header deliberately defines no value, publishing
  `CPLUS_C23_FENV_TIESAWAY_STATUS == "UNSUPPORTED-BY-HOST"` and
  `cplus_fenv_has_tiesaway() == 0` (`c23_fenv: 8 0`). **No fabricated value.**

### 2c. Runtime-hardening additive wrappers — 10 new symbols, 3 groups

| Group | New symbols | Test → exact result |
| --- | --- | ---: |
| Checked sync destroy | `cplus_mtx_destroy_checked`, `cplus_cnd_destroy_checked`, `cplus_thrd_join_checked` | `sync_checked: 156 0` |
| Allocation hardening | `cplus_alloc_atomic_realloc`, `cplus_alloc_checked_free`, `cplus_alloc_state_dump` | `alloc_hardened: 32 0` |
| Violation log | `cplus_violation_log_open`, `cplus_violation_log_event`, `cplus_violation_log_close`, `cplus_violation_log_count` | `violation_log: 41 0` |

All 10 are additive (no signature/layout/export changed), wired into `libc/src/**`;
`git show --name-only 2a2e13f` touches no `runtime/**` or `compiler/**` core file.
ABI fingerprint stayed MATCH (defined count 784 → 812, L/C/F sections unchanged).
`cplus_fenv_has_tiesaway()` is header-only/static inline and not part of the 10.

### 2d. Checked-layer safety under hostile input (runtime)

`STDC_SAFETY 64 48 64 0` — 16 APIs × 4 cases, **48 refusals**, **64/64 canaries
intact**, 0 failures (`stdc_safety/run.sh`: 68 passed / 0 failed). Raw-C control
corrupted 13 canaries and aborted/crashed 23 times (SIGSEGV 11, SIGABRT 6, SIGTRAP 5).
UBSan-instrumented checked run: exit 0, identical token line, **0** `runtime error`
reports. Three honest exceptions disclosed (`fopen` not remapped; `fread`/`fwrite`
"small" uses bounded variants; `printf`/`fprintf` hostile cases use `*_s`).
Production violation trap (exit 70) unchanged.

---

## 3. All examples / modern GUI / secure apps / utilities (catalogue)

Counts are catalogued source programs with verification evidence. Campaigns use
different units; this is an inventory, **not** a behavioral grand total.

| Category | Count | Breakdown / verdict scope |
| --- | ---: | --- |
| Safety demos A–P | **16** | demos_a_d 4, demos_e_h 4, demos_i_l 4, demos_m_p 4 |
| CRT safe-API demos | **26** | crt_memory 9, crt_string 6, crt_io 7, crt_format 4 |
| Games (deterministic core) | **4** | 2048, snake, tetris, tiles (seed 12345; interactive UI excluded) |
| CLI / system / database | **3** | cgame (8/8 paths), student (2/2 CRUD), macpulse (tty-guard path) |
| Expanded example groups | **6 groups / 20 examples** | CLI 4, DB 3, ENGINE 3, GAME_CORES 3, SNAKE 3, SAFETY 4 — all PASS |
| More examples (new comparisons) | **25** | more_games_a 6, more_apps_a 11, more_data_math 8 — **25 MATCH / 0 DIFFERENT**, 105 fixed cases |
| Modern GUI apps | **4** | dashboard, settings, breakout_ui, invaders_ui |
| Secure apps | **4** | vault, firewall, ratelimit, auditlog |
| Real-world utilities | **30** | 6 categories × 5 (file/text/data/security/dev/net) |
| External real upstream projects | **3** (+1 C+ conversion) | xdump, snake, 2048; snake converted with 12/12 checks matched |

**Comparison-verdict roll-up (per campaign):**
- Original 49-example aggregate: **43 MATCH / 5 DIFFERENT / 1 BLOCKED**, exit 1 (intentional).
  The 5 DIFFERENT are 4 C-only safeclib `ignore_handler_s` lines + `B_bounds`
  (C+ SIGABRT 134 vs Rust panic 101); the 1 BLOCKED is `K_state_class` (strict C+
  `[CPLUS-ARITH]` compile rejection). `MATCH` = observable behavior parity on tested
  inputs only.
- Expanded example groups: **6/6 PASS, aggregate exit 0**.
- Expanded C+ vs Rust mirrors: `expanded_cli_games` **9/9 MATCH**;
  `expanded_db_engine` **10/10 MATCH**.
- Modern GUI + secure apps: **8 MATCH / 0 DIFFERENT / 0 BLOCKED / 0 PENDING**, 82 fixed cases.
- Utilities vs safe Rust: **30 MATCH / 0 DIFFERENT / 0 BLOCKED / 0 PENDING**, 83 fixed
  invocations; exit-status semantics verified with negative controls.
- `apps/legacy_examples/utilities` runner aggregate: **6/6 runners PASS**, 65 passed units,
  146 refused hostile inputs, exit 0.
- All safe-Rust models use **no `unsafe`, no FFI, no raw pointers, no external crates**.

---

## 4. Testing totals

### 4a. Compile-time results

- ABI static: 1235-record fingerprint MATCH (empty diff); 69/0 compat; 70/70 layouts;
  22/0 `stdc_abi`; `_Static_assert`/`__builtin_types_compatible_p` in all three builds;
  24/24 C99+C++17 consumer matrix; `extern "C"` audit; 0 `_Z`/`__Z` symbols.
- Source integrity: 7/7 UNCHANGED (git blob IDs); 18 added/0 modified sources;
  19 added headers (0 or 1 modified depending on capture — see §1a discrepancy).
- Symbol provenance: 30 checked targets `nm`-verified; TU-isolation `nm -u` probes;
  SDK 27/27 mappings present as defined `T` symbols; new C23 symbols defined exactly once.
- Static API coverage: **411/414, worst 87.0% — not 100%**.
- Host capability probes: `FE_TIESAWAY` absent; `_Float16` present.

### 4b. Runtime results

| Suite | Headline | Scope / unit |
| --- | --- | --- |
| Per-method suites | **518 methods invoked / 518 passed / 0 failed / 0 skipped** | 8 groups: alloc_mem 87, str_vec 47, collections 63, file_io 28, thread_async 37, json_log_time 56, multimedia 127, net_http 73 |
| Adversarial (`adversarial_1000`) | **8000 scenarios / 0 failures**, 5209 refusals, exit 0 | 8 groups × 1000 (target 9000 = 9×1000); ninth group **[PENDING]**; canary/state/sentinel integrity all ok |
| Fuzz (`fuzz10k`) | **97,250 scenarios / 0 crashes / 0 hangs / 0 corruptions**, 85,635 refusals, exit 0 | 9 groups (target ≥90,000); 5 groups exceed 10,000; guard/watchdog/fork containment; ~100k scenarios |
| Hostile C safety (`stdc_safety`) | `STDC_SAFETY 64 48 64 0` — 64 canaries intact, 48 refusals, UBSan 0 reports | 16 APIs × 4 cases |
| Per-method C+/Rust tokens | **283 cases / 277 matched / 0 mismatched / 13 blocked**; 9 drivers exit 0 | memory, strcopy, strquery, alloc, search, transform, field, io, format |
| stdc Rust tokens | `72/72 matched, 0 mismatched`, empty diff, exit 0 | 12 APIs × 6 cases; `CANARY_BROKEN` never observed |
| Safe-Rust examples aggregate | 560 matched token lines, **0 real mismatches, 29 BLOCKED, 2 BAD_REPORT**, exit 1 | 9 groups; BLOCKED never counted as passes |
| Original examples vs Rust | **49 = 43 MATCH / 5 DIFFERENT / 1 BLOCKED**, exit 1 | deterministic inputs only |
| Expanded examples | **6/6 groups PASS, exit 0** | CLI 4/4, DB 3/3, ENGINE 3/3, GAME_CORES 3/3, SNAKE 3/3, SAFETY 4/4 |
| Expanded adversarial | **135 cases / 135 refusals / 0 crashes / 0 unexpected OK** **[HANDOFF]** | committed body still shows 110/110; refreshed figure from commit `c6195a0` |
| Expanded C+ vs Rust | **9/9 + 10/10 MATCH** | deterministic stdout + exit |
| More examples vs Rust | **25 MATCH / 0 DIFFERENT**, 105 fixed cases | all 3 scripts exit 0 |
| Modern GUI + secure apps | **8 MATCH**, 82 fixed cases | headless teaching apps |
| Utilities vs Rust | **30 MATCH / 0 DIFFERENT**, 83 invocations | all scripts exit 0 |
| Utilities aggregate | **6/6 runners PASS**, 65 units, 146 refusals, exit 0 | `apps/legacy_examples/utilities` |
| Resource exhaustion | **156 executions (78 checks × 2 modes) / 0 failures**, exit 0 | 8 harnesses; 1 declared SKIP |
| Hostile inputs | **143 checks × 2 modes / 0 failures** (exit 2 = 1 BLOCKED mode) | strict-none + workaround-UBSan |
| Security aggregate | **15 invocations / 15 PASS / 0 other**, exit 0 | stdlib 2, hostile_inputs 5, resource_exhaustion 8 |
| CVE-like safe-variant gate | **10 passed / 0 failed**, exit 0 | 15 baseline cases; some PARTIAL/NOT-FIXABLE-BY-LIBC |
| FS / thread security | `FS_SECURITY 68 0`; thread 37 pass / 0 fail | characterization |
| Screenshot captures | **6 transcripts / 7 runs / 0 failures**, all `VERDICT: PASS` | real binary runs |
| Legacy labs | **54/54 pass**, 0 failures | bash 5.3 |
| libc / CRT unit | **109/109**, **85/85** **[HANDOFF]** | refresh snapshots before quoting older counts |
| Fil-C technique (narrow) | `ALLOC_DIAG 26 0` in strict + UBSan | metadata liveness/bounds query only; ABI-neutral |
| Trap-C | **NOT TESTABLE** | no source/repo/license/toolchain; 0 code integrated |
| C standard matrix | **[PENDING]** | `tests/c_standards/RESULTS.md` + `STD_SUPPORT_MATRIX.md` absent |
| Red/blue battle | **[PENDING]** | `docs/security/RED_BLUE_BATTLE_FINAL.md` absent |

---

## 5. Runtime safety evidence + non-claims

**Evidence observed (runtime, fixed deterministic inputs):**
- Checked hostile inputs refused with exit 70 and intact `0xA5`/`0x5A`/`0xDEADBEEFCAFEBABE`
  canaries; raw C corrupts canaries and crashes on the same cases.
- 97,250 fuzz scenarios and 8000 adversarial scenarios produced 0
  crashes/hangs/corruptions/failures; 518/518 method cases invoked and verified;
  64/64 canaries intact under `stdc_safety`; UBSan checked run had 0 reports.
- Matching outcome tokens (`OK`/`REFUSED`/`CANARY_OK`/`N/A`) across 9 per-method
  groups, 72 stdc tokens, 49 original examples and 44 expanded comparison examples
  are **behavioral correspondences on fixed case sets only**.
- C mechanisms are **runtime constraint checks** (`errno_t`/`errno` codes
  400/403/404/406, bounded live-allocation/recent-free caches, canaries, fail-fast
  trap disabled for observability); Rust mechanisms are **compile-time**
  ownership/borrowing plus `Option`/`Result`/`checked_mul`/`try_reserve_exact`/
  bounds-checked `get`/`get_mut` — **different mechanisms**.
- Negative controls confirm refusals are real: independent integrity gates,
  deliberate breaking probes (containers `BREAKING` family 142 cases; alloc pinned
  `SIZE_MAX`/count-overflow/realloc-failure/double-free; forbidden `%n` catalogue),
  asymmetric plain control (13 corrupted canaries, 23 abnormal exits), watchdog
  positive control, forced-failure classifier checks.

**Explicit non-claims (mandatory):**
1. **No Rust equivalence.** Matching tokens/stdout record printed words/bytes for
   enumerated cases only — not equal guarantees, semantics, or performance. The
   broader Rust aggregate is **not green** (29 BLOCKED, 2 BAD_REPORT, exit 1); 5
   DIFFERENT example verdicts and 1 BLOCKED (`K_state_class`) are direct
   counter-evidence to interchangeability.
2. **No 100% memory-safety proof.** The checked layer is runtime constraint checking
   with caller-derived capacities. `BOS_UNKNOWN` calls disable the object-size check;
   an over-large `dmax` below the policy cap can still over-read; caches are finite
   (diagnosis/mitigation, not lifetime proof). Raw aliases, stale pointers,
   untrusted formats, foreign/raw-`malloc` pointers, arbitrary C UB and non-local
   exits remain outside the guarantee. No static call-site coverage of all 414 APIs
   (411/414, worst 87.0%). Passing canaries on fixed cases is evidence for those
   cases only.
3. **No platform claim beyond tested.** All evidence is macOS arm64 / Apple clang
   16.0.0 / C+ 0.1.0 (plus Rust 1.100.0 for token comparisons). macOS x86_64, Linux
   (glibc/musl), Windows (MSVC/UCRT, x86/x64), other clang versions and optimizers
   are **UNTESTED**.
4. **No C++ mangled-ABI claim.** Only C-linkage/plain-C symbols were audited;
   the positive observation is limited to "0 `_Z`/`__Z` symbols found".
5. **No blanket "all green".** ABI/mapping/safety/consumer/SDK groups, 6/6 expanded
   example groups and the comparison mirrors are green in scope; the original
   49-example aggregate (exit 1), the Rust examples aggregate (`RESULT: FAIL`, exit 1)
   and two BAD_REPORT groups are not.
6. **No ASan pass.** ASan/Leak/Thread/Memory/CFI unsupported by the selected backend;
   host Apple clang 16 ASan aborts at startup even for `int main(){return 0;}`.
   Only UBSan on the checked harness (0 reports), not a substitute.
7. **No live-game / injected-DLL / Wine success claim.** No such validation was
   performed; a build or emulator/harness result is not live-game success.
8. **No "supports every C version" claim.** Only the C89/C99/C11/C17/C23 matrix
   directories exist, and the result artifacts are **[PENDING]** — no version-support
   claim is made without evidence. There is **no C21 ISO standard**; none is claimed.

---

## 6. ABI preservation (summary)

ABI is unchanged by the checked layer at every measured layer: **source blobs**
(7/7 core files unchanged since `cbcea57`; the one header edit is compile-time
spelling only), **fingerprint** (1235-record MATCH, empty diff), **layouts** (70/70),
**signatures** (static asserts in all 3 builds + 12 `F` records), **symbols**
(plain == disabled byte-identical; `plain minus safe = 0`; 38 `cplus_sc_*` + 26
`cplus_v_*` + C23/hardening additions all additive; no C++ mangling), **calling
convention** (byte-identical opcodes), **precompiled archives** (same saved `.a`
hash before/pre/post link, byte-identical stdout), **24/24 consumers**, and
**installed SDK** (static + dylib byte-identical stdout, flat C symbols, 27/27
mappings). The checked rewrite happens in the including TU only; archives define
no `malloc`/`memcpy`/`printf` override — **not interposition**. Baseline was
refreshed additively (defined count 784 → 812; L/C/F unchanged); the MATCH is
legitimate, not a stale-baseline artifact. Cross-run archive-hash drift from
concurrent sibling rebuilds is disclosed; each claim applies to its own run.

---

## 7. Remaining blockers / gaps

| # | Blocker | Status / evidence |
| --- | --- | --- |
| 1 | **External `CPLUS_EXIT_ARITHMETIC`** | Sanitized/instrumented C+ builds fail in *external* `c-plus-core/runtime/cplus_runtime.c:129:23` (`use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`); local enum ends at `CPLUS_EXIT_IO = 109`. Owned by `c-plus-core`, **out of scope / unresolved**. All green results use `--strict --sanitize=none` or non-instrumented paths. |
| 2 | **ASan unsupported** | ASan/Leak/Thread/Memory/CFI unsupported by selected backend; Apple clang 16 ASan aborts at startup. **No ASan pass claimed**; UBSan (0 reports) is not a substitute. |
| 3 | **macOS arm64 only** | Windows/Linux, x86_64, MSVC/UCRT, other clang versions/optimizers untested. `nm -gU`, LP64, two default archives only; no cross-platform ABI/calling-convention claim. |
| 4 | **Trap-C — no source** | No repo, commit, license, source, compiler or interpreter; **ZERO code integrated**, NOT TESTABLE, research-only, one-way ABI-incompatible managed pointers. No SPDX identifier assigned. |
| 5 | **No C21 standard** | C21 is not an ISO C standard (C89/C90, C99, C11, C17, C23). The `tests/c_standards` matrix covers c89/c99/c11/c17/c23 only; no C21 support claimed or measurable. |
| 6 | **C standard matrix artifacts PENDING** | `tests/c_standards/RESULTS.md` and `STD_SUPPORT_MATRIX.md` absent (directories exist); no per-version support claim without evidence. |
| 7 | **Red/blue battle PENDING** | `docs/security/RED_BLUE_BATTLE_FINAL.md` absent; no result asserted. |
| 8 | **`FE_TIESAWAY` host-blocked** | Genuinely absent from both clang versions and SDK; honest `UNSUPPORTED-BY-HOST` refusal. 49 deliberate `UNSUPPORTED` ISO C entries unchanged — **no "100% ISO C complete" claim**. |
| 9 | **Adversarial ninth group missing** | 8 groups ran → 8000/9000 scenarios; discovery is dynamic and a ninth compliant group is included automatically. |
| 10 | **Stale committed `RESULTS.md` snapshots** | Expanded examples body shows 5/6-exit-1 (refreshed 6/6 exit 0); expanded adversarial body shows 110/110 (refreshed 135/135); libc/CRT bodies show older counts (refreshed 109/109, 85/85). Refresh rather than re-interpret. |
| 11 | **On-disk/uncommitted comparison artifacts** | Several expanded comparison results and converted-project files appear `??`/` M` in `git status`; landed commits (`c08c9d0`, `c6195a0`, `58860a8`) carry the authoritative bodies. |
| 12 | **Numerical fidelity caveats** | `hypot3` may differ a few ulp from a correctly-rounded 3-arg hypot; `exp16` lowers through host `expf`; `__builtin_frexpf16` crashes arm64 backend at `-O0` so f32 compositions are used. Platform-backed, not formal proof. |

---

## 8. Final verdict

**Delivery is substantive and internally well-evidenced, but bounded.** On the
single tested host (macOS arm64 / Apple clang 16.0.0 / C+ 0.1.0 / Rust 1.100.0):

- **Standard C ABI is preserved** at source, fingerprint, layout, signature,
  symbol, opcode, precompiled-archive, consumer and installed-SDK layers — with
  **no C++ mangling** and no interposition.
- **All 27 `MISSING` API rows have a disposition:** **26 RESOLVED** with executed
  tests, **1 honestly BLOCKED-BY-HOST**, 0 PENDING; **22 vendored safeclib functions**
  and upstream safecrt integrated; **10 additive runtime-hardening symbols** across
  3 groups.
- **Testing is broad (compile-time + runtime separated):** fingerprint 1235 MATCH;
  compat 69/69; layouts 70/70; 24/24 consumers; SDK 27/27; **518/518** methods;
  **8000/0** adversarial; **97,250** fuzz scenarios with 0 crashes/hangs/corruptions;
  stdc safety 64/48/64; **283** per-method token cases; **72/72** stdc tokens;
  49 original examples (**43/5/1**); **6/6** expanded groups; **135/135** expanded
  refusals; expanded Rust mirrors **9/9** and **10/10**; **25** more examples, **8**
  modern/secure apps and **30** utilities, all MATCH; **54/54** legacy labs.
- **MATCH ≠ equivalence. Evidence ≠ proof.** No Rust-equivalence, no 100%
  memory-safety proof, no ASan pass, no platform claim beyond macOS arm64, no
  "supports every C version" claim, no live-game success, and no blanket "all green"
  is asserted. The external `CPLUS_EXIT_ARITHMETIC` blocker remains unresolved in
  `c-plus-core`; C-standard-matrix and red/blue-battle artifacts are **PENDING**.

**Headline counts:** ABI fingerprint **1235 records MATCH** (empty diff); ABI
checks **69/69 compat, 70/70 layouts, 22/0 `stdc_abi`, 24/24 consumers, 27/27 SDK
mappings**; source integrity **7/7 core files UNCHANGED since `cbcea57`, 18 added/0
modified sources**; APIs **27 = 26 RESOLVED + 1 BLOCKED-BY-HOST + 0 PENDING**;
**22** vendored safeclib functions; **10** hardening symbols / 3 groups; stdc
safety **64 cases / 48 refusals / 64 canaries**; per-method tests **518/518**;
adversarial_1000 **8000 scenarios / 0 failures** (5209 refusals); fuzz10k
**97,250 scenarios / 0 crashes / 0 hangs / 0 corruptions** (85,635 refusals);
expanded adversarial **135 cases / 135 refusals**; per-method tokens **283 / 277
matched / 0 mismatched / 13 blocked**; stdc tokens **72/72**; Rust examples
aggregate **0 mismatches / 29 BLOCKED / 2 BAD_REPORT / exit 1**; original examples
**49 = 43 MATCH / 5 DIFFERENT / 1 BLOCKED**; expanded examples **6/6 groups PASS,
exit 0**; expanded Rust comparisons **9/9 + 10/10 MATCH**; newer comparisons
**25 + 8 + 30 MATCH**; external upstream projects preserved **3 (+1 conversion)**.

**Report path:** `docs/FINAL_COMPLETE_DELIVERY.md`
