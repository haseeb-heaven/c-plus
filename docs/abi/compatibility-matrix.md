# C+ ABI compatibility matrix

> **Snapshot, not a live dashboard.** This document records only evidence that
> was present in the working tree when it was captured. Cells without a
> sibling-produced result are marked **PENDING**; they are neither passes nor
> failures. Do not read a PENDING cell as an assertion of any kind.

## Snapshot metadata

| item | value |
| --- | --- |
| captured (UTC) | `2026-11-23T03:58:05Z` (local capture `2026-11-23 09:27`, UTC+5) |
| repo | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` |
| branch | `agent/modern-stdlib` |
| HEAD | `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` |
| author role | documentation only (docs/abi); no compiler, runtime, header or test edits |

---

## 1. Exact platform tested

All completed evidence in this matrix was produced on **one** machine and
toolchain. Values below are copied from `tests/abi/ABI_COMPAT_NOTES.md` and the
driver's own `--verbose` transcript at
`tests/abi/c_to_cp/probe_verbose.log`.

| item | exact value |
| --- | --- |
| OS / arch | macOS, `arm64` (`uname -s` = `Darwin`, `uname -m` = `arm64`) |
| clang version | Apple clang version 16.0.0 (`clang-1600.0.26.6`) |
| clang target | `arm64-apple-darwin27.2.0`, thread model `posix` |
| cc1 target / SDK | `arm64-apple-macosx15.2.0`, SDK `MacOSX.sdk`, `-target-cpu apple-m1` |
| InstalledDir | `/Library/Developer/CommandLineTools/usr/bin` |
| resource dir | `/Library/Developer/CommandLineTools/usr/lib/clang/16` |
| C+ driver | `./build/c+` → `C+ 0.1.0`, "Backend: host Clang/LLVM" |
| driver binary | Mach-O 64-bit arm64 executable, 79,699,520 bytes, built `2026-11-23 08:07` |
| driver runtime link | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build-verify-1123/libcplus_runtime.a` (externally supplied — see §6) |
| system tools | `/usr/bin/clang`, `/usr/bin/ar`, `/usr/bin/nm`, `/usr/bin/diff`, `/usr/bin/timeout` |
| runner environment | `PATH=/usr/bin:/bin`, `CPLUS_CLANG=/usr/bin/clang` (set by `run_abi_compat.sh`) |
| step timeouts | compiles 120–300 s, program runs 60 s, `diff`/`nm` 60 s |

## 2. Platform scope

| platform | status | note |
| --- | --- | --- |
| macOS arm64 (Apple clang 16, C+ 0.1.0) | **TESTED** | the only platform with completed evidence in this snapshot |
| macOS x86_64 | **UNTESTED** | no evidence |
| Linux (glibc/musl, any arch) | **UNTESTED** | no evidence |
| Windows (MSVC / UCRT, x86 or x64) | **UNTESTED** | no evidence; the repo's retail-x86 DLL guide is out of scope for this matrix |
| other C++ compilers / C++ ABI | **UNTESTED / out of scope** | no mangled-C++ ABI is exercised (see §7) |

No result in this matrix may be generalized beyond the "TESTED" row.

---

## 3. Master matrix (by directory)

Denominators are stated as `passed/total`; timeouts are `timed-out/invocations`.
"Runner steps" counts compile/link/run/check operations of the shell runners,
not the program-internal `ck()` assertions (the two are different scales and are
reported separately where known).

