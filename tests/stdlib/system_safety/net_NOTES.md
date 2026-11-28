# Task 3 Phase 5 — network safety test notes

Owner: subagent (network safety). Branch `agent/modern-stdlib`.
Strict file ownership honoured: only `tests/stdlib/system_safety/net_*.cp` and this
`net_NOTES.md` were created/modified. `stdlib/net.hp` was **not** modified — no
defect was reproduced. No git add/commit and no compiler/runtime edits by this
agent.

## Files (5 test programs)

| File | Lines | Checks | Purpose |
| --- | --- | --- | --- |
| `net_invalid.cp` | 220 | 6 | Invalid ports, invalid-address SKIP, invalid-handle I/O, borrowed std fds, double/triple drop, fd balance |
| `net_refusal.cp` | 275 | 4 | Closed-loopback-port refusal, connect timeout (backlog + poll watchdog), fd balance |
| `net_partial.cp` | 315 | 4 | Partial receive, partial send, 64 KiB payload, remote FIN disconnect |
| `net_signal.cp` | 375 | 3 | EINTR on `read`/`accept` via forked SIGALRM, remote RST write failure |
| `net_cycles.cp` | 190 | 2 | 20 repeated connect/accept/exchange/disconnect cycles, fd balance |

Self-checking contract: one `<tag> <1|0>` line per check, final line
`NET_SAFETY <checks> <failures>`; exit 0 only when `<failures> == 0`.
`print()` from `cplus.h` is used throughout; `fflush(stdout)` after each tag so
forked children and watchdog exits cannot lose output.

## Coverage matrix

| Required case | Check(s) | Result |
| --- | --- | --- |
| Invalid ports available in `net.hp` | `net_invalid_port_range` (`-1`, `70000`, `65536`, `INT_MIN`, plus `0` control) | PASS — inert handle, `descriptor() == -1`, no fd leak |
| Invalid addresses | `net_invalid_address` | **SKIP** — see reason below (no address API) |
| Connection refusal (closed loopback port) | `net_conn_refused_closed_port` | PASS — non-blocking connect + `poll(POLLOUT,2000)` + `SO_ERROR == ECONNREFUSED` |
| Connection timeout via poll watchdog | `net_connect_timeout_poll` (RFC 5737 `192.0.2.1:9`, `poll(POLLOUT,250)`, `SO_ERROR == 0`) | PASS — watchdog fired, connect still pending |
| Connection timeout (saturated local backlog) | `net_connect_timeout_backlog` | **SKIP** — see reason below; the poll-watchdog timeout above still covers the requirement |
| Partial receives (controlled peer) | `net_partial_recv` (forked peer writes 4 + 6 bytes with delays; 2 reads + EOF) | PASS |
| Partial sends | `net_partial_send` (`SO_SNDBUF = 4096`, slow local reader, 64 KiB via `connection::write`) | PASS — write returns `STDLIB_OK`, all bytes delivered |
| Remote disconnect | `net_remote_disconnect` (clean FIN → `STDLIB_EOF`), `net_peer_reset_write` (RST → `STDLIB_FAIL`/`IO`/`"connection::write failed"`) | PASS (both) |
| Large payload ≤ 64 KiB | `net_large_payload` (65536 bytes, deterministic pattern, child verifies every byte) | PASS |
| Socket cleanup after failure | `net_invalid_port_range`, `net_invalid_connection_io`, `net_invalid_no_fd_leak` | PASS — invalid handles inert, rejects NULL/zero-capacity with `CATEGORY_INVALID`, no fd leak |
| Repeated connect/disconnect cycles | `net_repeated_cycles` (20 rounds) + `net_cycles_no_fd_leak` | PASS |
| Double close/drop | `net_double_drop_owning` (owning `connection`/`listener` dropped 3×; fd is `EBADF`) and borrowed handles | PASS — idempotent, borrowed fds stay open |
| fd leak check via `/dev/fd` | `opendir("/dev/fd")` count before/after in 3 files | PASS (counts equal) |
| Bounded waits / no parent hang | Every file: `poll()` for all peer handshakes, `SO_RCVTIMEO`/`SO_SNDTIMEO` (3–5 s), bounded `waitpid` loops, per-child `alarm(5/8)`, parent `alarm(25)` watchdog (`_exit(90)`), external `timeout -k 5 30` | PASS — no timeout observed in any run |

Notes on technique (strict compiler contract from the parent session):
- No pointer-parameter subscripts or variable modulo/division were used; copies
  use `__builtin_memcpy` / `__builtin_memcmp` / `memset`.
- Every owned handle has a unique name; repeated cycles live in a helper
  function (`nsc_one_cycle`) so each frame gets fresh RAII bookkeeping.
- No external servers; all peers are local loopback sockets or `AF_UNIX`
  `socketpair()`s with forked children. The only non-loopback address is the
  RFC 5737 documentation address `192.0.2.1` (no service exists there).

## SKIPs (verbatim reasons)

