# C2y-compatible features in C+

C+ currently emits **gnu17** C through the host Clang driver, not C2y/C23.
Where a C2y feature can be lowered safely to portable C, C+ provides it as a
source-level extension. Features that require unsupported Clang machinery are
rejected or left for the roadmap.

| C2y feature | C+ status | notes |
| --- | --- | --- |
| `defer statement;` | supported | lowered to deterministic cleanup in LIFO order at normal exit, `return`, `break`, `continue`; `longjmp`/`exit` still bypass it |
| `countof(arr)` | supported | provided by `cplus/cplus.h`; a decayed pointer fails a compile-time assertion; `.len` remains the C+ equivalent |
| `if (T x = init; cond)` | supported | lowered to a scoped block; `else` handled |
| `0o755` octal literals | supported | rewritten to classic leading-zero octal |
| `case 1 ... 5:` ranges | supported | Clang GNU extension, available in gnu17 mode |
| rotation helpers | partial | `cplus_rotl32` / `cplus_rotr32` in `cplus/cplus.h`; no full `<stdbit.h>` |
| `unreachable()` | rejected | C23/C2y construct with cascading UB |
| `[[unsequenced]]` / `[[reproducible]]` | rejected | attributes can lie to the optimiser |
| `nullptr`, `_BitInt` | rejected | no safe lowering in this backend |
| delimited escapes `\x{...}` | not implemented | see `tests/known_gaps/c2y/` |
| labeled `break label;` / `continue label;` | not implemented | see `tests/known_gaps/c2y/` |
| empty parameter list change | avoided | C+ targets gnu17; explicit `(void)` is recommended |

## `defer` example

```c
int process(void) {
    FILE *fp = fopen("x", "r");
    defer fclose(fp);
    char *buf = malloc(1024);
    defer free(buf);
    if (!fp || !buf) return -1;
    /* ... */
    return 0;
}
```

The generated C contains the cleanup calls in reverse order before every
supported exit; inspect it with `--emit-c`.

## Notes on "demon removal"

C2y removes roughly 45 undefined behaviours (zero-length null-pointer string
operations, `NULL + 0`, empty translation units, duplicate typedefs, and
others). C+ does not claim those C2y rules because it compiles as gnu17;
where C+ already rejects the dangerous form (for example zero-size
allocations or raw `realloc`), that is a C+ rule rather than a C2y rule.

The remaining C/C2y undefined behaviours — use-after-free, out-of-bounds
dereference, signed overflow, data races and uninitialised reads — are
exactly where C+ adds its own mechanisms: managed allocations with side-table
checks, static bounds analysis, deterministic RAII, and promoted Clang
diagnostics. See `docs/THREAT_MODEL.md` for what is and is not covered.
