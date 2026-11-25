# Final expanded delivery report — `agent/modern-stdlib`

**Capture:** 2026-11-23 (~06:0x UTC), expanded sections refreshed from the
latest landed runs · **Branch:** `agent/modern-stdlib`
**Role:** synthesis/aggregation only. Every number below is quoted from a named,
on-disk authoritative artifact (listed in §0) or supplied by the coordinating
agent and labelled **[HANDOFF]**. Nothing was re-executed for this report;
nothing was rounded, promoted, or invented. A missing artifact is never
treated as a pass.

**Refresh note (this update):** the expanded examples, expanded adversarial and
expanded comparison sections now carry the latest verified counts. Where a
committed `RESULTS.md` snapshot still records an earlier run, that earlier
figure is disclosed in a provenance note with the exact refresh command; it is
never silently substituted for the newer number. The newer figures are backed
by landed commit messages (`c08c9d0`, `c6195a0`, `58860a8`), captured runner
output (`build/expanded_examples/final_lines.txt`) or the coordinating agent.

**File ownership:** this document is the only file created/modified by this
agent. No source, header, build file, test, or git index entry was changed; no
`git add` / `git commit` was run.

---

## 0. Authoritative artifacts read

| Artifact | What it supplies |
| --- | --- |
| `docs/abi/STDC_ABI_SAFETY_FINAL.md` | ABI preservation, source integrity, mapping, precompiled archive, hostile-input safety, stdc Rust tokens, consumers, SDK |
| `docs/libc/MISSING_API_CLOSURE.md` | 27-API closure ledger, C23 APIs, runtime-hardening wrappers, ABI fingerprint |
| `benchmarks/safety_compare/per_method/RESULTS.md` | 9-group C+/safe-Rust per-method outcome-token matrix |
| `benchmarks/rust_examples/RESULTS.md` | 9-group safe-Rust examples aggregate |
| `tests/adversarial_1000/RESULTS.md` | ADV1000 aggregate (8 groups) |
| `tests/stdlib/method_tests/RESULTS.md` | 518/518 per-method test aggregate |
| `benchmarks/example_rust_compare/ALL_EXAMPLES.md` + per-group `RESULTS.md` | 49-example index; demos A–P, `crt/*`, games, `apps_system` comparisons |
| `benchmarks/example_rust_compare/RESULTS.md` | 49-example aggregate: 43 MATCH / 5 DIFFERENT / 1 BLOCKED |
| `examples/expanded/RESULTS.md` + `build/expanded_examples/final_lines.txt` | 6/6 expanded example groups PASS, aggregate exit 0 |
| `tests/expanded_examples_adversarial/RESULTS.md` | Expanded adversarial refusal aggregate (135 cases) |
| `benchmarks/example_rust_compare/expanded_cli_games/RESULTS.md` | CLI/game expanded C+ vs Rust: 9/9 MATCH |
| `benchmarks/example_rust_compare/expanded_db_engine/RESULTS.md` | DB/engine/safety expanded C+ vs Rust: 10/10 MATCH |
| `git status --short`, `git log --oneline -30`, `git branch --show-current` | Landed vs uncommitted state |

**Evidence labels:** **[LANDED]** = committed on `agent/modern-stdlib`;
**[ON-DISK, UNCOMMITTED]** = files present but shown as `??`/` M` by
`git status`; **[PENDING]** = not present as a runnable artifact, exact
reproduction command given; **[SIBLING]** = quoted from a named `RESULTS.md`/
`NOTES.md`; **[HANDOFF]** = supplied by the coordinating agent and treated as
authoritative for the refreshed expanded counts (cross-checked against landed
commit messages / captured runner output where available).

