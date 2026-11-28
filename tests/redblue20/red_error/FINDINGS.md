# RED20 agent 7/20 — error handling & runtime liveness (red_error)

**Target:** `stdlib/error.hp` (checked allocator, metadata side table, generation
tokens, error record, callback, telemetry) and the runtime side tables linked by
the C+ driver (`cplus_runtime.c`: live table, retired ledger, generation counter).

**Harness:** `harness.cp` — supervisor forks a fresh child per case (2400 cases);
each child runs one hostile scenario and publishes a verdict through shared
memory. Signals, hangs (>200 ms) and unexpected exits are `CRASH`. Deliberate
`cplus_fatal(104/105)` is a *correct* deterministic safety refusal and is
reported as `REFUSED`.

**Build/run:** `bash tests/redblue20/red_error/run.sh` (build and run each under
`timeout 60`, command form `./build/c+ build --default --sanitize=none -I. -o`).

## Result (exact, reproducible)

```
RED20 error 2400 0 118 36 2246
```

| verdict | count |
|---|---:|
| CRASH | 0 |
| CORRUPT | 118 |
| UNEXPECTED_OK | 36 |
| REFUSED | 2246 |

Non-REFUSED cases are concentrated in five deterministic sub-variants:

| family / sub-variant | cases | verdict |
|---|---:|---|
| F3/sub2 callback longjmp guard latch | 50 | CORRUPT |
| F3/sub4 callback mutates recorded message | 50 | CORRUPT |
| F7/sub8 length-query truncation | 18 | CORRUPT |
| F7/sub4 realloc ignores injected failure | 18 | UNEXPECTED_OK |
| F7/sub10 release of non-stdlib block | 18 | UNEXPECTED_OK |

---

## E1 — CORRUPT: `stdlib_safe_div` truncates every length query above 65535 elements
**18 cases (`F7/sub8`, ids 2208, 2219, …).** `stdlib_alloc(70000)` has
`stdlib_alloc_size(p) == 70000` but `stdlib_alloc_length(p) == 65535`;
`stdlib_alloc_query_length(p, 1, &n)` returns 0 with `n == 65535`.

Root cause: `stdlib/error.hp:468-480`. The shift-and-subtract loop only spans
`sh = 15..0`, so the quotient is computed modulo 2^16 (max 65535). The comment
claims this "covers all STDLIB_ALLOC_LIMIT element-count queries on this host",
but `STDLIB_ALLOC_LIMIT` is 2^40, so any allocation of 65536+ bytes with
`elem_size == 1` is misreported. Affects `stdlib_alloc_length` (`:505-517`),
`stdlib_alloc_query_length` (`:544-562`) and every container/telemetry consumer
that derives a count from them. Expected division is not actually avoided — the
loop is just an incomplete long division.

## E2 — CORRUPT: re-entrancy guard is latched forever if the error callback does not return
**50 cases (`F3/sub2`, ids 1102, 1108, …).** Install a callback that
`longjmp`s out, raise one error, install a normal counting callback, raise a
second error: the second callback is never invoked (`cb_calls == 0`).

Root cause: `stdlib/error.hp:192-228`. `stdlib_note` sets the TLS flag
`stdlib_error_reporting = 1`, calls the callback, and resets it to 0 **only on
normal return**. A `longjmp`, `pthread_exit`, or any other non-local exit leaves
the guard latched, permanently suppressing *all* future callback delivery for
that thread. Counters keep incrementing, so the loss is silent: runtime
diagnostics become permanently blind while the program keeps running. This is
a runtime-liveness/observability defect, not just a callback edge case.

## E3 — CORRUPT: the error callback can rewrite the error it is reporting
**50 cases (`F3/sub4`, ids 1104, 1110, …).** Pass a mutable buffer holding
`"original-message"` to `error_fail_cat`; the installed callback writes `'Z'`
into that buffer; after return `e.what()` is `"Zoriginal-message"`, not the
string the caller passed.

