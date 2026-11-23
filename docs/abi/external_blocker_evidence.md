# External compiler blocker — final ABI evidence (post-integration)

**Branch:** `agent/modern-stdlib`
**Final capture (UTC):** `2026-11-23T04:33:33Z` (local `2026-11-23 09:59:40`, UTC+5:30)
**Host:** Darwin / arm64, macOS 27.2.1 (build 26B131); Apple clang 16.0.0 (`/usr/bin/clang`)
**HEAD at final capture:** `2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f`
**Compiler under test:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+`
  - `C+ 0.1.0`, `Backend: host Clang/LLVM`
  - SHA-256: `b8b4be0e95533f4ea91b2756ded2a50d12192b3cbf622d761ab4eed92fa111a4`
    (unchanged from the earlier Task 4 capture — the driver was **not** rebuilt by
    the upstream integration)

This document has two layers:

1. **Section 1–2 — final, post-integration ABI verification.** Four required
   commands were run **serially, one at a time, each wrapped in an external
   `/usr/bin/timeout 600` bound**. No result was retried until green, no output
   was masked, and no exit code was rewritten. The numbers below are the literal
   final lines and the literal shell exit codes observed.
2. **Section 3–5 — the external `CPLUS_EXIT_ARITHMETIC` blocker**, carried
   forward from the earlier Task 4 capture. It remains an **unresolved external
   blocker** and is outside this repository's ownership. None of the four final
   ABI commands exercises the instrumented-runtime path that triggers it.

---

## 1. Final post-integration ABI results (exact final lines and exit codes)

All four commands exited **0**. Commands were executed in this exact order,
serially, from the repository root with a 600 s per-command timeout:

| # | Command (serialized) | Exit code | Exact final line(s) |
|---|----------------------|-----------|---------------------|
| 1 | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | `0` | `abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).` / `abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt` |
| 2 | `sh tests/abi/run_abi_compat.sh` | `0` | `abi_compat: 69 passed, 0 failed` |
| 3 | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` | `0` | `abi_layouts: 70/70 layouts match` |
| 4 | `bash tests/abi/run_legacy_labs.sh` | `0` | `run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0` / `required_failures=0  summary=…/build/abi-labs/summary.json  logs=…/build/abi-labs/logs` |

Notes on the exact output:

- **Command 1** captured `1162 records` (the refreshed baseline now contains
  1162 records; the older `RESULTS.md` figure of 1160 predates the two added
  symbols). The diff file was empty — a clean **MATCH**.
- **Command 2** printed 69 `PASS …` lines (prerequisite libc/safecrt builds,
  T1 pointers, T2 layouts, T3a/T3b both C↔C+ directions, T5 symbol and
  no-C++-mangling checks, T6 header neutrality) then the summary line.
- **Command 3** emitted two benign clang warnings about linker inputs being
  unused (`-Wunused-command-line-argument`) from the compile-only step, then
  `abi_layouts: 70/70 layouts match`. Pinning `CPLUS_CLANG=/usr/bin/clang`
  avoids the broken-default-SDK toolchain state described in §3.4; this is now
  the official command-3 result, not merely a diagnostic probe.
- **Command 4** began with
  `unified ABI lab runner: branch=agent/modern-stdlib started=2026-11-23T04:32:48Z`
  and `discovered=54 selected=54  build_timeout=300s run_timeout=120s`. All 54
  labs passed: 50 `legacy_c/lc01`–`lc50`, plus `c_to_cp`, `cp_to_c/cp_to_c`,
  `precompiled/precompiled_lab` and `abi_compare`. 0 failures, 0 build
  failures, 0 timeouts, 0 skipped, 0 blocked, `required_failures=0`.

---

## 2. Fingerprint baseline refresh — additive-only verification

The committed baseline `tests/abi/fingerprints/baseline.txt` was refreshed
after the upstream integration added two **additive** safeclib symbols. The
diff of the refreshed baseline against the prior baseline
(`git show 960c997:tests/abi/fingerprints/baseline.txt`) is **exactly**:

```diff
-S      archive build/libc/libcplus_hardened.a       count=782
+S      archive build/libc/libcplus_hardened.a       count=784
+S      symbol build/libc/libcplus_hardened.a::_cplus_sc_upstream_bsearch_s  1
+S      symbol build/libc/libcplus_hardened.a::_cplus_sc_upstream_memrchr_s  1
```

(Fields are TAB-separated in the actual file; rendered here with spacing for
readability.)

Verified properties of that diff:

- Prior baseline: `1163` lines; refreshed baseline: `1165` lines.
- Record count: `1160` → `1162` (exactly **+2**).
- The **only** changed records are one `S archive … count=` line (782 → 784)
  and two **new** `S symbol` records. Both new symbols are additive exports
  (`_cplus_sc_upstream_bsearch_s`, `_cplus_sc_upstream_memrchr_s`).
