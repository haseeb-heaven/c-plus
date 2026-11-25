# Engine examples — minimal deterministic models

## Scope (explicit)

These three programs are **minimal deterministic engine models, not a full game
engine**. They deliberately model only one narrow mechanic each (ECS component
updates, a fixed-timestep loop, and a resource cache) so that their behaviour is
small enough to be byte-for-byte reproducible and auditable. There is no
renderer, audio, asset loader for real files, scripting, networking, threading,
or wall-clock scheduling. Every run uses a fixed seed and a fixed tick/script
count, so output is a deterministic transcript that can be diffed.

## Files

| file | model |
| --- | --- |
| `ecs.cp` | entity/component system: position, velocity, health; move, damage, remove-dead systems; lookup refusals |
| `engine_loop.cp` | fixed-timestep loop with argv input queue, state update, collision, scoring |
| `resource_cache.cp` | string-id resource manager with refcounts, generation-tagged handles, double-release and use-after-release refusals |
| `run.sh` | builds and runs all three with fixed inputs, byte-compares against `expected/*.out` |
| `expected/*.out` | checked-in deterministic transcripts |

## Exact commands (from the repository root)

Build each program:

```sh
./build/c+ build examples/expanded/engine/ecs.cp --strict --sanitize=none -I. -o examples/expanded/engine/.build/ecs
./build/c+ build examples/expanded/engine/engine_loop.cp --strict --sanitize=none -I. -o examples/expanded/engine/.build/engine_loop
./build/c+ build examples/expanded/engine/resource_cache.cp --strict --sanitize=none -I. -o examples/expanded/engine/.build/resource_cache
```

All three builds exit **0** with empty stdout/stderr.

Run each program under a 30 s timeout (engine_loop uses the fixed 12-token
input queue `N N N N N N F F F F N N`):

```sh
timeout 30 ./examples/expanded/engine/.build/ecs
timeout 30 ./examples/expanded/engine/.build/engine_loop N N N N N N F F F F N N
timeout 30 ./examples/expanded/engine/.build/resource_cache
```

All three runs exit **0**. The full byte-exact transcripts are in
`expected/ecs.out` (103 lines), `expected/engine_loop.out` (19 lines) and
`expected/resource_cache.out` (20 lines). Key exact lines:

```
ECS seed=0x12345678 ticks= 12 entities= 6
REFUSE unknown-entity ok= 0 category= invalid code= 106 msg= missing entity: no component record for id
REFUSE unknown-component ok= 0 category= invalid code= 108 msg= missing component: unknown component name
REFUSE removed-entity ok= 0 category= invalid code= 106 msg= missing entity: no component record for id
SUMMARY survivors= 5 checksum= 551
```

```
COLLISION tick= 6 lives= 2
HIT tick= 6 target= 1 score= 10
SUMMARY ticks= 12 score= 10 hits= 1 collisions= 1 lives= 2 checksum= 1025
```

```
REFUSE duplicate-load ok= 0 category= invalid code= 106 msg= asset id is already loaded
REFUSE stale-handle ok= 0 category= lifecycle code= 103 msg= stale handle generation
REFUSE double-release ok= 0 category= lifecycle code= 103 msg= double release
REFUSE use-after-release ok= 0 category= lifecycle code= 103 msg= use after release
SUMMARY used= 4 index= 2
```

Aggregate runner (builds from scratch, runs, byte-compares):

```sh
sh examples/expanded/engine/run.sh
```

Exact output:

```
PASS ecs
PASS engine_loop
PASS resource_cache
ENGINE_EXAMPLES 3 3 0
```

Exit status `0`. Note that `run.sh` recomputes the binaries from the checked-in
`.cp` sources; the `expected/*.out` files are the oracles, never overwritten by
the runner.

## Determinism and failure behaviour

- `ecs.cp` uses LCG seed `0x12345678`, `TICK_COUNT = 12`, six fixed initial
  entities and bounded sweeps; no `time()` and no uninitialised state.
- `engine_loop.cp` uses seed `0xabcd1234`; every tick's input comes from argv and
  all motion is driven by the tick counter. No wall-clock API is called.
- `resource_cache.cp` executes a fixed 20-line operation script; payload bytes
  are derived from the id length and index, never from addresses.
- Determinism was checked by running each binary repeatedly and comparing bytes
  against `expected/*.out`; all three matched. `run.sh` re-does this comparison
  on every invocation.
- Valid runs exit 0. Handled input/allocation failures exit non-zero (2):
  - `engine_loop` with no args → `ERROR usage: ... code= 108`, exit 2.
  - `engine_loop X` (invalid token) → `ERROR invalid input token token= X code= 108`, exit 2.
  - Any unhandled C+ runtime safety violation terminates with the runtime's
    deterministic exit code rather than corrupting memory.

## How checked stdlib APIs enforce safety

- **`stdlib/vector.hp`** — `vector::at_checked` is the only read path for the
  entity-id list (`engine_loop` uses it for the input queue). An out-of-range
  index returns `-1` and records a bounds violation instead of reading memory;
  the caller checks the return value before using the output. `push_back` and
  `erase` are failure-atomic and bump a generation counter that invalidates any
  outstanding `slice`, so borrowed windows cannot be used after a shape change.
- **`stdlib/collections.hp` (`intmap`, `strmap`)** — component storage in
  `ecs.cp` is `intmap` keyed by stable entity id: `get` on a missing id returns
  `-1` (the refusal path), `put` overwrites safely, and `remove` is checked.
  `resource_cache.cp` uses `strmap` for id→slot lookup; it copies keys into
  owned storage, refuses null keys, and returns `-1` for absent ids. The hash
  probe loops are bounded by the table capacity.
- **`stdlib/error.hp`** — refusals are reported through the recoverable
  `error` API (`error::set_category`, `ok`, `category`, `code`, `what` backed by
  `error_fail_cat`). Nothing in this path terminates the process; the stable
  category and `cplus_exit_code` make each refusal printable and comparable.
  The same header supplies `stdlib_alloc` / `stdlib_release`: containers
  allocate only through this wrapper, which keeps a metadata table of live
  blocks and routes releases through the C+ managed allocator so invalid or
  duplicate releases are detectable.
- **`stdlib/string.hp`** — `string` owns its buffer, `c_str()` is never NULL
  (it returns `""` after `drop()`), `append` is failure-atomic, and forbidden
  operations (null argument / oversized request) return `-1` rather than
  writing out of bounds. The cache builds ids with `string` and passes only
  `c_str()` into the checked `strmap` API.
- **Metadata for use-after-release** — the allocator cannot detect a use after
  a block has been reallocated, so `resource_cache` keeps explicit per-slot
  state (`EMPTY` / `LIVE` / `RELEASED`), a refcount and a generation counter.
  Handles encode `slot * 16 + generation`; `resolve` checks slot bounds, state
  and generation before every use or release. That is why stale handles, double
  release and use-after-release are refused instead of dereferencing a freed
  payload.
- **`--strict --sanitize=none`** — strict mode rejects unverified operations at
  compile time. Two checks shaped the sources: variable subscripts of the
  `argv` pointer parameter are rejected, so the loop copies each element with a
  sized `__builtin_memcpy` before use; and a modulo with a variable divisor is
  rejected as possibly zero, so both RNGs guard the span explicitly. All array
  scans use loop bounds derived from the container's checked `size()`.
- All containers are C+ owning types with `drop()` destructors, so they are
  released deterministically in reverse declaration order at scope exit,
  including on early returns.
