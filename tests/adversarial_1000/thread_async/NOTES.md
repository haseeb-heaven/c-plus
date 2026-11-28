# ADV1000 thread_async — deterministic adversarial scenarios

## What this is

1000 deterministic adversarial scenarios over `stdlib/thread.hp`,
`stdlib/async.hp`, `stdlib/lifetime_adapt.hp` and the
`stdlib/allocator.hp` generation/liveness helpers. A single fixed seed
(`0xA5D0100047C0FFEE`) drives a splitmix64 stream; no pid, clock, wall-time
value, environment randomness or network input is consulted. Threads are
short-lived workers that are always joined; a SIGALRM(90) watchdog and the
external `timeout 120` bound every run. No wall-clock assertion is made.

The API families and adversarial cases covered:

| API (output token) | cases | scenarios |
|---|---|---|
| `mutex` | cycle, double_unlock, double_lock, double_drop, zero_handle, null_guard | 186 |
| `condvar` | signal_broadcast, wait_invalid, zero_handle, double_drop | 102 |
| `thread` | start_join, double_start, failed_join, null_fn, drop_joins | 153 |
| `future` | get_once, double_get, before_start, consumed_slot | 123 |
| `timer` | lifecycle, null_loop, null_cb, stop_invalid, double_drop | 154 |
| `lifetime` | valid_rw, stale_token, double_release, transfer, null_handles, uaf_meta | 160 |
| `alloc` | id_live, stale_generation, cap_bounds, null_interior | 122 |

All 34 cases and all 7 API families are exercised.

### BREAKING cases (each refuses via metadata, canary/state intact)

- **stale token borrow** — `lifetime` stale_token / transfer, `alloc`
  stale_generation, `future` consumed_slot. After release/transfer the borrow's
  old token is checked by metadata (`stdlib_alloc_check_generation`) before any
  access; the guarded read/write is refused with
  `STDLIB_CATEGORY_LIFECYCLE` / `CPLUS_EXIT_USE_AFTER_FREE` while the output
  buffer is untouched. The stale pointer is **never dereferenced**.
- **double release** — `lifetime` double_release: second
  `lifetime_owner_release` refused with `STDLIB_CATEGORY_OWNERSHIP` /
  `CPLUS_EXIT_DOUBLE_FREE`; `stdlib_state.meta_live` unchanged.
- **double join** — `thread` double_start / drop_joins, `future` double_get /
  before_start: join of a non-joinable handle is refused with
  `STDLIB_CATEGORY_LIFECYCLE` / `CPLUS_EXIT_RESOURCE`.
- **destroy-locked-mutex / double lock** — `mutex` double_lock configures a
  dedicated `PTHREAD_MUTEX_ERRORCHECK` mutex (white-box via `native()`, same
  technique as `f100_thread_guard.cp`); the same-thread second lock returns
  `EDEADLK` instead of deadlocking. `mutex` double_drop / `condvar` double_drop
  / `timer` double_drop confirm that a second `drop()` is refused by the
  cleared handle (`NULL` / `initialized == 0`) with canary intact.
- **use-after-release (metadata refusal only)** — `lifetime` uaf_meta and
  `alloc` null_interior/stale_generation query `stdlib_alloc_size`,
  `stdlib_alloc_generation`, `stdlib_alloc_view_live`,
  `stdlib_alloc_cap_state`, `stdlib_alloc_hasvalidcap`,
  `stdlib_alloc_cap_covers`, `stdlib_alloc_cap_check` and
  `stdlib_alloc_id_matches` on stale/interior/NULL pointers. These are
  fail-closed metadata queries; no stale byte is read.
- **failed join / null handles** — `thread` failed_join/null_fn,
  `condvar` wait_invalid/zero_handle, `timer` null_loop/null_cb/stop_invalid,
  `mutex` null_guard/zero_handle, `lifetime` null_handles.

Every scenario verifies a two-value canary (`0xA11CE15EDEADBEEF`,
`0x5EED5A17C0FFEE01`) and that `stdlib_state.meta_live` returns to its
pre-scenario value. Result token is `refused`, `noop` or `defined`;
`state_ok` is 1 only when the expected outcome **and** canary/metadata
integrity both hold.

`future` is modelled as a single-consumption result handle built only from
allowed headers (`thread::join` and a `lifetime_owner`/`lifetime_borrow` result
slot) because `stdlib/async.hp` has no dedicated future type. This is a test
model, not a claim about a stdlib API that does not exist here.

## Files

- `adv1000_thread_async.cp` — the harness (single translation unit).
- `run.sh` — exact build + bounded run + digest recorder.
- `.gitignore` — ignores `results/`, the built `.bin` and inspection scratch.
- `results/scenarios.log`, `results/scenarios.sha256`, `results/commands.txt`,
  `results/build.*`, `results/run.stderr` — generated, gitignored.

