# Deep allocation-failure contract (`stdlib/collections.hp`)

Deep, self-checking tests for the allocation points and failure atomicity of the
`stdlib/collections.hp` containers **not** covered by
`tests/stdlib/deep_collections_growth.cp` / `deep_collections_collisions.cp`.
`stdlib/collections.hp` and `stdlib/error.hp` are **unchanged** — no library
defect was reproduced.

## Scope and ownership

| File | Role |
| --- | --- |
| `tests/stdlib/deep_collections_alloc.cp` | new test: enumeration of every uncovered allocation point + atomicity |
| `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md` | this document |
| `stdlib/collections.hp`, `stdlib/error.hp` | **unchanged** |
| `tests/stdlib/deep_collections_growth.cp`, `deep_collections_collisions.cp` | **unchanged** (read only) |

No other files were created or modified; no `git add`/`git commit` was done.

Existing coverage deliberately **not** duplicated: `intmap` rebuild allocations
1/2/3, `strmap` rebuild 1/2/3 plus key copy at 4, the `fail_after(0)` case, and
the post-failed-grow telemetry check — all in `deep_collections_growth.cp`.

## Uncovered allocation points and probed counts

Counts come from `stdlib_alloc_seq()` (reset with `stdlib_alloc_fail_reset()`
immediately before the operation, read immediately after a *successful* probe
run). The test asserts each count at runtime; the values below were also
verified independently before writing the test.

| Scenario (tag prefix) | Operation | Allocation point | Count |
| --- | --- | --- | --- |
| `isf` | `intset::insert` into empty set (cap 0 -> 8) | `stdlib_rebuild`: `keys` + `state` | **2** |
| `isg` | `intset::insert`, 5 live entries (cap 8 -> 16) | `stdlib_rebuild`: `keys` + `state` | **2** |
| `dpbf` | `deque::push_back` from empty (cap 0 -> 8) | `stdlib_make_room` -> `reserve` -> `stdlib_ring_copy` | **1** |
| `dpff` | `deque::push_front` from empty (cap 0 -> 8) | same ring-copy buffer | **1** |
| `dgw` | `deque::push_back`, full wrapped ring (head=2, 8 -> 16) | ring copy linearises wrapped ring | **1** |
| `lpbe` | `list::push_back` into empty list | one `list_node` | **1** |
| `lpbn` | `list::push_back` into non-empty list (3 nodes) | one `list_node` | **1** |
| `lpfe` | `list::push_front` into empty list | one `list_node` | **1** |
| `lpfn` | `list::push_front` into non-empty list (3 nodes) | one `list_node` | **1** |
| `stf` | `stack::push` first push (cap 0 -> 8) | one `int` buffer | **1** |
| `stg` | `stack::push` growth (8 -> 16) | one `int` buffer | **1** |
| `qf` | `queue::push` first push (cap 0 -> 8) | `stdlib_ring_copy` (source NULL) | **1** |
| `qgw` | `queue::push` growth, wrapped ring (head=2, 8 -> 16) | ring-copy buffer | **1** |

Wrapped-ring construction (used by `dgw` and `qgw`): push values `0..7`, pop
the front twice, push `100,101`.  This yields `head == 2`, `used == 8`,
`space == 8` with logical contents `[2,3,4,5,6,7,100,101]`, so the subsequent
push must linearise a genuinely wrapped ring.

## Method

For each operation:

1. **Probe.** Build an identical container, reset injection, run the operation
   successfully, read `stdlib_alloc_seq()` -> `count`.  Assert the operation
   succeeded and `1 <= count <= 2` (`count == 1` for all but the two `intset`
   cases).
2. **Enumerate failures.** For `N = 1 .. count`, rebuild an identical container
   from scratch and call `stdlib_alloc_fail_at(N)` just before the operation.
   Assert all of:
   - (a) return value `-1`;
   - (b) every pre-existing element still readable with the identical value,
     and the new element absent (`contains` / `at_checked` / `top_checked` /
     `front_checked`, or a full pop-drain for `stack`/`queue`);
   - (c) `size()` / `capacity()` / `valid()` unchanged from the pre-state (only
     accessors actually exposed by that type are checked — `list`, `stack` and
     `queue` expose no `capacity()`/`valid()`, `intset` exposes no
     `capacity()`);
   - (d) with telemetry enabled and reset **before** the container is built,
     `alloc_count == free_count` after `drop()` and `alloc_failures >= 1`;
   - (e) after `stdlib_alloc_fail_reset()`, the same operation succeeds on a
     freshly built identical container (and the new element is present).
3. No system memory is ever exhausted: injection is a logical counter inside
   the checked allocator. Successful allocations alone are **not** treated as
   proof of memory safety; only atomic rollback plus balanced telemetry is.

Every container variable in the test has a unique name. This is required by
the C+ lowering: a method call is dispatched by the owning variable's name
within a scope, so reusing one name for two container types makes the compiler
emit the wrong method (observed as `s.insert(...)` lowering to
`stack_insert(&s, ...)` when `s` was both an `intset` and a `stack` in one
function).

