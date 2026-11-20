# C2y features not implemented in C+ MVP

- delimited escapes `\x{...}`, `\o{...}`, `\u{...}` — no lowering yet
- full `<stdbit.h>` byte-order utilities — only rotation helpers provided
- labeled `break label;` / `continue label;` — no lowering yet
- `_Countof` keyword itself — C+ provides `countof()` in `cplus/cplus.h`
