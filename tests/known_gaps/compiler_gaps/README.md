# Known C+ compiler gaps (Agent 1 ownership)

These negative fixtures describe C bugs the CURRENT compiler does not reject.
They were originally written against a different prebuilt compiler binary that
emitted additional `[CPLUS-*` diagnostics. They are kept here, not deleted, so
the gaps remain visible and traceable.

They are deliberately NOT in `tests/compile_fail/` (which must only contain
checks the compiler actually enforces). Do not treat their presence here as a
passing safety check.

| Fixture | Bug pattern | Current behaviour |
|---|---|---|
| bugcat27 | unguarded input-controlled negative index | compiles |
| bugcat30 | multibyte truncation | compiles |
| bugcat32 | widening index OOB | compiles |
| bugcat33 | heap negative index | compiles |
| bugcat35 | signed time overflow | compiles |
| bugcat36 | signed multiply overflow | compiles |
| bugcat37 | division by input zero | compiles |
| bugcat40 | ABI sizeof(function pointer) assumption | compiles |
| bugcat47 | implicit declaration / varargs mismatch | compiles |

See `docs/libc/KNOWN_COMPILER_GAPS.md` and `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`.
