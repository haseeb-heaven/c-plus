# Runnable Rust examples vs C+ checked thread/synchronization wrappers

Scope: `benchmarks/rust_examples/threading/` only. This is a **safety-outcome**
comparison, not a performance benchmark. Each case prints one deterministic
line `<api> <case> <token>`.

## Files

- `threading.rs` — safe-Rust examples (`std::thread`, `Arc`, `Mutex`, `Condvar`,
  `JoinHandle`, `thread_local!`). **No `unsafe`.**
- `harness.c` — C99 harness driving the corresponding C+ wrappers from
  `build/libc/libcplus_hardened.a` with `-pthread -lm`.
- `compare.sh` — driver: verifies symbols with `nm`, builds both, runs each
  under a 30 s timeout, diffs tokens, prints
  `RUST_EXAMPLES threading <apis> <cases> <matched> <mismatched> <blocked>`,
  and exits nonzero only on a real token mismatch.
- `results/` — generated binaries, raw `.out` streams, `.err` and `.exit`
  files (gitignored by `results/.gitignore`).
- `NOTES.md` — this file.

## Symbols verified with `nm` (before writing any harness)

```
nm -g build/libc/libcplus_hardened.a | grep -E ' T _cplus_(thrd|mtx|cnd|tss|pthread)' | sort -k3
0000000000000874 T _cplus_cnd_broadcast
0000000000000a34 T _cplus_cnd_destroy
0000000000000768 T _cplus_cnd_init
00000000000007f0 T _cplus_cnd_signal
000000000000098c T _cplus_cnd_timedwait
00000000000008f8 T _cplus_cnd_wait
0000000000000724 T _cplus_mtx_destroy
000000000000037c T _cplus_mtx_init
0000000000000488 T _cplus_mtx_lock
000000000000050c T _cplus_mtx_timedlock
000000000000061c T _cplus_mtx_trylock
00000000000006a0 T _cplus_mtx_unlock
00000000000001d8 T _cplus_pthread_cond_broadcast
0000000000000248 T _cplus_pthread_cond_destroy
0000000000000168 T _cplus_pthread_cond_init
00000000000001a4 T _cplus_pthread_cond_signal
000000000000020c T _cplus_pthread_cond_wait
0000000000000000 T _cplus_pthread_create
0000000000000054 T _cplus_pthread_join
0000000000000134 T _cplus_pthread_mutex_destroy
0000000000000090 T _cplus_pthread_mutex_init
00000000000000cc T _cplus_pthread_mutex_lock
0000000000000100 T _cplus_pthread_mutex_unlock
0000000000000000 T _cplus_thrd_create
0000000000000160 T _cplus_thrd_current
00000000000002fc T _cplus_thrd_detach
0000000000000118 T _cplus_thrd_equal
0000000000000228 T _cplus_thrd_exit
0000000000000254 T _cplus_thrd_join
00000000000001a0 T _cplus_thrd_sleep
0000000000000204 T _cplus_thrd_yield
0000000000000ac8 T _cplus_tss_create
0000000000000c28 T _cplus_tss_delete
0000000000000b6c T _cplus_tss_get
0000000000000ba0 T _cplus_tss_set
```

Deterministic wrappers selected for the cases:

| case | C+ wrappers used |
| --- | --- |
| `thrd_create spawn_join` / `thrd_join result` | `cplus_thrd_create`, `cplus_thrd_join` |
| `mtx_lock lock_unlock` | `cplus_mtx_init`, `cplus_mtx_lock`, `cplus_mtx_unlock`, `cplus_mtx_destroy` |
| `mtx_trylock busy` | `cplus_mtx_init`, `cplus_mtx_lock`, `cplus_mtx_trylock`, `cplus_mtx_unlock`, `cplus_mtx_destroy` |
| `cnd_signal signal_wait` | `cplus_mtx_*`, `cplus_cnd_init`, `cplus_cnd_wait`, `cplus_cnd_signal`, `cplus_cnd_destroy`, `cplus_thrd_create`, `cplus_thrd_join` |
| `cnd_timedwait bounded` | `cplus_mtx_*`, `cplus_cnd_init`, `cplus_cnd_timedwait`, `cplus_cnd_destroy` |
| `tss_get unset` / `tss_set set_get` | `cplus_tss_create`, `cplus_tss_get`, `cplus_tss_set`, `cplus_tss_delete` |
| `*_destroy double_destroy` | none — BLOCKED (see below) |

`cplus_thrd_sleep`, `cplus_thrd_yield`, `cplus_thrd_detach`, `cplus_thrd_exit`,
`cplus_mtx_timedlock`, `cplus_cnd_broadcast`, `cplus_call_once` and the raw
`cplus_pthread_*` layer are also exported but were **not selected**: they are
either timing/scheduler dependent, involve non-joined thread lifetimes, or are
themselves the implementation layer the C11 wrappers delegate to. Choosing the
deterministic subset keeps the token stream free of wall-clock assertions.

### Unsupported case: double-destroy refusal (BLOCKED, with nm evidence)

There is **no checked double-destroy API** in the archive. The only destroy
symbols are the plain `void` wrappers shown above:

```
nm -g build/libc/libcplus_hardened.a | grep -E ' T _?cplus_(mtx|cnd)_destroy_checked'
# (no output)
0000000000000a34 T _cplus_cnd_destroy
0000000000000724 T _cplus_mtx_destroy
```