## Exact commands and observed results

All commands were run from the repository root
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`) on branch
`agent/modern-stdlib`, macOS/arm64.

### Build (exit 0)

```
./build/c+ build tests/adversarial_1000/thread_async/adv1000_thread_async.cp --strict --sanitize=none -I/opt/homebrew/include -L/opt/homebrew/lib -luv -o tests/adversarial_1000/thread_async/adv1000_thread_async.bin
```

`build_rc=0`. stderr contained only two benign clang driver warnings
(`-luv` / `-L/opt/homebrew/lib` unused during the *compile* step); the link
step resolved libuv and the binary ran successfully (`loop.valid()==1` exercises
`uv_loop_init`). No C+ errors.

### Bounded run (exit 0, empty stderr)

```
timeout 120 ./tests/adversarial_1000/thread_async/adv1000_thread_async.bin
```

`run_rc=0`. Last four stdout lines:

```
S 1000 lifetime valid_rw defined 1
ADV1000_DISTINCT 0
ADV1000_DIGEST ffe4b39f52a0f9e5
ADV1000 thread_async 1000 750 1000 0
```

Interpretation of the final line: `1000` scenarios, `750` refusal scenarios
verified, `1000` scenarios with intact canary/metadata and expected result
(`state_ok`), `0` failures. First 1000 lines have the form
`S <id> <api> <case> <result> <state_ok>` with ids 1..1000.

`ADV1000_DISTINCT 0` = zero duplicate `(case, p1, p2)` tuples across the 1000
fixed-seed scenarios.

Result-token distribution: `defined` 214, `noop` 96, `refused` 690.

### Digest

- In-harness FNV-1a over `(id, case index, p1, p2, state_ok)` for all 1000
  scenarios: `ffe4b39f52a0f9e5` (printed as `ADV1000_DIGEST`).
- sha256 of the full `results/scenarios.log`:
  `e1e08f0cba16e74ab3e52c772fec0fd406ec2c2cc85dc853f58ed24c0f72cfbe`.

### Determinism check

Two consecutive bounded runs produced byte-identical stdout (`cmp` returned 0),
including both digest lines.

### End-to-end runner

```
timeout 120 sh tests/adversarial_1000/thread_async/run.sh
```

Output:

```
ADV1000 thread_async: build_rc=0 run_rc=0 scenario_lines=1000
ADV1000 thread_async 1000 750 1000 0
FNV=ffe4b39f52a0f9e5 sha256=e1e08f0cba16e74ab3e52c772fec0fd406ec2c2cc85dc853f58ed24c0f72cfbe
```

Runner exit 0. `/bin/sh` was used; no bash-only features.

## C+ lowering constraint found (and worked around)

The C+ lowering binds a local variable name to one class type and one array
bound for the entire translation unit (it de-duplicates ownership by variable
name). Two consequences observed while building this harness:

1. Using the local name `t` for both `thread` and `timer` lowered every
   `t.method()` call to `timer_*` and produced implicit-declaration errors.
   Fixed by using distinct names (`thr` for threads, `tmr` for timers).
2. Declaring `src`/`dst` at different sizes (8 and 4) in different functions
   made every subscript use the single (last) bound, so an `i` in 0..7 tripped
   `index 4 is outside the bounds of 'src' (length 4)`. Fixed by giving each
   array a unique name per case (`rw_src`, `st_src`, `tr_src`, `slot_dst`,
   `nh_dst`).

Both are properties of the C+ name-based lowering, not defects in the tested
headers. Final build has zero C+ errors.

## Non-goals / scope compliance

- No header file was edited. No file outside
  `tests/adversarial_1000/thread_async/` was created or modified.
- No `git add`, `git commit`, or branch operation was performed.
- No live-game, injected-DLL, Wine prefix, user path or hard-coded hotkey is
  involved. No raw game-memory offset is used.
- No wall-clock assertion: timer scenarios start a large (3600000 ms) timeout
  and stop it before drop; the loop is never run to fire a callback.

## Evidence, not proof

These results are **evidence**: they demonstrate that the enumerated 34
adversarial cases, under 1000 fixed-seed randomized parameter sets, produced
the documented refusal/defined result with canary and metadata intact, and
that output is reproducible byte-for-byte. They are **not a proof** of safety
for all inputs, all interleavings, or all build modes: the sweep is finite, the
thread sequences are short and fully joined (no concurrent stress), allocator
fault injection was not armed, and this run used `--sanitize=none`. A broader
sanitizer matrix or concurrent stress would be separate evidence.
