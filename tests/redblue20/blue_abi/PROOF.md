# BLUE-TEAM 20/20 — ABI / source-integrity proof

Branch: `agent/modern-stdlib` @ `7edf2c6`. Baseline: `cbcea57`.
Host: arm64-apple-darwin, Apple clang 16.0.0, `CPLUS_CLANG=/usr/bin/clang`.
**Strict ownership:** only `tests/redblue20/blue_abi/` and
`docs/security/RED_BLUE_20_FINAL.md` were written. No source/header edit, no
`git add`, no commit. Reproduce everything with:

```sh
sh tests/redblue20/blue_abi/run.sh
```

## Headline

```
BLUE20 abi 7 7 0
BLUE20 abi-raw 7 5 2
```

`BLUE20 abi 7 7 0` = seven required integrity properties, each **proven PASS**
(using documented drift-neutralized harness copies for two of them).
`BLUE20 abi-raw 7 5 2` = the unmodified official runners: two of them exit
nonzero at this HEAD for **toolchain-drift reasons**, not because the ABI
changed. Both lines are reported; the difference is fully explained below and
is classified OPEN in the aggregate document — nothing is papered over.

## Exact results table

| # | Check | Command | Raw result | Proven result |
|---|---|---|---|---|
| 1 | Core C files unchanged vs `cbcea57` | `sh tests/redblue20/blue_abi/check_core_c.sh` | 7/7 UNCHANGED, rc=0 | PASS |
| 2 | ABI fingerprint compare | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | MATCH, empty diff, 1246 records, rc=0 | PASS |
| 3 | ABI layouts | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` | `abi_layouts: 70/70 layouts match`, host-vs-C+ diff empty, rc=0 | PASS |
| 4 | Ordinary C ABI | `abi_probe.c` built with `/usr/bin/clang`, linked against both archives | `abi_probe: 32 passed, 0 failed`, rc=0 | PASS |
| 5 | `abi_compat` | `sh tests/abi/run_abi_compat.sh` (official) | `66 passed, 3 failed`, rc=1 | **69 passed, 0 failed** with drift-neutralized copy |
| 6 | Legacy labs | `bash tests/abi/run_legacy_labs.sh` (official) | `total=54 pass=52 fail=2`, rc=1 | both failing labs pass in drift-neutralized copies (24/0 and 37/0) |
| 7 | Precompiled C library with/without `safe_c.h` | `sh tests/abi/precompiled_safe_layer/run_safe_layer_lab.sh` | `safe_layer_lab: PASS`, rc=0 | PASS |

Raw logs: `out/run_all.stdout.log`, `out/core_c_check.log`,
`out/fingerprint_compare.log`, `out/abi_layouts.log`,
`out/abi_layouts_diff.log`, `out/abi_probe.stdout.log`,
`out/abi_compat_official.log`, `out/abi_compat_corrected.stdout.log`,
`out/legacy_labs.log`, `out/c_to_cp_corrected.stdout.log`,
`out/cp_to_c_corrected.stdout.log`, `out/precompiled_safe_layer.log`.

## 1. Core C source integrity vs `cbcea57` — PASS (7/7)

Canonical core files (the same seven used by `tests/abi/stdc_source/run.sh`),
git blob id equal at `cbcea57` and `HEAD`, empty `git diff`, and no uncommitted
modification:

| file | blob (identical at both revisions) |
|---|---|
| `libc/src/string/memory.c` | `46de2624d29cfc15848d8d4d624b145192365148` |
| `libc/src/string/string.c` | `104accdec51d92dc88066b7e5271f7c3d451a574` |
| `libc/src/string/extra.c` | `4f98cca100bc503ebb8f93ba9464dbe61c75cf59` |
| `libc/src/string/bounded.c` | `231e7a9928efc992a0ca0c2666d2a468de68f281` |
| `libc/src/stdlib/alloc.c` | `cd2c44687e565ab6fec18520a4b985a4660f69fd` |
| `libc/src/stdio/stdio.c` | `830b32f212db11bcdab21f24509005157d84188f` |
| `runtime/cplus_runtime.c` | `c6a921d5c306a89594b667057c1d4a62a5d3cfab` |

**Honest disclosures (not hidden):**

* The stronger historical claim "every `libc/src/` and
  `libc/install-include/` change since `cbcea57` is an addition" is **no longer
  true at this HEAD**: `git diff --name-status cbcea57..HEAD` shows 8 `M` files
  in `libc/src/` and 1 `M` header (`libc/install-include/cplus_libc.h`). These
  are committed portability/hardening fixes (NULL guards, `_XOPEN_SOURCE`,
  local `strlcpy`/`strlcat`/`strnstr`) from commits after the original audit —
  none is an uncommitted battle edit, and none changes an exported signature
  (the fingerprint still matches). The red-abi team independently reported the
  same 8 files.
* The working tree also contained uncommitted edits to **other** stdlib files
  being made concurrently by sibling agents in response to RED20 findings:
  initially four (`stdlib/error.hp`, `stdlib/safe/safe_io.h`,
  `stdlib/safe/src/safe_format.c`, `stdlib/safe/src/safe_memory.c`), later also
  `stdlib/arena.hp`, `stdlib/datastack.hp` and `stdlib/mempool.hp`. **None of the
  seven canonical core C files was among them**, and a final re-check after
  those edits still reports `7/7 UNCHANGED` and an empty fingerprint diff. This
  proof does not claim the whole tree was frozen; it claims the seven
  ABI-critical core files were untouched by the battle.

## 2. ABI fingerprint — PASS

```
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1246 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
exit=0
```

Covers sizeof/_Alignof/offsetof/enum values, 12 compile-time function
signatures, compiler ABI defines, and `nm -gU` exported symbols from
`libcplus_hardened.a` and `libcplus_safecrt.a`. This is a change detector; a
match means no ABI surface drift, not a claim of universal correctness.

## 3. ABI layouts — PASS (70/70)

With the auditable toolchain (`CPLUS_CLANG=/usr/bin/clang`):
`abi_layouts: 70/70 layouts match` and `diff -u build/abi/host.out
build/abi/cplus.out` empty. (Without that env var the ambient Homebrew clang
selects a non-existent `MacOSX27.sdk` and the prerequisite `build_libc.sh`
fails — an environment issue, independently reported by red_abi.)

## 4. Ordinary C ABI — PASS (32/32)

`abi_probe.c` is plain C (`/usr/bin/clang -std=gnu11`) linked against both
shipped archives. It asserts: `sizeof(void*) = sizeof(char*) = sizeof(int*) =
sizeof(fnptr) = sizeof(size_t) = sizeof(uintptr_t) = 8`,
`_Alignof(void*) == 8`; `cplus_alloc_meta` 40/8 with offsets 0/8/16/24/32;
`cplus_safe_alloc_meta` 40/8; `cplus_thrd_t` 32, `cplus_mtx_t`/`cplus_cnd_t`
128, `cplus_once_flag` 64, `cplus_tss_t` 8; `char16_t` 2, `char32_t` 4;
`cplus_safe_stat` 48 with offsets 24/32/40; `cplus_safe_platform_info` 28/4;
`errno_t` 4, `rsize_t` 8. Final line: `abi_probe: 32 passed, 0 failed`.

## 5. `abi_compat` — 69/0 proven; official runner 66/3 (OPEN harness defect)

Official runner (unmodified): `66 passed, 3 failed`, rc=1. The three failures
are `T3a_c_driver_compile`/`run`/`summary`, caused by:

```
Undefined symbols for architecture arm64:
  "___cplus_checked_add_i64"  (from the lowered C+ object)
  "_cplus_check_nonnull"
