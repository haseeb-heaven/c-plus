# Windows cross-build: verified status (2026-11-28)

Scope: cross-compile the C+ compiler C++ sources for Windows from this macOS
host. **No Windows executable was run.** Every verdict below is a build-time
observation only; there is no runtime claim. "NOT_TESTED" means the check was
not possible here, not that it passed.

## 1. Verified host and toolchain inventory

Host: Darwin 27.2.0, arm64 (Apple M-series).

| Tool | Path / version | Windows capability |
| --- | --- | --- |
| `x86_64-w64-mingw32-gcc` | `/opt/homebrew/bin`, GCC 16.2.0 | present; produced a PE32+ console EXE |
| `x86_64-w64-mingw32-g++` | `/opt/homebrew/bin` | present; libstdc++ 16.2.0 headers available |
| `aarch64-w64-mingw32-gcc` | absent | NOT_TESTED; no ARM64 Windows sysroot on host |
| `clang` / `clang++` | Homebrew LLVM 23.1.6 | both Windows GNU triples recognised; no `lld`/`lld-link` installed |
| LLVM/Clang dev headers | `/opt/homebrew/opt/llvm/include` | host-configured (UNIX), not Windows-configured |
| LLVM/Clang static archives | `/opt/homebrew/Cellar/llvm/23.1.6/lib` | Mach-O arm64, not PE/COFF |
| MinGW sysroots | `toolchain-x86_64`, `toolchain-i686` only | no aarch64 sysroot |

Exact inventory commands:

    command -v x86_64-w64-mingw32-gcc aarch64-w64-mingw32-gcc clang llvm-config
    clang --version
    llvm-config --version --prefix --includedir --libdir
    x86_64-w64-mingw32-gcc -print-sysroot
    find /opt/homebrew /usr/local -maxdepth 5 -type d -iname "*aarch64*w64*"
    ls /opt/homebrew/opt/llvm/bin/ | grep -iE "^ld|^lld"

## 2. Result matrix

| Stage | x86_64 | aarch64 |
| --- | --- | --- |
| clang accepts `--target=...-w64-windows-gnu` | PASS | PASS |
| Windows CRT/C++ sysroot | PASS (Homebrew `mingw-w64` 14.0.0_3) | BLOCKED (none) |
| C source -> COFF object | PASS | PASS (freestanding only) |
| C++ source -> COFF object | PASS (probe with libstdc++) | BLOCKED (host libc++ headers) |
| MinGW gcc links a plain C EXE | PASS (PE32+ artifact) | NOT_TESTED (no compiler) |
| Compiler TUs compiled to COFF | 7 / 13 PASS, 6 BLOCKED | NOT_TESTED (no sysroot) |
| LLVM archives usable for PE link | BLOCKED (Mach-O arm64) | BLOCKED (Mach-O arm64) |
| Final `c+.exe` link | BLOCKED | NOT_TESTED |
| Runtime execution | NOT_TESTED | NOT_TESTED |

Compile flags used (mirrors `CMakeLists.txt`):

    clang++ --target=x86_64-w64-windows-gnu \
      --sysroot=/opt/homebrew/Cellar/mingw-w64/14.0.0_3/toolchain-x86_64 \
      -std=c++20 -fno-rtti -isystem $(llvm-config --includedir) \
      -Icompiler -Iruntime/include \
      -DCLANG_CPLUS_LANG_STRING=\"C+\" -DCPLUS_VERSION_STRING=\"0.2.0\" \
      -c <source> -o <object>

The 7 sources that compile: `Diagnostics.cpp`, `Sema/SemaCPlus.cpp`,
`Sema/CPlusLexer.cpp`, `Sema/CPlusRewriter.cpp`, `Sema/RaiiLowering.cpp`,
`Sema/StaticBoundsCheck.cpp`, `runtime/CPlusRuntime.cpp`.
The 6 that block: `SourceManager.cpp`, `Analysis/OwnershipAnalysis.cpp`,
`Sema/AstSafetyCheck.cpp`, `Sema/TypeChecker.cpp`, `Driver/Driver.cpp`,
`Main.cpp` (all at the same LLVM header line; see blocker B1).

Objects were verified as COFF, for example:

    $ file build-windows-x86_64/obj/compiler_Diagnostics.o
    Intel amd64 COFF object file, not stripped, 252 sections, ...

A plain C cross-link via MinGW gcc also produced a PE artifact (build only,
never executed):

    $ x86_64-w64-mingw32-gcc /tmp/probe.c -o /tmp/probe_gcc.exe
    $ file /tmp/probe_gcc.exe
    PE32+ executable (console) x86-64, for MS Windows

## 3. Exact blockers

### B1 -- LLVM config headers are generated for the macOS host

`llvm/Config/llvm-config.h` from Homebrew contains `#define LLVM_ON_UNIX 1`,
while the clang driver defines `_WIN32` for the Windows triple. LLVM headers
therefore take the UNIX branch and require POSIX types that MinGW does not
expose. This is the header-level form of "LLVM/Clang headers unavailable for
Windows targets".

    $ grep -nE "LLVM_ON_(UNIX|WIN32)" /opt/homebrew/opt/llvm/include/llvm/Config/llvm-config.h
    58:#define LLVM_ON_UNIX 1
    $ clang --target=x86_64-w64-windows-gnu -dM -E -x c /dev/null | grep _WIN32
    #define _WIN32 1

