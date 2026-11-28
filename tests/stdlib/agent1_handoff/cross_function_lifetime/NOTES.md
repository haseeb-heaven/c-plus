# Handoff — P0 "Cross-function lifetime analysis"

Agent 1 of 12 (`agent/modern-stdlib`). This directory contains **minimal,
evidence-only** reproducers. Only files under
`tests/stdlib/agent1_handoff/cross_function_lifetime/` were created or modified.
No git add/commit; no compiler, runtime, header, or test-runner edits. All five
`.cp` files were passed **only** to `c+ check` (lower/type-check, no executable
written). No binary was built or run, and no runtime behavior is claimed here.

## Environment

- cwd: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
- `./build/c+ --version` → `C+ 0.1.0`, `Backend: host Clang/LLVM`, exit 0
  (`results/cplus-version.log`).
- Command run for every case:

  ```
  ./build/c+ check --strict tests/stdlib/agent1_handoff/cross_function_lifetime/<case>.cp
  ```

- Raw stdout, stderr, and exit code are stored per case in
  `results/<name>.check.stdout`, `results/<name>.check.stderr`,
  `results/<name>.check.exit`, and consolidated in
  `results/<name>.check-strict.log`.

## Cases and exact results

All five cases are expected `REJECT` (they return or retain a reference to
storage whose owner dies at a function boundary).

| Case | Unsafe shape | check exit | `[CPLUS-` tag? | Classification |
| --- | --- | --- | --- | --- |
| `cfl_string_view_from_local.cp` | `string_view` returned from a local `string` | 0 | none | **ACCEPT** — capability gap |
| `cfl_vector_slice_from_local.cp` | `slice` returned from a local `vector` | 0 | none | **ACCEPT** — capability gap |
| `cfl_store_local_address.cp` | `&local` passed to a callee that stores it | 0 | none | **ACCEPT** — capability gap |
| `cfl_borrowed_file_from_local_stream.cp` | borrowed `file` wrapper returned around a local `FILE*` | 0 | none | **ACCEPT** — capability gap |
| `cfl_pointer_to_parameter.cp` | address of a by-value parameter returned | 1 | none | **UNTAGGED rejection** (Clang `-Wreturn-stack-address`, not C+ enforcement) |

Verbatim output for the four accepted cases:

```
check: tests/stdlib/agent1_handoff/cross_function_lifetime/<case>.cp ok
```

with empty stderr and exit 0. Verbatim stderr for
`cfl_pointer_to_parameter.cp` (stdout empty, exit 1):

```
tests/stdlib/agent1_handoff/cross_function_lifetime/cfl_pointer_to_parameter.cp:10:13: error: address of stack memory associated with parameter 'value' returned [-Werror,-Wreturn-stack-address]
   10 |     return &value;             /* address of parameter outlives the call */
      |             ^~~~~
1 error generated.
```

### Classification rule (same as `tests/stdlib/compile_time/run_compile_time.sh`)

- **Tagged REJECT**: exit != 0 **and** literal `[CPLUS-` token in stdout/stderr.
  **No case in this directory reaches this state today.**
- **UNTAGGED / PARTIAL**: exit != 0 but no `[CPLUS-` token. Only
  `cfl_pointer_to_parameter.cp`, caught by the Clang backend's
  `-Wreturn-stack-address` promoted through `-Werror`. This is a syntactic
  direct-return warning; it is not a C+ ownership/lifetime proof and does not
  generalize to the other four shapes.
- **ACCEPT (capability gap)**: exit 0, no diagnostic. The four remaining cases.

This matches the existing handoff record:
`docs/libc/AGENT1_PRIORITY_HANDOFF.md` lists `string_dangling_view.cp` and
`json_dangling_ref.cp` as accepted cross-function lifetime gaps, and
`docs/libc/KNOWN_COMPILER_GAPS.md` records the stack-escape gap (Clang
`-Wreturn-stack-address` only covers direct returns) and the absence of
interprocedural analysis.

## What interprocedural summary Agent 1 needs