## Classification

| Class | Observed |
| --- | --- |
| **success** | every probe (`*_probe`) and every retry (`*_oN`/`*_rN` recovery) returns `0` with correct contents |
| **recoverable** | every injected failure returns `-1`, leaves contents/state identical, `alloc_failures >= 1`, no leak |
| **fatal** | none — no `cplus_fatal`, no runtime abort, no sanitizer diagnostic; `alloc_no_fatal 1` and exit code `0` |

## Exact expected stdout

59 checks + final line; all `<tag> 1`, exit code `0`:

```
isf_probe 1
isf_o1 1
isf_r1 1
isf_lk1 1
isf_o2 1
isf_r2 1
isf_lk2 1
isg_probe 1
isg_o1 1
isg_r1 1
isg_lk1 1
isg_o2 1
isg_r2 1
isg_lk2 1
dpbf_probe 1
dpbf_o1 1
dpbf_r1 1
dpbf_lk1 1
dpff_probe 1
dpff_o1 1
dpff_r1 1
dpff_lk1 1
dgw_probe 1
dgw_o1 1
dgw_r1 1
dgw_lk1 1
lpbe_probe 1
lpbe_o1 1
lpbe_r1 1
lpbe_lk1 1
lpbn_probe 1
lpbn_o1 1
lpbn_r1 1
lpbn_lk1 1
lpfe_probe 1
lpfe_o1 1
lpfe_r1 1
lpfe_lk1 1
lpfn_probe 1
lpfn_o1 1
lpfn_r1 1
lpfn_lk1 1
stf_probe 1
stf_o1 1
stf_r1 1
stf_lk1 1
stg_probe 1
stg_o1 1
stg_r1 1
stg_lk1 1
qf_probe 1
qf_o1 1
qf_r1 1
qf_lk1 1
qgw_probe 1
qgw_o1 1
qgw_r1 1
qgw_lk1 1
alloc_no_fatal 1
ALLOC 59 0
```

Tag legend: `<prefix>_probe` = count probe; `<prefix>_oN` = injected-failure
atomicity at allocation `N`; `<prefix>_rN` = recovery after reset;
`<prefix>_lkN` = telemetry leak check.

## Commands and observed results

From the repository root (C+ compiler `build/c+`):

```sh
./build/c+ build tests/stdlib/deep_collections_alloc.cp --strict --sanitize=none -o /tmp/dca_strict
./build/c+ build tests/stdlib/deep_collections_alloc.cp -o /tmp/dca_default
./build/c+ build tests/stdlib/deep_collections_alloc.cp --sanitize=undefined -o /tmp/dca_ubsan

timeout 30 /tmp/dca_strict
timeout 30 /tmp/dca_default
timeout 30 /tmp/dca_ubsan
```

Observed (all three stdout streams are byte-for-byte identical to the block
above, 60 lines, no compiler diagnostics, no sanitizer output):

| Mode | Build exit | Run exit (`timeout 30`) | Final line |
| --- | --- | --- | --- |
| `--strict --sanitize=none` | 0 | 0 | `ALLOC 59 0` |
| default (Safe) | 0 | 0 | `ALLOC 59 0` |
| `--sanitize=undefined` | 0 | 0 | `ALLOC 59 0` |

`--sanitize=address` is not supported by the selected backend (per
`DEEP_COLLECTIONS_CONTRACT.md`) and was not part of the required matrix.

## Findings

**No defect in `stdlib/collections.hp` or `stdlib/error.hp` was reproduced.**
All 13 uncovered allocation points fail atomically: return `-1`, all
pre-existing elements keep their values, exposed state is unchanged, telemetry
is balanced after `drop()`, and a reset retry succeeds.  No permanent leak or
wrong state was observed.  Reaching `ALLOC 59 0` under `timeout 30` also proves
no injected-failure path hangs.

Four issues were found and fixed **in the test itself** (headers were never
edited to force a pass):

1. **Wrong-variable method dispatch.** Reusing `s` for an `intset` and a
   `stack` in one function made strict compilation emit `stack_insert` for
   `s.insert(...)`.  Fixed by giving every container a unique name.
2. **`%ld` vs `int`.** Strict FFI checking rejected `snprintf(..., "%ld", N)`
   because `N` is `int`; changed to `%d`.
3. **Uninitialized out-parameters.** Strict lifetime checking flagged
   `at_checked`/`top_checked`/`front_checked`/`pop` output variables read
   after being passed by address while uninitialized.  All such variables are
   now initialized to `0` at declaration.
4. **Leak-check ordering.** The first run produced 7 failing `*_lk*` checks
   (`isg`, `dgw`, `lpbn`, `lpfn`, `stg`, `qgw`) because
   `stdlib_telemetry_reset()` was placed *after* building the pre-existing
   elements: their allocations were uncounted but their frees were, giving
   `free_count > alloc_count`.  Moving the reset before the container build
   made all checks pass.  This was a measurement bug, not a library leak.

No API behaviour was changed to make a test pass.
