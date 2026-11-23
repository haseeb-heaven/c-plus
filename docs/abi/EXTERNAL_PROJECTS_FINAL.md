# Final integrity statement — external projects and C+ compiler support

Status of this document: **complete — no outstanding PENDING artifacts.** Every
document named in the task brief had landed and was read before finalization. Every `OK` claim below is
backed by a command that was actually executed on this machine, an observed file
hash, or a cited evidence file. Nothing is inferred from a build alone and nothing
is fabricated. Anything not yet verified is marked **PENDING**, not asserted.

- Authoring agent: 9/10 (final integrator doc).
- Branch: `agent/modern-stdlib`.
- Date of verification: 2026-11-23 (UTC).
- Scope: only `docs/abi/EXTERNAL_PROJECTS_FINAL.md` was created/modified by this agent.
  No source edits, no `git add`, no commit were performed here.
- Exclusive file ownership: this document. All other paths below are **read-only
  evidence** produced by sibling agents.

---

## 0. Environment (observed, not assumed)

| Item | Observed value |
|---|---|
| Host | macOS (Darwin), `uname -m` = `arm64` |
| Plain C compiler | `/usr/bin/clang` — `Apple clang version 16.0.0 (clang-1600.0.26.6)`, target `arm64-apple-darwin27.2.0`, thread model `posix` |
| C+ compiler | `build/c+` — `C+ 0.1.0`, "Language: C+ (C with memory safety, deterministic RAII, lightweight classes)", "Backend: host Clang/LLVM" |
| C+ frontend default C mode | `compiler/Sema/SemaCPlus.h`: `stdVersion = "c17"`; `-std=...` is forwarded verbatim to the Clang backend (`compiler/Sema/SemaCPlus.cpp`, `compiler/Main.cpp`) |
| ABI fingerprint tool | `tools/abi_compare/run.sh`, canonical baseline `tests/abi/fingerprints/baseline.txt` |
| C+ conversion tools | `tools/c2modern/convert.sh` (CLI) and `tools/c2modern_gui/converter_gui.cp` (GUI) |

**This is a single-platform result: macOS arm64 with Apple clang 16 only.** No Linux,
Windows, x86, GCC, MSVC, or other Clang version was tested here.

---

## 1. External projects — provenance, license, byte preservation, builds, output parity

Three real, already-made, permissively-licensed public projects were downloaded unmodified,
compiled with **plain clang** and with the **C+ compiler**, and run against fixed inputs.
The originals under `external_projects/<project>/original/` were not edited.

### 1.1 game_a — Corg-Labs/snake (terminal Snake)

| Field | Value |
|---|---|
| Project | "Classic Snake game in the terminal, in C" |
| Repository | `https://github.com/Corg-Labs/snake` |
| Clone URL | `https://github.com/Corg-Labs/snake.git` |
| Pinned commit | `b6bc0404635842526c2b4b31b0389c60a87d3bc1` (HEAD == `main` tip at verification) |
| Commit | Anurag Singh, 2026-06-04T19:35:16Z, `docs: add Tutorial section to README (#2)`, GitHub-verified PGP signature |
| License | **MIT** (`License`, "Copyright (c) 2026 Corg-Labs"), permissive; GitHub API `license.spdx_id = MIT`; no GPL/AGPL |
| Raw pinned source | `https://raw.githubusercontent.com/Corg-Labs/snake/b6bc0404635842526c2b4b31b0389c60a87d3bc1/snake.c` |

Preserved originals — SHA-256 **recomputed now** from the on-disk bytes and compared
against `external_projects/game_a/ORIGINAL_EVIDENCE.md` §2 (all **MATCH**):

