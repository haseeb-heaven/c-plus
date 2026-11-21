# Native capability probe

- compiler: Apple clang version 16.0.0 (clang-1600.0.26.6)
- Apple bounds safety (-fbounds-safety): unsupported by this compiler
- __builtin_object_size: supported
- ASan runtime: broken/unavailable on this host
- TSan runtime: broken/unavailable on this host
- strcpy_s (native CRT): unavailable (do not emulate silently)

C+ policy: use installed Clang AST/CFG and supported sanitizers; do not vendor libc/CRT.