**Snapshot provenance (common):** macOS `arm64` / Darwin 27.2.0, Apple clang
16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin*`, `C+ 0.1.0`,
`rustc 1.100.0 (0ef1bc386 2026-11-09)`. Source-integrity baseline `cbcea57`;
audited HEAD `ce336ab93e569ce25f223fad6db661f09109f1ea` (24 commits) for the
ABI proof and HEAD `e55c425` for the API-closure audit. `cpuset` note: the
default `clang` on PATH is Homebrew 23.1.6 with a broken `MacOSX27.sdk`; all
green runs pin `CPLUS_CLANG=/usr/bin/clang`.

**Expanded-run provenance:** the refreshed expanded results were captured on
Darwin 27.2.1 (arm64) with the same pinned Apple clang 16.0.0 / C+ 0.1.0 /
rustc 1.100.0. The relevant landed commits are `c08c9d0` (6/6 expanded example
groups PASS), `c6195a0` (135-case expanded adversarial suite) and `58860a8`
(49-example aggregate, 43/5/1). Several committed `RESULTS.md` bodies still
embed the earlier pre-refresh snapshot; where that happens the earlier figure
is disclosed as provenance only, never as the final count.

---

## 1. Standard C ABI + source-preservation evidence

All eight ABI groups are **LANDED** (`docs/abi/STDC_ABI_SAFETY_FINAL.md` §"Group
status"), each with a named reproduce script.

### 1a. Compile-time / static evidence

| Check | Exact result | Reproduce |
| --- | --- | --- |
| ABI fingerprint | **MATCH**, empty diff, 1235 records | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` |
| ABI compatibility | `abi_compat: 69 passed, 0 failed` | `sh tests/abi/run_abi_compat.sh` |
| ABI layouts | `abi_layouts: 70/70 layouts match` | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` |
| `stdc_abi` | `22 PASS, 0 FAIL` | `sh tests/abi/stdc_abi/run.sh` |
| Original C source integrity | 7/7 core files **UNCHANGED** (equal git blob ids); `libc/src/` **18 ADDED / 0 MODIFIED / 0 DELETED / 0 RENAMED** (2375 insertions, 0 deletions); `libc/install-include/cplus*.h` **19 ADDED / 0 MODIFIED / 0 DELETED / 0 RENAMED** (1211 insertions, 0 deletions) | `bash tests/abi/stdc_source/run.sh` |
| Checked-layer mapping | 16/16 ordinary spellings; all checked targets defined `T`/`D`/`B` symbols in the shipped archives; no interposition (`nm -u` TU-isolation probes) | `sh tests/abi/stdc_mapping/verify.sh` |
| C/C++ consumer matrix | **24 PASS, 0 FAIL** (9 headers × C99/C++17 + 2 overlays + combined TU) | `sh tests/abi/stdc_consumers/run.sh` |
| Installed SDK symbol audit | 27/27 implemented/vendored mappings present as defined `T` symbols in both static and dylib; 0 C++-mangled symbols found | `sh tests/abi/stdc_sdk/run.sh` |

Static assertions in all three builds (`plain`/`safe`/`disabled`) assert
`sizeof(void*)==8`, `sizeof(size_t)==8`, `sizeof(abi_sample)==40`,
`_Alignof==8`, exact field offsets and exact types of the five external
functions. Stripping the Mach-O underscore, all three builds export exactly
`abi_add abi_fill abi_glob abi_roundtrip abi_sum main`; `plain minus safe = 0`
(no ordinary symbol removed/added/renamed). Mechanism is caller-side
preprocessor remapping in the including translation unit only — **not** symbol
interposition.

### 1b. Runtime / dynamic evidence

- **Opcode identity:** `objdump -d --disassemble-symbols` bytes for `_abi_add`,
  `_abi_sum`, `_abi_fill` byte-identical in plain/safe/disabled.
- **Precompiled archive:** saved `libstdc_legacy.a` SHA-256
  `e6f00c99…467403` hashed identically at baseline, PRE-LINK and POST-LINK;
  plain and safe callers emit byte-identical stdout on valid input (SHA-256
  `2919b20d…98ae7d64`); overflow caller refused.
- **Legacy labs:** `total=54 pass=54 fail=0 build_failure=0 timeout=0
  skipped=0 blocked=0` (`bash tests/abi/run_legacy_labs.sh`).
- **Installed SDK runtime:** plain static/plain shared/checked-valid/
  checked-hostile all produce the same 42-byte stdout (SHA-256
  `d5d86a4a…0f94b4`); hostile `memcpy` into `char[8]` refused with exit **70**;
  plain build on same input dies SIGTRAP (133).
- **C++17 gap:** the former `cplus_libc.h` C++17 incompatibility is fixed
  ABI-neutrally via `CPLUS_RESTRICT`/`CPLUS_*_COMPLEX` macros; fingerprint
  remains MATCH. A stale supplementary `UNEXPECTED` probe row is disclosed in
  the sibling artifact rather than hidden.

**Honest scope:** source integrity holds *since baseline `cbcea57`*, not "never
modified in project history" (pre-baseline hardening commits are disclosed).
No ABI claim beyond the tested host/toolchain.

---

## 2. All added safe libraries

### 2a. Vendored safeclib + upstream safecrt — **LANDED**

- Commit `ce018ca`: "Integrate proven upstream implementations into C+
  stdlib" — upstream safecrt with safe C runtime wrappers.
- Commit `de79021`: "Vendor already-made safeclib implementations into the
  checked runtime" — 10 vendored safeclib group sources + headers.
- Checked ordinary-C layer (commit `4ec4ca2`, deterministic fuzz harnesses
  `a11de7f`): `stdlib/safe/safe_c.h` remaps 16 ordinary spellings
  (`malloc`, `calloc`, `realloc`, `free`, `memset`, `memcpy`, `memmove`,
  `memcmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `fread`, `fwrite`,
  `printf`, `fprintf`) to checked entry points compiled in the including TU.
- Defined checked targets are `nm`-verified in `libcplus_safecrt.a` and
  `libcplus_hardened.a` (`malloc_s`, `cplus_memset_s_auto`, `cplus_printf`,
  `_bounded`/`_s_auto` families, `_cplus_sc_upstream_bsearch_s`,
  `_cplus_sc_upstream_memrchr_s`, the vendored `cplus_v_*` `_chk` family, …).