| File | Size | SHA-256 (recomputed 2026-11-23) | git blob SHA-1 |
|---|---|---|---|
| `original/snake.c` | 2312 | `13de0d13e1ee1e621c48d7f91ac283126a04c3980b0c22c4fafbf81d25bc5774` | `7148c45271b7b68e7c0b0c5dc25c0b4639725eca` |
| `original/License` | 1066 | `4bee40b5b896b3546630630dbd4bdb9b65087e06a665db843840b11adfd29308` | `0d36d21c250aaa7df0ea759a75e8ad2aa7c8f6d6` |
| `original/Readme.md` | 5271 | `6b5a90307802b0aeddb32f0cdcd8558f7232326ade9fdc681aafd28560baec9a` | `84e3799389255914a9109dcfca7d324401391972` |

The three git blob SHA-1 values equal the GitHub contents-API blob IDs for that pinned
commit, proving byte-for-byte identity with upstream.

- Plain clang: `/usr/bin/clang -std=c99 original/snake.c -o build/snake` → exit 0, empty
  stderr, Mach-O arm64 executable. Deterministic run `printf 'q' | timeout 30 ./build/snake`
  → exit 0, 44 bytes stdout, SHA-256
  `234d359e90fb4b5c0253ad2134cdb5f8cacc8f33db8394b7f5ab9019d428d282`.
- C+ compiler: `external_projects/game_a/cplus/snake_modern.cp` built strict with `build/c+`
  → exit 0; `sh external_projects/game_a/cplus/verify_conversion.sh` reported
  `CONVERT game_a 12 12 0` (12 checks, 12 matched, 0 differed). The captured
  `cplus/run.stdout` SHA-256 was **recomputed now** as
  `234d359e90fb4b5c0253ad2134cdb5f8cacc8f33db8394b7f5ab9019d428d282` and `cmp` against
  `build/run.stdout` is byte-identical.
- Conversion evidence: `external_projects/game_a/cplus/CONVERSION.md` (**landed**).
- Companion ABI evidence: `docs/abi/EXTERNAL_PROJECTS_ABI.md` (**landed**). It quotes the
  same `CONVERT game_a 12 12 0` line and records the original/converted `nm` comparison.

### 1.2 game_b — mevdschee/2048.c (terminal 2048)

| Field | Value |
|---|---|
| Project | "2048.c — console version of 2048", single self-contained C file |
| Repository | `https://github.com/mevdschee/2048.c` |
| Pinned commit | `90343eb209e84fa9687d596f5d5eecfdf7d4df51` (author date 2026-10-07T09:37:19Z, message `update`) |
| License | **MIT** — GitHub API `license.spdx_id = MIT`; pinned `LICENSE` = standard MIT, "Copyright (c) 2024 Maurits van der Schee"; no extra/restrictive clauses, no GPL |
| Raw pinned source | `https://raw.githubusercontent.com/mevdschee/2048.c/90343eb209e84fa9687d596f5d5eecfdf7d4df51/2048.c` |

Preserved originals — SHA-256 **recomputed now** (all **MATCH** against
`external_projects/game_b/ORIGINAL_EVIDENCE.md` §2):

| File | Size | SHA-256 (recomputed 2026-11-23) |
|---|---|---|
| `original/2048.c` | 12762 | `90f794c675a696aa10980dd120d1f0503ee4ed33a61fef92c7df8f443f5b18a8` |
| `original/LICENSE` | 1088 | `593783abd95546a2817d104b2656dd985a979a9e90da895729f211fc844b6402` |

Byte-for-byte identity was additionally confirmed by streaming `curl | cmp -` against
fresh downloads of the pinned raw URLs (ORIGINAL_EVIDENCE.md §2).

- Plain clang: `/usr/bin/clang -std=c99 -o 2048_build original/2048.c` → exit 0, empty
  stderr. Runs under `timeout 30`: `test` printed `All 13 tests executed successfully`
  (exit 0), `--version` printed `2048.c version 1.0.3` (exit 0), `--help` printed usage
  (exit 0), stdin `qy` quit cleanly (exit 0, one `QUIT?` prompt, no keyboard error).
