# Handoff — P0 "Pointer arithmetic and bounds verification"

Minimal reproducers for the gap where C+ rejects a constant out-of-bounds
subscript on a local fixed-size array but accepts equivalent (or worse)
out-of-bounds arithmetic through pointers into **stdlib class-owned buffers**.

Scope of this handoff: `.cp` files + this notes file + `manifest.json` only.
No compiler/runtime/header changes. No `git add`/commit.

## Command under test

```
./build/c+ check --strict <file>
```

Run from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`).
Only diagnostics containing a literal `[CPLUS-` token count as tagged C+
enforcement (per `docs/libc/AGENT1_COMPILER_GAPS.md` and
`tests/stdlib/compile_time/vector.json`).

Classification used below:

* **tagged REJECT** — command exits non-zero with a `[CPLUS-...]` diagnostic.
* **untagged REJECT** — command exits non-zero with *no* `[CPLUS-` token.
* **ACCEPT** — command exits 0, no diagnostic.

Each OOB expression lives in a never-called `static` function, so the test
program itself contains no executed UB; `c+ check --strict` still analyses the
expression (verified: a never-called function containing `a[9]` over `int[3]`
is still rejected). The only suspect expressions are the ones named under
test in each file.

## Observed results (exact output / exit)

| File | Scenario | Exit | Exact output | Class |
|---|---|---|---|---|
| `const_ptr_arith_oob.cp` | constant OOB on stdlib-owned buffer via pointer arithmetic (`*(p + 9)`, `p = v.items`, `used == 2`) | 0 | stdout: `check: tests/stdlib/agent1_handoff/pointer_bounds/const_ptr_arith_oob.cp ok`; stderr: empty | **ACCEPT** |
| `variable_index_oob.cp` | variable index provably too large (`const size_t idx = 9`, never modified; extent 2) | 0 | stdout: `check: tests/stdlib/agent1_handoff/pointer_bounds/variable_index_oob.cp ok`; stderr: empty | **ACCEPT** |
| `past_end_in_checked_api.cp` | one-past-end base pointer used in an `at_checked`-like API (`slice(v.items + v.size(), 1).at_checked(0, ...)`) | 0 | stdout: `check: tests/stdlib/agent1_handoff/pointer_bounds/past_end_in_checked_api.cp ok`; stderr: empty | **ACCEPT** |
| `interior_pointer_wrong_extent.cp` | interior pointer with wrong extent (`slice(v.items + 1, 4)`, only 1 int remains; `at_checked(3, ...)`) | 0 | stdout: `check: tests/stdlib/agent1_handoff/pointer_bounds/interior_pointer_wrong_extent.cp ok`; stderr: empty | **ACCEPT** |
| `negative_index.cp` | negative index (`*(p - 1)` on `v.items`) | 0 | stdout: `check: tests/stdlib/agent1_handoff/pointer_bounds/negative_index.cp ok`; stderr: empty | **ACCEPT** |
| `class_at_checked_const_oob.cp` | canonical class-owned constant OOB (`v.at_checked(9, &out)`, `used == 2`) | 0 | stdout: `check: tests/stdlib/agent1_handoff/pointer_bounds/class_at_checked_const_oob.cp ok`; stderr: empty | **ACCEPT** |
| `control_fixed_array_rejected.cp` | CONTROL: fixed local `int[3]`, `a[9]` | 1 | stderr (exact, three lines): `tests/stdlib/agent1_handoff/pointer_bounds/control_fixed_array_rejected.cp:16:12: error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 9 exceeds length 3` / `      return a[9];   /* tagged [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] */` / `             ^`; stdout: empty | **tagged REJECT** |

Summary: 6 ACCEPT, 1 tagged REJECT, 0 untagged REJECT.

Note the asymmetry the handoff is about: the control and the class cases are
all constant, statically obvious out-of-range accesses, yet only the fixed
local array gets the `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS]` tag. No class-owned
case produced any diagnostic, tagged or untagged, so none is a PARTIAL /
untagged-REJECT case — they are clean ACCEPTs.

## Why each class case is accepted (mechanism)

* `vector::items`, `vector::used`, `vector::space` are ordinary class fields.
  C+ attaches no extent metadata to `int *items`, so pointer arithmetic
  (`p + 9`, `p[idx]`, `p - 1`) has no bound to compare against.
* The bounds pass is token-level and single-function
  (`compiler/Sema/StaticBoundsCheck.cpp`); it only knows the declared size of
  a local array. It neither follows the pointer to a class field nor reads the
  `used`/`space` fields.
* Indices passed to methods arrive as ordinary `size_t` parameters, so the
  constant 9 in `at_checked(9, ...)` is never related to the receiver's extent.
* A `slice` is a returned class value with its own `items`/`count` fields; C+
  does not link it back to the source vector's buffer, so a one-past-end base
  or an inflated count is accepted.

This matches `docs/libc/AGENT1_COMPILER_GAPS.md` capability 5 and
`docs/libc/COMPILE_TIME_GAPS.md` §1 (no interprocedural / pointer-extent /
heap-capacity analysis).

## Metadata Agent 1 must attach to stdlib class buffers

To make the six ACCEPT cases fail with a tagged `[CPLUS-BOUNDS]` diagnostic
without breaking the controls, the compiler needs the following metadata and
plumbing. All of it is derivable from `stdlib/vector.hp`, `stdlib/string.hp`
and `stdlib/collections.hp`.

1. **Pointer-field ↔ extent-field binding on class fields.** Record, per
   class, which pointer field is a buffer and which field(s) hold its extent:
   - `vector::items` (int*) ↔ `vector::used` (logical length, element count)
     and `vector::space` (allocated capacity, element count); element stride
     `sizeof(int)`.
   - `slice::items` (int*) ↔ `slice::count` (extent), with
     `slice::generation_ref` / `slice::generation_at` as staleness metadata.
   - `string::chars` / `string_view::chars` (char*) ↔ length fields in
     `stdlib/string.hp` (same pattern; needed for the "checked string
     accessor" regression named in the gap doc).
   This is the `ClassLowering.cpp` change the gap doc asks for.

2. **Allocation-site capacity metadata.** When `stdlib_alloc(sizeof(int) * n)`
   is assigned to a bound pointer field (directly or via `this->items`),
   record `n` elements as that pointer's capacity. `vector::reserve`,
   `copy_from` and `shrink_to_fit` are the relevant sites; their results flow
   into `items`/`space`. Without this, raw `p[k]` still has no bound.

3. **Method-call receiver/index exposure.** Lowering must expose
   `receiver`, method name and the index argument to the bounds pass so
   `v.at_checked(9, &out)` and `v.at(9)` can be checked against `v.used`, and
   `s.at_checked(3, &out)` against `s.count`. Keep the runtime `-1` semantics;
   the compile-time rejection is an additional tagged diagnostic.

4. **Interprocedural propagation through class helpers.** Follow writes to
   bound fields made by free helpers such as `vector_reserve(this, want)`
   (called from `push_back`/`insert`) and by `vector::view()`, which copies
   `items` and `used` into a returned `slice`. The extent must travel with the
   value so a borrowed slice inherits the owner's extent.

5. **Pointer-arithmetic extent checking with signed offsets.** For a pointer
   with known extent, check `p + k`, `p[k]`, `*(p + k)` and `p - k` against
   `[0, extent)` using a signed offset: catch constant and provable-constant
   offsets, offsets past the end, and negative offsets (`p - 1`, `p[-1]`).
   Also validate view construction: for `slice(base + off, count)`, require
   `off >= 0` and `off + count <= source_extent`; reject a one-past-end base
   used as a readable base (the `past_end_in_checked_api` case) and a count
   larger than the remaining extent (the `interior_pointer_wrong_extent`
   case).

6. **Negative/unsigned-index handling.** Treat index parameters as
   range-constrained: `(size_t)-1` and negative pointer offsets must be
   flagged rather than silently wrapping.

7. **Stable tag.** Emit exactly `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS]` (the tag
   pair used by the `int[3]` control) with a message naming the buffer/index/
   extent, e.g. `[CPLUS-BOUNDS] ... array index 9 exceeds length 2 (v.items,
   field 'used')`. An untagged rejection would be only PARTIAL and would not
   satisfy the acceptance criterion.

Regression expectation for Agent 1: all six ACCEPT cases should become tagged
REJECT; `control_fixed_array_rejected.cp` must keep the same tagged
diagnostic; and ordinary in-range accesses (`vector_valid.cp`,
`vector_valid_slice.cp` in `tests/stdlib/compile_time/`) must stay clean.

## Provenance / verification

* Compiler binary used: `./build/c+` (host Clang backend, C+ 0.1.0).
* Each result above was obtained by actually running
  `./build/c+ check --strict tests/stdlib/agent1_handoff/pointer_bounds/<file>`;
  no diagnostic text was inferred.
* `manifest.json` in this directory records the same results in machine-readable
  form.