- Per-method matrix (`benchmarks/safety_compare/per_method/RESULTS.md`) `nm`
  gates 9 method groups: memory, strcopy, strquery, alloc, search, transform,
  field, io, format.

### 2b. C23 / ISO missing-API closure — **LANDED**

From `docs/libc/MISSING_API_CLOSURE.md` (authoritative denominator
`docs/libc/iso_c_inventory.json`, 948 APIs):

| Metric | Count |
| --- | ---: |
| APIs classified `MISSING` | **27** |
| **RESOLVED** (changed source + executed test) | **26** |
| **BLOCKED-BY-HOST** (`FE_TIESAWAY`) | **1** |
| Still MISSING / PENDING | **0** |
| Bare ISO spellings (`nextup`/`nextdown` family) | 6 |
| Namespaced `cplus_*` / `cplus_c23_*` entry points | 20 |
| Test suites independently re-executed | 8 (8/8 pass) |

- **23 C23 math/temporal/Unicode APIs:** `c23_nextup: 84 0`,
  `c23_arith: 83 0`, `c23_hypot16: 357 0`, `c23_time_unicode: 102 0`.
- **3 closure APIs:** `timespec_getres`, `mbrtoc8`, `c8rtomb` (from-standard
  originals; host `uchar.h` absent).
- **`FE_TIESAWAY`** is **BLOCKED-BY-HOST**: both `/usr/bin/clang` (Apple 16)
  and Homebrew clang 23.1.6 lack the macro; SDK-wide and vendored searches
  empty. The header `cplus_c23_fenv_compat.h` deliberately defines no value,
  publishing `CPLUS_C23_FENV_TIESAWAY_STATUS == "UNSUPPORTED-BY-HOST"` and
  `cplus_fenv_has_tiesaway() == 0` (test `c23_fenv: 8 0`). Tested by exact
  `-fsyntax-only` probe (exit 1).
- `_Float16` is **available** (`__FLT16_MANT_DIG__ == 11`), so the four f16
  APIs are RESOLVED, not host-blocked.

### 2c. Runtime-hardening additive wrappers — **LANDED**

3 groups, **10 new symbols**, all additive (no signature/layout/export changed),
wired into `libc/src/**` (not `runtime/**`/`compiler/**`):

| Group | New symbols | Test → exact result |
| --- | --- | --- |
| Checked sync destroy | `cplus_mtx_destroy_checked`, `cplus_cnd_destroy_checked`, `cplus_thrd_join_checked` | `sync_checked: 156 0` (also 5× repeat, UBSan, negative control 5 failures) |
| Allocation hardening | `cplus_alloc_atomic_realloc`, `cplus_alloc_checked_free`, `cplus_alloc_state_dump` | `alloc_hardened: 32 0` |
| Violation log | `cplus_violation_log_open`, `cplus_violation_log_event`, `cplus_violation_log_close`, `cplus_violation_log_count` | `violation_log: 41 0` (10× repeat; 1,048,576-event cap; 2×100-thread concurrency) |

`cplus_fenv_has_tiesaway()` is header-only/static inline and not part of the 10.
`git show --name-only --format= 2a2e13f | grep -E '^(runtime|compiler)/'` returns
nothing — the 10 parallel agents touched no `runtime/**` or `compiler/**` core
file. **ABI fingerprint remained MATCH** (baseline refreshed additively;
per-archive defined count 784 → 812; L/C/F sections unchanged).

### 2d. Hostile-input safety of the checked layer — **LANDED**

`STDC_SAFETY 64 48 64 0` — 16 APIs × 4 cases, **48 refusals**,
**64/64 canaries intact**, 0 failures; 68 passed / 0 failed in the runner
(`sh tests/abi/stdc_safety/run.sh`). Raw-C control corrupted 13 canaries and
crashed/aborted 23 times (SIGSEGV 11, SIGABRT 6, SIGTRAP 5). UBSan-instrumented
checked run: exit 0, identical token line, **0** `runtime error` reports.

Three honest exceptions disclosed: `fopen` not remapped by `safe_c.h` (checked
build calls `cplus_fopen_s`); `fread`/`fwrite` "small" uses bounded `cplus_fread_s`/
`cplus_fwrite_s` (bmax 16); `printf`/`fprintf` hostile cases use
`printf_s`/`fprintf_s`. Production violation trap (exit 70) unchanged.

---

## 3. New examples / demos by category

### 3a. Safety demos (demos A–P) — **LANDED** (`benchmarks/example_rust_compare/`)

| Group | Result | Source |
| --- | --- | --- |
| `demos_a_d` | **3 MATCH, 1 DIFFERENT, 0 BLOCKED** (B_bounds: C+ SIGABRT 134 vs Rust panic 101, both suppress final line) | `demos_a_d/RESULTS.md` |
| `demos_e_h` | **4 MATCH, 0 DIFFERENT, 0 BLOCKED** (E normalized for ephemeral port/line order) | `demos_e_h/RESULTS.md` |
| `demos_i_l` | **3 MATCH, 0 DIFFERENT, 1 BLOCKED** (K_state_class: strict C+ `[CPLUS-ARITH]` compile rejection) | `demos_i_l/RESULTS.md` |
| `demos_m_p` | **4 MATCH, 0 DIFFERENT, 0 BLOCKED** | `demos_m_p/RESULTS.md` |