- C+ compiler: `external_projects/game_b/cplus/2048_modern.cp` produced arm64 binary
  `2048_modern`. **Re-verified now** by direct execution: `test`, `--version`, `--help`
  each exit 0 with stdout **byte-identical** (`cmp`) to the original's recorded logs and
  empty stderr; stdin `qy` exits 0 with one `QUIT?` prompt, zero keyboard-error lines,
  and 1492 stdout bytes (same byte count as the original; the board is time-seeded so
  exact tile positions are expected to differ).
- Conversion evidence `external_projects/game_b/cplus/CONVERSION.md`: **LANDED** at
  12:30 UTC. It reports `CONVERT game_b 14 14 0` (14 checks, 14 matched, 0 differed,
  stable across three runs) from `external_projects/game_b/cplus/verify.sh`, covering
  deterministic CLI modes plus RNG/board/signal cases (the latter compared with both
  binaries launched in the same wall-clock second). That agent noted
  `ORIGINAL_EVIDENCE.md` was not present when it wrote the doc; it has since landed and
  its recorded baseline outputs agree with the conversion's BEFORE captures.

### 1.3 cli_app — xfgusta/xdump (hex/ASCII dump CLI)

| Field | Value |
|---|---|
| Project | **xdump** — displays a file or stdin in hexadecimal and ASCII |
| Repository | `https://github.com/xfgusta/xdump` |
| Commit (exact, pinned) | `97fdfbdc1e49c10aabac736a57b0256251b39b66` (HEAD == `main`, author Gustavo Costa, 2022-09-03T11:21:53-03:00, `Set variables only if it's not already set`) |
| License | **MIT** — "Copyright (c) 2021 Gustavo Costa"; in-repo `LICENSE` at pinned commit; README states "Distributed under the MIT license"; no GPL |
| Pinned tarball | `https://github.com/xfgusta/xdump/archive/97fdfbdc1e49c10aabac736a57b0256251b39b66.tar.gz` |

Preserved originals — SHA-256 **recomputed now** (all **MATCH** against
`external_projects/cli_app/ORIGINAL_EVIDENCE.md` §2):

| File | Size | SHA-256 (recomputed 2026-11-23) |
|---|---|---|
| `original/xdump.c` | 9828 | `93106c3b4b7bb4f0608bd3308621eca30d8ab7c4c7883b23f54d3c7fe0309253` |
| `original/LICENSE` | 1070 | `a26531ee3ee8f9e12415618e04293813c4f248e4f8bc759c6ae0fb89ec3560a8` |

- Plain clang: `/usr/bin/clang -std=c99 original/xdump.c -o build/xdump` → exit 0, empty
  stderr. Eight fixed cases (version, help, file default, `-w 8 -g 4`, `-s 32 -n 16`,
  stdin, bad flag, missing file) were captured with exact exit/stdout/stderr under
  `timeout 30`; see `external_projects/cli_app/ORIGINAL_EVIDENCE.md` §4.
  Ancillary honesty note from that evidence: `-std=c11 -Wall -Wextra -Werror -pedantic`
  fails with two `-Wstrict-prototypes` diagnostics under Apple clang 16 (also under the
  upstream makefile flags); this is an Apple-clang diagnostic, not a C99 defect, and the
  original source was left unchanged.
- C+ compiler: `external_projects/cli_app/cplus/xdump_modern.cp` produced arm64 binary
  `xdump_modern`. **Re-verified now** by direct execution for six deterministic cases
  (`-v`, `-h`, file default, `-w 8 -g 4`, `-s 32 -n 16`, stdin default): all stdout
  **byte-identical** (`cmp`) to `external_projects/cli_app/build/runs/*.stdout`.
  Honesty note (from the landed companion ABI doc): the `-Z` case's stderr embeds
  `argv[0]` via `getopt`; invoking the converted binary by a different path changes only
  that prefix (`./build/xdump: illegal option -- Z` vs
  `…/xdump_modern: illegal option -- Z`). Invoking it as `./xdump` reproduces the
  original form — no behavioural regression.
