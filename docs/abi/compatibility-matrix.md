# C+ ABI compatibility matrix

> **Snapshot, not a live dashboard.** This document records only evidence that
> was present in the working tree when it was captured. Every cell below was
> produced by a sibling verification run on disk at capture time; no cell is a
> prediction, and no result is generalized beyond the "TESTED" platform row.

## Snapshot metadata

| item | value |
| --- | --- |
| captured (UTC) | `2026-11-23T04:33:33Z` (local `2026-11-23 09:59:40`, UTC+5:30) |
| repo | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` |
| branch | `agent/modern-stdlib` |
| HEAD | `2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f` |
| phase | final evidence **after upstream integrations** (`ce018ca`, `2294cb1`) |
| author role | documentation only (docs/abi); no compiler, runtime, header or test edits |
| driver | `./build/c+` → `C+ 0.1.0`, SHA-256 `b8b4be0e95533f4ea91b2756ded2a50d12192b3cbf622d761ab4eed92fa111a4` (unchanged from prior capture) |

---

## 1. Exact platform tested

All evidence in this matrix was produced on **one** machine and toolchain.

| item | exact value |
| --- | --- |
| OS / arch | macOS, `arm64` (`uname -s` = `Darwin`, `uname -m` = `arm64`) |
| clang version | Apple clang version 16.0.0 (`clang-1600.0.26.6`) |
| clang target | `arm64-apple-darwin27.2.0`, thread model `posix` |
| cc1 target / SDK | `arm64-apple-macosx15.2.0`, SDK `MacOSX.sdk`, `-target-cpu apple-m1` |
| InstalledDir | `/Library/Developer/CommandLineTools/usr/bin` |
| resource dir | `/Library/Developer/CommandLineTools/usr/lib/clang/16` |
| C+ driver | `./build/c+` → `C+ 0.1.0`, "Backend: host Clang/LLVM" |
| system tools | `/usr/bin/clang`, `/usr/bin/ar`, `/usr/bin/nm`, `/usr/bin/diff`, `/usr/bin/timeout` |
| runner environment | `PATH=/usr/bin:/bin`, `CPLUS_CLANG=/usr/bin/clang` where the script honours it |
| step timeouts | external `/usr/bin/timeout 600` per command; lab build 300 s / run 120 s |

## 2. Platform scope

| platform | status | note |
| --- | --- | --- |
| macOS arm64 (Apple clang 16, C+ 0.1.0) | **TESTED** | the only platform with evidence in this snapshot |
| macOS x86_64 | **UNTESTED** | no evidence |
| Linux (glibc/musl, any arch) | **UNTESTED** | no evidence |
| Windows (MSVC / UCRT, x86 or x64) | **UNTESTED** | no evidence; the repo's retail-x86 DLL guide is out of scope |
| other C++ compilers / C++ ABI | **UNTESTED / out of scope** | no mangled-C++ ABI is exercised (see §8) |

No result in this matrix may be generalized beyond the "TESTED" row.

---

## 3. Final post-integration verification commands

The four required commands were run **serially**, each bounded by an external
`/usr/bin/timeout 600`, from the repository root. No retry-until-green, no
output masking, no rewritten exit codes.

| # | Command | Exit code | Exact final line(s) |
| --- | --- | --- | --- |
| 1 | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | `0` | `abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).` / `abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt` |
| 2 | `sh tests/abi/run_abi_compat.sh` | `0` | `abi_compat: 69 passed, 0 failed` |
| 3 | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` | `0` | `abi_layouts: 70/70 layouts match` |
| 4 | `bash tests/abi/run_legacy_labs.sh` | `0` | `run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0` / `required_failures=0  summary=…/build/abi-labs/summary.json  logs=…/build/abi-labs/logs` |

No layout, signature, enum, calling-convention or exported-symbol *name*
changed by this work; the only baseline drift is two **additive** symbol
exports (see §5.6).

---

## 4. Master matrix (by directory)

Denominators are stated as `passed/total`; timeouts are `timed-out/invocations`.
"Runner steps" counts compile/link/run/check operations of the shell runners,
not the program-internal `ck()` assertions (the two are different scales).