ld: symbol(s) not found
```

Root cause (diagnosed, not assumed): `build/c+` is a wrapper into a newer
external `c-plus-core` checkout whose lowering emits calls to
`__cplus_checked_add_i64`; this repo's `build/libcplus_runtime.a`, built from
this repo's older `runtime/cplus_runtime.c`, does not define it, and the
official T3a link line supplies no runtime at all. The missing symbol is
defined in the external core's `build/libcplus_runtime.a` (verified with
`nm`), but linking that whole archive here produces a duplicate `_cplus_realloc`
against `libcplus_hardened.a`.

Drift-neutralized copy: `tests/redblue20/blue_abi/run_abi_compat_corrected.sh`
is a byte copy of the official runner with only two changes — output redirected
into this directory and `toolchain_drift_shim.c` added to the T3a link line.
The shim defines **only** the two missing helpers with semantics copied from
the external core's `runtime/cplus_runtime.c`; it is linked into no shipped
library. Result: **`abi_compat: 69 passed, 0 failed`, rc=0**, including
`abi_compat_c_driver: 13/13 C->C+ boundary checks match`. All 37 exported
symbol checks, pointer tests, layout tests, T3b and header-neutrality diff were
already passing in the raw run. This is evidence the C ABI is intact, not a
claim the official harness is fixed.

## 6. Legacy labs — official 52/54; both failing labs proven under drift

Official: `total=54 pass=52 fail=2` (rc=1). The 50 `legacy_c` labs, the
`precompiled` lab and the `abi_compare` lab all pass. The two failures:

* `cp_to_c`: `T2_c_driver_link` fails with the same two undefined runtime
  helpers as §5. Corrected local copy
  (`run_cp_to_c_corrected.sh`, shim added to its link line): **`cp_to_c lab:
  37 passed, 0 failed`**, rc=0.
* `c_to_cp`: `Cplus_probe_link`/`Cplus_build` fail because the newer external
  driver's `--strict` mode rejects the pass-through `-Wno-unused-command-line-
  argument` flag (and then rejects the `.a` pass-through itself). Corrected
  local copy (`run_c_to_cp_corrected.sh`, `--strict` dropped, same source and
  archive): **`c_to_cp: 24 passed, 0 failed`**, rc=0.

These are harness/toolchain drift, not ABI regressions: all expected unmangled
symbols in both labs pass in the raw runs too.

## 7. Precompiled C library links unchanged with/without `safe_c.h` — PASS

`safe_layer_lab: PASS`, rc=0. One plain-C library compiled exactly once by
`/usr/bin/clang` into `libsafe_layer.a`; one caller source compiled twice
against that same saved archive — plainly and with
`-include stdlib/safe/safe_c.h` plus caller-side support archives. Measured:

* `libsafe_layer.a` sha256 `2be1e47b9d227fd045d7453b9b2e12d7a840992bba2c11adfd37a2291be1bf58`
  identical at baseline, PRE-BUILD and POST-BUILD; a reuse run reported
  "REUSING saved archive (no compile)".
* `app_plain` and `app_safe` both rc=0, stdout byte-identical (`cmp -s` rc=0;
  sha256 `88393e817390174e1e8fcdf281c996157c9d2e2f59ac3c52cf0bb84022331f18`).
* Negative control rc=70, refusing a 64-byte `memset` into an 8-byte buffer
  while still calling the unchanged C library in the same process.

Scope: static `.a` only; the two support archives are extra caller-side link
inputs, never written.

## Scope / what this does not claim

This is evidence for the tested inputs, the committed baseline `cbcea57`, the
current HEAD `7edf2c6`, and this host toolchain (`arm64-apple-darwin`, Apple
clang 16, the local external C+ driver). It is **not** a proof of absolute
security: it does not establish Windows x86/MSVC/UCRT layout, calling
convention, bitfield packing, -fshort-enums behaviour, arbitrary raw-pointer
safety, or absence of defects outside the enumerated cases. The fingerprint is
a change detector, not a correctness theorem. Two official runners are
currently red for toolchain drift and are recorded as OPEN in
`docs/security/RED_BLUE_20_FINAL.md` rather than being silently relabelled.