- Conversion evidence `external_projects/cli_app/cplus/CONVERSION.md`: **LANDED** at
  12:31 UTC. It reports `CONVERT cli_app 24 24 0` from
  `bash external_projects/cli_app/cplus/verify.sh`: 8 cases × 3 streams
  (stdout, stderr, exit), all byte-identical. It also confirms all 22 original global
  symbols are present with identical Mach-O types, with additive `_cplus_*` /
  `_stdlib_*` runtime symbols only. The `bad_flag` case normalizes `argv[0]` to
  `./build/xdump` via `exec -a` so the getopt diagnostic is directly comparable.

### 1.4 Summary of project integrity

| Project | URL + pinned commit | License | Original SHA-256 rechecked | Plain clang | C+ compiler | Outputs matched |
|---|---|---|---|---|---|---|
| game_a | `github.com/Corg-Labs/snake` @ `b6bc0404…` | MIT | 3/3 MATCH | exit 0 | exit 0, 12/12 checks | yes (44-byte stdout SHA identical) |
| game_b | `github.com/mevdschee/2048.c` @ `90343eb2…` | MIT | 2/2 MATCH | exit 0 | exit 0, 14/14 checks | yes (deterministic modes byte-identical; stdin quit same control flow) |
| cli_app | `github.com/xfgusta/xdump` @ `97fdfbdc…` | MIT | 2/2 MATCH | exit 0 | exit 0, 24/24 checks | yes (8 cases × 3 streams byte-identical) |

---

## 2. ABI — no exported symbol, signature, or layout change; fingerprints MATCH

### 2.1 Whole-surface ABI fingerprint (independently re-run for this document)

Command actually executed from the repository root, with `CPLUS_FP_OUT` redirected to a
temporary directory so that **no repository file outside this document was modified**:

```
CPLUS_FP_OUT=<tempdir> sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt
```

Observed output (verbatim, with the temp path substituted):

```
abi_fp: wrote <tempdir>/compare/fingerprint.txt (1238 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: <tempdir>/compare/diff.txt
```

Exit code **0**; independent `wc -c` of `diff.txt` = **0 bytes**. The committed baseline
`tests/abi/fingerprints/baseline.txt` currently has **1238 records** (3 comment lines,
1241 lines total), SHA-256
`5dd609678598db2620605546ceb9cef1a27c95a7b9d829a0ee4e032d0a615ef9`.
This fingerprint covers sizes/alignments/offsets/enums (`L`), ABI-relevant compiler
defines and version (`C`/`V`), exported symbols from both shipped archives
(`build/libc/libcplus_hardened.a`, `build/safe/libcplus_safecrt.a`), and compile-time
function signatures (`F`). **Result: MATCH, empty diff.**

*Reconciliation with the companion ABI doc.* `docs/abi/EXTERNAL_PROJECTS_ABI.md`
(landed) records a transient fingerprint drift during its run: an earlier capture had
1235 records and MATCHed, then a later capture had 1238 records and exit 1 because
three **additive-only** symbols from concurrent in-flight edits to `stdlib/safe/`
appeared: `_cplus_io_stream_is_closed`, `_cplus_safe_transfer_ownership_generation`,
`_free_checked_generation_s`. No layout (`L`), signature (`F`), `size void *`, or symbol
removal/rename was involved. The committed baseline was subsequently refreshed to
include those three additive symbols (they are present in the current baseline at lines
992, 1051 and 1166), which is why the fresh capture performed for this document
**MATCHES** with an empty diff. This is consistent with the "additive only" conclusion,
not a contradiction of it.

Supporting ABI suites already recorded in sibling docs (`docs/abi/ABI_ORIGINAL_C_FINAL.md`,
`docs/abi/FINAL_ABI_AUDIT.md`, `docs/abi/ABI_SAFER_LIBS_FINAL.md`):

- `sh tests/abi/run_abi_compat.sh` → `abi_compat: 69 passed, 0 failed`, including
  `T5_no_cxx_mangling` on all three symbol dumps (flat C symbols, no `_Z`).
