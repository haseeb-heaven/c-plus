# Safety matrix (actual support, not requirements)

| Area | Compile-time | Runtime | STRICT | DEFAULT | No sanitizer | Release |
| --- | --- | --- | --- | --- | --- | --- |
| Array bounds (constant/index/extent) | yes (CPLUS-BOUNDS) | `__cplus_bounds_check` for dynamic indices | reject proven | warn/reject proven | yes | preserved (no code removal) |
| Pointer bounds across decay/calls | yes for recorded extents (direct, nested, returns) | unknown lengths rejected in STRICT | fail closed | warns | yes | preserved |
| Arithmetic signed/div/shift | yes for provable | runtime-variable overflow NOT checked natively | reject provable | warn/reject provable | yes | preserved |
| Initialization | CFG definite-init | runtime for managed | reject | reject proven | yes | preserved |
| Lifetime/ownership | AST + token + RAII checks | runtime table for managed | reject live-resource cleanup bypass | warn | yes | preserved |
| Type safety | Clang Sema + selected C+ checks | n/a | subset | subset | yes | preserved |
| RAII cleanup | generated C | destructor calls on supported exits | reject goto/longjmp over live resource | warn | yes | preserved |

Unsupported / partial: general races, cross-TU ownership ABI, field/container
element lifetimes, raw malloc/free UAF in default builds, runtime-variable
signed overflow (no native check), full pointer provenance. See known_gaps.md.