| Directory | What it checks | Result (denominators) | Status | Evidence |
| --- | --- | --- | --- | --- |
| `tests/abi/fingerprints/` | deterministic C-ABI fingerprint (layout `L`, signature `F`, compiler `C`, symbol `S`, metadata `V`) vs committed baseline | **MATCH**, exit 0, empty diff; 1162 records | **COMPLETE** | command 1; refreshed baseline vs prior: additive-only (§5.6) |
| `tests/abi/` (baseline suite) | pointer representation, public C layouts, both C↔C+ directions, exported symbol names, header neutrality | **69/69 passed, 0/69 failed, 0/69 timeouts** (runner steps) | **COMPLETE** | command 2; `abi_compat: 69 passed, 0 failed` |
| `tests/abi/abi_layouts` | host-C vs C+-driver public-type layout agreement | **70/70 layouts match**, exit 0 | **COMPLETE** | command 3 |
| `tests/abi/legacy_c/` | unchanged ordinary C: primitives, pointers, arrays, structs/unions/bitfields/enums, function pointers, callbacks, TLS, opaque handles, stdio/stdlib interop | **50/50 passed** (`lc01`–`lc50`), 0 fail/timeout | **COMPLETE** | command 4 (`run_legacy_labs`) |
| `tests/abi/c_to_cp/` | C+ caller links and calls a precompiled plain-C archive | **PASS**, exit 0 | **COMPLETE** | command 4 (`c_to_cp`) |
| `tests/abi/cp_to_c/` | C+ constructs lowered to plain C symbols called from C | **PASS**, exit 0 | **COMPLETE** | command 4 (`cp_to_c/cp_to_c`) |
| `tests/abi/precompiled/` | saved C library (`.a`) usable by a rebuilt C+ app | **PASS**, exit 0 | **COMPLETE** | command 4 (`precompiled/precompiled_lab`) |
| aggregated labs | unified runner over all of the above plus fingerprint compare | **54/54 passed, 0 fail, 0 build-failure, 0 timeout, 0 skipped, 0 blocked; `required_failures=0`** | **COMPLETE** | command 4; `build/abi-labs/summary.json` |

Legend: **COMPLETE** = authoritative runner result produced at capture. All
cells are populated for this snapshot.

---

## 5. Per-directory detail

### 5.1 Fingerprint compare — `tools/abi_compare/` (COMPLETE)

Command 1 regenerated a fingerprint (1162 records) and diffed it against
`tests/abi/fingerprints/baseline.txt`: clean **MATCH**, empty diff, exit 0.
The probe checks, at compile time, twelve public function signatures via
`_Static_assert(__builtin_types_compatible_p(...))`, so a signature change
would be a hard compile failure; none occurred.

### 5.2 Baseline suite — `tests/abi/run_abi_compat.sh` (COMPLETE)

Restricted `PATH`, every command wrapped in `/usr/bin/timeout`, no `|| true`
masking. Headline: `abi_compat: 69 passed, 0 failed` — 69/69 runner steps,
0 failures, 0 timeouts. The 69 steps decompose as:

| group | runner steps |
| --- | --- |
| prerequisite library builds (libc, safecrt) | 2 |
| T1 pointer ABI | 3 |
| T2 layout constants | 3 |
| T3a C calls C+ | 5 |
| T3b C+ calls precompiled C | 6 |
| T5 symbols (2 `nm`, 39 symbol checks, 3 no-mangling checks) | 44 |
| T6 header neutrality | 6 |
| **total** | **69** |

Program-internal self-reported counts (different scale): T1 `20/20 pointer ABI
checks match`, T2 `80/80 layouts match`, T3a `13/13 C->C+ boundary checks
match`, T3b `10/10 C+->C boundary checks match`, T5 `39/39` audited symbol
names present with **no** ` T __Z` C++-mangled symbols, T6 `diff -u` empty
(37 layout lines each).

### 5.3 Layout agreement — `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` (COMPLETE)

