# FUZZ10K / thread_async — deterministic metadata fuzz evidence

Agent 7 of 10 (`agent/modern-stdlib`). All files live only under
`tests/fuzz10k/thread_async/`. No headers, build scripts or other agent
directories were modified.

## Scope

Exercised APIs (metadata/liveness behaviour only — no raw game/heap offsets):

- `stdlib/thread.hp` — `mutex`, `guard`, `condvar`, `thread`
  (double-lock, double-unlock, double-drop, zero/NULL handles, failed join,
  double start, NULL thread function, drop-joins, bounded multi-thread pool).
- `stdlib/async.hp` — `loop`, `timer` (libuv): lifecycle, NULL loop, NULL
  callback, stop on a zero-initialised timer, repeated drop.
- `stdlib/lifetime_adapt.hp` — `lifetime_owner` / `lifetime_borrow`:
  valid read/write, stale generation token, double release, ownership
  transfer invalidation, NULL handles, use-after-release metadata-only probes,
  out-of-range guarded access.
- `stdlib/allocator.hp` + `stdlib/error.hp` — generation/liveness helpers:
  `stdlib_alloc_identity`, `stdlib_alloc_id_matches`, `stdlib_alloc_cap_*`,
  `stdlib_alloc_view_*`, `stdlib_alloc_check_generation`,
  `stdlib_transfer_ownership`, including stale (post-release) generations.

**"future" modelling.** `stdlib/async.hp` has no dedicated future/promise type.
The four `future` cases model a single-consumption result handle using only
allowed headers: `thread::join` for the one-shot value and a
`lifetime_owner`/`lifetime_borrow` byte slot for consumed-slot refusal. This is
an explicit modelling choice, not a claim that a `future` type exists.

## Determinism contract

- **Fixed PRNG seed:** `0xF0A7704A5EED5A17ULL` (splitmix64, three draws per
  scenario: case selector, `p1`, `p2`). No `rand()`, clock, pid, environment
  input or wall-clock assertion anywhere in the harness.
- **12,000 scenarios** (≥ 10,000 required). Each selects one of **39** case
  functions across 7 families and supplies derived parameters that vary the
  operation-sequence length, sizes and values.
- **Bounded joins / no hangs:** every worker thread is tiny and always joined.
  Double-lock is reconfigured to `PTHREAD_MUTEX_ERRORCHECK` (test-only cast
  through `mutex::native()`) so a second lock returns `EDEADLK` instead of
  deadlocking. Each scenario arms `alarm(5)` and clears it afterwards, so a
  stuck join/loop exits with `_Exit(99)`. The external `timeout 600` is a
  second, process-wide bound.
- **Metadata-only stale access:** pointers captured before a release/transfer
  are queried with `stdlib_alloc_*` / `lifetime_*` and are **never
  dereferenced**. Guarded `lifetime_read`/`lifetime_write` validate the token
  and range before touching a byte; the destination canary is checked after a
  refused read.
- **Per-scenario state check:** two 64-bit canaries plus `stdlib_state.meta_live`
  are captured before the case and compared after it; a case must either hit a
  documented refusal (defined error category/code) or a documented defined
  result. All cases net to zero live tracked blocks.

## Exact commands (run from the repository root)

Build (exact required invocation):

```sh
./build/c+ build tests/fuzz10k/thread_async/fuzz10k_thread_async.cp --strict --sanitize=none \
    -I/opt/homebrew/include -L/opt/homebrew/lib -luv -I. \
    -o tests/fuzz10k/thread_async/fuzz10k_thread_async.bin
```

Run directly under the required bound:

```sh
/usr/bin/timeout 600 \
    ./tests/fuzz10k/thread_async/fuzz10k_thread_async.bin \
    tests/fuzz10k/thread_async/scenarios.log
```

Or end-to-end via the driver (rebuilds, runs under `timeout 600`, maps abnormal
exits to crash/hang failures):

```sh
sh tests/fuzz10k/thread_async/run.sh
```

## Exact results (verified, not fabricated)

Harness stdout, plus the final lines written to `scenarios.log`:

```
FUZZ10K_DISTINCT 0
FUZZ10K_DIGEST fbc723f3b4a63067
FUZZ10K thread_async 12000 0 0 0 8551
```

Field meaning: `<scenarios>=12000 <crashes>=0 <hangs>=0 <corruptions>=0 <refusals>=8551`.

- `crashes=0`, `hangs=0`: the process reached the final summary line. A signal
  or the per-scenario watchdog would have prevented that line; `run.sh` then
  reports a crash/hang and exits nonzero.
- `corruptions=0`: every scenario passed its canary + `meta_live` + refusal/
  defined-result contract. `g_state_ok` equalled 12,000.
- `refusals=8551`: scenarios whose case set the documented-refusal flag and
  passed all checks. This is the explicit refusal flag, not the count of the
  literal `refused` result string (some cases set the flag while reporting a
  `noop`/`defined` outcome, e.g. a second guard `unlock()` is a safe no-op
  driven by a cleared owner).
- Distinctness: 0 duplicate `(case_index, p1, p2)` tuples (sort + adjacency
  check). All 12,000 `state_ok` flags are `1`.
- Scenario coverage from `scenarios.log`: mutex 2114, lifetime 2105, thread
  1871, alloc 1560, condvar 1545, timer 1534, future 1271. Results: refused
  7912, defined 3134, noop 954.

**Determinism check.** Two consecutive runs produced byte-identical
`scenarios.log`:

```
97214c141150c1fc2cb367b2d48bd401176f4a35358b178a1c94f1bf1fcb4e85  scenarios.log
97214c141150c1fc2cb367b2d48bd401176f4a35358b178a1c94f1bf1fcb4e85  scenarios.log
```

Same digest `fbc723f3b4a63067` on both runs. The driver script completed in
about 1.2 s (including the build), well inside `timeout 600`. Build emitted only
the expected clang warnings that `-luv`/`-L/opt/homebrew/lib` are unused during
the compile stage; the link succeeded and the binary ran cleanly.

Exit status: `0` on a clean run; nonzero if any corruption, duplicate tuple or
abnormal termination occurs.

## Evidence, not proof

This is **evidence for the 12,000 deterministic inputs generated from the fixed
seed above**, not a proof of safety. The harness observes metadata refusals and
defined results through the public stdlib helpers; it does not model every
possible interleaving, allocation failure, or compiler lowering. In particular:

- thread interleavings are not exhaustively enumerated (workers are tiny and
  joined; shared state is not contended here);
- the double-lock case relies on a test-only `PTHREAD_MUTEX_ERRORCHECK`
  reconfiguration, not on `mutex` itself detecting double-lock;
- `future` is modelled, not a native stdlib type;
- a clean run says nothing about inputs outside this seeded corpus.

Any claim beyond "these 12,000 scenarios did not crash, hang or corrupt the
checked metadata/canaries under `--strict --sanitize=none`" would be
unsupported.

## Files

- `fuzz10k_thread_async.cp` — harness (generator, 39 cases, driver, digest).
- `run.sh` — build + `timeout 600` driver with crash/hang exit mapping.
- `.gitignore` — ignores `scenarios.log`, `run.*.log`, `*.bin`.
- `scenarios.log` — generated per-scenario evidence (gitignored).
- `run.stdout.log`, `run.stderr.log`, `fuzz10k_thread_async.bin` — generated
  artifacts (gitignored).
