# Deep contract: `stdlib/net.hp`

Status: **verified, no defects found in `stdlib/net.hp`** (no changes to the
header were required). This document records the observed contract of the
RAII TCP wrappers under adversarial inputs and injected syscall faults, so
future changes can be checked against evidence rather than intuition.

## Scope and isolation

- Subject: `stdlib/net.hp` (`listener`, `connection`, `listener_borrow`,
  `connection_borrow`) and the `STDLIB_OK` / `STDLIB_EOF` / `STDLIB_FAIL`
  result protocol from `stdlib/error.hp`.
- Independent test: `tests/stdlib/deep_net.cp`. It is self-contained and does
  not modify or depend on `tests/stdlib/net_test.cp`.
- Files owned by this task: `stdlib/net.hp` (untouched), `tests/stdlib/deep_net.cp`,
  this document. No other file was created or modified.
- All network endpoints are `AF_INET` bound to `INADDR_LOOPBACK`; fault-injection
  pairs use `AF_UNIX` `socketpair`. Nothing connects off-host.
- Every blocking wait is bounded: `poll()` timeouts of 3 s per pipe handshake,
  `SO_RCVTIMEO`/`SO_SNDTIMEO` of 3 s on injected sockets, and a 45 s `SIGALRM`
  watchdog that writes a fatal line to stderr and `_exit(90)`.
- The test ignores `SIGPIPE` so a broken peer produces a returned error rather
  than process termination. This is caller responsibility, not a `net.hp`
  guarantee.
- All owned handles are released; the parent compares `/dev/fd` entry counts
  before and after every scenario.

## Result classification

Each scenario prints `SCENARIO <name> <cls>`:

| cls | meaning |
| --- | --- |
| `0` | SUCCESS — expected successful operation or exact contract result |
| `1` | RECOVERABLE — expected rejection/error, handled without crash or leak |
| `2` | FATAL — contract violation, crash, hang, or descriptor leak |
| `3` | SKIP — not portable on this host; preceded by `SKIP_REASON <name> <reason>` |

The final line is `SUMMARY <success> <recoverable> <fatal> <skipped>`.
Exit code is `0` when `fatal == 0`, otherwise `1`.

## Observed contracts (verified on this host)

| Scenario | Contract exercised | Observed result | cls |
| --- | --- | --- | --- |
| `port_ephemeral_zero` | `listener(0)` is an ephemeral **loopback** bind; `bound_port()` matches `getsockname()`, descriptor ≥ 3 | valid, exact port/address | 0 |
| `port_65535` | highest legal port binds when free | valid, `bound_port()==65535`; SKIP if `127.0.0.1:65535` is occupied | 0 |
| `port_invalid_range` | `-1`, `70000`, `INT_MIN` produce inert handles (`descriptor()==-1`, `bound_port()==0`); `drop()` is idempotent | inert, no fd | 1 |
| `bind_in_use_fork` | child holds `127.0.0.1:P` **without** `SO_REUSEADDR`; parent's `listener(P)` (with `SO_REUSEADDR`) must fail with `EADDRINUSE`, close its socket, and stay inert | invalid handle, no fd | 1 |
| `borrow_no_close_012` | zero-init and borrowed handles for fd 0/1/2 are `!is_valid()` and their destructors never close stdio | all fds still open | 0 |
| `borrow_ge3_no_close` | borrowed fd ≥ 3 is `is_valid()` but destructor never closes it | fds still open after scope | 0 |
| `write_broken_pipe` | write to a closed `AF_UNIX` peer returns `STDLIB_FAIL`, `STDLIB_CATEGORY_IO`, message `connection::write failed` (with `SIGPIPE` ignored) | exact error | 1 |
| `eof_exact` | peer closes after 2 bytes: first `read()` returns 2, second returns exactly `STDLIB_EOF` with a clear `error` | `2`, `STDLIB_EOF`, `err.ok()` | 0 |
| `partial_write_short` | injected `write()` returning at most 1024 bytes/call: `connection::write` loops until all 65536 bytes are sent; child verifies every byte | `STDLIB_OK`, >1 write call, payload intact | 0 |
| `invalid_handles` | borrowed `-1`, zero-init and invalid args yield `STDLIB_FAIL` / `STDLIB_CATEGORY_INVALID` with exact messages; `err == NULL` is safe; `bound_port()==0`; `accept()` inert | exact errors, no crash | 1 |
| `fault_write_no_progress` | injected `write()==0` yields `STDLIB_FAIL`, `STDLIB_CATEGORY_IO`, `connection::write made no progress`; a following real write recovers; zero-length write on a valid handle is `STDLIB_OK` | exact error then recovery | 1 |
| `fault_read_retry` | one injected `EINTR` is retried and returns data; injected hard `EIO` yields `STDLIB_FAIL`, `STDLIB_CATEGORY_IO`, `connection::read failed` | retry + exact error | 0 |
| `fault_socket_emfile` | child lowers `RLIMIT_NOFILE` to 3 so `socket()` fails with `EMFILE`; `listener(0)` is inert | invalid handle, no fd | 1 |
| `no_fd_leak` | `/dev/fd` count unchanged across all scenarios | equal before/after | 0 |

