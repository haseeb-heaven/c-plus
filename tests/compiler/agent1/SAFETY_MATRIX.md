# Safety matrix (actual support, not requirements)

| Area | Compile-time | Runtime | STRICT | DEFAULT | No sanitizer | Release |
| --- | --- | --- | --- | --- | --- | --- |
| Array bounds (constant/index/extent) | yes (CPLUS-BOUNDS) | `__cplus_bounds_check` for dynamic indices | reject proven | warn/reject proven | yes | preserved (no code removal) |
| Pointer bounds across decay/calls | yes for recorded extents (direct, nested, returns) | unknown lengths rejected in STRICT | fail closed | warns | yes | preserved |
| Arithmetic signed/div/shift | yes for provable | native guards for runtime add/sub/mul/div/rem/shl/shr | reject provable | warn/reject provable | yes | preserved |
| Integer conversions | narrowing constants only | native value round-trip guard (exit 111) | reject provable | warn + runtime guard | yes | preserved |
| Pointer arithmetic | constant offsets | native formation guard with one-past allowed (exit 101) | reject proven | runtime guard | yes | preserved |
| Initialization | CFG definite-init | runtime for managed | reject | reject proven | yes | preserved |
| Lifetime/ownership | AST + token + RAII checks | runtime table for managed | reject live-resource cleanup bypass | warn | yes | preserved |
| Type safety | Clang Sema + selected C+ checks | n/a | subset | subset | yes | preserved |
| RAII cleanup | generated C | destructor calls on supported exits | reject goto/longjmp over live resource | warn | yes | preserved |

Unsupported / partial: general races, cross-TU ownership ABI, field/container
element lifetimes, raw malloc/free UAF in default builds, `__int128` arithmetic,
macro/header arithmetic sites, full pointer provenance. See known_gaps.md.