- `sh tests/abi/run_abi_layouts.sh` → `abi_layouts: 70/70 layouts match`.
- `sh tests/abi/stdc_abi/run.sh` → `stdc_abi: RESULT PASS -- ordinary C ABI unchanged by
  the safety layer`; `sizeof(void*)=8`, offsets `tag=0 count=4 value=8 ptr=16 name=24
  big=32`; no ordinary symbol removed and no new ordinary C API symbol added
  (additional SAFE-build symbols are internal archive helper implementations).
- `bash tests/abi/run_legacy_labs.sh` (bash 5) → `total=54 pass=54 fail=0 …`.
- The committed baseline was previously refreshed **additively only** (two new safeclib
  symbol exports); no public header, struct layout, calling convention, or signature was
  changed.

### 2.2 Per-project ABI evidence

- **game_a**: original TU exports only `T _main` (`nm -g build/snake.o`); `raw_on`,
  `raw_off`, `orig` are file-static; remaining symbols are libc imports; only system
  `libSystem.B.dylib` is dynamically linked (`otool -L`). The conversion keeps `int
  main(void)` unchanged and `raw_on`/`raw_off` local; the C+ class lowering emits internal
  helper symbols (`RawModeGuard_ctor`, `RawModeGuard_drop`,
  `RawModeGuard_restore_now`) that are implementation artifacts, not caller-facing API.
  `verify_conversion.sh` explicitly checks `T _main` and local `t _raw_on` / `t _raw_off`.
- **game_b**: standalone executable; `nm -g` global set matches the unmodified upstream
  function set (19 game functions + `__mh_execute_header`); `U` symbols are libc/POSIX
  imports only. No exported-library ABI.
- **cli_app**: standalone executable; `nm -g --defined-only` set matches the single
  original translation unit plus `__mh_execute_header`; `nm -u` shows only libc/POSIX
  imports. No exported-library ABI.

The landed companion ABI doc also performed direct original-vs-converted `nm` set
comparisons: for **game_b** all 20 original global text symbols (19 game functions +
`__mh_execute_header`) remain defined (`comm -23` set difference empty), with 28 additive
C+/safe runtime symbols; for **cli_app** all 22 original global symbols remain defined
with unchanged type (`comm -23` empty), with 32 additive C+/safe runtime symbols. No
original symbol was removed, renamed, or retyped.

**Conclusion:** no exported symbol name, signature, calling convention, struct layout, or
pointer representation changed in the original C surface. Any added symbols are additive
internal/statically-linked helpers; the fingerprint diff is empty.

Sibling ABI document `docs/abi/EXTERNAL_PROJECTS_ABI.md`: **LANDED** (verified at
12:29 UTC with 22,477 bytes). Its §1 provenance/hashes and §3 per-project `nm`
comparisons agree with the independently recomputed values above; its only qualified
result is the additive-only transient fingerprint drift explained above, which has since
been resolved by the baseline refresh.

---

## 3. C+ compiler C-version support — exact, honest list

**Correction of a common misstatement:** there is **no C21 standard**. ISO/IEC 9899 was
published as C89/C90, C94/C95 (Amendment 1), C99, C11, C17/C18, and C23. "C2y" was the
working name for the revision after C23; its intended publication name is **C26**, and
as of this verification **C2y/C26 is still a draft**, not a published ISO standard. Any
claim of "C21 support" is incorrect terminology and is not made here.

The C+ driver forwards `-std=` spellings verbatim to its Clang backend; it does **not**
add `-pedantic-errors` by default, so a newer-language construct can compile under an
older `-std` spelling as a Clang extension. The C+ frontend default is `c17`. Its **Safe**
mode rejects legacy C sources for C+ *safety* reasons (bounds/lifetime/FFI diagnostics),
which is distinct from standard-version support; legacy C sources build in
`--legacy-c` mode.

