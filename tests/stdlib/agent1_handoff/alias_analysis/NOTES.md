# P0 "Sound alias analysis" — minimal reproducers for Agent 1

Scope: this directory contains **reproducers and evidence only**. No `compiler/**`,
`runtime/**`, header, build-script, or git-index changes were made by this handoff.
Run all commands from the repository root.

## Provenance

- `docs/libc/AGENT1_PRIORITY_HANDOFF.md:16` maps the P0 item "Sound alias analysis"
  to `tests/stdlib/agent1_handoff/alias_analysis/`.
- `docs/libc/AGENT1_COMPILER_GAPS.md` §4 (lines 220–296): capability 3,
  "Alias / exclusivity analysis for class owners". It records that the six
  existing alias/view cases compile cleanly (`check rc=0`, no `[CPLUS-`), proposes
  `[CPLUS-ALIAS]` / `[CPLUS-BORROW]`, and splits "compiler must reject" from
  "runtime mitigation already exists (not enforcement)".
- `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`: the dynamic function-pointer index is
  currently caught only by Clang UBSan, not by C+.
- `docs/libc/api_status_matrix.json:818` (`cplus_at_quick_exit`): "Handler
  function-pointer validity and registration-capacity limits are caller
  responsibility; delegates host semantics" — the only linked evidence for the
  function-pointer case, i.e. **thin**, not enforcement. See also line 14386:
  "raw runtime-index bounds are UBSan-only".

Classification rule (same as `tests/stdlib/compile_time/run_compile_time.sh`):
only a diagnostic containing a literal `[CPLUS-` token counts as tagged C+
enforcement. `check --strict` exiting 0 with no such token is **ACCEPT**, i.e. a
compile-time capability gap here — not a pass.

## Exact `check --strict` results

Command form: `./build/c+ check --strict <file>`. Stdout is one line; stderr is
empty for all five cases; no `[CPLUS-` token appears in any result.

| case | requested | check exit | stderr | observed |
|---|---|---|---|---|
| `two_mutable_aliases.cp` | REJECT `[CPLUS-ALIAS]` | 0 | empty | ACCEPT (gap) |
| `borrow_mutate_same_expression.cp` | REJECT `[CPLUS-ALIAS]`/`[CPLUS-BORROW]` | 0 | empty | ACCEPT (gap) |
| `interior_ptr_before_mutation.cp` | REJECT `[CPLUS-BORROW]` | 0 | empty | ACCEPT (gap) |
| `funcptr_dispatch_dynamic.cp` | REJECT `[CPLUS-SAFE-BOUNDS]` | 0 | empty | ACCEPT (gap) |
| `alias_through_parameter.cp` | REJECT `[CPLUS-ALIAS]` | 0 | empty | ACCEPT (gap) |

Verbatim stdout (stderr empty, exit 0 in every case):

```
$ ./build/c+ check --strict tests/stdlib/agent1_handoff/alias_analysis/two_mutable_aliases.cp
check: tests/stdlib/agent1_handoff/alias_analysis/two_mutable_aliases.cp ok

$ ./build/c+ check --strict tests/stdlib/agent1_handoff/alias_analysis/borrow_mutate_same_expression.cp
check: tests/stdlib/agent1_handoff/alias_analysis/borrow_mutate_same_expression.cp ok

$ ./build/c+ check --strict tests/stdlib/agent1_handoff/alias_analysis/interior_ptr_before_mutation.cp
check: tests/stdlib/agent1_handoff/alias_analysis/interior_ptr_before_mutation.cp ok

$ ./build/c+ check --strict tests/stdlib/agent1_handoff/alias_analysis/funcptr_dispatch_dynamic.cp
check: tests/stdlib/agent1_handoff/alias_analysis/funcptr_dispatch_dynamic.cp ok

$ ./build/c+ check --strict tests/stdlib/agent1_handoff/alias_analysis/alias_through_parameter.cp
check: tests/stdlib/agent1_handoff/alias_analysis/alias_through_parameter.cp ok
```

## Compiler must reject (the requested contract — currently unmet)

These are requirements for Agent 1, **not** observations of current behavior:

1. `two_mutable_aliases.cp` — `[CPLUS-ALIAS]` naming `left` and `right` as
   overlapping mutable borrows of the single owner `v`.
