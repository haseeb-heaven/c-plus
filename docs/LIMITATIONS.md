# Limitations and honest status (cross-platform)

C+ is a small, working feature layer over C, not a complete new language
implementation. It lowers to plain C and reuses the host C compiler, Clang AST
checks and the host CRT. This page is the single consolidated statement of what
is implemented, what is partial, and what is out of scope on each platform.
Where a guarantee depends on a runtime check, the check is named.

Evidence in this page is either a local run on this macOS arm64 host or a CI
job definition. A CI job that has not been observed is labelled CI-ONLY, never
"passing". Older notes cite line numbers in a previous revision of this file;
use the section names below, not those stale line numbers.

## 1. Compile and runtime support matrix

| Target | Compile | Runtime | Status / evidence |
| --- | --- | --- | --- |
| macOS arm64 | yes | yes | Only locally executed target. System `/usr/bin/clang` (Apple clang 16.0.0) is the default backend; SDK `MacOSX.sdk` 15.2. See [PR32 evidence](abi/PR32_LIBC_PORTABILITY.md). |
| macOS x86-64 | not available here | not tested | No runner on this host; not claimed. |
| Linux x86-64 | CI-only | CI-only | `ubuntu-latest` jobs `linux-crt`, `asan-libc-crt`, `linux-x86-64` and `ubuntu-24.04` job `stdlib-deep-family-integration` in [libc-crt](../.github/workflows/libc-crt.yml) / [libc-portability](../.github/workflows/libc-portability.yml). Not run or observed locally. |
| Linux arm64 | no runner | no evidence | EXPERIMENTAL. No arm64 job exists; must not be claimed supported. |
| Windows PE x86-64 | compile-only in CI | host/engine-specific only | `windows-compile-only` job on `windows-latest` compiles a PE `.exe`, explicitly with NO runtime. Local MinGW GCC 16.2.0 PE compile passes. See §3. |
| Windows x86 (32-bit DLL) | script only | unverified | DLL build script exists; no ABI/live-game proof. See [STATUS](STATUS.md). |

The backend is resolved by `findClangBinary()` in
[Driver.cpp](../compiler/Driver/Driver.cpp). `CPLUS_CLANG` overrides it in
DEFAULT/LEGACY; STRICT Safe mode deliberately ignores environment overrides so
a stub backend cannot bypass a safety gate. LLVM/Clang development packages are
needed to build the driver, not to run programs it produces.

## 2. Sanitizers: exact caveats

`c+ build --sanitize=LIST` accepts `none`, `address` (ASan), `undefined`
(UBSan), `memory` (MSan), `thread` (TSan), `leak` (LSan), `cfi`, `integer`.
Incompatible combinations are rejected before any compilation
(`address+thread`, `address+memory`, `thread+memory`, `leak` with
address/thread/memory, `cfi` with memory/thread). An explicitly requested
sanitizer is accepted only after a compile-**and-run** probe prints the
expected diagnostic; a successful compile alone is not enough. See
[CAPABILITIES](CAPABILITIES.md).

On this host (macOS arm64, selected backend `/usr/bin/clang`, Apple clang 16):

- **ASan driver is unsupported here.** The driver probe rejects every
  `--sanitize=address` build with
  `error: sanitizer 'address' is not supported by the selected backend`; no
  executable is produced and no pass is claimed. Invoked directly, that
  clang's ASan runtime aborts even for `int main(void){return 0;}` with
  `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`. Homebrew
  clang 23.1.6 works locally **only** with an explicit
  `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`; without it
  the default `MacOSX27.sdk` is absent and linking fails. That is a toolchain
  workaround for local evidence, not a language guarantee. See
  [ASAN_AUDIT](libc/ASAN_AUDIT.md).
- **TSan interceptors fail on Apple clang 16.** The runtime requires
  `DYLD_INSERT_LIBRARIES=.../libclang_rt.tsan_osx_dynamic.dylib`; without it the
  probe reports `Interceptors are not working` and aborts. The driver probe
  therefore rejects `thread` on the selected backend. TSan is run separately
  in Linux CI. Homebrew clang 23 + `-isysroot` detects races locally. See
  [libc COVERAGE](libc/COVERAGE.md).