| Directory | What it checks | Checks on disk at capture | Pass / Fail / Timeout (denominators) | Status |
| --- | --- | --- | --- | --- |
| `tests/abi/` (baseline suite) | pointer representation, public C layouts, both C↔C+ directions, exported symbol names, header neutrality | `run_abi_compat.sh` + 2 build scripts; 6 numbered test groups | **69/69 passed, 0/69 failed, 0/69 timeouts** (runner steps) | **COMPLETE** — `build/abi/compat/results.log` |
| `tests/abi/legacy_c/` | unchanged ordinary C: primitives, pointers, arrays, structs/unions/bitfields/enums, function pointers, callbacks, TLS, opaque handles, stdio/stdlib interop | 37 probe sources (`lc01`–`lc19`, `lc26`–`lc43`) | PENDING / PENDING / PENDING — no consolidated result log at capture | **PENDING** |
| `tests/abi/c_to_cp/` | C+ caller links and calls a precompiled plain-C archive (struct by value, varargs, callbacks, alloc/free) | 1 shared header, 1 C source, 1 C+ caller, 1 archive, 1 built binary | build 1/1 rc=0, run 1/1 rc=0, **4/4 expected output values observed**, 0/2 timeouts; formal assertion total PENDING | **PARTIAL** — `tests/abi/c_to_cp/probe_run.log` |
| `tests/abi/cp_to_c/` | C+ constructs lowered to plain C symbols called from C (classes, temporaries, div/mod, layout/ownership probes) | 13 `.cp` probes + lowered `.c`/`.o` artifacts | PENDING / PENDING / PENDING — exploratory probes only, no result log at capture | **PENDING** |
| `tests/abi/precompiled/` | saved C library (`.a`) usable by a rebuilt C+ app; layout and by-value/by-pointer interchange; stdlib dependency rebuilt independently | 1 header, 1 C lib, 1 C+ app containing 30 `precomp_ck()` assertions | PENDING / PENDING / PENDING — no build/run result at capture | **PENDING** |
| `tests/abi/fingerprints/` | (intended: symbol/header fingerprints pinning the audited revision) | empty directory at capture | PENDING / PENDING / PENDING — no files at capture | **PENDING** |

Legend: **COMPLETE** = authoritative runner result on disk; **PARTIAL** = some
artifacts/results exist but the formal count is not yet produced; **PENDING** =
no sibling result available at capture.

---

## 4. Per-directory detail and commands

### 4.1 Baseline suite — `tests/abi/` (COMPLETE)

Entry point: `sh tests/abi/run_abi_compat.sh`. Restricted `PATH`, every command
wrapped in `/usr/bin/timeout`, no `|| true` masking. Artifacts and transcript in
`build/abi/compat/`.

Headline: `abi_compat: 69 passed, 0 failed` — 69/69 runner steps, 0 failures,
0 timeouts. The 69-step denominator decomposes exactly as:

| group | runner steps |
| --- | --- |
| prerequisite library builds (libc, safecrt) | 2 |
| T1 pointer ABI (compile, run, summary line) | 3 |
| T2 layout constants (compile, run, summary line) | 3 |
| T3a/4a C calls C+ (compile, ar, C compile, run, summary) | 5 |
| T3b/4b C+ calls precompiled C (compile, ar, direct-link probe, build, run, summary) | 6 |
| T5 symbols (2 `nm`, 22+10+7 = 39 symbol checks, 3 no-mangling checks) | 44 |
| T6 header neutrality (5 steps + 1 line check) | 6 |
| **total** | **69** |

Program-internal (self-reported) assertion counts, a different scale:
T1 `20/20 pointer ABI checks match`, T2 `80/80 layouts match`,
T3a `13/13 C->C+ boundary checks match`,
T3b `10/10 C+->C boundary checks match`, T5 `39/39` audited symbol names
present with **no** ` T __Z` C++-mangled symbols in any audited archive, T6
`diff -u` of with/without-cplus-headers output empty (37 layout lines each).

Key commands (reproduced from `tests/abi/ABI_COMPAT_NOTES.md`, `$ROOT` = repo
root):

