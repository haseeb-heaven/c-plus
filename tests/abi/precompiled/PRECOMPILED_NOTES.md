# Precompiled library compatibility lab — evidence

Suite entry point: `sh tests/abi/precompiled/run_precompiled_lab.sh`.

The C compatibility library is built **once** with `/usr/bin/clang` into a
single object, then that object is reused to make both a static archive and a
shared dylib.  A C+ application links the **saved binary** (it never recompiles
`precomp_lib.c`) and verifies scalar behavior plus data layout across the
boundary.  A second script re-runs **only the C+ app build** — the workflow used
after a stdlib source change — and proves the saved library is byte-identical.

Ownership: every source/script listed below lives in `tests/abi/precompiled/`;
every generated artifact lives in `build/abi-precompiled/` (git-ignored,
not committed). No compiler, runtime, stdlib, or other test file was edited.

Final clean run: **`precompiled_lab: 3 passed, 0 failed`**.
Machine-readable transcripts:
`build/abi-precompiled/results.log`, `lib_build.log`, `app_build.log`,
`rebuild.log`.  Program stdout: `app_static.stdout`, `app_shared.stdout`.

## 1. Environment (captured by the runner)

| item | value |
| --- | --- |
| date (UTC) | `2026-11-23T03:59:52Z` |
| compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6) |
| target | `arm64-apple-darwin27.2.0`, thread model posix |
| install dir | `/Library/Developer/CommandLineTools/usr/bin` |
| arch / os | `arm64` / `Darwin` |
| C+ driver | `./build/c+` → `C+ 0.1.0`, backend host Clang/LLVM |
| archiver / shasum / symbol tools | `/usr/bin/ar`, `/usr/bin/shasum`, `/usr/bin/otool` |
| PATH during run | `/usr/bin:/bin` (restricted, explicit) |
| `CPLUS_CLANG` | `/usr/bin/clang` |

## 2. Files created

| file | role |
| --- | --- |
| `tests/abi/precompiled/precomp_shared.h` | plain-C ABI contract shared by both sides (no C+ syntax, no C+ headers) |
| `tests/abi/precompiled/precomp_lib.c` | the small C library; compiled exactly once by `/usr/bin/clang` |
| `tests/abi/precompiled/precomp_app.cp` | C+ app: links the saved binary, exercises behavior + layout + `stdlib/vector.hp` |
| `tests/abi/precompiled/build_precompiled_lib.sh` | builds the C library once; refuses to rebuild unless `--force`; records sha256 baseline |
| `tests/abi/precompiled/build_app_against_precompiled.sh` | builds/links/runs the C+ app against the existing binaries (static + shared); refuses if sha256 baseline differs |
| `tests/abi/precompiled/rebuild_app_after_stdlib_change.sh` | re-runs ONLY the C+ app build with a fresh nonce; verifies library unchanged |
| `tests/abi/precompiled/run_precompiled_lab.sh` | top-level driver; restricted PATH, exit-code accounting, cumulative transcript |
| `tests/abi/precompiled/PRECOMPILED_NOTES.md` | this document |

Generated in `build/abi-precompiled/`: `precomp_lib.o`, `libprecomp.a`,
`libprecomp.dylib`, `app_static`, `app_shared`, `*.stdout`, `lib.sha256`,
`last_nonce`, and the four `*.log` transcripts.

## 3. Exact commands and exit codes

`$ROOT` = `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.  Logs contain
the fully expanded absolute forms.  Every command below exited **0**.

```sh
# ---- stage 1: build the C library ONCE (one object -> archive + dylib) ----
export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I$ROOT/tests/abi/precompiled \
  -c $ROOT/tests/abi/precompiled/precomp_lib.c \
  -o $ROOT/build/abi-precompiled/precomp_lib.o                       # rc=0, stdout/stderr empty
/usr/bin/ar rcs $ROOT/build/abi-precompiled/libprecomp.a \
  $ROOT/build/abi-precompiled/precomp_lib.o                          # rc=0, stdout/stderr empty
/usr/bin/clang -dynamiclib -install_name @rpath/libprecomp.dylib \
  $ROOT/build/abi-precompiled/precomp_lib.o \
  -o $ROOT/build/abi-precompiled/libprecomp.dylib                    # rc=0, stdout/stderr empty

# ---- stage 2: build + link + run the C+ app against the SAVED binaries ----
$ROOT/build/c+ build --sanitize=none \
  $ROOT/tests/abi/precompiled/precomp_app.cp \
  -o $ROOT/build/abi-precompiled/app_static -- \
  $ROOT/build/abi-precompiled/libprecomp.a \
  -I$ROOT/tests/abi/precompiled \
  -DPRECOMP_REBUILD_NONCE=0 -Wno-unused-command-line-argument         # rc=0, stdout/stderr empty
$ROOT/build/abi-precompiled/app_static                                # rc=0, stdout captured, stderr empty

$ROOT/build/c+ build --sanitize=none \
  $ROOT/tests/abi/precompiled/precomp_app.cp \
  -o $ROOT/build/abi-precompiled/app_shared -- \
  $ROOT/build/abi-precompiled/libprecomp.dylib \
  -Wl,-rpath,@loader_path \
  -I$ROOT/tests/abi/precompiled \
  -DPRECOMP_REBUILD_NONCE=0 -Wno-unused-command-line-argument         # rc=0, stdout/stderr empty
$ROOT/build/abi-precompiled/app_shared                                # rc=0, stdout captured, stderr empty
/usr/bin/otool -L $ROOT/build/abi-precompiled/app_shared              # rc=0; shows @rpath/libprecomp.dylib