The authoritative aggregate files `tests/c_standards/RESULTS.md` and
`tests/c_standards/STD_SUPPORT_MATRIX.md` **have landed**. They aggregate seven
independently executed group reports under `tests/c_standards/<group>/RESULTS.md`
(`c89`, `c99`, `c11`, `c17`, `c23`, `latest`, `real_projects`). The aggregate runner
reported **7 suites run, 7 passed, 0 failed, 0 pending**. Verdict tokens below follow
`STD_SUPPORT_MATRIX.md`: **PASS** = accepted and correct; **PARTIAL** = accepted and
useful but some tested target/feature/mode failed; **FAIL (source)** = genuine source
issue the reference compiler agrees with; **FAIL (spelling)** = clang rejects the value;
**FAIL (safety)** = C+ default Safe-mode static-analysis rejection (not a C-version
signal); **UNRECOGNIZED** = no such flag / no published standard behind it.

| C family / spelling(s) | Aggregate verdict | Exact evidence / honest qualifiers |
|---|---|---|
| **C89/C90**: `c89`, `c90`, `iso9899:1990`, `gnu89` | **PARTIAL** (minimal source **PASS** for all four; real projects **FAIL (source)** 0/3 strict, 1/3 in legacy — 2048 only) | All four spellings build the genuine minimal C89 source with stdout byte-identical to strict clang. None of the three downloaded projects is genuine C89: strict clang rejects all three too. `-std=c89` alone does **not** enforce C90 (no `-pedantic-errors` by default); strict C90 requires the caller to add it. (`RESULTS.md` §3, `STD_SUPPORT_MATRIX.md` §1) |
| **C94**: `iso9899:199409` | **PARTIAL** (minimal **PASS**; real **PARTIAL** 1/3 — 2048 only) | Valid spelling; C94-conformant source compiles/runs and matches. In legacy mode 2048 builds with 3/3 outputs matched; snake/xdump fail on genuine `redefinition of 'i'`. |
| `c94` | **FAIL (spelling) / UNSUPPORTED** | `error: invalid value 'c94' in '-std=c94'` from clang 16; C+ forwards verbatim. 0/2 sources, 0/3 real projects. |
| **C99/C9X/GNU99**: `c99`, `c9x`, `gnu99` | **PASS** (minimal and real) | `c99`/`c9x`/`gnu99` build both applicable minimal sources and all three real projects in `--legacy-c`, with 10/10 compiled legacy cells matching golden outputs (7/7 per project in the C99 group). Safe mode 0/15 — C+ safety diagnostics, not a C-version failure. |
| **C11/C1X/GNU11**: `c11`, `c1x`, `gnu11` | **PASS** for `c11`, `c1x`, `gnu11` minimal; real projects tested with `c11` only (**PASS** 3/3, 12/13 byte-identical) | Genuine C11 source builds with 0 diagnostics and runs (`int counter=7`); three real projects build under `--legacy-c -std=c11`, 12/13 runs byte-identical (the one difference is a time-seeded 2048 stdin board). `c1x`/`gnu11` real-project cells are **NOT TESTED** in the cross-check except `gnu11` (Matrix B 3/3). Verbose logs confirm user `-std=` is appended last and wins over the driver's hardcoded default (`-std=gnu17` Safe, `-std=gnu89` legacy). |
| **C17/C18**: `c17`, `c18`, `iso9899:2017`, `iso9899:2018`, `gnu17` | **PASS** (all five, minimal); real **PASS** with `c17` (3/3, 11/11 byte-identical) and `gnu17` (Matrix B 3/3) | clang and C+ both exit 0 with empty diagnostics for all five spellings; correct stdout. Negative controls (`c89` on C17 source, `bogus`) correctly rejected. `c18`/`iso9899:*` real-project cells are **NOT TESTED**. `gnu17` is a GNU superset, not strict ISO. |
| **C23 by its published name**: `c23`, `gnu23` | **FAIL (spelling)** — 0/0 builds | `error: invalid value 'c23' in '-std=c23'` from clang 16, forwarded verbatim by C+. The published standard name is not reachable on this toolchain. |
| **C23 draft mode**: `c2x`, `gnu2x` | **PASS** for minimal and real projects (legacy); **PARTIAL** for C23 feature coverage | Minimal C23-feature program builds/runs byte-identical to clang; real projects 6/6 scenarios byte-identical under `--legacy-c` (Safe mode 6/6 rejections). Feature matrix only 3/6 clean C+ passes: `bool_true`, `binary_literal`, `typeof_operator` pass; `nullptr` and `_BitInt` are rejected with `[CPLUS-FFI]`; digit separators hang (>10 s, rc=124). clang reports `__STDC_VERSION__ == 202000L` (working-draft value), not the final ISO C23 value. |
| **C2y / C26** (draft only): `c2y`, `c26`, `gnu2y`, `c2y_draft`, `c2b`, `iso9899:2024` | **UNRECOGNIZED** — 0/0 | All rejected by clang 16 (`error: invalid value …`); no flag exists and no published standard under these names. Any C2y/C26 behaviour claim would be fabricated. |
| **"C21"** | **NOT APPLICABLE** | No published ISO C standard and no compiler flag by this name. |