### 3b. CRT safe-API demos (`examples/crt/*`, 26 examples) — **LANDED**

| Group | Result | Source |
| --- | --- | --- |
| `crt_memory` | **9/9 MATCH, 0 DIFFERENT, 0 BLOCKED** | `crt_memory/RESULTS.md` |
| `crt_string` | **2 MATCH, 4 DIFFERENT, 0 BLOCKED**; normalized **6/6** (4 DIFFERENT are C-only safeclib `ignore_handler_s` lines) | `crt_string/RESULTS.md` |
| `crt_io` | **7/7 MATCH, 0 DIFFERENT, 0 BLOCKED** | `crt_io/RESULTS.md` |
| `crt_format` | **4/4 MATCH, 0 DIFFERENT, 0 BLOCKED** | `crt_format/RESULTS.md` |

### 3c. Games, including snake — **[ON-DISK, UNCOMMITTED]**

`examples/games/{2048.c,snake.c,tetris.c,tiles.c}` compiled unmodified into
fixed-seed (`seed=12345`) deterministic-core drivers and compared byte-exactly
to safe-Rust models. Result: **4 MATCH, 0 DIFFERENT, 0 BLOCKED
(deterministic-core scope)** — 111 lines / 6456 bytes of identical output;
snake stdout 984 B/21 lines (sha256 `4288b770…00ad`) matched. Interactive UI,
time/pid-seeded randomness, wall-clock gravity and score-file I/O are explicitly
**BLOCKED** with evidence (`sh: cls: command not found`, ncurses escape output,
differing per-run hashes). See `benchmarks/example_rust_compare/games/RESULTS.md`
(`?:?` in `git status` — not committed). Reproduce:
`sh benchmarks/example_rust_compare/games/compare.sh`.

### 3d. CLI / system / database / game engine (`apps_system`) — **[ON-DISK, UNCOMMITTED]**

`benchmarks/example_rust_compare/apps_system/out/compare.run.log` records
**3/3 examples matched on the tested deterministic paths**:

- **CLI / game engine** — `examples/apps/cgame.c`: **8/8** deterministic
  menu/quiz/sandbox paths byte-identical (`quit`, `invalid`, `reset`,
  `levels_back`, `libmenu`, `quiz_wrong`, `quiz_correct`, `libex0`); deeper
  level content not modeled.
- **Database** — `examples/apps/student.cp`: **2/2** deterministic CRUD +
  persistence paths byte-identical (stdout, stderr, exit, `students.db`).
- **System CLI** — `examples/system/macpulse.cp`: MATCH on the non-interactive
  tty-guard path via a minimally patched copy; live interactive sampling is
  timing/hardware-dependent and **BLOCKED** for byte comparison.

Reproduce: `sh benchmarks/example_rust_compare/apps_system/compare.sh`.
**Note:** `ALL_EXAMPLES.md` still lists rows 43–49 as PENDING (its recorded run
predates these files landing); the `games/RESULTS.md` and `apps_system`
artifacts above are the more recent on-disk evidence. `cgame.rs` shows ` M`
and `compare.sh`/`macpulse.rs` show `??` in `git status` — **not committed**.

### 3e. `examples/expanded/` — 6/6 groups PASS **[LANDED / HANDOFF]**

The expanded campaign has landed: **6/6 runner groups PASS, aggregate exit 0**.
`build/expanded_examples/final_lines.txt` records these verbatim final lines
(commit `c08c9d0`):

| Group | Token | Result |
| --- | --- | --- |
| CLI | `CLI_EXAMPLES 4 4 0` | 4/4 PASS |
| Database | `DB_EXAMPLES 3 3 0` | 3/3 PASS |
| Engine | `ENGINE_EXAMPLES 3 3 0` | 3/3 PASS |
| Game cores | `GAME_CORES 3 3 0` | 3/3 PASS |
| Snake | `SNAKE_EXAMPLES 3 3 0` | 3/3 PASS |
| Safety | `SAFETY_DEMOS 4 4 0` | 4/4 PASS |

Reproduce: `sh examples/expanded/run_all.sh` (also `sh examples/expanded/safety/run.sh`
for the safety group alone), from the repository root.

**Provenance note (not a downgrade):** the committed body of
`examples/expanded/RESULTS.md` still records the earlier 5/6 snapshot (safety
group PENDING, aggregate exit 1). The safety `run.sh` has since landed and the
captured `final_lines.txt` shows `SAFETY_DEMOS 4 4 0`; the coordinating agent
confirms the refreshed aggregate is 6/6 with exit 0. Refresh the snapshot with
the command above before quoting that file's older table verbatim.

### 3e-bis. Expanded examples adversarial — 135/135 refusals **[HANDOFF]**