First diagnostic for each of the 6 failed translation units:

    /opt/homebrew/Cellar/llvm/23.1.6/include/llvm/Support/FileSystem.h:140:3:
    error: unknown type name 'uid_t'; did you mean 'pid_t'?

Neither `-D_POSIX_C_SOURCE=200809L` nor `-D_GNU_SOURCE` changes this: the
mismatch is in the generated config header, not in feature macros. Fixing it
would require LLVM headers generated for a Windows build, not a source edit
in C+.

### B2 -- LLVM/Clang static archives are host Mach-O objects

The archives that `CMakeLists.txt` links into `cplus_core` were built for
macOS arm64. They cannot be linked into a PE/COFF executable on any host.

    $ llvm-config --libdir
    /opt/homebrew/Cellar/llvm/23.1.6/lib
    $ ar x /opt/homebrew/Cellar/llvm/23.1.6/lib/libLLVMSupport.a blake3.c.o
    $ file blake3.c.o
    Mach-O 64-bit object arm64
    $ x86_64-w64-mingw32-ld -r blake3.c.o -o /tmp/force.o
    blake3.c.o: file not recognized: file format not recognized

The archive index is BSD-style (`__.SYMDEF`), so MinGW `ld` cannot resolve
LLVM symbols from it either. A Windows-target LLVM/Clang dev package with
PE/COFF component archives is required to finish the link.

### B3 -- Homebrew clang has no Windows linker

No `lld`, `lld-link` or `ld.lld` is installed with this LLVM formula, so the
clang driver falls back to Apple `ld`, which rejects the cross flags:

    $ clang --target=x86_64-w64-windows-gnu --sysroot=$TC probe.c -o probe.exe
    ld: warning: -m is obsolete
    ld: unknown option: --sysroot=/opt/homebrew/Cellar/mingw-w64/14.0.0_3/toolchain-x86_64
    clang: error: linker command failed with exit code 1

Plain `x86_64-w64-mingw32-gcc` linking does work; clang cross-linking does not.

### B4 -- ARM64 Windows target has no sysroot or C++ standard library

`aarch64-w64-mingw32-gcc` is absent and no aarch64 Windows sysroot exists on
this host. The clang driver probes a non-installed prefix:

    $ command -v aarch64-w64-mingw32-gcc
    (nothing)
    $ clang --target=aarch64-w64-windows-gnu -### -c probe.c -o /tmp/x.o
    -internal-isystem /opt/homebrew/Cellar/llvm/23.1.6/aarch64-w64-mingw32/include
    (directory does not exist)
    $ clang --target=aarch64-w64-windows-gnu -c probe.c -o /tmp/x.o
    probe.c:1:10: fatal error: 'stdio.h' file not found

Code generation itself is present (compile only, no headers):

    $ echo "int add(int a,int b){return a+b;}" > free.c
    $ clang --target=aarch64-w64-windows-gnu -c free.c -o free.o
    $ file free.o
    Aarch64 COFF object file, not stripped, 7 sections, ...

Host libc++ headers also carry Apple availability markup and cannot be reused:

    $ clang++ --target=aarch64-w64-windows-gnu -stdlib=libc++ \
        -isystem $(llvm-config --includedir)/c++/v1 -c free.cpp -o free.o
    /opt/homebrew/Cellar/llvm/23.1.6/include/c++/v1/__configuration/availability.h:239:4:
    error: "It looks like you are trying to enable vendor availability markup,
    but you haven't defined the corresponding macros yet!"

### B5 -- No Windows runtime

This macOS host cannot execute a Windows PE binary without an emulator or a
Windows machine. Consequently all runtime verification is NOT_TESTED by
design; the script never runs any artifact it produces.

## 4. Reproduce

    sh tools/build-windows.sh                  # x86_64 probe + source compile
    TARGET=aarch64 sh tools/build-windows.sh   # arm64 probe
    sh tools/build-windows.sh --link           # also attempt final link

The script prints PASS / BLOCKED / NOT_TESTED per stage, quotes the first
compiler diagnostic on each failure, and always reports:

    FULL WINDOWS BUILD: NOT_TESTED
    RUNTIME: NOT_TESTED (no Windows executable was executed)

It never suppresses a compiler warning or test, and never converts a blocked
stage into a pass. Overrides: `TARGET`, `CLANG`, `CLANGXX`, `LLVM_CONFIG`,
`LLVM_PREFIX`, `MINGW_PREFIX`, `SYSROOT`, `BUILD_DIR`.

## 5. What would unblock a real Windows build

1. LLVM/Clang development package built for the Windows GNU target: generated
   config headers with `LLVM_ON_WIN32`, plus PE/COFF static component archives.
2. A Windows ARM64 sysroot with CRT headers, libs and a C++ standard library
   (and the matching `aarch64-w64-mingw32` toolchain or equivalent).
3. `lld-link` (or MinGW `ld`) able to consume those Windows-target archives.
4. Re-derive the runtime config macros in `CMakeLists.txt` (`CPLUS_CLANG_BIN`,
   `CPLUS_CLANG_RESOURCE_DIR`, `CPLUS_RUNTIME_LIB`) for the Windows install
   rather than embedding macOS host paths.
5. A Windows host or emulator before any runtime claim is made.