Compiles and runs `tests/abi/abi_layouts.c` with both the host compiler and the
C+ driver and diffs the outputs. Exit 0, final line `abi_layouts: 70/70 layouts
match`. Two benign `-Wunused-command-line-argument` clang warnings arise from
passing archives to a compile-only step; they do not affect the result. The
`CPLUS_CLANG=/usr/bin/clang` pin is required to avoid the broken default-SDK
toolchain state documented in §7.

### 5.4 Legacy C and sibling labs — `bash tests/abi/run_legacy_labs.sh` (COMPLETE)

Unified runner: `discovered=54 selected=54  build_timeout=300s
run_timeout=120s`. All 54 labs PASS: `legacy_c/lc01_primitives` …
`legacy_c/lc50_state_machine`, `c_to_cp`, `cp_to_c/cp_to_c`,
`precompiled/precompiled_lab`, and `abi_compare`. 0 failures, 0 build
failures, 0 timeouts, 0 skipped, 0 blocked; `required_failures=0`.
Summary: `build/abi-labs/summary.json`; per-lab logs: `build/abi-labs/logs`.

### 5.5 What the labs cover (detail)

- `legacy_c/` — 50 ordinary-C probes: primitives, pointer basics and nested
  pointers, arrays/multidim, structs/unions/nested structs, bitfields, enums,
  function pointers, variadics, callbacks, flexible/packed arrays, alignment,
  arrays of structs, string structs, pointer arithmetic, struct returns,
  union bitfields, typedef fn pointers, `va_list`, nested flexible arrays,
  ABI-mixed records, globals/aggregates, TLS, opaque handles/state machines,
  stdio/stdlib interop, string and allocator interop, calloc zeroing, file
  handles/seek-tell, string buffers, exported functions/vtables, tagged unions,
  linked lists, state machines.
- `c_to_cp` — C+ caller links and calls a precompiled plain-C archive.
- `cp_to_c/cp_to_c` — C+ constructs lowered to plain C symbols called from C.
- `precompiled/precompiled_lab` — saved C library consumed by a rebuilt C+ app.
- `abi_compare` — fingerprint check from §5.1, re-run inside the unified lab.

### 5.6 Fingerprint baseline refresh — additive only

The committed baseline was refreshed after the upstream integration added two
additive safeclib symbols. Diff of refreshed baseline vs prior baseline
(`git show 960c997:tests/abi/fingerprints/baseline.txt`):

```diff
-S      archive build/libc/libcplus_hardened.a       count=782
+S      archive build/libc/libcplus_hardened.a       count=784
+S      symbol build/libc/libcplus_hardened.a::_cplus_sc_upstream_bsearch_s  1
+S      symbol build/libc/libcplus_hardened.a::_cplus_sc_upstream_memrchr_s  1
```

(TAB-separated in the actual file.) Prior `1163` lines / `1160` records →
refreshed `1165` lines / `1162` records. Only `S archive count` and two new
`S symbol` records changed: **no** `L` layout, **no** `F` signature, **no** `V`
metadata, **no** `C` compiler-define, no removed/renamed symbol, no size,
alignment, offset or enum drift. Therefore: **no ABI layout/signature changes**;
the only drift is additive exports.

---

## 6. External blocker (must be read with these results)

`./build/c+` is supplied externally, outside this branch's runtime. Instrumented
builds compile `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`,
which fails:

