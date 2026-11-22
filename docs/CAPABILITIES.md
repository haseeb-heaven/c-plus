# Native capability probe

- compiler: Apple clang version 16.0.0 (clang-1600.0.26.6)
- Apple bounds safety (-fbounds-safety): unsupported by this compiler
- __builtin_object_size: supported
- ASan runtime: broken/unavailable on this host
- TSan runtime: broken/unavailable on this host
- strcpy_s (native CRT): unavailable (do not emulate silently)

## Configurable runtime sanitizers

`c+ build --sanitize=none|address|undefined|address,undefined` selects the
sanitizers for a build. The flags are added to both the per-file compile
commands and the final link, so the sanitizer runtime is linked into the
executable. Safe mode defaults to UBSan (`undefined`); legacy-C mode defaults
to `none`. `--no-sanitize` is an alias for `--sanitize=none`.

Apple clang 16's ASan runtime aborts during initialisation on this host
(`sanitizer_malloc_mac.inc:189`). A working ASan backend is Homebrew clang
23 with an explicit SDK, for example:

```
CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang c+ build --sanitize=address \
  prog.c -o prog -- -isysroot $(xcrun --show-sdk-path)
```

`tests/core/sanitizer_suite.py` auto-detects a working backend and asserts
that each heap/stack/UAF/double-free/flexible-array case prints the exact
sanitizer diagnostic, not merely a non-zero exit code.

C+ policy: use installed Clang AST/CFG and supported sanitizers; do not vendor libc/CRT.
