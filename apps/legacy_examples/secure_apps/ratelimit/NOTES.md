# Secure-app example: deterministic token-bucket rate limiter

## What this is (and is not)

This is a **teaching example** of checked, defensive C+ coding using the
standard library. It is **NOT a proof of security**, NOT a production rate
limiter, and NOT a substitute for a reviewed, battle-tested enforcement point.
It deliberately uses **no wall clock** — every "time tick" is a fixed integer
on the command line — so its transcript is byte-for-byte reproducible and can
be golden-tested. It demonstrates a small set of defensive habits: hard input
bounds, checked numeric conversion, overflow-checked arithmetic, fixed-size
arrays, and first-class refusal events.

## Source

- `ratelimit.cp` — the limiter.
- `run.sh` — builds it and runs a fixed scenario suite; prints one summary line
  and exits non-zero on any mismatch.

## Input grammar

```
ratelimit <capacity> <refill> <n> [<tick0> <cost0>] ... [<tickN-1> <costN-1>]
```

- `capacity` — bucket size in tokens, must be `1..1000000`.
- `refill` — tokens added per elapsed tick, must be `1..1000000`.
- `n` — number of requests, must be `0..64`.
- Each request is a `(tick, cost)` pair. `tick >= 0` and non-decreasing;
  `1 <= cost <= capacity`.

The bucket starts full at tick 0. Per request: refill by
`(tick - last_tick) * refill` (capped at `capacity`), then `ALLOW` and debit if
the level covers the cost, otherwise `DENY` without debiting.

## Exact build command (from repo root)

```
./build/c+ build examples/secure_apps/ratelimit/ratelimit.cp --strict --sanitize=none -I. -o <bin>
```

Observed: build exits `0`, no diagnostics. (`--sanitize=none` is required by
the task; refusals are intentional, so no sanitizer trap is wanted.)

## Exact run command and output

```
$ timeout 120 sh examples/secure_apps/ratelimit/run.sh
SECURE_RATELIMIT 19 19 0 15
```

Fields: `SECURE_RATELIMIT <scenarios> <passed> <failed> <refused>`.
All 19 scenarios matched their expected exit code and exact stdout; 15 of
those scenarios are intentional refusals (expected passes, not failures).
Each individual binary invocation is itself wrapped in `timeout 30`.

## Representative exact outputs (captured from the built binary)

Valid run with both ALLOW and DENY — `ratelimit 3 1 4 0 3 0 1 1 1 5 1`
(exit `0`):

```
SECURE_RL_CONFIG 3 1 4
SECURE_RL_REQ 0 0 3 3 ALLOW 0
SECURE_RL_REQ 1 0 1 0 DENY 0
SECURE_RL_REQ 2 1 1 1 ALLOW 0
SECURE_RL_REQ 3 5 1 3 ALLOW 2
SECURE_RL_DONE 3 1 2
```

Refusals (each prints one `REFUSED` line and exits with its code):

```
$ ratelimit 0 2 1 0 1        -> REFUSED 3 CAPACITY out of range    (exit 3)
$ ratelimit 5 2 1 -1 1       -> REFUSED 6 TICK negative tick       (exit 6)
$ ratelimit 5 2 1 9223372036854775807 1
                             -> REFUSED 8 OVERFLOW refill multiply (exit 8)
```

## Refusal codes

| Code | Name       | Trigger |
| ---- | ---------- | ------- |
| 1    | ARGS       | wrong argument count / missing or extra tick-cost pairs |
| 2    | PARSE      | a numeric argument failed checked `strtol` parsing |
| 3    | CAPACITY   | capacity <= 0 or > 1000000 |
| 4    | REFILL     | refill <= 0 or > 1000000 |
| 5    | COUNT      | n < 0 or n > 64 |
| 6    | TICK       | negative or non-monotonic tick |
| 7    | COST       | cost <= 0 or > capacity |
| 8    | OVERFLOW   | overflow in `elapsed * refill` or `tokens + added` |

A refusal is emitted **before any transcript line**: output is either a
complete `SECURE_RL_CONFIG ... SECURE_RL_DONE` transcript or exactly one
`REFUSED` line, never a partial transcript followed by a refusal.

## Threat model

**In scope (what the design defends against):**

- Malformed / hostile argv: empty strings, trailing junk, out-of-range numbers,
  wrong arity. Checked `strtol` (errno + end-pointer checks) rejects them.
- Resource exhaustion via input size: `n` is capped at 64 and all state lives
  in fixed-size arrays (`RL_MAX_REQUESTS`); there is no allocation or unbounded
  growth driven by argv.
- Arithmetic overflow / wraparound in the core model: `__builtin_mul_overflow`
  and `__builtin_add_overflow` guard the two arithmetic operations, so a
  crafted huge tick cannot wrap the bucket level into a wrong ALLOW decision.
- Inconsistent time: negative or backwards ticks are refused, so a caller
  cannot "rewind" the bucket to mint tokens.
- Impossible requests: a cost larger than capacity can never be served, so it
  is refused rather than silently treated as DENY.
- Strict-mode pointer-safety: argv reads go through one `__builtin_memcpy`
  helper because strict mode cannot prove the extent of a direct subscript.

**Out of scope (not defended here):**

- No wall clock: ticks come from argv, so this is a deterministic *model* of a
  limiter, not a live enforcement mechanism. Nothing calls this from a server.
- No concurrency, no atomics, no shared mutable state — multi-thread or
  multi-process enforcement is not addressed.
- No authentication, authorization, identity, or per-client keying: every
  request in the sequence is treated identically.
- No persistent state across runs; each invocation is independent.
- No resistance to a malicious *host* compiler/runtime, side channels, denial
  of service by repeatedly invoking the process, or clock manipulation (there
  is no clock).
- Return-value/exit-status handling by a caller is the integrator's job; this
  program only reports, it does not enforce anything beyond its own process.

## Verification performed

1. Built with the exact command above (`--strict --sanitize=none`), rc 0.
2. Ran `timeout 120 sh examples/secure_apps/ratelimit/run.sh` -> `SECURE_RATELIMIT 19 19 0 15`, rc 0.
3. Individually exercised every refusal code (1-8) and both ALLOW and DENY
   paths, plus both overflow sub-paths ("refill multiply", "bucket add"), and
   the maximum accepted boundary values (capacity/refill = 1000000).
4. `check_det` re-runs the basic scenario and requires a byte-identical
   transcript, asserting determinism.

No result above is fabricated; every transcript and exit code was observed
from the compiled binary. This evidence shows the example behaves as designed
for the tested inputs — it is not a security guarantee.