`tests/expanded_examples_adversarial/RESULTS.md` (commit `c6195a0`) is the
authoritative ledger for hostile-input testing of the expanded examples:
**135 cases, 135 refusals, 0 crashes, 0 unexpected OK, 0 blocked**, aggregate
exit 0. Inputs include 10 KB strings, SIZE_MAX-looking decimals, empty/
whitespace arguments, duplicate ids/keys/cells, out-of-range indices, truncated
and one-over files, invalid UTF-8, self-collision move sequences and double/
stale-release scenarios. Classification: signal/timeout exit → CRASHED; nonzero
exit or explicit refusal token → REFUSED; anything else → UNEXPECTED_OK.

Reproduce: `sh tests/expanded_examples_adversarial/run_all.sh`

**Provenance note:** the committed body of that `RESULTS.md` still displays the
earlier `ADV_EXPANDED_TOTAL 110 110 0 0` run (`logs/run3.out`, `logs/final.out`);
the refreshed 135-case total is supplied by the coordinating agent and is
consistent with commit `c6195a0`'s message. Re-run the command above to refresh
the on-disk snapshot before quoting its per-case lines verbatim.

### 3e-ter. Expanded C+ vs safe-Rust comparison mirrors **[LANDED]**

These are separate from the 49-example aggregate (§3f) and from each other:

| Mirror | Scope | Result |
| --- | --- | --- |
| `expanded_cli_games/RESULTS.md` | CLI + game cores + snake (9 examples) | **9/9 MATCH, 0 DIFFERENT, 0 BLOCKED, exit 0** |
| `expanded_db_engine/RESULTS.md` | database + engine + safety (10 examples, incl. `sync_demo.c`) | **10/10 MATCH, 0 DIFFERENT, 0 BLOCKED** |

`MATCH` = byte-identical stdout and equal process exit status for the exact
deterministic argv/stdin exercised under a 30 s timeout — **not** Rust
equivalence and **not** a safety proof (§5). Reproduce:
`sh benchmarks/example_rust_compare/expanded_cli_games/compare.sh` and
`sh benchmarks/example_rust_compare/expanded_db_engine/compare.sh`.

### 3f. 49-example index reconciliation

`benchmarks/example_rust_compare/RESULTS.md` (commit `58860a8`) records the
final aggregate over **49 examples: 43 MATCH, 5 DIFFERENT, 1 BLOCKED**. The
aggregate exit is 1 by contract — the 5 DIFFERENT verdicts and 1 build failure
must not be reported as a false green. The five DIFFERENT rows are `strcpy_s`,
`strcat_s`, `strcmp_s`, `strnlen_s` (C-only safeclib `ignore_handler_s` line) and
`B_bounds` (C+ SIGABRT 134 vs Rust panic 101); the single BLOCKED row is
`K_state_class` (strict C+ `[CPLUS-ARITH]` compile rejection).
`ALL_EXAMPLES.md` still carries the older 36/5/1 index with 7 PENDING rows
(cgame, student, macpulse, 2048, snake, tetris, tiles) — those rows are
superseded by the aggregate and by §3c/§3d. "MATCH" = observable behavior
parity for the tested inputs only — **not** Rust equivalence.

---

## 4. Testing totals

### 4a. Compile-time results

- ABI static: 1235-record fingerprint MATCH (empty diff); 69/0 compat; 70/70
  layouts; 22/0 `stdc_abi`; `_Static_assert`/`__builtin_types_compatible_p` in
  all three builds; 24/24 C99+C++17 consumer matrix; `extern "C"` audit.
- Source integrity: 7/7 UNCHANGED (git blob IDs); 18 added/0 modified sources;
  19 added/0 modified headers.
- Symbol provenance: all 30 checked targets `nm`-verified; TU-isolation `nm -u`
  probes; SDK 27/27 mappings present as defined `T` symbols; all 8 new C23
  symbols defined exactly once (12 farith symbols `nm` count == 1).
- Static API coverage: `sh tests/stdlib/run_coverage.sh` → `headers=24
  covered=411 total=414; worst 87.0%` (**411/414, not 100%**).
- Host capability probes: `FE_TIESAWAY` absent (exit 1); `_Float16` present.

### 4b. Runtime results