Root cause: `stdlib/error.hp:677-699`. `error_fail_cat` calls `stdlib_note`
(line 680) *before* it copies the message into the record (lines 690-695), and
the callback receives the caller's `const char *message`. A callback that
mutates shared/global scratch text (a plausible logging pattern) changes the
error record. The record should snapshot the message before user code runs.

## E4 — UNEXPECTED_OK: `stdlib_realloc` bypasses allocation-failure injection
**18 cases (`F7/sub4`, ids 2204, 2215, …).** After
`stdlib_alloc_fail_after(0)`, `stdlib_realloc(p, 96)` still succeeds and returns
a new block instead of NULL.

Root cause: `stdlib/error.hp:428-462`. `stdlib_realloc` calls `cplus_realloc`
directly (line 452); `cplus_realloc` calls `cplus_alloc` directly. The injected
failure state (`stdlib_state.fail_at` / `fail_from`, checked only in
`stdlib_alloc` at `:383-390`) is never consulted. Consequences: (a) the
documented "allocation-failure injection for tests" cannot exercise realloc;
(b) more seriously, the documented *recoverable, failure-atomic* realloc
contract is not implementable this way — under real OOM `cplus_alloc` calls
`cplus_fatal` and terminates the process instead of returning NULL, so no
caller can observe the promised failure and keep its original block.

## E5 — UNEXPECTED_OK: `stdlib_release` accepts blocks it never allocated
**18 cases (`F7/sub10`, ids 2210, 2221, …).** A C+ `malloc(16)` block has
`stdlib_alloc_size(p) == 0` (no stdlib metadata), yet `stdlib_release(p)`
returns normally and increments `stdlib_telemetry_stats.free_count` without a
matching `alloc_count`.

Root cause: `stdlib/error.hp:588-594`. `stdlib_release` calls
`stdlib_meta_forget(ptr)`, which silently ignores unknown pointers
(`:354-368`), then unconditionally bumps `STDLIB_TM_FREE` and calls
`cplus_release`. In C+ mode `malloc` is registered with the runtime side table,
so the runtime accepts the release. There is no ownership check tying a release
to a stdlib allocation, so foreign pointers are absorbed into stdlib ownership
and the telemetry free/alloc accounting is corrupted.

---

## Attack surface that held (2246 REFUSED)

- **Stale generation acceptance (400 cases):** release, realloc (grow/shrink/in
  place), ownership transfer, free+reuse and churn all advance/invalidate the
  token; `stdlib_alloc_check_generation(ptr, stale)` always returned 0.
- **Recycled-address identity confusion (400 cases):** after release, stdlib
  metadata is gone (`size/generation == 0`), the runtime retired ledger still
  reports the address retired, a reused address gets a fresh generation, and a
  stale alias is refused by `stdlib_realloc` / `stdlib_transfer_ownership`.
  The documented F2 limitation (a *live* reused address is indistinguishable
  from a stale alias at pointer level) is not reachable as a false accept here.
- **Metadata cache exhaustion / side-table stress (300 cases):** 200-500 live
  blocks with interleaved releases and rehashes preserved `size`, `elem_size`,
  generation and liveness for every survivor; runtime generations stayed
  monotonic; no hang or lookup failure.
- **Telemetry & races (300 cases):** exact single-thread counter accounting,
  disabled-telemetry inertness, snapshot monotonicity, and 4-thread
  note/alloc/callback churn all completed without crashes or lost counts.
- **Invalid ownership transfer (300 cases):** occupied `dst`, NULL handles,
  `dst == src`, freed source and untracked source were all refused; refused
  transfers never mutated generation or source ownership.
- **Error record (200 cases):** category mapping for every `cplus_exit_code`,
  255-byte truncation + NUL, NULL message/target, and `clear()` all correct.

No `CRASH` verdicts: the runtime's fatal paths (double free 104 / invalid free
105) fired deterministically where expected and were counted as refusals.