1. `net_invalid_address`:
   > stdlib/net.hp listener(port) accepts only an integer port and always binds
   > INADDR_LOOPBACK; connection has no address argument. Strings such as
   > "not.an.address" or "" cannot be passed to any net.hp API, so no
   > invalid-address case exists to exercise.

2. `net_connect_timeout_backlog`:
   > listen backlog did not saturate on this host (poll returned completion or
   > an immediate error)

   The requirement is still covered by `net_connect_timeout_poll`, which observed
   a real poll-watchdog timeout with a pending connect.

Both SKIPs print a passing tag (`1`) plus a `SKIP_REASON` line, matching the
existing `deep_net_task5` convention. SKIPs are reported, never counted as a
sanitizer-mode timeout or a defect.

## Exact commands and results

Working directory: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
(`TD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode` for
scratch binaries/logs; nothing was written outside the owned file list in the
repo).

### 1. Strict / no-sanitizer FIRST

For each `t` in `net_invalid net_refusal net_partial net_signal net_cycles`:

```sh
timeout -k 10 30 ./build/c+ build tests/stdlib/system_safety/$t.cp \
  --strict --sanitize=none -o "$TD/${t}_strict"
timeout -k 5  30 "$TD/${t}_strict" </dev/null \
  > "$TD/${t}.strict.out" 2> "$TD/${t}.strict.err"
```

| Test | build rc | run rc | stderr bytes | final line |
| --- | --- | --- | --- | --- |
| `net_invalid` | 0 | 0 | 0 | `NET_SAFETY 6 0` |
| `net_refusal` | 0 | 0 | 0 | `NET_SAFETY 4 0` |
| `net_partial` | 0 | 0 | 0 | `NET_SAFETY 4 0` |
| `net_signal` | 0 | 0 | 0 | `NET_SAFETY 3 0` |
| `net_cycles` | 0 | 0 | 0 | `NET_SAFETY 2 0` |

Total strict-none: **19 checks, 0 failures, 0 timeouts, 2 SKIPs** (all 5 files x
2 modes would be 38 check-executions).

### 2. UBSan second (under the same 30 s timeout)

Plain `--strict --sanitize=undefined` initially failed for **every** test,
including the pre-existing `tests/stdlib/container_safety/cs_basic.cp`, with:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23:
error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

This is a pre-existing toolchain/header mismatch (the core runtime expects
`CPLUS_EXIT_ARITHMETIC`, which this worktree's `runtime/include/cplus/cplus.h`
does not define); it is not caused by, and cannot be fixed from, any of the
owned files. No compiler/runtime file was edited. A command-line define
(`-DCPLUS_EXIT_ARITHMETIC=110`, matching the core header's enum value) lets the
instrumented runtime build, so UBSan was verified without file edits:

```sh
timeout -k 10 30 ./build/c+ build tests/stdlib/system_safety/$t.cp \
  --strict --sanitize=undefined -DCPLUS_EXIT_ARITHMETIC=110 \
  -o "$TD/${t}_ubsan"
timeout -k 5  30 "$TD/${t}_ubsan" </dev/null \
  > "$TD/${t}.ubsan.out" 2> "$TD/${t}.ubsan.err"
```

| Test | build rc | run rc | stderr bytes | final line | stdout identical to strict-none |
| --- | --- | --- | --- | --- | --- |
| `net_invalid` | 0 | 0 | 0 | `NET_SAFETY 6 0` | yes |
| `net_refusal` | 0 | 0 | 0 | `NET_SAFETY 4 0` | yes |
| `net_partial` | 0 | 0 | 0 | `NET_SAFETY 4 0` | yes |
| `net_signal` | 0 | 0 | 0 | `NET_SAFETY 3 0` | yes |
| `net_cycles` | 0 | 0 | 0 | `NET_SAFETY 2 0` | yes |

Total UBSan: **19 checks, 0 failures, 0 timeouts, 2 SKIPs**; empty stderr in all
cases (no UBSan diagnostics); `cmp -s` confirmed strict-none and UBSan stdout are
byte-identical for all 5 programs (determinism).

### 3. Stability

Each strict-none binary was run 3 additional times under `timeout -k 5 30`; all
15 repetitions exited 0, produced empty stderr and byte-identical stdout.
No hangs, no watchdog exits (rc 90), no 124/137 timeouts.

## Defects found

**None reproduced in `stdlib/net.hp`.** All observed behaviours matched the
documented contract:
- invalid ports leave an inert handle; `read`/`write` on invalid handles and
  NULL/zero-capacity arguments return `STDLIB_FAIL` with `CATEGORY_INVALID`;
- `drop()` is idempotent for owning handles and does not close borrowed fds;
- `read` retries EINTR and reports clean FIN as `STDLIB_EOF`;
- `write` loops over short writes and reports RST as a recoverable
  `CATEGORY_IO` failure;
- `accept` retries EINTR;
- no descriptor growth across failure paths, 20 cycles or repeated drops.

The only anomaly encountered was the UBSan runtime build issue described above,
which is outside this task's file ownership and was worked around on the command
line rather than by editing any file.