| Suite | Headline | Scope |
| --- | --- | --- |
| Per-method suites (`tests/stdlib/method_tests/RESULTS.md`) | **518 methods invoked · 518 passed · 0 failed · 0 skipped** | 8 groups: alloc_mem 87, str_vec 47, collections 63, file_io 28, thread_async 37, json_log_time 56, multimedia 127, net_http 73 |
| Adversarial (`tests/adversarial_1000/RESULTS.md`) | **8000 scenarios / 0 failures**, 5209 refusals, 8 groups (target 9000 = 9×1000); aggregate exit 0 | alloc, containers, fmt_io, json_log, memstr, multimedia, net_http, thread_async — each 1000; canary/state/sentinel integrity all ok |
| Hostile C safety (`stdc_safety`) | `STDC_SAFETY 64 48 64 0` — 64/64 canaries intact, 48 refusals, 0 failures; UBSan 0 reports | 16 APIs × 4 cases |
| Per-method C+/Rust tokens (`safety_compare/per_method`) | **283 cases, 277 matched tokens, 0 mismatched, 13 blocked**; 9 groups each `compare.sh` exit 0 | memory 20/20, strcopy 42/42, strquery 42/42+1, alloc 27/21+6, search 29/29+1, transform 42/42, field 42/42+4, io 18/18+1, format 21/21 |
| stdc Rust tokens (`stdc_compare`) | `RUST_EXAMPLES stdc_compare 12 72 72 0 0` — **72/72 matched, 0 mismatched**, empty diff | 12 APIs × 6 cases; `CANARY_BROKEN` never observed |
| Safe-Rust examples aggregate (`rust_examples/RESULTS.md`) | **9 groups, 0 real mismatches, 29 BLOCKED, 2 BAD_REPORT, runner exit 1 (`RESULT: FAIL`)** | 560 matched token lines; BLOCKED never counted as passes |
| Example-vs-Rust, games | **4 MATCH, 0 DIFFERENT, 0 BLOCKED** (deterministic core) | 2048, snake, tetris, tiles |
| Example-vs-Rust, apps_system | **3/3 MATCH** (8/8 cgame, 2/2 student, macpulse tty-guard) | on disk, uncommitted |
| Legacy labs | 54/54 pass, 0 build/timeout/skip failures | both bash 3.2.57 and 5.3.15 |
| libc tests (`tests/libc/run_libc_tests.sh`) | **109/109 passed, 0 failed** **[HANDOFF]** | coordinator-supplied latest count; see provenance note |
| CRT unit (`tests/crt/unit/run_unit_tests.sh`) | **85/85 passed, 0 failed** **[HANDOFF]** | coordinator-supplied latest count; see provenance note |
| Expanded examples (`examples/expanded/RESULTS.md`) | **6/6 groups PASS, aggregate exit 0** | CLI 4/4, DB 3/3, ENGINE 3/3, GAME_CORES 3/3, SNAKE 3/3, SAFETY 4/4 (§3e) |
| Expanded adversarial (`tests/expanded_examples_adversarial/RESULTS.md`) | **135 cases / 135 refusals / 0 crashes / 0 unexpected OK** **[HANDOFF]** | hostile inputs on the expanded examples (§3e-bis) |
| Expanded C+ vs Rust, CLI/games | **9/9 MATCH** | deterministic stdout + exit (§3e-ter) |
| Expanded C+ vs Rust, DB/engine/safety | **10/10 MATCH** | deterministic stdout + exit (§3e-ter) |

**libc/CRT provenance note:** the on-disk `docs/STATUS.md` reconciliation
(2026-11-23) records `libc tests: 85 passed, 0 failed` and `CRT unit: 80 pass,
0 fail` for these runner names. The refreshed **109/109** and **85/85** figures
are supplied by the coordinating agent **[HANDOFF]**; refresh the on-disk
snapshot with `sh tests/libc/run_libc_tests.sh` and
`sh tests/crt/unit/run_unit_tests.sh` before quoting those files' older counts.

**Compile-time vs runtime separation note:** no compile-only result (fingerprint,
`nm`, static assert, source blob, consumer compile) is counted as a behavioral
pass; no runtime token match is counted as a proof of guarantee. `skipped` is
never added to `passed`; BLOCKED is neither a match nor a failure.

---

## 5. Safety evidence vs non-claims

**Evidence (what was actually observed):**
- Checked hostile inputs are refused with exit 70 and intact 0xA5/0x5A/
  0xDEADBEEFCAFEBABE canaries on the tested cases; raw C corrupts canaries and
  crashes on the same cases.
- 8000 adversarial scenarios produced 5209 refusals with 0 integrity failures;
  518/518 method cases invoked and verified; UBSan checked run had 0 reports.
- Matching outcome tokens (`OK`/`REFUSED`/`CANARY_OK`/`N/A`) across 9 per-method
  groups, 72 stdc tokens, 49 original examples and 19 expanded comparison
  examples (9 CLI/games + 10 DB/engine/safety) are **behavioral correspondences
  on a fixed case set only**. The 6/6 expanded example groups and 135/135
  expanded adversarial refusals are likewise evidence for the tested inputs
  only.
- C mechanisms are runtime constraint checks (`errno_t`/`errno` codes 400/403/
  404/406, bounded live-allocation/recent-free caches, canaries, fail-fast trap
  disabled for observability). Rust mechanisms are compile-time ownership/
  borrowing plus `Option`/`Result`/`checked_mul`/`try_reserve_exact`/bounds-checked
  `get`/`get_mut` — **different mechanisms**.

**Explicit non-claims (mandatory):**
1. **No Rust-equivalence.** Matching tokens record printed words for the
   enumerated cases, not equal safety guarantees, semantics, or performance.
   The broader Rust aggregate is not green (29 BLOCKED, 2 BAD_REPORT, exit 1);
   several `REFUSED` rows are deliberate models of states safe Rust cannot
   construct. 5 DIFFERENT example verdicts and 1 BLOCKED (K_state_class) are
   direct counter-evidence to interchangeability.