# ---- stage 3: rebuild ONLY the C+ app (stdlib-change workflow) ----
sh $ROOT/tests/abi/precompiled/build_app_against_precompiled.sh \
  --nonce 1 --mode all                                               # rc=0
# stage-1 commands are deliberately NOT run here; rebuild.log lists them as SKIPPED.
```

Top-level: `sh tests/abi/precompiled/run_precompiled_lab.sh` → rc=0 with
`build_library_once`, `build_and_test_app`, and
`rebuild_only_app_after_stdlib_change` all rc=0.

### Link warning note (exact)
Without `-Wno-unused-command-line-argument`, the C+ driver's *compile* step
prints one benign warning because the archive is only consumed by the *link*
step; the build still succeeds and the program still runs:

```
clang: warning: build/abi-precompiled/libprecomp.a: 'linker' input unused [-Wunused-command-line-argument]
```

The production command above suppresses it; the logged compile stderr was
empty.  The same suppression is used for the dylib link path.

## 4. sha256 of the saved binaries (final clean run)

From `build/abi-precompiled/lib.sha256` and `lib_build.log`:

| saved binary | sha256 |
| --- | --- |
| `precomp_lib.o` | `bc1d74d3e7c51033dc80aca04ad3693c07de8c546d1f72e120f87e96d3f21c22` |
| `libprecomp.a` | `7363e5cccfd534192a7f138729056b0716d30ff18d96c5f51f2fba4b05974d2e` |
| `libprecomp.dylib` | `27426fe95fb03a1878bb51cf99f08eb2299c366565a317be455d6e6ecceb4fd1` |

`precomp_lib.o` and `libprecomp.dylib` produced identical sha256 in two
independent clean builds.  Apple `/usr/bin/ar` embeds an archive creation
timestamp, so `libprecomp.a` sha256 differed between those two clean builds
(an earlier run recorded `19ead9d597716c47bc142084f0475a03a38b082ed9097c7c59aa00feb63aac1f`).
Within a single run the archive baseline is byte-stable: the rebuild-only stage
re-measured it before and after and both equals `7363e5cc…b05974d2e`.

## 5. stdout / verification results

Both `app_static.stdout` and `app_shared.stdout` are identical.  The files
currently on disk are from the final nonce=1 rebuild (the stage-2 build used
nonce=0, recorded in `app_build.log`).  Final lines:

```
precomp_app: nonce=1
precomp_app: C+ sizeof(record)=32 align=8
...
PRECOMP_APP 29 0
```

All 29 checks reported `OK` in each mode.  Groups verified:

* scalar behavior from the saved binary: `precomp_lib_version()==3`,
  `precomp_mul(20,22)==440`, `precomp_checksum(byte[8])==36`,
  `precomp_checksum(NULL,0)==-1`.
* C library layout view: `sizeof=32`, `align=8`, offsets `i=0,u=4,d=8,p=16,n=24`.
* C+ layout view equals the saved binary's view (`sizeof`, `__alignof__`).
* a record filled by C is read field-correctly by C+ (`i,u,d,p,n`).
* C+ passes it back BY VALUE and BY POINTER; the saved binary accepts it,
  refuses NULL (`7`), and detects a corrupted field (`2`) both ways.
* stdlib dependency: `stdlib/vector.hp` push/at/sum/drop all pass — this is the
  code that must be rebuilt after a stdlib source change.

Aggregate results: stage 2 `app build/run: 6 passed, 0 failed` (build+run+summary
for static and shared); stage 3 `rebuild: 5 passed, 0 failed` (baseline match
before, app rc=0, byte-identical library after, fresh nonce present in both
static and shared stdout); top level `3 passed, 0 failed`.

`otool -L app_shared` proves the dynamic link:
```
@rpath/libprecomp.dylib (compatibility version 0.0.0, current version 0.0.0)
/usr/lib/libSystem.B.dylib (...)
```

## 6. Supported and unsupported cases (exact)

Supported and exercised here:

* static archive (`.a`) built by `/usr/bin/clang` from one object, linked by
  the C+ driver's `--` forwarding path, run successfully;
* shared dylib (`.dylib`) built from the *same single object*, linked with
  `-Wl,-rpath,@loader_path`, resolved and run successfully on macOS;
* C-compatible struct passed by value and by pointer in both directions;
* rebuild-after-stdlib-change where only the C+ app is rebuilt and the saved
  library sha256 is verified unchanged before and after.

Not supported / not exercised (stated exactly, no claim of success):

* **Windows `.lib`/`.dll`**: not built or tested. This lab ran on macOS/arm64
  with the Darwin clang toolchain; no PE/COFF cross toolchain or import-library
  step was configured, so no Windows result is claimed.
* **x86_64 (Intel) slice**: not built or tested. The host is `arm64`; no
  cross-architecture compile or Rosetta run was performed. The recorded layout
  contract (32/8, offsets 0/4/8/16/24) is asserted only for `arm64` LP64.
* **`--strict` C+ mode**: not used; builds used `--sanitize=none`, matching the
  existing `tests/abi/run_abi_compat.sh` convention. Strict-mode rejection of
  any construct in `precomp_app.cp` was not evaluated.
* **Sanitizer builds** (`address`/`undefined`/`memory`/etc.): not exercised;
  only `--sanitize=none` builds were run.
* **Symbol-versioning / two-phase dylib interposition**: not tested. The dylib
  was linked by install name `@rpath/libprecomp.dylib` with `@loader_path`
  rpath only; no `DYLD_INSERT_LIBRARIES` or versioned install-name scenario.
