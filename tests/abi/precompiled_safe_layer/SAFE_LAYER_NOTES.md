# Precompiled-safe-layer lab — evidence

Suite entry point:

```sh
sh tests/abi/precompiled_safe_layer/run_safe_layer_lab.sh
```

## Claim under test

Adding the checked ordinary-name layer (`stdlib/safe/safe_c.h`, force-included
into caller code) **does not require rebuilding precompiled C libraries**.
The safe layer is a compile-time macro mapping applied to the *caller* source;
it is not an ABI change to an already-compiled C archive.

## How the claim is isolated

1. A tiny plain-C library is compiled **exactly once** with `/usr/bin/clang`
   (`-std=c11`, no C+/SafeCRT headers, no `safe_c.h`) into `libsafe_layer.a`.
   Its sha256 is recorded as the compatibility baseline. A second invocation
   of the build script reuses the saved bytes and never recompiles.
2. **One caller source** (`safe_layer_app.c`) is compiled twice against the
   **same saved archive**:
   - `app_plain` — without `safe_c.h`, ordinary host libc, links only `.a`;
   - `app_safe` — with `-include stdlib/safe/safe_c.h`, links `.a` plus the
     caller-side support archives `libcplus_safecrt.a` and
     `libcplus_hardened.a`.
3. Both programs are run; their stdout is compared **byte-for-byte** for valid
   inputs. The archive sha256 is checked before and after every caller build
   to prove it was not touched.
4. A **negative** program (`safe_layer_negative.c`) is compiled with the same
   force-include. It still calls the unchanged C library, then attempts a
   64-byte `memset` into an 8-byte local buffer; the checked layer must refuse
   it rather than overflow.

Ownership: every source/script below lives in
`tests/abi/precompiled_safe_layer/`; every generated artifact lives in
`tests/abi/precompiled_safe_layer/artifacts/` (git-ignored, not committed).
No compiler, runtime, stdlib, or other test file was edited. `build/` support
archives are read-only inputs.

Final clean run: **`safe_layer_lab: PASS`** / `caller lab: ALL CHECKS PASSED`
(`failures=0`). Machine-readable transcripts: `artifacts/lib_build.log`,
`artifacts/caller_build.log`, `artifacts/results.log`; program streams:
`artifacts/{app_plain,app_safe,negative_safe}.{stdout,stderr,rc}`.

## 1. Environment (captured by the runner)

| item | value |
| --- | --- |
| date (UTC) | `2026-11-23T04:35:26Z` |
| compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6) |
| target | `arm64-apple-darwin27.2.0`, thread model posix |
| install dir | `/Library/Developer/CommandLineTools/usr/bin` |
| arch / os | `arm64` / `Darwin` |
| archiver / hash / compare | `/usr/bin/ar`, `/usr/bin/shasum`, `/usr/bin/cmp` |
| PATH during run | `/usr/bin:/bin` (restricted, explicit) |

## 2. Files created

| file | role |
| --- | --- |
| `safe_layer_lib.h` | plain-C ABI contract (no C+ syntax, no SafeCRT headers) |
| `safe_layer_lib.c` | the C library; compiled exactly once by `/usr/bin/clang` |
| `safe_layer_app.c` | ONE caller source compiled twice (plain vs force-included safe layer) |
| `safe_layer_negative.c` | negative control: refused overflow + still calls the C library |
| `build_lib_once.sh` | builds the C library once; reuses saved bytes unless `--force`; records sha256 |
| `build_and_test_callers.sh` | builds both callers + negative against the saved `.a`, runs, compares, verifies sha256 |
| `run_safe_layer_lab.sh` | top-level driver |

## 3. Exact commands and exit codes

`$ROOT` = `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`. Logs contain
the fully expanded absolute forms. Every build command below exited **0**,
with empty stdout and stderr.

```sh
export PATH=/usr/bin:/bin

# ---- stage 1: build the C library ONCE ------------------------------------
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic -I$ROOT/tests/abi/precompiled_safe_layer \
  -c $ROOT/tests/abi/precompiled_safe_layer/safe_layer_lib.c \
  -o $ROOT/tests/abi/precompiled_safe_layer/artifacts/libsafe_layer.o     # rc=0, out/err empty
/usr/bin/ar rcs $ROOT/tests/abi/precompiled_safe_layer/artifacts/libsafe_layer.a \
  $ROOT/tests/abi/precompiled_safe_layer/artifacts/libsafe_layer.o        # rc=0, out/err empty

# re-run build_lib_once.sh without --force -> "REUSING saved archive (no compile)",
# libsafe_layer.a sha256 unchanged.  No compiler invocation occurs.

# ---- stage 2: two callers + negative, SAME saved archive ------------------
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include \
     -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
LIBS="$ROOT/build/safe/libcplus_safecrt.a $ROOT/build/libc/libcplus_hardened.a"
OUT="$ROOT/tests/abi/precompiled_safe_layer/artifacts"

# (A) plain caller: links ONLY the unchanged archive
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I$ROOT/tests/abi/precompiled_safe_layer \
  $ROOT/tests/abi/precompiled_safe_layer/safe_layer_app.c \
  $OUT/libsafe_layer.a -o $OUT/app_plain                                # rc=0, out/err empty

# (B) safe caller: force-included header (caller-side only), SAME archive
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I$ROOT/tests/abi/precompiled_safe_layer $INC \
  -include stdlib/safe/safe_c.h \
  $ROOT/tests/abi/precompiled_safe_layer/safe_layer_app.c \
  $OUT/libsafe_layer.a $LIBS -lm -pthread -o $OUT/app_safe               # rc=0, out/err empty

# negative control: same force-include, SAME archive
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I$ROOT/tests/abi/precompiled_safe_layer $INC \
  -include stdlib/safe/safe_c.h \
  $ROOT/tests/abi/precompiled_safe_layer/safe_layer_negative.c \
  $OUT/libsafe_layer.a $LIBS -lm -pthread -o $OUT/negative_safe          # rc=0, out/err empty

$OUT/app_plain      # rc=0
$OUT/app_safe       # rc=0
$OUT/negative_safe  # rc=70 (overflow refused)
/usr/bin/cmp -s $OUT/app_plain.stdout $OUT/app_safe.stdout               # rc=0 (identical)
```