```sh
sh tests/libc/build_libc.sh
sh scripts/build_safecrt.sh
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic \
  tests/abi/abi_compat_pointers.c -o build/abi/compat/pointers
build/abi/compat/pointers
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic $INC \
  tests/abi/abi_compat_layouts.c build/libc/libcplus_hardened.a \
  build/safe/libcplus_safecrt.a -lm -pthread -o build/abi/compat/layouts
./build/c+ build --sanitize=none tests/abi/abi_compat_cplus_lib.cp \
  -c -o build/abi/compat/abi_compat_cplus_lib.o -- -I$ROOT/libc/install-include
/usr/bin/ar rcs build/abi/compat/libabi_compat_cplus.a \
  build/abi/compat/abi_compat_cplus_lib.o
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic $INC \
  tests/abi/abi_compat_c_driver.c build/abi/compat/libabi_compat_cplus.a \
  build/libc/libcplus_hardened.a -lm -pthread -o build/abi/compat/c_driver
/usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic -I$ROOT/tests/abi \
  -c tests/abi/abi_compat_c_part.c -o build/abi/compat/abi_compat_c_part.o
/usr/bin/ar rcs build/abi/compat/libabi_compat_cpart.a \
  build/abi/compat/abi_compat_c_part.o
./build/c+ build --sanitize=none tests/abi/abi_compat_cplus_caller.cp \
  -o build/abi/compat/cplus_caller -- build/abi/compat/libabi_compat_cpart.a \
  -Wno-unused-command-line-argument
/usr/bin/nm -gU build/libc/libcplus_hardened.a
/usr/bin/nm -gU build/safe/libcplus_safecrt.a
/usr/bin/diff -u build/abi/compat/neutral_plain.out \
  build/abi/compat/neutral_cplus.out
```

`$INC` = `-I$ROOT/libc -I$ROOT/libc/include -I$ROOT/libc/install-include
-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include
-I$ROOT/third_party/intel-safestringlib/include -I$ROOT/tests/abi`.

The C+ driver links a precompiled `.a` directly through its `--` forwarding
path; the only diagnostic is a benign compile-step
`-Wunused-command-line-argument` warning, suppressed in the production build.

### 4.2 `legacy_c/` (PENDING)

37 probe sources exist: `lc01_primitives.c` … `lc19_pointer_arithmetic.c`,
`lc26_global_vars.c` … `lc43_bitfields.c`. Each prints one stable result line
and is intended to exit 0 only when its checks pass (pattern visible in
`lc26_global_vars.c`: `lc26 result: OK counter=20 total=20 epoch=7 label=lc26`).

No runner script, result log or pass/fail tally was present in this directory
at capture. **Counts: PENDING / PENDING / PENDING; 0 consolidated invocations
recorded.** Do not infer results from the presence of source files.

### 4.3 `c_to_cp/` (PARTIAL — available sibling result)

A plain-C library is built by `/usr/bin/clang` and consumed by a C+ caller
through the driver. Artifacts on disk: `probe_shared.h`, `probe_c.c`,
`probe_c.o`, `libprobe.a`, `probe_caller.cp`, `probe_caller`,
`probe_caller_v`, `probe_build.log` (empty), `probe_run.log`,
`probe_verbose.log`.

