# Documentation index

Start here. Everything below is verification evidence for the C+ standard
library layer: a checked, additive C API that keeps the ordinary C ABI.

## Current authoritative documents

| Topic | Document |
| --- | --- |
| **Core memory philosophy (Goose-style)** | [CORE_PHILOSOPHY.md](CORE_PHILOSOPHY.md) |
| Architecture and layout | [ARCHITECTURE.md](ARCHITECTURE.md), [STRUCTURE.md](STRUCTURE.md) |
| Standard library API | [STDLIB.md](STDLIB.md), [MODERN_STDLIB.md](MODERN_STDLIB.md) |
| Checked C layer | [SAFE_CRT.md](SAFE_CRT.md), [LOGGING.md](LOGGING.md) |
| Allocators / ownership | [stdlib/allocator/](stdlib/allocator/), [stdlib/MODERN_ALLOCATORS.md](stdlib/MODERN_ALLOCATORS.md) |
| ABI preservation | [ABI_SAFE_LAYER.md](ABI_SAFE_LAYER.md), [abi/](abi/) |
| Security evidence | [security/](security/) — start with `RED_BLUE_20_FINAL.md` |
| Limitations and honest scope | [LIMITATIONS.md](LIMITATIONS.md), [THREAT_MODEL.md](THREAT_MODEL.md) |
| Modern 3D/GUI/CLI libraries | [stdlib/MODERN_LIBS_3D_GUI_CLI.md](stdlib/MODERN_LIBS_3D_GUI_CLI.md) |
| Edge-case campaigns | [stdlib/EDGE100_FINAL.md](stdlib/EDGE100_FINAL.md) |
| Captured runtime demonstrations | [../examples/screenshots/README.md](../examples/screenshots/README.md) |

## Conventions

- **Evidence, not proof.** A passing finite campaign is evidence for the
  recorded inputs, host and toolchain — not a memory-safety theorem.
- **No false claims.** Windows runtime, ASan/UBSan on this host, Rust
  equivalence and live-game success are explicitly marked BLOCKED or
  non-equivalent where applicable.
- `FINAL_*`, `AGENT*_*` and `*_PROOF.md` files are historical campaign records.
  Prefer the consolidated documents linked above; older reports are retained
  only as traceable evidence and are not the current summary.
EOF
echo "created docs/README.md"