- **MSan** is rejected by the compiler on this host; it is expected available
  only on Linux x86-64. **LSan** and **CFI** are unsupported by the selected
  backend. **UBSan** and `integer` work and are the only sanitizer backstops
  claimed locally.
- The Agent 2 sanitizer matrix recorded 72 builds attempted, 27 executed,
  23 PASS, 4 FAIL (`--sanitize=integer` unsigned overflow in runtime hashing),
  45 rejected as UNSUPPORTED; 0 of 9 selected tests ran under ASan. See
  [SANITIZER_MATRIX](../tests/stdlib/agent2/SANITIZER_MATRIX.md).

Dynamic indices into fixed-size arrays are checked by C+ itself, not by a
sanitizer: safe builds insert `__cplus_bounds_check` after static analysis.
`c+ check` produces no executable and `--legacy-c` inserts no checks. A clean
UBSan run is not a memory-safety proof.

## 3. Windows PE: compile-only vs runtime

- The [libc-portability](../.github/workflows/libc-portability.yml) job
  `windows-compile-only` records toolchain versions and compiles a trivial C
  source to a PE `.exe`. It states explicitly that runtime is NOT TESTED and
  never invokes the game or Wine. That is the only general Windows claim.
- Local MinGW GCC 16.2.0 produces a PE `.exe` (compile PASS), but the libc+
  headers do not compile under MinGW (`sigset_t` errors), so a Windows libc+
  link is not available. See [PR32](abi/PR32_LIBC_PORTABILITY.md) §2–3.
- A host-specific runtime result does exist: Highball/Wine engine
  `x64-crossover26.3-r27` runs genuine PE32+ apps when
  `libwinpthread-1.dll` and `libgcc_s_seh-1.dll` sit beside the exe and the
  bottle state is clean. Eight real C+ apps produced identical marker lines
  on macOS arm64 and Windows PE32+
  (`CROSS_PROOF 58 58 0`; [proof](../proof/real_apps_cross_platform/)). This
  is evidence for that host/engine pair only, **not** for all Windows
  configurations, and it does not upgrade the CI job to a runtime gate.

## 4. BSD / glibc feature-macro differences

The installed header [cplus_libc.h](../libc/install-include/cplus_libc.h)
defines `_POSIX_C_SOURCE 200809L` when neither `_WIN32` nor the macro is
already defined. Several APIs are host-dependent and are guarded rather than
emulated silently:

- `timespec_get`, `clock_gettime`, `nanosleep` are declared only when
  `!_WIN32` and the glibc feature macros `__USE_POSIX199309` / `__USE_ISOC11`
  (or a non-glibc libc) allow them.
- `qsort_r` has incompatible BSD/macOS and glibc argument orders. The wrapper
  uses the BSD order on macOS/BSD and a trampoline on glibc; on an unknown ABI
  it refuses the call rather than guessing the order. See
  `libc/src/stdlib/stdlib_extra6.c`.
- `memset_explicit` has two mutually exclusive implementations: the host one
  and a vendored LLVM one guarded by
  `#if !defined(__GLIBC__) && !defined(CPLUS_C23_HOST_HAS_MEMSET_EXPLICIT)`.
  The guard avoids a duplicate-provider link collision.
- `strlcpy` / `strlcat` delegate to the host BSD functions (present on macOS);
  they are C23 additions, not portable ISO C17.
- `signal()` restart semantics differ between BSD and SysV; handlers must be
  async-signal-safe, and `sigaction` is preferred in multithreaded programs.
- C11 `_Generic` macros in `<stdatomic.h>` / `<tgmath.h>` are used through the
  host header. No fake `cp_*` linkable symbol is invented for a type-generic
  macro; compile-only consumer tests cover that pattern.

## 5. libcurl version floor and C+ fallback