Observed result (`tests/abi/c_to_cp/probe_run.log`, all 4 lines matching the
caller's expected values):

```
pair=3,9
vsum=15
fired=17
alloc/free ok
```

Counts: build rc=0 (1/1), run rc=0 (1/1), 4/4 expected values observed,
0 timeouts in 2 recorded invocations. **Formal assertion count PENDING** — the
caller prints values but has no explicit `N/N` summary line, so no pass total
is claimed beyond the four observations.

Commands (reconstructed from the artifacts and `--verbose` transcript; the
sibling's authoritative runner command list was not on disk at capture):

```sh
/usr/bin/clang -O2 -std=c99 -Wall -Wextra -pedantic \
  -I tests/abi/c_to_cp -c tests/abi/c_to_cp/probe_c.c \
  -o tests/abi/c_to_cp/probe_c.o
/usr/bin/ar rcs tests/abi/c_to_cp/libprobe.a tests/abi/c_to_cp/probe_c.o
./build/c+ build --sanitize=none tests/abi/c_to_cp/probe_caller.cp \
  -o tests/abi/c_to_cp/probe_caller \
  -- tests/abi/c_to_cp/libprobe.a -I tests/abi/c_to_cp
tests/abi/c_to_cp/probe_caller > tests/abi/c_to_cp/probe_run.log
```

The verbose log confirms the driver linked `libprobe.a` directly together with
the external `c-plus-core` runtime archive (see §6).

### 4.4 `cp_to_c/` (PENDING)

13 `.cp` probes plus lowered `.c`/`.o` artifacts exist, exploring C+ constructs
lowered to plain C symbols: class method dispatch (`_probe.cp`, `_probe9.cp`),
temporaries (`_probe7.cp`, `_probe8.cp`), direct loops and `__builtin_memcpy`
(`_probe2.cp`, `_probe3.cp`), integer division/modulo (`_probe4.cp`–`_probe6.cp`),
layout/ownership probes (`_probe10.cp`–`_probe12.cp`) and a record-field probe
(`_probe13.cp`). This looks like exploratory investigation of what the
externally rebuilt driver can lower; no consolidated run, no result log and no
pass/fail tally was present at capture.

**Counts: PENDING / PENDING / PENDING; 0 consolidated invocations recorded.**

### 4.5 `precompiled/` (PENDING)

Artifacts: `precomp_shared.h` (plain-C contract; documented layout
`sizeof=32`, `align=8`, offsets `i=0,u=4,d=8,p=16,n=24`), `precomp_lib.c` (the
library built once by `/usr/bin/clang`; 13 exported `precomp_*` functions), and
`precomp_app.cp` (the C+ app, 30 `precomp_ck()` assertions covering scalar
behaviour, C-library layout view, C+ layout view, by-value/by-pointer record
round-trips, bad-record rejection, and a rebuilt `stdlib/vector.hp`
dependency). The app sources reference a not-yet-present
`build_app_against_precompiled.sh` runner.

The intended build (from the header comment of `precomp_app.cp`, `$ROOT` = repo
root):

```sh
/usr/bin/clang -c tests/abi/precompiled/precomp_lib.c \
  -o build/abi-precompiled/precomp_lib.o
/usr/bin/ar   rcs build/abi-precompiled/libprecomp.a \
  build/abi-precompiled/precomp_lib.o
./build/c+ build --sanitize=none tests/abi/precompiled/precomp_app.cp \
  -o build/abi-precompiled/app_static \
  -- build/abi-precompiled/libprecomp.a \
     -I$ROOT/tests/abi/precompiled \
     -DPRECOMP_REBUILD_NONCE=<n> -Wno-unused-command-line-argument
build/abi-precompiled/app_static
```

No build output or run result existed at capture. **Counts: PENDING /
PENDING / PENDING** (30 is the assertion *denominator designed into* the app,
not a passing result).

### 4.6 `fingerprints/` (PENDING)

The directory existed but was **empty** at capture. No checks, commands or
results are available. **Counts: PENDING / PENDING / PENDING.**

---

## 5. How counts were derived / refreshed

1. Run the relevant sibling runner and keep its stdout/stderr plus exit codes.
2. Record `passed/total`, `failed/total` and `timed-out/invocations` explicitly.
3. Update only the affected cells in §3 and the matching §4 subsection; change
   the status from PENDING to COMPLETE only when a result log is on disk.
4. Re-stamp the snapshot metadata with the new UTC time and HEAD.
5. Never fill a PENDING cell from a source file's *intended* assertion count.

## 6. External blocker (must be read with these results)

`./build/c+` was **rebuilt externally**, outside this branch's runtime: its
verbose transcript links
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build-verify-1123/libcplus_runtime.a`,
and the failing builds compile
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`.
That runtime source references `CPLUS_EXIT_ARITHMETIC`:

```
cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", ...
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

This branch's runtime header `runtime/include/cplus/cplus.h` does **not**
define that enumerator; its `cplus_exit_code` enum ends at `CPLUS_EXIT_IO = 109`
(`OK=0`, `BOUNDS=101`, `NULL_POINTER=102`, `USE_AFTER_FREE=103`,
`DOUBLE_FREE=104`, `TYPE_CONFUSION=105`, `OWNERSHIP=106`, `RESOURCE=107`,
`UNSUPPORTED=108`, `IO=109`).

Consequences and boundaries:

* At capture, **51** build logs under `tests/out/*.build.log` (timestamps
  `2026-11-23 09:25`–`09:26`) show this failure. It is an external
  compiler/runtime mismatch, **not** an ABI mismatch found by these probes.
* The probes that used the driver's already-built runtime (§4.1, §4.3) still
  built and ran; the blocker affects paths that try to rebuild the instrumented
  runtime from `c-plus-core` sources.
* **No compiler or runtime edits were made by Agent 2 (or by this documentation
  task).** This document records the blocker; it does not fix it. Any fix must
  come from the owner of the external `c-plus-core` driver/runtime or from an
  explicitly authorized change on this branch.

## 7. Explicit non-claims

This matrix does **not** claim any of the following:

1. **No cross-platform proof.** Only macOS arm64 / Apple clang 16 / C+ 0.1.0
   was exercised. Linux and Windows (MSVC/UCRT), x86_64 and x86 are UNTESTED;
   nothing here predicts their layouts, calling conventions or symbol
   behaviour.
2. **No C++ mangling claim.** The checks exercise only C-linkage/plain-C
   symbols. The only related observation is negative: `nm -gU` found no
   ` T __Z` C++-mangled symbols in the audited archives. No Itanium/MSVC
   mangled C++ ABI, class layout across compilers, or C++ exception ABI was
   tested, and none is claimed to be stable.
3. **No ABI changes.** This work produced probes and documentation only; no
   public header, struct layout, enum value, calling convention or exported
   symbol name was changed, and no ABI change is claimed, required or proven.
   The results say the checked subsets matched the captured expectations — not
   that any ABI was modified or that future edits cannot break it.
4. **No blanket safety claim.** Passing ABI checks say nothing about runtime
   memory safety, arbitrary raw pointers, uninstrumented precompiled libraries,
   compiler diagnostics, or the unresolved `CPLUS_EXIT_ARITHMETIC` blocker in
   §6.
5. **No PENDING result is a result.** PENDING cells are absence of evidence at
   capture time, not passes, not failures, and not timeouts.

## 8. Evidence index

| artifact | role |
| --- | --- |
| `build/abi/compat/results.log` | machine-readable command/exit-code transcript for the 69/69 baseline suite |
| `tests/abi/ABI_COMPAT_NOTES.md` | authoritative notes for the baseline suite |
| `tests/abi/run_abi_compat.sh` | baseline suite runner |
| `tests/abi/abi_compat_expected_{libc,safecrt,compat}_symbols.txt` | 22 + 10 + 7 audited symbol names |
| `tests/abi/c_to_cp/probe_run.log` | 4/4 observed values for the c_to_cp probe |
| `tests/abi/c_to_cp/probe_verbose.log` | clang/cc1/ld transcript and external runtime link path |
| `tests/out/*.build.log` (51 files) | external `CPLUS_EXIT_ARITHMETIC` blocker evidence |
| `runtime/include/cplus/cplus.h` | this branch's `cplus_exit_code` enum (no `CPLUS_EXIT_ARITHMETIC`) |
| `tests/abi/legacy_c/`, `c_to_cp/`, `cp_to_c/`, `precompiled/`, `fingerprints/` | sibling probe directories (status per §3) |
| [`source_audit.md`](source_audit.md) | sibling source-level header-change audit (static complement to these runtime checks); concludes no consumer-visible `ABI_CHANGE` in the audited header series |
