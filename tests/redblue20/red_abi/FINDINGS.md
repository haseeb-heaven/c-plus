# RED20 red-team findings -- ABI / source integrity (agent 10/20)

Branch: `agent/modern-stdlib` @ `7edf2c6`.  Comparison base: `cbcea57`.
All commands were run from the repository root.  Full machine transcripts are
in `artifacts/`; rerun everything with `sh tests/redblue20/red_abi/run.sh`.

## Headline

```
RED20 abi 6 0 8
```

`checks=6  mismatches=0  modified_core_files=8`

Interpretation of the three numbers:

* **checks (6)** -- the four requested command families, plus the core-file
  git audit and the precompiled-library symbol experiment.
* **mismatches (0)** -- *no* ABI-fingerprint mismatch and *no* exported-symbol
  mismatch was found.  The two direct preservation claims survive the attack.
* **modified_core_files (8)** -- eight `M`-status C files under `libc/src/`
  (none under `runtime/`).  This is a genuine source-integrity change even
  though it does not move the ABI.

## The four requested command families

| # | Command | Result | classification |
|---|---------|--------|----------------|
| 1 | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | **PASS** -- `ABI fingerprint MATCHES baseline` (`diff is empty`), 1246 records | ABI preservation HOLDS; no mismatch |
| 2 | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_compat.sh` | **FAIL** -- `66 passed, 3 failed` (rc=1) | genuine failing check (harness defect) |
| 3 | `sh tests/abi/run_abi_layouts.sh` | **FAIL** (rc=1) | environmental only -- see below |
| 4 | `bash tests/abi/run_legacy_labs.sh` | **FAIL** -- `total=54 pass=52 fail=2` (rc=1) | genuine failing checks (harness defects) |

### Check 1 -- ABI fingerprint: PASS (no mismatch)

```
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1246 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
EXIT=0
```

`tools/abi_compare/.out/compare/diff.txt` is empty.  Layouts (`sizeof`,
`_Alignof`, `offsetof`, enum values), function signatures, compiler defines and
exported symbols from `libcplus_hardened.a` / `libcplus_safecrt.a` are all
byte-for-byte identical to `baseline.txt`.  The claim "the C ABI is preserved"
could not be broken.

### Check 2 -- ABI compat suite: 3 genuine failing assertions

```
PASS ... (66 checks incl. T1 pointers, T2 layouts, T3b, T5 symbol audits, T6 header neutrality)
FAIL T3a_c_driver_compile (rc=1)
FAIL T3a_c_driver_run (rc=127)
FAIL T3a_c_driver_summary (final summary line missing)
abi_compat: 66 passed, 3 failed
EXIT=1
```

Exact linker error (`build/abi/compat/results.log`, lines 37-56):

```
Undefined symbols for architecture arm64:
  "___cplus_checked_add_i64", referenced from:
      ___cplus_inline_add_i64 in libabi_compat_cplus.a[2](../../../proof/all_apps_games_screenshots/README.md)
  "_cplus_check_nonnull", referenced from:
      _AbiAdder_apply in libabi_compat_cplus.a[2](../../../proof/all_apps_games_screenshots/README.md)
      _record_ok in libabi_compat_cplus.a[2](../../../proof/all_apps_games_screenshots/README.md)
      _abi_cplus_meta_write in libabi_compat_cplus.a[2](../../../proof/all_apps_games_screenshots/README.md)
      ...
