# Deep task-5 contract: `stdlib/net.hp`

Per-family evidence for `tests/stdlib/deep_net_task5.cp`. This suite covers only
the genuinely uncovered gaps left by `deep_net.cp` (which already pins port
ranges, bind-in-use, borrowed fd 0/1/2 and >=3, broken-pipe write, exact EOF,
injected partial write, invalid handles, write-no-progress, injected EINTR/EIO,
EMFILE and fd leaks).

Status: **no defect found**. `stdlib/net.hp` was not modified.

## Scope, ownership and isolation

| Item | Path |
| --- | --- |
| Test | `tests/stdlib/deep_net_task5.cp` |
| Contract | `docs/stdlib/DEEP_NET_TASK5_CONTRACT.md` (this file) |
| Header under test | `stdlib/net.hp` (untouched) |
| Other files created | none |

The test is self-checking, has no `.stdin`/`.expected` file, and returns `0`
only when every check passes. Each check prints `<tag> <0|1>`; the final line
is `NET_TASK5 <checks> <failures>`. A documented SKIP prints `<tag> 1` followed
by `SKIP_REASON <tag> <reason>`.

**No external servers are used.** Every endpoint is either `AF_INET` bound to
`INADDR_LOOPBACK` (`127.0.0.1`) or an `AF_UNIX` `socketpair()`. Every remote
peer is a `fork()`ed child of this test process; the parent and child exchange
small status bytes over anonymous `pipe()`s. Nothing connects off-host, no
well-known service is contacted, and no `data/` asset or user path is used.

All blocking waits are bounded: 3 s `poll()` handshakes, 3 s
`SO_RCVTIMEO`/`SO_SNDTIMEO` on sockets, a 5 s `alarm()` in each forked child,
a bounded `waitpid(WNOHANG)` reap loop (3 s, then `SIGKILL`), and a 25 s process
watchdog (`SIGALRM` → message on stderr + `_exit(90)`), which is shorter than
the harness `timeout 30`. `SIGPIPE` is routed to a no-op handler so a reset peer
yields a returned error rather than process termination. Every owned handle is
dropped and the parent compares `/dev/fd` counts before and after.

## Scenarios

| Tag | What is asserted | Result |
| --- | --- | --- |
| `net_conn_refused_closed_port` | `listener(0)` publishes a loopback port, is dropped, then a non-blocking `connect()` to that now-closed port fails with `ECONNREFUSED`; the probe fd is closed | pass |
| `net_peer_reset_recoverable` | a forked peer connects, sets `SO_LINGER{onoff=1,linger=0}` and closes (RST), then signals the parent over a pipe; `connection::write` returns `STDLIB_FAIL` / `STDLIB_CATEGORY_IO` / `"connection::write failed"` | pass |
| `net_invalid_address` | **SKIP** — no address-string argument exists in `net.hp` | — |
| `net_repeated_drop` | owning `listener` and borrowed `connection` each `drop()`ped 3×; descriptors are `-1`, both handles are inert, and the borrowed fd is still open | pass |
| `net_repeated_drop_no_leak` | `/dev/fd` count unchanged across the triple-drop scenario | pass |
| `net_read_eintr_signal` | child blocks in `connection::read`; a real `SIGALRM` (no `SA_RESTART`) interrupts it; the wrapper retries and returns the 4-byte peer payload with a clean `error` | pass |
| `net_accept_eintr_signal` | same with `listener::accept`: a real `SIGALRM` interrupts the blocked `accept()`, the wrapper retries, and the forked connector is accepted (valid fd) | pass |
| `net_partial_read` | a controlled peer writes 4 bytes, pauses 120 ms, writes 6 bytes, pauses and closes; the parent accumulates over multiple `read()` calls, sees exact bytes `AAAABBBBBB`, `>=2` data reads and a final `STDLIB_EOF` with a clean `error` | pass |
| `net_task5_no_fd_leak` | `/dev/fd` count unchanged across the whole suite | pass |

### Connection-refused evidence (no external server)

The "service" is a `net.hp` `listener(0)` created solely to obtain an ephemeral
loopback port. It is immediately dropped, so no socket is listening when the
raw, non-blocking `connect()` is issued. `poll(POLLOUT, 2000 ms)` plus
`getsockopt(SO_ERROR)` bound the connect and recover the exact `errno`. The
loopback kernel returns `ECONNREFUSED`, a defined recoverable socket failure,
and the probe fd is closed on the same path — no fd leak.