`stdlib/http.hp` links libcurl 8.7.1 via `pkg-config`; it is never vendored.
The protocol-policy setopt uses `CURLOPT_PROTOCOLS_STR` /
`CURLOPT_REDIR_PROTOCOLS_STR`, available only when
`LIBCURL_VERSION_NUM >= 0x075500` (libcurl 7.85.0). Below that floor the C+
(`cp`) fallback branch uses the legacy `CURLOPT_PROTOCOLS` /
`CURLOPT_REDIR_PROTOCOLS` long mask `CURLPROTO_HTTP | CURLPROTO_HTTPS`. Both
branches fail closed with `CPLUS_EXIT_UNSUPPORTED` if setopt fails. See
[http.hp](../stdlib/http.hp) and the provenance record in
[DEPENDENCY_PROVENANCE](libc/DEPENDENCY_PROVENANCE.md). On this host libcurl
development files are absent, so HTTP modules have static source inspection
only and no runtime safety claim.

## 6. No macOS-only hack may leave Linux/Windows broken

- The `#ifdef __APPLE__` branch in `findClangBinary()` only prefers the system
  clang because it resolves the installed SDK. It does not change generated
  code, C ABI or checks; the generic PATH and compiled-in candidates remain.
- The Homebrew clang 23 + `-isysroot` ASan path is local evidence only. Never
  gate a safety feature on it, and never add an Apple-only `#ifdef` that
  removes a bounds/lifetime check or alters a struct layout elsewhere.
- C ABI is fixed: raw pointers stay one word; metadata is out-of-band. No
  platform-specific fat pointer, tag or calling-convention change is allowed.
- Any change must be re-verified on Linux x86-64 CI and the Windows
  compile-only job; a green macOS run alone is not a portability claim.

## 7. Language and safety limits (condensed)

Supported: ordinary C passthrough; `resource struct` with `T_drop`;
scope-exit and pre-`return` cleanup; `cplus_move`; compile-time copy rejection;
managed allocation with double/invalid release detection; known-extent bounds
checks; `.len`; `auto` (`__typeof__`); `.cp`/`.hp`; format-free `print`/`scan`;
default arguments; simple range-`for`; lightweight classes/method sugar;
mutex primitives; `--legacy-c` compatibility (unsafe).

Not implemented: full Rust-style borrow checking, typed lifetimes, aliasing or
exclusivity rules; general temporal safety and UAF detection; data-race
verification or a verified lock discipline; cross-TU resource registration;
inheritance, virtual dispatch and access control; generic reflection; leak
detection for borrowed pointers; full ISO C coverage.

Not caught with raw C idioms: missing terminator, macro double evaluation,
flexible-array copy, signed-to-unsigned comparison warning, cross-TU ABI
mismatch, pointer-reassignment leak, and general races. `goto`/`longjmp`
bypassing live cleanup is a STRICT error; plain jumps warn. `LangFeatures.h`
names for `move`, `borrow`, `cplus_trusted`, `cplus_no_cleanup` are planned,
not implemented. See [known gaps](../tests/known_gaps/README.md) and
[RUST_COMPARISON](RUST_COMPARISON.md).

`--legacy-c` turns every check off and is for compatibility experiments, not
safe code. Performance numbers in [benchmarks](../benchmarks/run_bench.sh)
are host-specific measurements, not portability or safety evidence.

## 8. Evidence index

- Compiler capabilities and sanitizer support: [CAPABILITIES](CAPABILITIES.md)
- Portability probes and platform matrix: [PR32](abi/PR32_LIBC_PORTABILITY.md)
- ASan host audit: [ASAN_AUDIT](libc/ASAN_AUDIT.md)
- libc coverage and tool limits: [COVERAGE](libc/COVERAGE.md),
  [COVERAGE_TOOL_LIMITATIONS](stdlib/COVERAGE_TOOL_LIMITATIONS.md)
- Build/test status: [STATUS](STATUS.md)
- Cross-platform runtime proof: [proof](../proof/real_apps_cross_platform/)
- CI definitions: [libc-crt](../.github/workflows/libc-crt.yml),
  [libc-portability](../.github/workflows/libc-portability.yml)