```
cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", ...
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

- The failing source is in the separate `c-plus-core` git repository, not this
  checkout. The external header declares `CPLUS_EXIT_ARITHMETIC = 110` at
  `runtime/include/cplus/cplus.h:85`, but the instrumented-runtime compile
  still fails in the external source path — an external compiler/runtime
  mismatch owned by `c-plus-core`.
- This branch's `runtime/include/cplus/cplus.h` ends its `cplus_exit_code` enum
  at `CPLUS_EXIT_IO = 109` and does not define `CPLUS_EXIT_ARITHMETIC`.
- Earlier Task 4 evidence: 51 `tests/out/*.build.log` files (mtime
  `2026-11-23 09:44:59` local) show this failure for default/UBSan modes of
  `run_tests.sh` / `run_deep_tests.sh`. It is **not** an ABI mismatch found by
  the probes in this matrix.
- All four final §3 commands use already-built runtime / non-instrumented
  (`--sanitize=none`) paths, so they are unaffected by this blocker. No fresh
  instrumented-path run was performed in this final capture; the blocker is
  carried forward as **unresolved and external**, explicitly out of scope for
  this ABI evidence task, and is not fixed here.

See [`external_blocker_evidence.md`](external_blocker_evidence.md) for the
full diagnostic and ownership analysis.

---

## 7. Toolchain-selection note (command 3)

An earlier unpinned run of `run_abi_layouts.sh` failed with
`fatal error: 'errno.h' file not found` because the c+ driver resolved a clang
with a broken default SDK (`MacOSX27.sdk` absent; only up to `MacOSX15.2.sdk`
installed). This is toolchain/sysroot selection state, **not** an ABI issue and
not the `CPLUS_EXIT_ARITHMETIC` problem. Pinning `CPLUS_CLANG=/usr/bin/clang`
(as in the required command 3) selects the working SDK and yields the official
`abi_layouts: 70/70 layouts match` result above.

---

## 8. Explicit non-claims

This matrix does **not** claim any of the following:

1. **No cross-platform proof.** Only macOS arm64 / Apple clang 16 / C+ 0.1.0
   was exercised. Linux and Windows (MSVC/UCRT), x86_64 and x86 are UNTESTED;
   nothing here predicts their layouts, calling conventions or symbol behaviour.
2. **No C++ mangling claim.** Checks exercise only C-linkage/plain-C symbols.
   The only related observation is negative: `nm -gU` found no ` T __Z`
   C++-mangled symbols in the audited archives. No Itanium/MSVC mangled C++ ABI,
   cross-compiler class layout, or C++ exception ABI was tested or is claimed
   stable.
3. **No ABI layout or signature changes.** This work produced probes, a
   refreshed fingerprint baseline and documentation only. No public header,
   struct layout, enum value, calling convention or exported-symbol *name* was
   changed. The fingerprint baseline drift is **two additive symbol exports**
   (§5.6); no layout, signature, enum or metadata record changed. The results
   say the checked subsets matched their expectations on this toolchain — not
   that future edits cannot break the ABI.
4. **No blanket safety claim.** Passing ABI checks say nothing about runtime
   memory safety, arbitrary raw pointers, uninstrumented precompiled libraries,
   compiler diagnostics, or the unresolved `CPLUS_EXIT_ARITHMETIC` blocker (§6).
5. **No future-binary claim.** Results apply only to driver SHA-256
   `b8b4be0e…` on the stated toolchain; they say nothing about a rebuilt
   compiler, a different SDK/clang or remote CI (none was used).

---

## 9. Evidence index

| artifact | role |
| --- | --- |
| command 1 (`tools/abi_compare/run.sh compare …`) | fingerprint MATCH, empty diff, exit 0 |
| command 2 (`tests/abi/run_abi_compat.sh`) | `abi_compat: 69 passed, 0 failed` |
| command 3 (`CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh`) | `abi_layouts: 70/70 layouts match` |
| command 4 (`bash tests/abi/run_legacy_labs.sh`) | 54/54 labs pass, 0 fail/timeout |
| `build/abi-labs/summary.json`, `build/abi-labs/logs/` | machine-readable lab summary and per-lab logs |
| `build/abi/compat/results.log` | command/exit transcript for the 69/69 baseline suite |
| `tests/abi/ABI_COMPAT_NOTES.md` | authoritative notes for the baseline suite |
| `tests/abi/fingerprints/baseline.txt` / `baseline.json` | refreshed committed fingerprint baseline |
| `tests/abi/fingerprints/RESULTS.md` | fingerprint capture/negative-test history (counts predate the 2 added symbols) |
| `tests/out/*.build.log` (51 files) | external `CPLUS_EXIT_ARITHMETIC` blocker evidence |
| [`external_blocker_evidence.md`](external_blocker_evidence.md) | full external blocker diagnostic and final ABI results |
| [`source_audit.md`](source_audit.md) | sibling source-level header-change audit; concludes no consumer-visible `ABI_CHANGE` |
