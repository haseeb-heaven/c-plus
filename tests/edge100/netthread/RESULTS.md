# EDGE100 netthread -- results

Agent: EDGE agent 4/10 (`agent/modern-stdlib`).
Scope: edge-case coverage of every public API in `stdlib/net.hp`,
`stdlib/http.hp`, `stdlib/thread.hp`, `stdlib/async.hp` and `stdlib/time.hp`.
No source or header file was edited; only this directory was written.

```
EDGE100 netthread 81 169 169 0
```

| Field | Value |
| --- | --- |
| APIs covered | 81 |
| Cases executed | 169 |
| Passed | 169 |
| Failed | 0 |

## How to reproduce

From the repository root:

```sh
sh tests/edge100/netthread/run.sh
```

`run.sh` builds `harness.cp` with the required flags
(`-lcurl -I/opt/homebrew/include -L/opt/homebrew/lib -luv`, plus the repo
include path), writes the local `file://` fixture under `out/`, and runs the
campaign under `timeout 60`. Each case is executed in a freshly forked child
with a 3-second `alarm()` watchdog; the parent uses `poll()` plus a bounded
`waitpid()`. A case counts as passed only when the child exits zero and writes
a `PASS` verdict; signal death, a short/missing result, a hang or a `FAIL`
verdict counts as failed.

The last verified run (with the stable `c+` fallback, because the repo wrapper
was mid-rebuild) produced:

```
note: 'build/c+' failed; retrying with installed c+
EDGE100 netthread 81 169 169 0
SCENARIOS_SHA256 a48aebfb8ef02973c91e8ff0817b67336bbf087ff191fae7bdd451072579d255
```

The per-case trace is `out/scenarios.log` (digest in
`out/scenarios.log.sha256`); build diagnostics are in `out/build.log`.

## Notes on contract interpretation

These were confirmed against the headers, not assumed:

- `error_reset` / `error_fail_cat` are NULL-safe, so passing `error *err = NULL`
  is a valid edge case for every error-taking API.
- An explicit `.drop()` in C+ lowers to `drop()` followed by `cplus_zero()`, so
  post-drop field reads (e.g. `descriptor() == 0`) are a compiler artifact.
  Ownership assertions therefore check observable effects (fd closed, no
  double-close, `is_valid() == false`), not raw post-drop field values.
- `http_client::set_timeout` delegates to `curl_easy_setopt(CURLOPT_TIMEOUT)`.
  This libcurl build accepts at most `2147483` seconds (signed-int ms limit);
  larger values are a delegated `STDLIB_CATEGORY_INVALID` refusal, which the
  boundary case asserts rather than treating as a defect.
- "Stale generation" is exercised as use-after-drop, borrowed-fd close/reuse,
  thread-handle reuse after join, and client use after cleanup. "Epoch
  boundaries" are exercised as Unix-epoch rollover values in
  `time_format_iso8601` plus port/buffer/size extremes elsewhere.

## Findings

No contract violations were observed across the 169 cases. All verdicts were
`PASS`; there were no crashes, hangs or short results.