Top level: `sh tests/abi/precompiled_safe_layer/run_safe_layer_lab.sh` → rc=0;
stage 1 and stage 2 both rc=0.

## 4. sha256 of the saved / linked binaries

From `artifacts/lib.sha256`, `artifacts/lib_build.log` and
`artifacts/caller_build.log`:

| binary | sha256 |
| --- | --- |
| `libsafe_layer.o` (compiled once) | `23af1a5e46493e2de47ab5f09d2ed72a3354dc9c7fe96475fb36e3aa33e27721` |
| `libsafe_layer.a` (SAVED precompiled C library) | `2be1e47b9d227fd045d7453b9b2e12d7a840992bba2c11adfd37a2291be1bf58` |
| `build/safe/libcplus_safecrt.a` (caller-side support, read-only) | `10195ff2e11b119b190743fc79bcb5d12b01a3190372b0a0ff3c3ce738bfef71` |
| `build/libc/libcplus_hardened.a` (caller-side support, read-only) | `70b425cef68b3d9d73ef5541dc44b5264a2aabe936cce69cb98ad0e0c4b95964` |

`libsafe_layer.a` measured the **same** sha256 before the caller builds
(`PRE-BUILD`), after the caller builds (`POST-BUILD`), and on a separate
re-run of `build_lib_once.sh` that took the reuse path. The two Support
archives are extra caller-side link inputs; they are never written and are not
part of the precompiled C library.

## 5. stdout / stderr / exit codes

`app_plain.stdout` and `app_safe.stdout` are byte-identical (`/usr/bin/cmp -s`
rc=0; both sha256 `88393e817390174e1e8fcdf281c996157c9d2e2f59ac3c52cf0bb84022331f18`).
Both stderr files are empty.

```
app: version=3
app: mul=440
app: checksum=36
app: label=precompiled-ok
app: END
```

Exit codes: `app_plain=0`, `app_safe=0`, `negative_safe=70`.

`negative_safe.stdout` (proves the unchanged C library was still called and a
valid in-bounds write succeeded before the refused overflow):

```
negative: lib_version=3 lib_mul=42
negative: valid_write=ok small0=A
```

`negative_safe.stderr` (the checked layer refused the 64-byte write into the
8-byte local buffer; the `UNEXPECTED` line was NOT printed):

```
[CPLUS-SAFE] memset_s: buffer overflow refused (requested 64 > capacity 8, ESNOSPC 406)
```

## 6. Conclusion

The two callers were built from **one** source against the **same saved**
`libsafe_layer.a`; for valid inputs their stdout was byte-identical, and the
archive sha256 was unchanged before, after, and across a reuse run. The only
difference between them was the caller-side compile flag
`-include stdlib/safe/safe_c.h`, which expands ordinary names
(`memset`/`strcpy`/`strcat`/`printf`/…) into checked wrappers in the *caller*.
The negative control shows those checked wrappers refuse an overflow of a
local buffer (exit 70, ESNOSPC 406) while the precompiled C library is still
called normally in the same process.

Therefore the checked ordinary-name layer is a **compile-time macro mapping
for caller code, not an ABI change to precompiled libraries**: a C library
built once with `/usr/bin/clang` is linked unchanged by both the plain and the
safe caller, and enabling the safe layer requires no rebuild of that library.

### Limits (exact, no over-claim)

* Only the static `.a` path was exercised (the library is deliberately tiny).
  No shared-library/dylib or Windows `.lib` scenario is claimed here.
* The safe caller links two additional *caller-side* support archives
  (`libcplus_safecrt.a`, `libcplus_hardened.a`); the point proven is that the
  **precompiled C library itself** is neither rebuilt nor changed. These
  support archives are read-only inputs read from `build/`, not modified.
* Only the ordinary names used in the shared source were compared; the
  byte-for-byte stdout claim is scoped to those valid inputs.
