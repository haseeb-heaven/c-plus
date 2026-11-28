# C+ Linux build chain — verification report

Date: 2026-11-28. Source: `alpha-version` HEAD `121661f1d54fb6e6c80ceb08ca4e9ff9bf9c5d95`.
All findings below were produced by running the commands shown; nothing is inferred
from a successful configure or from a container that merely exists.

## 1. Environment (observed)

```
uname -a        Darwin ... RELEASE_ARM64_T8112 arm64
clang --version Homebrew clang version 23.1.6 / Target: arm64-apple-darwin27.2.0
llvm-config --targets-built  ... X86 ... AArch64 ...
cmake --version 4.4.5
colima list     frida-x86  Running  x86_64   docker
```

`clang --print-targets` registers both `x86_64` and `aarch64`, so the Homebrew
driver *can emit* ELF objects with `--target=...`. That is not sufficient to
build the C+ compiler: the compiler links LLVM/Clang static archives, and those
archives must themselves be target-architecture ELF objects.

## 2. Host cross-compile: BLOCKED (exact blocker)

`CMakeLists.txt` calls `find_package(LLVM REQUIRED CONFIG)` /
`find_package(Clang REQUIRED CONFIG)` and links Clang/LLVM static archives. The
Homebrew LLVM package found on this host is a Darwin arm64 build:

```
$ ar t /opt/homebrew/opt/llvm/lib/libLLVMCore.a | grep '\.o$' | head -1
AbstractCallSite.cpp.o
$ ar x .../libLLVMCore.a AbstractCallSite.cpp.o && file AbstractCallSite.cpp.o
Mach-O 64-bit object arm64
```

A Mach-O arm64 `.o` cannot be linked into an `x86_64-unknown-linux-gnu` or
`aarch64-unknown-linux-gnu` ELF executable. The registered LLVM backends only
prove code generation, not a linkable cross toolchain.

No Linux sysroot was found in any of `/opt/sysroot/<triple>`, `/usr/<triple>`,
`/opt/homebrew/sysroot/<triple>`, or an osxcross tree. Without both a target
ELF sysroot *and* target ELF LLVM archives, `clang --target=...` is not a
cmake invocation that can produce a C+ Linux compiler here. Reproduce with:

```
sh tools/build-linux.sh cross x86_64
sh tools/build-linux.sh cross aarch64
```

Both rows report `BLOCKED` with the `file(1)` evidence above. This script never
reports PASS for a step it did not run.

## 3. Native x86_64 Linux build (Colima x86_64 VM)

A running x86_64 Colima VM (`colima-frida-x86`, Ubuntu 24.04 kernel
`6.8.0-117-generic`) provides Docker context `colima-frida-x86`. Its cached
container `grc-x86` is Ubuntu 22.04.5 x86_64. apt access works and the required
dev packages are installable:

```
clang-15 15.0.7, llvm-config-15 15.0.7, cmake 3.22.1, ninja 1.10.1
LLVM/Clang CMake packages: /usr/lib/llvm-15/lib/cmake/{llvm,clang}
```

The source was copied into the container (`/tmp` from macOS is not shared into
this VM; a `-v /tmp/linuxbuild:/src` bind mount is empty here) and built with
`sh tools/build-linux.sh native x86_64`.

CMake configure result (verbatim excerpts):

```
-- libLLVM.dylib is usable: /usr/lib/llvm-15/lib/libLLVM-15.so.1
-- Using LLVM 15.0.7 from /usr/lib/llvm-15/lib/cmake/llvm
-- Using Clang from /usr/lib/llvm-15/lib/cmake/clang
-- Clang resource dir: /usr/lib/llvm-15/lib/clang/15.0.7
-- C+ backend C compiler: /usr/local/bin/clang
-- Configuring done
-- Generating done
```

`clang`/`clang++` symlinks to `clang-15` were created inside the container so
the driver's default backend name resolves. No source warning was suppressed
and no test was disabled.

## 4. aarch64 Linux build: NOT_TESTED (exact blocker)

The `grc-arm64` Colima profile is configured `arch: aarch64` but was not
running. Starting it failed while fetching its cloud image:

```
colima start --profile grc-arm64
level=fatal msg="error starting vm: ... error downloading
'ubuntu-24.04-minimal-cloudimg-arm64-docker.raw.gz': write
/Users/haseeb-mir/Library/Caches/colima/caches/...downloading:
no space left on device"
```

There is no cached aarch64 container image, no aarch64 sysroot, and a Docker
Hub pull from the x86 VM failed with `net/http: TLS handshake timeout` (the
only cached image there is `ubuntu:22.04` amd64). No aarch64 LLVM/Clang dev
packages for a Linux target are installed on the host. Therefore no aarch64
build was attempted and no aarch64 PASS is claimed.

## 5. Status

| Item | Status | Evidence / reason |
| --- | --- | --- |
| host toolchain probe | PASS | clang 23.1.6, llvm-config reports X86 + AArch64 backends |
| Linux sysroot discovery | NOT_TESTED | none of the searched sysroot locations exist |
| host cross build, x86_64 ELF compiler | BLOCKED | LLVM archive member is Mach-O arm64, not ELF x86-64 |
| host cross build, aarch64 ELF compiler | BLOCKED | same Mach-O arm64 archive |
| native build, x86_64 ELF compiler (container) | see build log | clang-15 / LLVM 15.0.7, configure succeeded |
| native build, aarch64 ELF compiler | NOT_TESTED | Colima aarch64 VM start: no space left on device |
| ctest suite | see build log | — |
| C ABI layout check | NOT_TESTED | no ABI-changing source edit was made; no layout was altered |

"C ABI is sacred": this work added only a shell script and this document. No
C struct, union, raw pointer, or header layout was touched, so `sizeof` /
`alignof` of the public C ABI are unchanged by construction.

## 6. Reproduce

```
sh tools/build-linux.sh probe                 # read-only capability table
sh tools/build-linux.sh cross x86_64           # host cross attempt (BLOCKED here)
sh tools/build-linux.sh cross aarch64
# inside an x86_64 Linux container with clang-15 + llvm-15-dev + cmake + ninja:
sh tools/build-linux.sh native x86_64
```

`BUILD_DIR`, `CPLUS_LINUX_SYSROOT`, `LLVM_DIR`, `Clang_DIR` and `LLVM_CONFIG`
are honoured. The script's PASS rows are set only after the corresponding
command exits successfully.
