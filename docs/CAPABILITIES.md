# Native capability probe

- compiler: Apple clang version 16.0.0 (clang-1600.0.26.6)
- Apple bounds safety (-fbounds-safety): unsupported by this compiler
- __builtin_object_size: supported
- ASan runtime: broken/unavailable on this host
- TSan runtime: broken/unavailable on this host
- strcpy_s (native CRT): unavailable (do not emulate silently)

## Complete sanitizer matrix

`c+ build --sanitize=LIST` accepts a comma-separated list of:
`none`, `address` (ASan), `undefined` (UBSan), `memory` (MSan),
`thread` (TSan), `leak` (LSan), `cfi`, `integer` (unsigned overflow and
suspicious conversions). `-fsanitize=...` is accepted as a gcc-compatible
alias, and `--no-sanitize` disables everything.

Selection is validated before any compilation:

- incompatible combinations are rejected with an explicit error
  (`address+thread`, `address+memory`, `thread+memory`, `leak+address/thread/memory`,
  `cfi+memory/thread`);
- when a sanitizer is explicitly requested, the selected backend is probed by
  compiling **and running** a defect program. A successful compile is not
  sufficient — the expected diagnostic string must be printed. Unsupported
  sanitizers produce `error: sanitizer 'X' is not supported by the selected
  backend ...`;
- every sanitizer build gets `-g` and `-fno-omit-frame-pointer`, and CFI also
  gets `-flto -fvisibility=hidden -fno-sanitize-trap=cfi` so violations print
  a diagnostic instead of trapping silently.

Flags are appended to the shared backend argument list used for the generated C,
the C+ runtime source, and the final link. When sanitizers are enabled the
runtime is recompiled from `runtime/cplus_runtime.c` with the same flags instead
of linking the prebuilt `libcplus_runtime.a`; if that source cannot be found a
warning is printed instead of silently using an uninstrumented library. The
in-process AST checker deliberately strips sanitizer flags (it only keeps
`-I`, `-D`, `-std=`, and the Clang resource dir).

### Measured support (this host: macOS arm64)

| Sanitizer | Apple clang 16 | Homebrew clang 23 + `-isysroot` | Evidence |
| --- | --- | --- | --- |
| ASan | compiles; runtime aborts in `sanitizer_malloc_mac.inc:189` | detects heap/stack/global overflow, UAF, double-free, flexible-array overflow | `tests/core/sanitizer_suite.py` |
| UBSan | detects signed overflow, shifts, null/misaligned access | same | suite |
| MSan | unsupported by compiler (`unsupported option`) | unsupported by compiler | explicit driver diagnostic |
| TSan | runtime aborts in interceptors | detects data race | suite (`--legacy-c`) |
| LSan | unsupported by compiler | detects leak | suite |
| CFI | unsupported by compiler | detects invalid indirect call with LTO flags | suite (`--legacy-c`) |
| Integer | detects unsigned overflow | same | suite |

Platforms not available here (Linux x86-64, Linux arm64, macOS x86-64) are
**not claimed as tested**. The capability probe runs on the selected backend
at build time, so those platforms get the same validation rather than an
assumption. MSan is expected to be available only on Linux x86-64; elsewhere it
is rejected with the explicit diagnostic above.

C+ policy: use installed Clang AST/CFG and supported sanitizers; do not vendor libc/CRT.

## Ownership boundary with Agent 2

The C+ runtime (`runtime/cplus_runtime.c`) uses system `malloc`/`free` with a
side-table of live allocations, a recent-free cache, and an optional delayed
free queue. It has **no private arenas, suballocators, or ASan manual poisoning
calls**. Therefore no manual poisoning was added — temporal detection comes
from the backend ASan runtime instrumenting system allocations and the
instrumented C+ runtime object. Agent 2 owns allocator implementation and any
future custom poisoning; sanitizer tests exercise the real `cplus_alloc`,
`cplus_zalloc`, `cplus_realloc` and `cplus_release` paths through the runtime
source instrumentation described above.

