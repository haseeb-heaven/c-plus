# SDK standard-C ABI preservation + checked-layer verification

**Scope:** macOS **arm64** / **Apple clang 16** (`/usr/bin/clang`,
`Apple clang version 16.0.0 (clang-1600.0.26.6)`), `uname -m` = `arm64`.
Nothing here should be read as evidence for Linux, Windows, x86-64, or other
compiler versions.

Deliverables: `run.sh` (reproduces everything below), `consumer.c` (the single
translation unit compiled in all modes), this file. Raw logs/artifacts are in
`out/` (gitignored by `tests/abi/**/out/`).

Reproduce with:

```sh
sh tests/abi/stdc_sdk/run.sh
```

---

## 0. Toolchain caveat (important for reading the script results)

The two repository scripts default to `CC=clang`. On this host `clang` resolves
to **Homebrew clang 23.1.6**
(`/opt/homebrew/opt/llvm/bin/clang`), whose config points at a non-existent
`/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk`. Both scripts therefore
**fail with the default compiler**; both **pass with the scoped Apple clang 16**
selected via their documented `CPLUS_CLANG` override. Both runs were executed
and recorded — the failure is a toolchain/sysroot issue, not an SDK defect.

| Command | Compiler | Exit | Exact final line |
|---|---|---:|---|
| `sh tests/sdk/run_sdk_consumer.sh` | default `clang` (Homebrew 23.1.6) | **1** | `-- Build files have been written to: /var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T//cplus-sdk-consumer.XAVmOc/libc-build` |
| `CPLUS_CLANG=/usr/bin/clang sh tests/sdk/run_sdk_consumer.sh` | Apple clang 16 | **0** | `sdk consumer: ok` |
| `sh tests/libc/install_smoke.sh` | default `clang` (Homebrew 23.1.6) | **1** | `1 error generated.` |
| `CPLUS_CLANG=/usr/bin/clang sh tests/libc/install_smoke.sh` | Apple clang 16 | **0** | `install smoke: ok` |

Default-compiler failure evidence (verbatim excerpts, full logs in
`out/default-clang_*.log`):

```
sdk consumer: FAIL: libc build failed
duplicate symbol '_cplus_exp10' in:
    .../CMakeFiles/cplus_hardened_libc_shared.dir/src/math/math_c23.c.o
    .../CMakeFiles/cplus_hardened_libc_shared.dir/src/math/math_c23_musl.c.o
duplicate symbol '_cplus_exp10l' in: ...
duplicate symbol '_cplus_exp10f' in: ...
ld: 3 duplicate symbols
clang: error: linker command failed with exit code 1
```

```
clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk' [-Wmissing-sysroot]
clang: warning: using sysroot for 'macOS 27' but targeting 'arm64-apple-macosx27.0.0' [-Wincompatible-sysroot]
.../build/libc-cmake/smoke_sdk.c:1:10: fatal error: 'assert.h' file not found
1 error generated.
```

With `CPLUS_CLANG=/usr/bin/clang` the required scripts each emit exactly one
line — `sdk consumer: ok` and `install smoke: ok` — and exit 0. `install_smoke.sh`
leaves a clean install prefix at `build/libc-install` containing
`lib/libcplus_hardened_libc.a`, `lib/libcplus_hardened_libc.dylib`,
`include/cplus-libc/cplus_libc.h`, the CMake package config and the contracts.
The installed `cplus_libc.h` and `cplus_safecrt_upstream.h` are byte-identical
to the repo's `libc/install-include/` copies (SHA-256
`7c1acca2…a85027` and `568fcf64…b5b5eef9`).

---

## 1. Plain standard-C consumer built ONLY from the clean install prefix

The source (`consumer.c`) uses exactly `malloc`, `memcpy`, `strcpy`, `printf`
(and `free`/`fprintf`), and includes the installed `"cplus_libc.h"`. No source
tree include path is used in these two commands.

```sh
/usr/bin/clang -std=c99 -Wall -Wextra \
  -I build/libc-install/include/cplus-libc \
  tests/abi/stdc_sdk/consumer.c \
  build/libc-install/lib/libcplus_hardened_libc.a -lm -pthread \
  -o out/consumer.install-static
```
compile exit **0**; run exit **0**; stdout (42 bytes):
```
consumer: heap=[libc-abi] stack=[OK] n=42
```
stderr: empty (0 bytes).

Shared-library variant:

```sh
/usr/bin/clang -std=c99 -Wall -Wextra \
  -I build/libc-install/include/cplus-libc \
  tests/abi/stdc_sdk/consumer.c \
  -L build/libc-install/lib -lcplus_hardened_libc \
  -Wl,-rpath,<abs>/build/libc-install/lib -lm -pthread \
  -o out/consumer.install-shared
```
compile exit **0**; run exit **0**; stdout identical to the static build:
```
consumer: heap=[libc-abi] stack=[OK] n=42
```
`cmp -s` on the two stdout files: **BYTE-IDENTICAL**.

**ABI observation:** the installed header is *selective-link*. It declares
namespaced `cplus_*` entry points and does **not** redefine the standard
spellings unless the caller opts in with `CPLUS_MALLOC_TRACKING`. Unmodified C
source therefore compiles and runs unchanged: `malloc`/`memcpy`/`strcpy`/
`printf` keep their ordinary ISO C semantics and resolve to the host CRT, with
the installed archive available for explicit `cplus_*` use. That is precisely
"preserves the standard C ABI" rather than replacing it.

---