2. **No 100% memory-safety proof.** The checked layer is runtime constraint
   checking with caller-derived capacities. `BOS_UNKNOWN` calls disable the
   object-size check; an over-large `dmax` below the policy cap can still
   over-read; caches are finite (diagnosis/mitigation, not lifetime proof).
   Raw aliases, stale pointers, untrusted formats, foreign/raw-`malloc`
   pointers, arbitrary C UB and non-local exits remain outside the guarantee.
   Passing canaries on 64/8000 cases is evidence for those cases only. There is
   **no** static call-site coverage of all 414 APIs (411/414, worst 87.0%).
3. **No platform claim beyond tested.** All evidence is macOS arm64 / Apple
   clang 16.0.0 / C+ 0.1.0 (plus Rust 1.100.0 for token comparisons). macOS
   x86_64, Linux (glibc/musl), Windows (MSVC/UCRT, x86/x64), other clang
   versions and optimizers are **UNTESTED**.
4. **No C++ mangled-ABI claim.** Only C-linkage/plain-C symbols were audited;
   the positive observation is limited to "0 `_Z`/`__Z` symbols found".
5. **No blanket "all green".** ABI/mapping/safety/consumer/SDK groups, the 6/6
   expanded example groups and both expanded comparison mirrors are green in
   their scope; the original 49-example aggregate (exit 1, 5 DIFFERENT + 1
   BLOCKED), the Rust examples aggregate (`RESULT: FAIL`, exit 1) and two
   BAD_REPORT groups are not.
6. **No "no ABI change" overreach.** The safety layer is additive /
   `static inline` / macro-only at the call site; no public struct layout,
   exported-symbol name, signature or calling convention was edited. Evidence
   shows the checked subset matched expectations on this toolchain; it does not
   prove future edits cannot break the ABI.

---

## 6. ABI preservation (summary)

ABI is unchanged by the checked layer at every measured layer: source blobs,
fingerprint (1235-record MATCH, empty diff), layouts (70/70), signatures
(static asserts in all 3 builds), symbols (plain == disabled byte-identical;
plain-minus-safe = 0; no C++ mangling), calling convention (byte-identical
opcodes), precompiled archives (self-contained same-`.a` hash before/pre/post
link), 24/24 C99+C++17 consumers, and installed SDK (static + dylib byte-identical
stdout, flat C symbols, 27/27 mappings). The checked rewrite happens in the
including TU only; archives define no `malloc`/`memcpy`/`printf` override —
**not interposition**. Source integrity is "unchanged since `cbcea57`", with
pre-baseline hardening commits disclosed. Baseline was refreshed additively
(28 new symbol records; defined count 784 → 812; L/C/F sections unchanged) and
the MATCH is legitimate, not a stale-baseline artifact.

---

## 7. Remaining blockers / gaps

| # | Blocker | Evidence / status | Exact reproduce |
| --- | --- | --- | --- |
| 1 | **External `CPLUS_EXIT_ARITHMETIC`** | Sanitized/instrumented C+ builds fail in the *external* `c-plus-core/runtime/cplus_runtime.c:129:23` with `error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`; this checkout's `runtime/include/cplus/cplus.h` enum ends at `CPLUS_EXIT_IO = 109`. Owned by `c-plus-core`, **out of scope / unresolved**. All green results use `--strict --sanitize=none` or non-instrumented paths. | `docs/abi/external_blocker_evidence.md`, `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` |
| 2 | **ASan unsupported** | AddressSanitizer (and Leak/Thread/Memory/CFI) **unsupported by the selected backend** (`error: sanitizer '…' is not supported by the selected backend`); host Apple clang 16 ASan aborts at startup even for `int main(){return 0;}`. **No ASan pass claimed.** Only UBSan on the checked harness (0 reports), not a substitute. | — |
| 3 | **macOS arm64 only** | Windows/Linux untested; Linux/macOS x86_64, MSVC/UCRT, other clang versions/optimizers untested. `nm -gU`, LP64, two default archives only. No cross-platform layout or calling-convention claim. | — |
| 4 | **Rust examples 29 BLOCKED** | Aggregate `RESULT: FAIL`, runner exit 1: 29 BLOCKED (alloc 24, runtime_diag 2, search 1, threading 2), 2 BAD_REPORT (`safecrt_memstr`, `vendored_v` emit 5 fields instead of 6), 2 inconsistent `cases` semantics (`format`, `json_log`). 0 real mismatches — but not a green aggregate. | `sh benchmarks/rust_examples/run_all.sh` |
| 5 | **`FE_TIESAWAY` host-blocked** | Genuinely absent from both clang versions and the SDK; honest `UNSUPPORTED-BY-HOST` refusal, not fabricated. 49 deliberate `UNSUPPORTED` ISO C entries unchanged; **no "100% ISO C complete" claim**. | `printf '#include <fenv.h>\n#ifndef FE_TIESAWAY\n#error FE_TIESAWAY_ABSENT\n#endif\n' | /usr/bin/clang -x c -std=c2x -fsyntax-only -` |
| 6 | **Ninth adversarial group missing** | 8 groups ran → 8000/9000 scenarios; campaign target is 9000 = 9×1000. Discovery is dynamic; a ninth compliant group will be included automatically. | `sh tests/adversarial_1000/run_all.sh` |
| 7 | **Stale committed `RESULTS.md` snapshots** *(was: expanded examples PENDING — now resolved: 6/6 PASS)* | The bodies of `examples/expanded/RESULTS.md` and `tests/expanded_examples_adversarial/RESULTS.md` still embed the earlier 5/6-exit-1 and 110/110 runs; the refreshed counts (6/6 exit 0, 135/135) come from landed commit messages, captured runner output and the coordinating agent. Refresh rather than re-interpret them. | `sh examples/expanded/run_all.sh` ; `sh tests/expanded_examples_adversarial/run_all.sh` |
| 8 | **On-disk/uncommitted comparison artifacts** | `benchmarks/example_rust_compare/expanded_db_engine/RESULTS.md` and several expanded-adversarial logs appear as `??`/` M` in `git status`, along with `scratch/*.cp` probes. The landed commits (`c08c9d0`, `c6195a0`, `58860a8`) carry the authoritative result bodies; uncommitted logs are refresh artifacts, not new claims. | `git status --short` |
| 9 | **Archive hash drift** | Archive SHA-256 differs across sibling runs due to concurrent rebuilds (`stdc_precompiled` hardened `86f6a722…` vs `stdc_safety` hardened `2aa3cc3e…`); each claim applies to its own run. Precompiled proof re-hashes the same saved `.a` and is self-contained. | — |
| 10 | **Numerical fidelity caveats** | `hypot3` may differ a few ulp from a hypothetical correctly-rounded 3-arg hypot; `exp16` lowers through host `expf`; `frexp16`/`logb16` use exact f32 compositions (`__builtin_frexpf16` crashes arm64 backend at `-O0`). Platform-backed, not formal proof. | — |