Exact stdout (all three required build modes):

```
SCENARIO port_ephemeral_zero 0
SCENARIO port_65535 0
SCENARIO port_invalid_range 1
SCENARIO bind_in_use_fork 1
SCENARIO borrow_no_close_012 0
SCENARIO borrow_ge3_no_close 0
SCENARIO write_broken_pipe 1
SCENARIO eof_exact 0
SCENARIO partial_write_short 0
SCENARIO invalid_handles 1
SCENARIO fault_write_no_progress 1
SCENARIO fault_read_retry 0
SCENARIO fault_socket_emfile 1
SCENARIO no_fd_leak 0
SUMMARY 8 6 0 0
```

stderr is empty on success. Exit code is `0`.

## Deterministic fault injection

`deep_net.cp` overrides `write` and `read` at link time. Each wrapper calls the
real syscall (`syscall(SYS_write/SYS_read, ...)`) unless a flag is set:

- `g_fault_write_zero` → `write` returns 0 (no-progress path).
- `g_fault_short_write` → `write` returns at most 1024 bytes per call and
  counts calls (partial-write loop).
- `g_fault_read_eintr_once` → one `-1`/`EINTR`, then normal read (retry path).
- `g_fault_read_error` → `-1`/`EIO` (hard IO failure path).
- `RLIMIT_NOFILE=3` in a forked child → deterministic `socket()` failure.

`syscall(2)` is deprecated on macOS but is used to avoid
`dlsym(RTLD_NEXT, ...)` provenance warnings in `--strict` mode; a
`#pragma clang diagnostic` keeps build logs clean.

## Skips and portability

No scenario was skipped on this host (`SUMMARY ... 0`). Portability rules
built into the test:

- `port_65535`: a raw bind probe runs first; if `127.0.0.1:65535` is already
  bound, the scenario prints `SKIP_REASON port_65535 probe bind ... failed`.
- Pipe-based scenarios SKIP with an explicit reason if `pipe()` fails, or if
  the child cannot publish its bound port within 3 s.
- `no_fd_leak` SKIPs with `opendir(/dev/fd) unavailable on this host` if
  `/dev/fd` cannot be opened.
- `syscall(SYS_write/SYS_read)` and `AF_UNIX socketpair` are POSIX/macOS
  facilities available here; on a host lacking them the build would fail
  loudly rather than silently pass, so those cases are not marked SKIP.

## Findings and non-defects

1. **No `stdlib/net.hp` defect was confirmed.** Every listed scenario passed
   against the unmodified header. No behaviour was changed to make a test pass.
2. **Caller-managed `SIGPIPE`**: a write to a closed peer terminates a process
   by default; `net.hp` does not suppress it. The test ignores `SIGPIPE` and
   verifies the returned `STDLIB_FAIL`/IO error. Documented as caller duty.
3. **`SO_REUSEADDR` ordering matters**: a failed-bind test only fails
   deterministically when the occupying socket did **not** set `SO_REUSEADDR`.
   With `SO_REUSEADDR` on both sockets BSD/macOS semantics can permit the bind.
   The test deliberately uses a non-reuse blocker in a forked child.
4. **C+ owned-locals are tracked by name for the whole function**
   (`compiler/Sema/RaiiLowering.cpp`: `owned_.count(name)`). Reusing a resource
   variable name in a later scope suppresses both zero-initialisation and the
   destructor. Early versions of this test reused `peer`, `srv`, `zl`, `zc`,
   `l`, which caused false FATAL results (uninitialised handles, leaked fds).
   The final test gives every owning local a unique name. This is a test-
   authoring constraint, not a `net.hp` defect.
5. **Port 0 and 65535 are legal** (ephemeral request and highest value);
   only negative values and values > 65535 are rejected. "Invalid ports" in
   the assignment therefore refers to the out-of-range subset.

## Verification commands and results

```sh
./build/c+ build tests/stdlib/deep_net.cp \
    -o build/stdlib-tests/deep_net_default                  # rc=0
./build/c+ build --strict --sanitize=none tests/stdlib/deep_net.cp \
    -o build/stdlib-tests/deep_net_strict                   # rc=0
./build/c+ build --sanitize=undefined tests/stdlib/deep_net.cp \
    -o build/stdlib-tests/deep_net_ubsan                    # rc=0
timeout 60 ./build/stdlib-tests/deep_net_default            # rc=0
timeout 60 ./build/stdlib-tests/deep_net_strict             # rc=0
timeout 60 ./build/stdlib-tests/deep_net_ubsan              # rc=0
sh tests/stdlib/run_stdlib_tests.sh deep_net                # 1 passed, 0 failed
```

- Counts in every mode: **8 SUCCESS, 6 RECOVERABLE, 0 FATAL, 0 SKIP**.
- stdout byte-identical across all three modes and across 10 repeated runs of
  the default build; stderr empty; no hang and no leaked descriptors.