**Cross-cutting caveats recorded by the aggregate report** (not C-standard defects):
the driver does not pass `-pedantic-errors` by default; the C+ rewriter drops the final
newline of sources ending in a single `\n`, so an *extra* `-pedantic-errors` can produce
`-Wnewline-eof` even for valid code (the prescribed command form is unaffected); and the
default Safe mode rejects all three unmodified legacy projects before `-std` becomes
decisive. **Real-project cross-check totals:** Matrix A (default Safe mode) 0/33 compiled;
Matrix B (`--legacy-c`) 27/33 compiled and matched, 6/33 failed (pre-C99 `for(int i…)`
in snake/xdump under `c89`/`c90`/`gnu89`). Combined 27/66 compiled and matched.

**Explicit limitation:** "PASS" means the observed spelling compiled and ran the tested
source on this host; it does not mean the C+ compiler implements every feature of that
ISO C edition. The aggregate bottom line is explicit: the claim "C+ supports every
version of C ever released" is **not supported** — C23-by-published-name fails at the
backend and C23 draft feature coverage is partial. Unknown spellings (e.g.
`-std=bogus`) are rejected, proving no silent normalization.

---

## 4. Conversion tools that are available — and their honest limits

### 4.1 CLI: `tools/c2modern/convert.sh`

- Usage: `tools/c2modern/convert.sh INPUT.c OUTPUT.cp` (writes `OUTPUT.mapping` beside it).
  POSIX `sh`, no Python; env overrides `C2MODERN_ROOT`, `CPLUS_DRIVER`, `CC`.
- What it actually does: (1) verbatim copy of the input after an additive preamble;
  (2) prepends `#include "stdlib/error.hp"` (skipped if already present); (3) emits an
  **advisory** `.mapping` report of ordinary→checked `_s` calls with explicit/best-effort
  bounds; (4) verifies with `./build/c+ build OUTPUT.cp --strict --sanitize=none -I.` and
  a `timeout 30` run; (5) reports line counts, mapped sites, build/run results.
- Verified on the shipped fixture: build PASS (rc 0), run PASS (rc 0), output
  `c2modern-ok 32` / `CHECKSUM=41`; original compiles/runs identically with plain clang
  and with the C+ driver; `.cp` is byte-identical to the `.c` after the 18-line preamble.
- **Honest limits** (`tools/c2modern/README.md`): **conservative and additive, not a
  full translator**. It does not translate pointer-heavy C into provably safe C+; it does
  not reason about aliasing, lifetimes, data flow, or C+ ownership/borrowing. The `_s`
  mappings are **reported, not applied** (`ADVISORY-NOT-APPLIED`): applying them requires
  the hardened CRT include/link flags, a `cplus_log` macro collision fix, and the two
  archives — a manual integrator step. Mapping can false-positive on strings/comments;
  bound inference is best-effort and `memcmp` would need a second bound. Pointers stay
  one word; no struct layout, signature, or exported symbol changes.
