# No-heap mode for C+ (additive, opt-in)

`stdlib/no_heap.hp` defines compile-time refusal macros for `malloc/calloc/
realloc/free/aligned_alloc` when `CPLUS_NO_HEAP` is defined. Include it **after**
all other headers so runtime declarations are not rewritten. C+ code then must
use `stdlib/scoped_mem.hp`: VM bump data stack (`stdlib_scoped_scope`, one-store drop),
dynamic arena blocks and stable slot-pool refs.

Proof: `tests/stdlib/no_heap/run.sh` -> `NO_HEAP_TEST 2 2 0`:
- positive app built with `-DCPLUS_NO_HEAP` uses only stacks/pools and runs;
- negative source calling raw `malloc` is refused at compile time.

Honest boundary: this is a policy for C+ source, **not** removal of the
ordinary C allocator. Precompiled C libraries, legacy C mode and the C ABI must
keep `malloc/free`; the checked heap remains the backing allocator inside the
library internals. No claim of Rust/the inspiration language compiler-enforced no-heap model.