Source confirmation: `libc/src/thread/threads_c11.c` defines
`cplus_mtx_destroy` / `cplus_cnd_destroy` as `void` functions that call
`cplus_pthread_mutex_destroy` / `cplus_pthread_cond_destroy` and discard the
return code; `libc/src/thread/thread.c` forwards straight to
`pthread_mutex_destroy` / `pthread_cond_destroy`. A second call is undefined
behaviour, **not a reported refusal**. The harness therefore never calls it and
both columns emit `BLOCKED`. (`_cplus_alloc_double_free_count` in the archive
is allocator bookkeeping, not a thread-object guard.) If a checked destroy
symbol is ever added, `compare.sh` prints a note about it.

`compare.sh` re-checks every selected symbol at runtime and aborts with exit 2
if a required one is missing; if several are missing it instead reports all
cases BLOCKED and prints the `nm` evidence. (It also waits up to 15 s for the
archive because parallel agents rebuild `build/libc`.)

## Exact commands and toolchains (from the repository root)

```
$ /usr/bin/clang --version | head -1
Apple clang version 16.0.0 (clang-1600.0.26.6)
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)
$ python3 --version
Python 3.12.x
$ command -v timeout
/usr/bin/timeout
$ nm -g build/libc/libcplus_hardened.a | grep ...     # see above
```

Build/run commands issued by `compare.sh` (archive timestamp/size is volatile
because other agents rebuild in parallel; the `nm` symbol check is the gate):

```sh
/usr/bin/clang -std=c99 -O1 -g -I. -Ilibc/install-include \
  benchmarks/rust_examples/threading/harness.c \
  build/libc/libcplus_hardened.a -pthread -lm \
  -o benchmarks/rust_examples/threading/results/threading_harness

rustc -O benchmarks/rust_examples/threading/threading.rs \
  -o benchmarks/rust_examples/threading/results/threading_rust

/usr/bin/timeout 30 .../results/threading_harness > .../results/c.out 2> .../results/c.err
/usr/bin/timeout 30 .../results/threading_rust    > .../results/rust.out 2> .../results/rust.err
```

`-I.` plus `-Ilibc/install-include` makes `cplus_threads.h` (self-contained, no
system `<threads.h>`) resolve. `-pthread` is required by the pthread-backed
wrappers; `-lm` satisfies the log/time helpers the archive pulls in.

Run everything with:

```sh
sh benchmarks/rust_examples/threading/compare.sh
```

## Case / token design

Tokens: `OK` (operation completed with the expected value), `BUSY` (`EBUSY`
mapped to `thrd_busy`), `TIMEDOUT` (expired absolute deadline), `NULL` (unset
TSS), `MISMATCH` (C+ behaviour disagreed with its contract), `BLOCKED` (no
checked API exists).

- **Bounded joins / no wall-clock assertions.** The condvar signal case uses a
  predicate set under the mutex plus `cnd_signal`/`notify_one`, so the waiter
  is guaranteed to finish regardless of scheduling; `JoinHandle::join` /
  `cplus_thrd_join` then completes deterministically. The `cnd_timedwait` case
  uses an already-expired absolute deadline (C) and `Duration::ZERO` (Rust) and
  asserts only the boolean/return-code outcome — never an elapsed duration.
  The process-level `/usr/bin/timeout 30` is the outer hang guard.
- **`mtx_trylock busy`** locks a plain mutex then trylocks it (C, same thread,
  `EBUSY` -> `thrd_busy`); Rust holds the mutex from a helper thread and uses
  an mpsc handshake so `try_lock` deterministically yields
  `TryLockError::WouldBlock`.
- **TSS** checks `tss_get == NULL` before the first set, then
  `tss_set(&value)` followed by `tss_get` returning the same pointer/value;
  Rust uses a `thread_local!` `Cell<Option<i32>>`.

## Actual output (verbatim; both streams identical)

```
thrd_create spawn_join OK
thrd_join result OK
mtx_lock lock_unlock OK
mtx_trylock busy BUSY
cnd_signal signal_wait OK
cnd_timedwait bounded TIMEDOUT
tss_get unset NULL
tss_set set_get OK
mtx_destroy double_destroy BLOCKED
cnd_destroy double_destroy BLOCKED
```

Driver summary line and exit status:

```
RUST_EXAMPLES threading 10 10 8 0 2
DRIVER_EXIT=0
```

Both harnesses exited 0 under `/usr/bin/timeout 30` (no timeout) and produced
empty stderr. C and Rust token streams were byte-identical across three
consecutive extra runs (six additional process executions).

## Explicit non-equivalence statement

The two columns are **not** the same kind of guarantee.

- **C+ (hardened libc)** exposes ordinary function calls over pthreads. The
  "checked" aspect here is the namespaces adapter and its ISO C11 return-code
  mapping (`thrd_success` / `thrd_busy` / `thrd_timedout`, derived from errno).
  `cplus_mtx_destroy` / `cplus_cnd_destroy` are **not** checked at all — they
  are void forwarders, and a double destroy is undefined behaviour. There is
  no runtime detector to observe, which is exactly why that case is BLOCKED.
- **Rust (safe subset)** expresses the same outcomes through the type system:
  a `MutexGuard` unlocks on `Drop`, `JoinHandle::join` yields the typed result,
  `Condvar` waits on a predicate, and `thread_local!` gives per-thread storage.
  There is no `destroy` operation and no way to call one twice; ownership and
  `Drop` make it unrepresentable. This is compile-time prevention, not a
  runtime refusal.
- A `MATCH` therefore means **the observable outcome token agrees** for a case
  expressible in both languages (e.g. "join returns the worker's value",
  "busy trylock reports BUSY", "expired deadline reports TIMEDOUT"). It does
  **not** mean the mechanisms are equivalent: C+ reports a mapped pthread
  return code at runtime, while Rust makes the valid state transition the only
  expressible one. `BLOCKED` is the honest marker of the double-destroy gap
  and is never counted as a mismatch.

No compiler, runtime, library, or game sources were modified; only the files
under this directory were added.