- Note: `c+ --emit-c` lowers C+ *to C* — the opposite direction; it is not a C→C+ rewriter.

### 4.2 GUI: `tools/c2modern_gui/converter_gui.cp`

- A **headless teaching GUI** (Nuklear via `stdlib/window.hp`, `stdlib/gui.hp`,
  `stdlib/file.hp`), built with `build/c+` strict and `SDL_VIDEODRIVER=dummy`.
  Harness: `sh tools/c2modern_gui/run.sh` → `C2GUI 23 7 0 16`
  (23 scenarios, 7 passed, 0 failed, 16 expected refusals), all deterministic and with
  empty stderr; refusals carry stable categories/codes.
- **Honest limits** (`tools/c2modern_gui/NOTES.md`): **not a production IDE**. A textual
  C→modern-C rewrite cannot prove behaviour preservation; every report is advisory and
  must be independently built, run, and diffed. Crucially, `gui_convert()` **models** its
  report deterministically — it marks `status=PENDING`, `build=NOT_RUN`, `run=NOT_RUN`
  and does **not** invoke `cplus_system()` or the real CLI converter. The NOTES were
  written while the CLI converter was still landing; the CLI has since landed and
  verifies PASS on its fixture, but the GUI has not been rewired to call it. Rewiring is
  an integrator-side change that would link `build/libc/libcplus_hardened.a`.

---

## 5. Explicit non-claims

1. **No claim of universal C support.** Only the versions/spellings listed in §3, on this
   host, with the tested sources. No claim that every ISO C feature of any edition is
   implemented: C23 coverage is explicitly **PARTIAL** (`c2x`/`gnu2x` only, with
   `nullptr`/`_BitInt` rejected and digit separators hanging the frontend), and C2y/C26
   is a draft that cannot be tested on this toolchain.
2. **No "C21" standard exists.** C2y is the working name for the post-C23 revision whose
   intended published name is C26, and it remains a **draft**. Any "C21" claim is wrong
   and is not made here.
3. **No ABI change beyond additive symbols.** No exported symbol was removed, renamed, or
   signature/layout-changed; added symbols are internal/statically-linked helpers. The
   fingerprint diff is empty (§2). Callers of the original ordinary C ABI are unaffected.
4. **macOS arm64 / Apple clang 16 only.** Every executed verification here used that
   single toolchain. No cross-platform, cross-architecture, or other-compiler result is
   claimed.
5. **Original sources unchanged.** All bytes under
   `external_projects/{game_a,game_b,cli_app}/original/` are unchanged and their
   SHA-256 values were recomputed and matched in §1. No upstream file was edited,
   patched, or reformatted.
6. **No live-game or production claim.** These are terminal games and a CLI utility run
   under `timeout` with captured deterministic input; nothing here concerns the retail
   I.G.I. game or any injected DLL.
7. **No conversion tool is a proof of safety.** Both tools are conservative/advisory;
   the GUI models its result and does not invoke the real converter (§4).

---

## 6. PENDING items — **none**

All documents named in the task brief had landed and were read before this document was
finalized:

- `external_projects/{game_a,game_b,cli_app}/ORIGINAL_EVIDENCE.md` and each
  `cplus/CONVERSION.md` (§1).
- `docs/abi/EXTERNAL_PROJECTS_ABI.md` companion ABI record (§2).
- `tests/c_standards/RESULTS.md` (aggregate), `tests/c_standards/STD_SUPPORT_MATRIX.md`
  (aggregate), plus the seven per-directory `tests/c_standards/<group>/RESULTS.md`
  reports (§3).

The only qualifications that remain are honest evidence qualifications, not missing
artifacts: the transient, additive-only fingerprint drift recorded by the companion ABI
doc (resolved by a baseline refresh, §2); the C+ default Safe-mode rejection of the three
unmodified legacy C sources (a safety gate, not a C-version result, §3); and the GUI
converter's modelled `status=PENDING` report (its design limit, §4).