ld: symbol(s) not found for architecture arm64
```

Root cause: the T3a link line (`tests/abi/run_abi_compat.sh:132-134`) links the
boundary archive and `libcplus_hardened.a` but supplies no runtime that defines
`cplus_check_nonnull` (defined in `runtime/cplus_runtime.c:63`) or
`__cplus_checked_add_i64` (no definition anywhere in `compiler/`, `runtime/`,
`stdlib/` or any built `*.a`).  This is a defect in the newly-added test
harness, not a change to the shipped ABI.  All 22 expected libc symbols, 10
safecrt symbols and 5 compat symbols (T5) still PASS with no C++ mangling.

### Check 3 -- layout suite: failing, but environmental (not a source/ABI break)

```
clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk'
clang: warning: using sysroot for 'macOS 27' but targeting 'arm64-apple-macosx27.0.0'
libc/log/log.c:27:10: fatal error: 'errno.h' file not found
EXIT=1
```

`tests/abi/run_abi_layouts.sh` leaves `PATH` unrestricted, so the C+ driver
backend resolves Homebrew clang (`/opt/homebrew/opt/llvm/bin` is early in the
ambient `PATH`) whose default sysroot `MacOSX27.sdk` does not exist on this
host (only `MacOSX.sdk -> MacOSX15.2.sdk` is installed; `xcrun` agrees).
Re-running the same script with the auditable restricted PATH from check 2:

```
env PATH=/usr/bin:/bin CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh
clang: warning: ... 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```

So the layout claim itself HOLDS (70/70); the raw failure is a toolchain/PATH
issue, not an ABI or source violation.

### Check 4 -- legacy labs: 2 genuine failing labs

```
status: total=54 pass=52 fail=2 build_failure=0 timeout=0
FAIL  c_to_cp                exit=1
FAIL  cp_to_c/cp_to_c        exit=1
```

Note: `required_failures=2` in the runner output is merely the runner's count
of labs that exited non-zero (it means "failures needing attention", not
"expected failures"); the runner still exits 1.

* `c_to_cp` (`build/abi-labs/logs/c_to_cp.stdout`): 22 passed, 2 failed --
  `FAIL Cplus_probe_link (rc=1)` and `FAIL Cplus_build (rc=1)`.
  `tests/abi/c_to_cp/artifacts/results.log` shows the C+ driver rejecting the
  pass-through flags in strict mode:
  `error: pass-through flag outside the STRICT Safe allow-list ... -Wno-* are rejected`
  (lines 22 and 25).  All 19 library symbols and `no_cxx_mangling` PASS.
* `cp_to_c` (`build/abi-labs/logs/cp_to_c_cp_to_c.stdout`): 34 passed, 3 failed
  -- `T2_c_driver_link`, `T3_driver_run`, `T3_driver_summary`.
  `tests/abi/cp_to_c/artifacts/results.log:24-41` shows the same undefined
  `__cplus_checked_add_i64` / `cplus_check_nonnull` symbols as check 2.
  All 20 `T4_exact_symbols`, lowering checks and `T6_file_object` PASS.

Again these are harness/toolchain-link defects; the ABI-visible symbols are all
present and unmangled.

## Genuine finding: 8 modified core C files

`git diff --name-status cbcea57..HEAD -- libc/src/ runtime/` reports these
`M` entries (and **no** `runtime/` changes):

| file | +/- | nature of change (signature preserved in every case) |
|------|-----|--------------------------------------------------------|
| `libc/src/math/math3.c` | 1/1 | `cplus_nan`: `nan(tagp)` -> `nan(tagp ? tagp : "")` (NULL guard) |
| `libc/src/math/math_float.c` | 1/1 | `cplus_nanf`: same NULL guard |
| `libc/src/math/math_ldouble.c` | 1/1 | `cplus_nanl`: same NULL guard |
| `libc/src/stdlib/stdlib_extra5.c` | 1/0 | added `#define _XOPEN_SOURCE 700` |
| `libc/src/stdlib/stdlib_extra6.c` | 25/3 | `_GNU_SOURCE`, NULL guards, `__GLIBC__` `qsort_r` trampoline |
| `libc/src/string/string_extra5.c` | 2/0 | `_XOPEN_SOURCE`, NULL/length guard in `cplus_swab` |
| `libc/src/string/string_extra6.c` | 36/3 | reimplemented `cplus_strlcpy`/`strlcat`/`strnstr` with `strlen`/`memcpy`/`memcmp` instead of delegating to host |
| `libc/src/time/time_extra.c` | 10/0 | feature-test macro guards for non-Apple builds |

Exact diff is in `artifacts/core_modified.diff`.  These are real changes to
core source bodies, so any claim of the form "no core C source changed" is
false.  They do **not** alter any function signature or exported symbol, which
is consistent with the ABI fingerprint MATCH in check 1.  Note also that the
many `A` (added) core files (C23 math, safecrt_vendor, alloc_hardened, ...)
are additive;  `string_extra6.c` is the most behaviour-invasive change because
it replaces host `strlcpy`/`strlcat`/`strnstr` delegation with local logic.

## Attack that did NOT break anything: precompiled library + safe_c.h

I compiled a fresh plain-C library (`red_probe_add`, `red_probe_copy`,
`red_probe_alloc`) once with `/usr/bin/clang`, then linked one caller against
that same `.a` twice: plain, and with `-include stdlib/safe/safe_c.h` plus the
caller-side support archives.  `nm -gU` on both executables:

```
plain-link:  0000000100003ed4 T _red_probe_add
             0000000100003edc T _red_probe_copy
             0000000100003ef0 T _red_probe_alloc
safe-link:   0000000100000cd8 T _red_probe_add
             0000000100000ce0 T _red_probe_copy
             0000000100000cf4 T _red_probe_alloc
```

After stripping addresses, the symbol name/type sets are byte-identical
(`artifacts/precomp/syms_*.txt`); both programs print `r=5 n=2 p=1`.  So
`safe_c.h` is confirmed to be a caller-side macro mapping, not an ABI change to
a precompiled C library.  No symbol mismatch here.

## Bottom line for the parent agent

* The **ABI-preservation claim survives**: 0 fingerprint mismatches, 0 symbol
  mismatches, 70/70 layouts, all expected unmangled symbols present.
* The **source-unchanged claim is false** if made absolutely: 8 core C files
  were modified (behavioural hardening / portability), and string_extra6.c in
  particular changes runtime behaviour of three functions.
* The **ABI verification suite is partly broken** on this host: 3 assertions in
  `run_abi_compat.sh` and 2 whole labs in `run_legacy_labs.sh` fail because
  runtime-helper symbols are never linked and strict pass-through flags are
  rejected; `run_abi_layouts.sh` fails only because of an ambient Homebrew
  sysroot and passes with `/usr/bin` first in PATH.  These are test-harness
  defects, not evidence that the shipped ABI changed.

All artifacts are under `tests/redblue20/red_abi/artifacts/`.  No source,
header or test outside this directory was modified; no git add/commit was run.