`stdlib/net.hp` has **no active-connect API** (only `listener`, `connection`,
`listener_borrow`, `connection_borrow`), so `ECONNREFUSED` cannot be observed
through a `net.hp` method. The complementary case that *does* exercise
`net.hp` is `net_peer_reset_recoverable`: a peer that aborts with RST makes
`connection::write` return the documented `STDLIB_FAIL` / `CATEGORY_IO` error.
Neither case uses a live external server.

### Skips

- `net_invalid_address` — `listener(port)` accepts only an integer port and
  always binds `INADDR_LOOPBACK`; `connection` has no address argument. Strings
  such as `"not.an.address"` or `""` cannot be passed to any `net.hp` API, so
  no INVALID_ADDRESS case exists. Documented rather than fabricated.

### Test-authoring observations (not header defects)

1. **`STDLIB_EOF == 1` collides with a legitimate 1-byte read count.**
   A read loop must test `n == STDLIB_EOF` (and `n == STDLIB_FAIL`) *before*
   `n > 0`. An early draft used only `n > 0`, so the EOF sentinel was consumed
   as data; UBSan then reported `index 16 out of bounds` in the accumulation
   buffer. This was a test bug, fixed in the final file, not a `net.hp` defect.
2. **The EINTR alarm must be armed before the blocking call.** The first draft
   armed `alarm(1)` after `accept()` returned, so the signal never interrupted
   it and the scenario self-reported SKIP. The final file arms it first and the
   scenario passes deterministically.
3. **C+ owned-locals are tracked by name for the whole function.** Every
   owning `listener`/`connection` local in this file has a unique name; reused
   names lose zero-initialisation and destructors.
4. **Compiler-stress incident.** During an earlier parent run, a `c+ build` of
   this path hung for >15 minutes while the source file was mid-write. The
   cause was an unbalanced brace (a missing closing `}` for the N4a scope) in
   that transient version: the C+ RAII lowering treats unbalanced scopes as a
   pathological case and does not terminate. The **final** file is brace-balanced
   (verified by an independent brace count) and contains no construct that
   stresses the compiler: three timed strict builds completed in **0.36 s,
   0.34 s and 0.34 s**, all rc 0. Five consecutive bounded runs of the built
   binary were rc 0 with empty stderr and byte-identical stdout.

## Exact stdout (byte-identical in all three build modes)

```
net_conn_refused_closed_port 1
net_peer_reset_recoverable 1
net_invalid_address 1
SKIP_REASON net_invalid_address stdlib/net.hp listener(port) takes only an integer port and always binds INADDR_LOOPBACK; connection has no address argument either. Strings such as "not.an.address" or "" cannot be passed to any net.hp API, so no INVALID_ADDRESS case exists.
net_repeated_drop 1
net_repeated_drop_no_leak 1
net_read_eintr_signal 1
net_accept_eintr_signal 1
net_partial_read 1
net_task5_no_fd_leak 1
NET_TASK5 9 0
```

stderr is empty; exit code is `0`.

## Verification commands

From the repository root (checked-in `build/c+` driver). Driver flags are
separate argv operands — zsh does not word-split a single flags string, which
makes `clang` reject `--strict --sanitize=none` as one option.

```sh
# STRICT, sanitizers explicitly off (run FIRST)
./build/c+ build --strict --sanitize=none tests/stdlib/deep_net_task5.cp \
  -o build/stdlib-tests/deep_net_task5.strict_none
timeout 30 ./build/stdlib-tests/deep_net_task5.strict_none < /dev/null

# default Safe mode (UBSan already on)
./build/c+ build tests/stdlib/deep_net_task5.cp \
  -o build/stdlib-tests/deep_net_task5.default
timeout 30 ./build/stdlib-tests/deep_net_task5.default < /dev/null

# explicit UBSan
./build/c+ build --sanitize=undefined tests/stdlib/deep_net_task5.cp \
  -o build/stdlib-tests/deep_net_task5.ubsan
timeout 30 ./build/stdlib-tests/deep_net_task5.ubsan < /dev/null
```

Results: all three modes build rc 0, run rc 0, stdout byte-identical to the
block above, stderr empty. `cmp` confirmed identical stdout across
strict/default/UBSan for all three task-5 suites. No sanitizer report and no
C+ runtime fatal in any mode. The other two suites report
`ERROR_TASK5 22 0` and `JSON_TASK5 23 0` with the same clean result.