The current escape check is purely syntactic and local.
`compiler/Sema/RaiiLowering.cpp::checkReturnValue` only looks at a direct
`return x;` where `x` is an identifier already registered in `borrows_`
(populated from `T *p = &owner;`) or `owned_`. It has no function-level summary,
no notion of a borrowed **class value** (`string_view`, `slice`, `file` with
`owns = 0`), and no parameter-capture information. A sound fix needs the
following per-function summaries computed at each call site.

1. **Return-origin / borrow-vs-own summary.**
   For every function, record whether the returned value is a *new owner* or a
   *borrow*, and if it is a borrow, the allocation site / local owner it
   aliases. This is the direct fix for cases 1, 2, and 4:
   - `make_view()` → returns borrow of `s.chars` (released by `string_drop` at
     the return's scope exit).
   - `make_slice()` → returns borrow of `v.items` **and** of `&v.generation`
     (both die with the local vector).
   - `make_borrowed()` → returns `file_borrow(local.native())`, i.e. a
     non-owning wrapper whose `stream` is fclose()d by `local.drop()` at exit.
   The summary must distinguish *owned resource return* (legal RAII transfer,
   already handled for `return <owned local>;`) from *borrowed return of a
   callee local* (illegal). Field sensitivity is required: `string_view.chars`,
   `slice.items`/`slice.generation_ref`, and `file.stream`/`file.owns` are the
   fields that carry the alias and ownership state.

2. **Parameter escape / capture summary.**
   For each parameter, record whether it is returned by address, stored into a
   global/heap/field, or passed on to another capturing callee; and the minimum
   lifetime the referent must have. Cases 3 and 5 need this:
   - `store_ptr(int *p)` → `captures(p)`; the call `store_ptr(&local)` in
     `produce()` is then an error because the actual argument is a callee-local
     stack slot with a shorter lifetime than the sink.
   - `ptr_to_parameter(int value)` → `returns_address_of(param)`, so the
     call-site result must inherit the parameter's storage lifetime, which ends
     when the call returns. (The direct `return &value;` is currently caught
     only by Clang; a summarized version must catch it as C+ enforcement and
     also catch indirect escapes through a helper.)

3. **Storage-lifetime lattice and call-site check.**
   Assign each value a region: callee-local, caller-local, by-value parameter,
   global/static, or managed heap. At a return or capture, reject when a value
   from a shorter-lived region is stored in a longer-lived one. The diagnostic
   should name the *actual invalid operation* and the owner, e.g.
   `[CPLUS-LIFETIME] returned value borrows storage of local 's' released at function exit`
   and `[CPLUS-LIFETIME] address of local 'local' stored in longer-lived sink 'sink'`.
   A stable code such as `[CPLUS-LIFETIME]` (optionally `[CPLUS-BORROW]`) must
   be present so `--strict` counts it as C+ enforcement under the suite rule.

4. **Transitive propagation through wrappers.**
   Summaries must compose: a function that calls `file_borrow(x.native())`,
   `v.view()`, or `s.to_view()` and returns/captures the result must inherit the
   borrow/capture from the wrapped call. Without this, fixing the literal
   `return &owner;` form leaves all five reproducers accepted.

5. **Positive controls that must not regress.**
   Any new summary must keep these accepted with zero `[CPLUS-` diagnostics:
   `tests/stdlib/compile_time/string_valid_view.cp`,
   `vector_valid_slice.cp`, `file_valid_borrow.cp`, `json_valid_ref.cp`, and
   the ACCEPT cases in `tests/stdlib/compile_time/*.json`. Returning an *owned*
   resource (`return <owned local>;`) remains legal RAII transfer; borrowing a
   caller-owned object that outlives the call (`file_borrow(stdout)`) remains
   legal.

### Definition of done for this directory

All five cases exit 1 from `c+ check --strict` with a literal `[CPLUS-LIFETIME]`
(or similar stable tagged) diagnostic naming the escape; the
`cfl_pointer_to_parameter.cp` Clang-only warning is replaced or accompanied by
a tagged C+ diagnostic; the positive controls above still exit 0.

## Evidence integrity

Every stdout/stderr string and exit code above was produced by the recorded
command in this workspace on branch `agent/modern-stdlib`. Nothing is
fabricated. No runtime result is implied: these cases were checked, not built
or executed.
