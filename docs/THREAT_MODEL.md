# Threat model and safety evidence

## Trusted computing base (TCB)

Safe C+ programs depend on:

1. the `c+` lowering stages (`src/Sema/`);
2. the host Clang driver and its C runtime/sanitizer libraries;
3. the C+ runtime archive (`src/Runtime/cplus_runtime.c`);
4. libc, POSIX and any linked C libraries;
5. the operating system.

A flaw in any of these can break the guarantee. C+ does not claim to verify
libc, the host compiler or external libraries.

## Adversary model

We consider the following sources of invalid operations:

- untrusted input that drives indices, sizes or parser state;
- programmer mistakes in safe C+ code (bad indices, double release, bad
  ownership transfers, escaping borrows);
- buggy legacy C called through an adapter;
- multiple threads sharing data.

We do **not** consider a hostile operating system, a compromised host compiler,
or deliberate circumvention via casts/inline assembly to be in scope for the
current MVP.

## Enforced guarantees and how they fail closed

| hazard | mechanism | failure behaviour |
| --- | --- | --- |
| fixed-array index past extent (constant) | `-Werror=array-bounds` + C+ static bounds pass | compile error; no executable |
| simple counted loop index past extent | `StaticBoundsCheck.cpp` | compile error |
| dynamic bad index | UBSan `bounds` with `-fno-sanitize-recover=all` | abort (SIGABRT) |
| null / misaligned dereference | UBSan `null,alignment` | abort |
| signed overflow and other UB | UBSan `undefined` | abort |
| double / invalid release | allocator side table + freed-address cache | exit 104 / 105 |
| copied or assigned owning resource | RAII lowering compile diagnostics | compile error |
| managed pointer escaping scope | RAII lowering compile diagnostic | compile error |
| borrow escaping function / use after move | borrow checker in RAII lowering | compile error |
| failed `scan` conversion | runtime input check | exit 109 |

No runtime failure is allowed to continue execution with corrupted state.

## Known gaps (do not rely on C+ for these)

- **Use-after-free is not detected.** ASan is broken on this host; enabling it
  would abort inside its own runtime. A stale pointer whose address was reused
  may be misattributed.
- **Arbitrary pointer arithmetic** (`p + i`, casts, raw `malloc` pointers) is
  not statically checked. Only array subscripts and simple loops are.
- **Data races are not detected.** A mutex RAII guard and atomic primitives
  exist, but no static or dynamic race verification. Shared mutable access is
  a documented unsafe area.
- **`goto`, `longjmp`, `setjmp`, `exit`** bypass inserted destructors. `goto`
  warns; the others are outside the supported RAII control flow.
- **Legacy C / FFI**: `--legacy-c` is a whole-file opt-out, not a checked boundary.
  Calling arbitrary native C code is not memory-safe by itself.
- **Printers and input**: `print` has no struct reflection; `scan` string
  bounds are per-array or a fixed 256 bytes for `char *`.
- **Aliasing through struct fields, unions and containers** is not tracked by
  the borrow subset.

## Evidence

Executable evidence lives in `tests/` and `demos/`:

- positive: `tests/cases/` (ordinary C, RAII order, classes, C ABI, managed
  allocations, `.len`, `print`/`scan`, constructors/destructors);
- compile-fail: `tests/compile_fail/` (copies, escapes, borrows, constant and
  loop bounds, invalid constructors);
- runtime-fail: `tests/runtime_fail/` (bounds, null, double release);
- demos: `demos/A`–`L`, including networking and pthread mutex RAII.

`./tests/run_tests.sh`, `./demos/run_demos.sh` and `ctest` are the
reproducible verification entry points. Passing tests are evidence for the
listed cases, not a proof of universal safety.

## Fuzzing / adversarial posture

There is no dedicated fuzz harness yet. The negative test suite is deliberately
adversarial for the supported hazards (bad indices, double release, null
dereference, ownership escapes). Adding systematic fuzzing of the rewriter and
bounds check is on the roadmap.
