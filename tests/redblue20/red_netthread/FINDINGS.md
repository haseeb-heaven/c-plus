# RED-TEAM agent 4/20 — networking / threading red-blue findings

Branch: `agent/modern-stdlib`. Attack only: **no source or header was edited**.
All deliverables are under `tests/redblue20/red_netthread/`.

Targets: `stdlib/net.hp`, `stdlib/http.hp`, `stdlib/thread.hp`,
`stdlib/async.hp`, and `libc/src/thread/sync_checked.c`.

## Result

```
RED20 netthread 3000 0 0 0 3000
SELFCHECK 5 5
SCENARIOS_SHA256 8d3fdcae4da81a2fef3f6205140083a69fd25de6e918a52da69d4a8a5c909fe4
```

| Class | Count |
| --- | --- |
| REFUSED (contract-honouring refusal or defined safe result) | 3000 |
| CRASH (signal / fatal / hang / short verdict in forked child) | 0 |
| CORRUPT (canary or documented invariant violated) | 0 |
| UNEXPECTED_OK (hostile input accepted where refusal is promised) | 0 |

**Genuine findings in the current tree: NONE.** Every one of the 3000
deterministic attacks was refused as documented. The classifier was checked for
non-vacuity: `SELFCHECK 5 5` confirms an accepted outcome, a non-zero child
exit, a fatal signal, a canary violation and an out-of-range verdict code each
land in their intended bucket. The campaign stdout has no unexpected stderr;
the run completes in ~7 s against the 60 s bound.

## Reproduce

From the repo root:

```sh
sh tests/redblue20/red_netthread/run.sh
# prints SELFCHECK 5 5, then RED20 netthread 3000 0 0 0 3000,
# then SCENARIOS_SHA256 <digest>
```

The script builds with the required mode and libraries:

```sh
./build/c+ build tests/redblue20/red_netthread/harness.cp \
  --default --sanitize=none -I. \
  -lcurl -I/opt/homebrew/include -L/opt/homebrew/lib -luv \
  -Lbuild/libc -lcplus_hardened \
  -o tests/redblue20/red_netthread/out/harness
```

`-lcplus_hardened` resolves `build/libc/libcplus_hardened.a`, which supplies
the three `*_checked` symbols under test. `out/` is gitignored.

## Harness design

* **Determinism.** Fixed splitmix64 seed `0x4ED7EA51DEADBEEF`; three consecutive
  draws per case; `family = case_index % 30`. 30 families × 100 cases = 3000.