- **No** `L` (layout) record, **no** `F` (function-signature) record, **no** `V`
  (metadata) record and **no** `C` (compiler-define) record changed. There are
  no removed or renamed symbols and no size/align/offset/enum-value drift.
- A fresh capture compared against the refreshed baseline is a clean **MATCH**
  (command 1, exit 0, empty diff).

**Conclusion:** there are **no ABI layout or signature changes**. The only
baseline drift is the two added symbol records and their archive count. Add
a symbol without changing any existing type layout, enum value or function
signature is ABI-compatible (it cannot break an existing caller).

---

## 3. The exact external diagnostic (carried forward, unresolved)

Verbatim, as emitted into `tests/out/<case>.build.log` for **all 51** failing
cases of the earlier Task 4 `run_tests.sh` capture (e.g.
`tests/out/alloc_overflow.build.log`, mtime `2026-11-23 09:44:59` local):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

Relative-path tracking form:

```
c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
```

### Why this is external, not a change in this repo

- The failing source lives in a **different git repository**:
  `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core` (its `rev-parse
  --show-toplevel` returns that path), *not* this `c-plus-stdlib` checkout.
  The external driver's "compile instrumented C+ runtime" step and its error
  string live in `c-plus-core/compiler/Driver/Driver.cpp`.
- The external repo's header now declares the enumerator
  (`runtime/include/cplus/cplus.h:85` → `CPLUS_EXIT_ARITHMETIC = 110`), but this
  repository's `runtime/include/cplus/cplus.h` still ends its `cplus_exit_code`
  enum at `CPLUS_EXIT_IO = 109` and does **not** define `CPLUS_EXIT_ARITHMETIC`.
  The instrumented-runtime compile nevertheless fails in the external source
  path; this mismatch is owned by `c-plus-core`, not by `compiler/`, `runtime/`,
  headers, or tests in this branch.
- The failure is deterministic and occurs only on the **instrumented** runtime
  path. Every green result in §1 uses the already-built runtime and the
  non-instrumented path: `run_abi_compat.sh` c+ builds use `--sanitize=none`;
  `run_abi_layouts.sh` and `run_legacy_labs.sh` likewise avoid rebuilding the
  instrumented runtime.
- **No fresh instrumented-path run was performed in this final capture.** The
  four required post-integration commands do not exercise it, so the blocker
  status is carried forward from the earlier Task 4 evidence (51 build logs)
  rather than re-measured here. It is stated as an unresolved external blocker
  and is explicitly out of scope for this ABI evidence task.

---

## 4. Scope and honesty statements

- **ABI results are valid only for the exact current binary and toolchain.**
  Every §1 result came from `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+`
  with SHA-256 `b8b4be0e95533f4ea91b2756ded2a50d12192b3cbf622d761ab4eed92fa111a4`,
  Apple clang 16.0.0, `arch=arm64 os=Darwin`. They say nothing about a rebuilt
  compiler, a different SDK/clang, or any future binary.
- **Platform scope:** macOS **arm64 only**. macOS x86_64, Linux (glibc/musl,
  any arch) and Windows (MSVC/UCRT) are **UNTESTED**. No cross-platform ABI
  claim is made.
- **No remote CI result is claimed.** Everything here comes from local,
  serialized execution in this workspace.
- **No fabricated green results.** The four §1 commands are the exact lines
  actually observed. The earlier Task 4 non-zero exits (run_tests.sh,
  run_deep_tests.sh, and the original unpinned run_abi_layouts.sh) remain
  honestly reported as failures in §3/history; command 3's green result here
  is with the explicit `CPLUS_CLANG=/usr/bin/clang` pin.
- **File ownership:** this task created/modified only
  `docs/abi/external_blocker_evidence.md` and `docs/abi/compatibility-matrix.md`.
  No compiler, runtime, header, source or test file was edited; no git
  add/commit was performed.

---

## 5. Bottom line

After the upstream integration, all four required ABI commands are green on
macOS arm64: fingerprint **MATCH** (exit 0), `abi_compat: 69 passed, 0 failed`,
`abi_layouts: 70/70 layouts match`, and `run_legacy_labs: total=54 pass=54
fail=0 …`. The refreshed fingerprint baseline differs from the prior baseline
**only by two additive safeclib symbols** and the archive count; there are
**no ABI layout or signature changes**. These results hold only on macOS
arm64 with the stated driver/toolchain; Windows and Linux are UNTESTED.

The external `c-plus-core` instrumented-runtime compile error
(`c-plus-core/runtime/cplus_runtime.c:129:23: … 'CPLUS_EXIT_ARITHMETIC'`) is an
**unresolved external blocker** outside this repository's ownership. It does
not affect the four non-instrumented ABI paths reported green above, but it
must be fixed upstream before a full instrumented/default/UBSan green run can
be claimed.