---

## 8. Final verdict

**Delivery is substantive and internally well-evidenced, but bounded.** On the
single tested host (macOS arm64 / Apple clang 16.0.0 / C+ 0.1.0 / Rust 1.100.0):

- **Standard C ABI is preserved** at source, fingerprint, layout, signature,
  symbol, opcode, precompiled-archive, consumer and installed-SDK layers — with
  **no C++ mangling** and no interposition.
- **All 27 `MISSING` API rows have a disposition:** 26 RESOLVED with executed
  tests, 1 honestly `BLOCKED-BY-HOST`, 0 PENDING; **10 additive runtime-hardening
  symbols** across 3 groups; vendored safeclib + upstream safecrt integrated.
- **Testing is broad:** ABI fingerprint MATCH with compat 69/69 and layouts
  70/70; libc 109/109 and CRT 85/85 **[HANDOFF]**; **518/518** per-method cases;
  **8000/0** adversarial scenarios; stdc safety **64 cases / 48 refusals /
  64 canaries**; **283** per-method token cases (277 matched, 0 mismatched,
  13 blocked); **72/72** stdc tokens; 49 original examples vs Rust (**43 MATCH
  / 5 DIFFERENT / 1 BLOCKED**); **6/6** expanded example groups PASS with exit 0;
  **135/135** expanded adversarial refusals; expanded comparison mirrors at
  **9/9** and **10/10** MATCH.
- **MATCH ≠ equivalence. Evidence ≠ proof.** No Rust-equivalence, no 100%
  memory-safety proof, no ASan pass, no platform claim beyond macOS arm64 /
  Apple clang 16.0.0 (expanded runs on Darwin 27.2.1), and no blanket "all
  green" is asserted. The external `CPLUS_EXIT_ARITHMETIC` blocker remains
  unresolved in `c-plus-core`.

**Headline counts:** ABI fingerprint **1235 records MATCH**; ABI checks
**69/69 compat, 70/70 layouts, 22/0 `stdc_abi`, 24/24 consumers, 27/27 SDK
mappings**; source integrity **7/7 UNCHANGED, 18 added/0 modified sources,
19 added/0 modified headers**; **libc 109/109, CRT 85/85 (HANDOFF)**; APIs
**27 total = 26 RESOLVED + 1 BLOCKED-BY-HOST (`FE_TIESAWAY`) + 0 PENDING**;
hardening **10 new symbols / 3 groups**; stdc safety **64 cases / 48 refusals /
64 canaries**; per-method tests **518/518**; adversarial_1000 **8000 scenarios /
0 failures** (5209 refusals); expanded adversarial **135 cases / 135 refusals /
0 crashes / 0 unexpected OK**; per-method tokens **283 cases / 277 matched /
0 mismatched / 13 blocked**; stdc tokens **72/72**; Rust examples aggregate
**0 mismatches / 29 BLOCKED / 2 BAD_REPORT / exit 1**; original examples
**49 = 43 MATCH / 5 DIFFERENT / 1 BLOCKED**; expanded examples **6/6 groups PASS,
exit 0**; expanded Rust comparisons **9/9 + 10/10 MATCH**.

**Report path:** `docs/FINAL_EXPANDED_DELIVERY.md`