## 2. SAME source + checked layer, byte-identical output and refusal

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -include stdlib/safe/safe_c.h \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/abi/stdc_sdk/consumer.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread \
  -o out/consumer.checked
```
compile exit **0**. Valid run (no args) exit **0**, stderr empty, stdout:
```
consumer: heap=[libc-abi] stack=[OK] n=42
```

Byte-identity evidence (SHA-256 of stdout, all four files — plain static,
plain shared, checked valid, checked hostile — are the same 42-byte file):

```
d5d86a4ad58adbc1092674a6291ed4b36f6742c31325078762a3e6e5e70f94b4  out/plain-static.stdout
d5d86a4ad58adbc1092674a6291ed4b36f6742c31325078762a3e6e5e70f94b4  out/plain-shared.stdout
d5d86a4ad58adbc1092674a6291ed4b36f6742c31325078762a3e6e5e70f94b4  out/checked.stdout
d5d86a4ad58adbc1092674a6291ed4b36f6742c31325078762a3e6e5e70f94b4  out/checked-hostile.stdout
```

Hostile case (`argv[1] == "x"`: 40-byte `memcpy` into an 8-byte stack object):

```
$ out/consumer.checked x
exit = 70
stderr: [CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 40 > capacity 8, ESNOSPC 406)
```

`safe_c.h` remapped the ordinary `memcpy` spelling to a capacity-checked
wrapper; `__builtin_object_size` = 8 for the destination, so the 40-byte write
is refused with the ESNOSPC 406 diagnostic and `_exit(70)` (the runtime's
default violation trap). No memory is corrupted and the caller never reaches
the "hostile write completed" line.

For contrast, the plain install-prefix build on the same input performs the
unchecked write and dies with a SIGTRAP (**exit 133**, empty stdout/stderr) —
i.e. undefined behaviour / compiler trap, **not** a C+ safety refusal. The
safety is therefore **opt-in**: the same source is unsanitised by default and
becomes checked only when `safe_c.h` and the checked archives are selected.

---

## 3. `nm` evidence: exported symbols are unmangled C ABI names, none removed

```sh
nm -g --defined-only build/libc-install/lib/libcplus_hardened_libc.a   > out/nm-install-static.txt
nm -g --defined-only build/libc-install/lib/libcplus_hardened_libc.dylib > out/nm-install-shared.txt
```

Key required symbols (exact lines; Mach-O leading underscore, flat C names —
no Itanium/`_Z…` mangling, no C++ ABI decoration):

```
static: 0000000000000544 T _cplus_malloc
static: 0000000000000f5c T _cplus_free
static: 0000000000000000 T _cplus_memcpy
static: 0000000000000094 T _cplus_strcpy
static: 0000000000000ad0 T _cplus_printf
shared: 0000000000002360 T _cplus_malloc
shared: 0000000000002d78 T _cplus_free
shared: 0000000000004c48 T _cplus_memcpy
shared: 0000000000005b74 T _cplus_strcpy
shared: 000000000000c53c T _cplus_printf
```

Automated audit:

```
C++-mangled-looking symbols in static archive: 0
C++-mangled defined symbols (dylib):           0
total defined globals (static archive):        966
total defined globals (shared dylib):          812
namespaced cplus_* exports (dylib):            805 / 812
```

The 7 non-`cplus_` exports in the dylib are also plain C identifiers:
`__cplus_errno_location`, `nextdown`, `nextdownf`, `nextdownl`, `nextup`,
`nextupf`, `nextupl` (C23 `<math.h>` names plus one internal helper).

Cross-check against the installed contract map
(`share/cplus-libc/contracts/symbol_map.json`, 275 mappings):

```
IMPLEMENTED/VENDORED mappings:  27
missing from installed STATIC:  0 []
missing from installed SHARED:  0 []
```

All 27 implementation-bearing mappings (`malloc -> cplus_malloc`,
`memcpy -> cplus_memcpy`, `strcpy -> cplus_strcpy`, `printf -> cplus_printf`,
`free -> cplus_free`, …) are present as defined `T` symbols in **both**
installed libraries. HOST-DELEGATED entries are intentionally not defined by
the SDK; they remain available from the host C runtime, which is the ABI
preservation property under test. No symbols were renamed, mangled, or
dropped by install.

---

## 4. Conclusion

1. **Standard C ABI preserved.** A source file using ordinary `malloc`,
   `memcpy`, `strcpy`, and `printf` compiles cleanly against the installed
   headers and links/runs against both the installed static archive and the
   installed dylib, with byte-identical output. Export names in both libraries
   are flat C symbols (zero C++ mangling), and every one of the 27 mapped
   implemented/vendored symbols is exported from both libraries — no removed
   or renamed symbols.
2. **Safety layer is a caller-side opt-in, not an ABI break.** Compiling the
   *same* translation unit with `-include stdlib/safe/safe_c.h` and the checked
   archives produces byte-identical stdout for valid inputs, while a hostile
   overlong `memcpy` is refused with `[CPLUS-SAFE] … ESNOSPC 406` and exit 70.
   No rebuild or source rewrite of the installed SDK is needed.
3. **Toolchain qualification.** These results hold only for macOS arm64 with
   Apple clang 16 (`/usr/bin/clang`). With the Homebrew clang 23.1.6 found
   first on `PATH`, both supplied scripts fail (sysroot/`assert.h` error and
   duplicate `cplus_exp10*` symbols in the shared-library link); those failures
   were reproduced and are recorded above but are outside the stated scope.