* **Isolation.** One `fork()` per case with a `pipe()` for the 12-byte verdict.
  The child redirects stdout to `/dev/null`, arms `alarm(2)` and writes exactly
  one `{cls, sub, got}` record before `_exit(0)`. The parent uses `poll()` for
  the verdict and a bounded reap (non-blocking probe then a blocking wait that
  the child's alarm bounds); `run.sh` adds a process-wide `timeout 60`.
* **Canaries.** A 16-byte pre/post guard surrounds a 512-byte sink buffer; the
  data region is `0x5A` and the source is `0x42`. Any OOB NUL or stray copy is
  detected. Byte reads use `memcpy` rather than C+ subscripts so the lowering's
  extent heuristics cannot mask a real out-of-bounds write.
* **Failure atomicity.** Refusals must leave the sink and both guards byte-for-byte
  unchanged; accepted-but-hostile inputs are only ever reported when the header
  explicitly promises refusal.

## Coverage — all 30 families, 100 cases each

| # | Family | Attack shape | Contract promise checked |
| --- | --- | --- | --- |
| 0 | `net_port` | -1, 65536, 70000, `0xFFFFFFFF`, arbitrary large ints, -12345 | inert handle (`descriptor == -1`, `!is_valid`, `bound_port == 0`), idempotent drop |
| 1 | `net_borrow012` | borrow fd 0/1/2, read/write/accept/bound_port, triple drop | `!is_valid`, all I/O refused, stdio fds unchanged |
| 2 | `net_borrow_stale` | borrow fd ≥ 3, close underlying fd, then read/write/drop | I/O fails cleanly, borrow never closes another fd, no canary write |
| 3 | `net_double_drop` | owning connection + listener, drop ×3 each | owned fd closed, fd 0 untouched, no double close |
| 4 | `net_invalid_io` | fd -1 / borrowed fd 2, NULL buffer, zero capacity, NULL/1-byte write | `STDLIB_FAIL` + `CATEGORY_INVALID`, buffer untouched |
| 5 | `net_write_loop` | zero-length and 1..256-byte writes over an AF_UNIX socketpair | defined success, byte-exact peer delivery |
| 6 | `net_accept_invalid` | accept on borrowed fd 0-2 and on an inert listener | inert connection, `bound_port == 0` |
| 7 | `sink_zero` | `size == 0`, NULL buf, random used/overflow, huge `size*nmemb` | no write, no NUL, canaries intact |
| 8 | `sink_full` | `used == size`, `used > size`, `used == SIZE_MAX`, near-`SIZE_MAX` | agent-5 findings 1 & 2 regressions: refuse before any write |
| 9 | `sink_oversize` | `bytes > size-used-1`, caps 8..511, every `used < cap` | clamp to `size-used-1`, NUL at `cap-1`, `overflow=1`, no guard write |
| 10 | `sink_mul` | `SIZE_MAX*2`, `2^32*2^32`, `(2^63|r)*4` | multiplication-overflow guard refuses before any write |
| 11 | `sink_boundary` | `used == size-1`, 1..32-byte chunk | room-0 clamp, NUL at `size-1`, canaries intact |
| 12 | `http_bad_url` | 10 malformed / unsupported URL shapes via `get()` | `STDLIB_FAIL` with error set, caller buffer cleared |
| 13 | `http_null_args` | NULL URL / buffer / zero size / use-after-drop | `STDLIB_FAIL`, buffer untouched |
| 14 | `http_double_drop` | http + headers + client, CRLF-free lines, drop headers before client | `set_headers` owns a 2-line copy; all double drops idempotent |
| 15 | `http_headers_hostile` | NULL line (must refuse), CRLF/control/empty lines (documented copy) | refusal for NULL; verbatim copy otherwise; double drop safe |
| 16 | `thread_double_join` | start, join, join again, drop ×2 | second join `STDLIB_FAIL` + `CATEGORY_LIFECYCLE`, return value preserved |
| 17 | `thread_start_invalid` | NULL fn, join non-joinable, start twice while running | each hostile start/join refused; valid start succeeds |
| 18 | `thread_guard_repeat` | ERRORCHECK mutex, `unlock()` ×2 + `drop()` + scope exit | second unlock is a no-op; mutex remains releasable |
| 19 | `thread_nested` | two ERRORCHECK mutexes, nested guards, reverse drop | exactly one unlock per mutex; both trylock free |
| 20 | `thread_condvar` | `wait(NULL)`, `signal`/`broadcast` with no waiter, drop ×2 | `-1` / `0` returns, no crash |
| 21 | `thread_mutex_cycle` | zero-init guard, 1..5 lock/unlock/drop cycles | inert zero guard; every mutex freed cleanly |
| 22 | `async_loop_mode` | modes -4..4 (valid 0/1/2, rest hostile) | invalid mode `STDLIB_FAIL` + `CATEGORY_INVALID`; valid mode OK |
| 23 | `async_loop_drop` | stop/now/drop ×2, then run on dropped loop | invalid loop refused, no-op methods safe |
| 24 | `async_timer` | NULL loop, NULL callback, stop inactive, one-shot close path | invalid timer refused; stop/close safe |
| 25 | `async_read_file` | NULL owner/path/buffer, zero size, missing path, `UINT_MAX+1` | `-1` with category, buffer untouched on refusal |
| 26 | `sync_mtx` | valid destroy, double destroy, NULL, locked mutex (EBUSY) + retry | `0` / `EINVAL` / raw code, rolled-back slot retryable |
| 27 | `sync_cnd` | valid destroy, double destroy, NULL | `0` / `EINVAL` |
| 28 | `sync_thrd` | zero handle, valid join, double join | `EINVAL` / `0` / `EINVAL`, result propagated |
| 29 | `sync_concurrent_exhaust` | 4 racing destroy callers; 64+ distinct objects to fill the table | exactly one winner; `ENOMEM` beyond capacity; no stale deref |

## Prior findings regression-confirmed fixed

Agent 5 (`tests/redteam/json_net/FINDINGS.md`) reported two genuine
`http_sink_write` CORRUPT defects: writing `buf[size]` when `used == size`, and
a wraparound `memcpy` before the buffer when `used == SIZE_MAX`. The current
header refuses `used >= size` before touching the buffer and clamps oversized
chunks. Family 8 drives both exact states (plus `SIZE_MAX-k`) 100 times against
guarded buffers; all were REFUSED with untouched canaries. The two fixes hold.

## Not reported as findings (documented or out-of-contract)

1. **Explicit `.drop()` zeroes the object in lowered C+.** The generated code is
   `connection_drop(&c); cplus_zero(&c, sizeof(c));`, so `descriptor()` reads 0
   after an explicit drop, not the header's `-1`. This is a compiler lowering
   artifact, not a `net.hp` defect; the campaign checks ownership (fd closed,
   stdio untouched) instead of the post-drop sentinel.
2. **`sync_checked` keys only on address/handle bytes.** Destroying a mutex,
   then re-initialising a *different* mutex at the same address and destroying
   it, is refused (`EINVAL`). That is the documented address-keyed design; the
   campaign uses distinct objects so it does not produce a false alarm.
3. **`generation` is stamped but not consulted by `slot_matches`.** The slot
   table has a monotonic generation field described as an ABA guard, but match
   decisions compare only identity bytes. No API exposes the generation, so
   there is no observable behavioural defect; noted for the blue team.
4. **HTTP scheme policy.** libcurl still accepts e.g. `file://` URLs; the header
   documents no scheme restriction, so this is not an unexpected acceptance.
5. **Caller-owned sink contract.** A caller that declares `size` larger than its
   real buffer can make any bounds-checking sink overflow; that is a caller
   contract violation, not a header defect. The harness never lies about `size`.
6. **`timer` outliving its borrowed `loop`** is documented caller ordering;
   this harness always drops the timer first.
7. **A guard on the same mutex twice deadlocks** (no recursive-lock promise).
   Nested-guard cases use distinct mutexes; same-mutex double-lock is therefore
   not counted as a finding.

## Honesty about harness bugs found during development

Early runs reported 302 CRASH / 396 CORRUPT / 100 UNEXPECTED_OK. Every one was
traced to the harness, not the target, and fixed before the final campaign:

* `canary_init()` initially filled the *data* region with the guard byte, so
  every "no write" check looked corrupted. It now fills guard bytes `0xA5` and
  data bytes `0x5A` separately.
* ERRORCHECK mutexes were reconfigured *after* the guard had locked them, so
  the detector itself caused `pthread_mutex_unlock` fatals. Reconfiguration now
  happens before guard construction.
* A 64→32-bit narrowing of a random `uint64_t` triggered the C+ runtime's
  conversion fatal; the port generator now stays within the intended range.
* Explicit `.drop()` zeroing (item 1 above) made `descriptor() == -1` checks
  fail spuriously.
* The source buffer was 128 bytes while the clamp can copy up to 511, so the
  copied-byte canary saw non-`0x42` bytes. `g_src` is now `SINK_BUF + 64`.
* The `pthread_create` identifier plus `fork()` trips a token-based C+ FFI
  diagnostic; the race family starts its workers through the C+ `thread`
  wrapper instead (they are started only after the fork).
* A 10 ms `usleep` in the reap loop made 3000 cases take 57 s; the reap is now
  event-driven and the whole campaign takes ~7 s.

None of these was a target defect; no reported result came from a hidden or
invented reproducer.

## Evidence, not proof

3000 fixed-seed, forked, bounded executions against the current tree. This is
behavioural evidence for the enumerated inputs, not a formal memory-safety or
absence-of-UB proof, and it does not establish live-game behaviour.