2. `borrow_mutate_same_expression.cp` — `[CPLUS-ALIAS]`/`[CPLUS-BORROW]` naming
   the read through `borrow` in the argument overlapping the mutating
   `push_back` on the same owner in one full-expression.
3. `interior_ptr_before_mutation.cp` — `[CPLUS-BORROW]` naming `p` as a stale
   interior pointer of `s` after `append()` may reallocate.
4. `funcptr_dispatch_dynamic.cp` — `[CPLUS-SAFE-BOUNDS]` dynamic guard on the
   subscript, emitted before the function-pointer load when the extent is known
   and the index is not statically provable in range
   (`docs/libc/DISPATCH_BOUNDS_HANDOFF.md` §"Requested Agent 1 behaviour").
5. `alias_through_parameter.cp` — `[CPLUS-ALIAS]` naming `a` and `b` as two
   aliases of one owner passed from `main()` (interprocedural alias facts).

Acceptance for Agent 1: all five `check --strict` runs exit nonzero **with the
named tag and an invalid-operation description**; the positive controls in
`tests/stdlib/compile_time/` (`vector_valid.cp`, `vector_valid_slice.cp`,
`collections_valid_borrow.cp`, `string_valid_basic.cp`, `string_valid_view.cp`)
remain ACCEPT with no `[CPLUS-` line.

## Runtime API currently refuses (mitigation — never counted as enforcement)

`check --strict` only lowers/type-checks; it does not build or run, so **no
runtime refusal appears in the classified result**. The following supplementary
observations are kept separate from that result. They were produced with
`./build/c+ build --strict --sanitize=none ... -o <tmp>/<name>.exe` and by running
the resulting binary (and, for case 4, a default-sanitizer build).

| case | build (strict, no sanitize) | run | runtime refusal? |
|---|---|---|---|
| `two_mutable_aliases.cp` | exit 0, stdout/stderr empty | exit 0, no output | **No** — checked methods keep the shared buffer coherent; no exclusivity guard exists. |
| `borrow_mutate_same_expression.cp` | exit 0, stdout/stderr empty | exit 0, no output | **No**. |
| `interior_ptr_before_mutation.cp` | exit 0, stdout/stderr empty | exit 1, no output | **No.** Exit 1 comes from this reproducer's own `return c == 'a' ? 0 : 1;` after reading the stale pointer — not from a C+ runtime guard. `string` has no `stale()`/`generation` API analogous to `vector::slice` (`stdlib/string.hp`). |
| `funcptr_dispatch_dynamic.cp` | exit 0, stdout/stderr empty | exit -11 (SIGSEGV), no output | **No C+ refusal.** With the default sanitizer the refusal is Clang UBSan, not C+ (verbatim below). |
| `alias_through_parameter.cp` | exit 0, stdout/stderr empty | exit 0, no output | **No** — same as case 1, across a call boundary. |

Verbatim UBSan stderr for `funcptr_dispatch_dynamic.cp`, default-sanitizer build:

```
tests/stdlib/agent1_handoff/alias_analysis/funcptr_dispatch_dynamic.cp:26:5: runtime error: index 5556 out of bounds for type 'handler_fn[3]' (aka 'void (*[3])(void)')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior tests/stdlib/agent1_handoff/alias_analysis/funcptr_dispatch_dynamic.cp:26:5 in 
```

That run exited -6 (SIGABRT). Per `docs/libc/DISPATCH_BOUNDS_HANDOFF.md` this is
explicitly **not** C+ enforcement, and `api_status_matrix.json:818` records
handler validity as caller responsibility. A sanitizer abort or SIGSEGV must not
be reported as a passing alias/bounds check.

## Discarded probe (recorded, not counted)

An earlier version of `interior_ptr_before_mutation.cp` used `s.data()`; `data()`
is a `string_view` method, not a `string` method, so `check --strict` rejected at
the type layer with exit 1 and this verbatim stderr:

```
tests/stdlib/agent1_handoff/alias_analysis/interior_ptr_before_mutation.cp:16:17: error: [CPLUS-FFI] incompatible integer to pointer conversion initializing 'const char *' with an expression of type 'int'
      const char *p = s.data(); /* interior pointer captured before mutation */
                  ^
```

That `[CPLUS-FFI]` error is a type/FFI diagnostic, **not** alias enforcement.
The case was changed to `string::c_str()` (a real interior-pointer accessor) so
it isolates the requested alias gap. The final five-case results above are the
only ones reported.
