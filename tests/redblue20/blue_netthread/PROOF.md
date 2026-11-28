# BLUE-TEAM agent 14/20 — `blue_netthread` proof

**Verdict:** `BLUE20 netthread 2048 2048 0` — **PASS** (2048 cases, 2048 passed,
0 failed). Runner exit code 0.

## What is proven

Executable evidence, on the current `agent/modern-stdlib` tree, for the eight
requested network/thread safety properties:

| Required property | Family (case body) | Assertion |
|---|---|---|
| Invalid ports refuse | `net_invalid_port` | `-1`, `-65536`, `65536`, `70000`, `-123456789`, `1000000` → `listener::is_valid()==0`, `descriptor()==-1`, `bound_port()==0`; `drop()` inert |
| Invalid URLs refuse | `http_invalid_url` | 7 fixed malformed URLs → `STDLIB_FAIL` with a non-OK error category, then idempotent `drop()` |
| Invalid headers refuse | `http_invalid_hdr` | `http_headers::add(NULL)` → `STDLIB_FAIL`/`CPLUS_EXIT_UNSUPPORTED`; `http_client::set_headers(NULL)` → `STDLIB_FAIL` |
| Owned handles | `net_owned` | `listener(0)` binds a loopback ephemeral port; `accept()` yields an owned `connection`; writes succeed; `drop()` closes and invalidates both |
| Borrowed handles | `net_borrowed` | `listener_borrow`/`connection_borrow` report valid for fd>=3, `drop()` leaves the caller's fd open (`fcntl(F_GETFD)` still succeeds); fds 0/1/2 and `-1` are invalid |
| Idempotent destroy | `sync_mtx_destroy`, `sync_cnd_destroy` | first `cplus_*_destroy_checked()` → `0`, second → `EINVAL`; NULL → `EINVAL`; an `EBUSY` failure rolls back so a retry succeeds |
| Double-join refusal | `sync_join`, `sync_join_raii` | `cplus_thrd_join_checked()` second join → `EINVAL`; all-zero thread handle → `EINVAL`; RAII `thread::join()` twice → second `STDLIB_FAIL` with `STDLIB_CATEGORY_LIFECYCLE` |
| Stale generation | `sync_generation` | an `EBUSY`-rolled-back claim frees its slot; a new identity reuses it; consumed identities remain refused (`EINVAL`) even after recycling |
| Bounded joins | `sync_bounded` | after `CPLUS_SYNC_CHECKED_CAPACITY` (64) claims the 65th → `ENOMEM`; a repeat identity → `EINVAL` (identity checked before capacity, no stale object dereference) |
| No fd leaks | `net_fd_leak` | 8–23 `listener(0)` create/drop cycles leave `/dev/fd` count unchanged |
| No deadlocks | `sync_nodeadlock` | worker signals under a mutex, main `cplus_cnd_timedwait()`s to a 2 s deadline and observes the flag, joins the worker, and RAII `drop()` joins a second thread |

`http_client::set_headers()` is also asserted to **own a copy**: after
`http_headers::drop()`, `header_lines()` still reports the original count.

## Method

- **Fixed seed, deterministic:** splitmix64 seeded with `0x4251E4D5A7C0FFEE`.
  Case index and parameter for case *i* are drawn purely from that stream — no
  pid, clock, wall-clock, or live network input. A second run produced a
  byte-identical log.
- **Fork-per-case:** `proof.cp` forks one child per case. The child installs
  `SIGALRM → _Exit(99)` with `alarm(5)` and exits with the case's own code;
  the parent `waitpid()`s and counts PASS only on exit status 0 (a signal,
  watchdog, or crash is a FAIL). The parent carries `alarm(55)`.
- **Bounded timeout 60:** `run.sh` wraps the whole harness in `timeout 60`.
  Measured wall time: **7.49 s** (well inside the bound). Four consecutive
  clean-rebuild runs produced 2048/2048/0 each. A failing case, if any, emits
  a `FAILCASE F=<family> idx=<i> param=<p> rc=<n>` diagnostic to the log.
- **Build:** `build/c+ build proof.cp --sanitize=none -w -I<root>
  -L<out> -lchecked -lcurl -pthread`. `libchecked.a` is a symlink to the
  checked-sync archive `build/libc/libcplus_hardened.a`, exposed with a
  conventional `-l` name because the C+ driver forwards extra args to both
  the compile and link steps. No source/header file was edited.

## Exact results

```
BLUE20 netthread 2048 2048 0
F net_invalid_port 156
F net_owned 159
F net_borrowed 176
F net_fd_leak 171
F http_invalid_url 150
F http_invalid_hdr 168
F sync_mtx_destroy 170
F sync_cnd_destroy 160
F sync_join 146
F sync_join_raii 148
F sync_generation 158
F sync_bounded 141
F sync_nodeadlock 145
```

Family counts sum to 2048. Runner exit code: `0`.

## Scope / limitations

- This is executable evidence for the enumerated cases against the checked
  sync layer and the `net`/`thread`/`http` C+ wrappers as built here. It is not
  a formal proof, and it makes no claim about live-game or injected-DLL
  behaviour.
- The checked-sync side table is process-local; fork-per-case gives every case
  a fresh table, so the bounded-table (`ENOMEM`) case is deterministic.
- The C+ lowering emits non-fatal FFI/lifetime warnings for the intentional
  `uintptr_t` opaque thread-argument round-trip and for the vendored
  `http.hp`; the build succeeds and all assertions pass.

## Reproduce

```sh
sh tests/redblue20/blue_netthread/run.sh
```
