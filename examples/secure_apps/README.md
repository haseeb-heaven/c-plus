# Secure apps

Four small, deterministic apps that make checked-stdlib **refusals** observable
at runtime.  Three are written in C+ on top of `stdlib/string.hp` and
`stdlib/error.hp`; the fourth is a C99 app on the additive
`cplus_violation_log_*` API from `libc/src/runtime/violation_log.c` (linked
against `build/libc/libcplus_hardened.a`).

> **These are headless teaching apps, not production security software.** Each
> one is a fully deterministic model: fixed `argv`/`stdin` transcripts, no wall
> clock, no randomness, no interactive prompt, no real credentials, no network
> listener and no real firewall enforcement.  A name like "vault", "firewall",
> "rate limiter" or "audit log" describes the *model* it demonstrates, not a
> claim to be a secure product.  The deliverable is runtime **refusal
> evidence** — not a proof of 100% safety or freedom from all invalid states.
> See [Non-claims](#non-claims).

## Run all groups

```sh
sh examples/secure_apps/run_all.sh
```

The aggregate runner discovers `<group>/run.sh` at depth 2 below this
directory, runs each from the repository root under `timeout -k 10 300` with a
restricted `PATH` and `CPLUS_CLANG=/usr/bin/clang`, captures the **last** final
report line from each group, prints a table, and exits non-zero on any real
failure.  It continues after failures and never masks a failure with a success
override.  Per-group logs land in `build/secure_apps/<group>.log`; the verbatim
final lines are saved to `build/secure_apps/final_lines.txt`.

### Final-line contract (for group authors)

Each `run.sh` must print, as its last matching line, exactly one line beginning
with its token:

| Runner | Token |
| --- | --- |
| `vault/run.sh` | `SECURE_VAULT` |
| `firewall/run.sh` | `SECURE_FIREWALL` |
| `ratelimit/run.sh` | `SECURE_RATELIMIT` |
| `auditlog/run.sh` | `SECURE_AUDIT` |

Recommended shape (the positional form these runners emit):

```
TOKEN <scenarios> <passed> <failed> <refused>
```

The aggregate parser also accepts `TOKEN total=<n> pass=<n> fail=<n>
[blocked=<n>]` and a bare status word.  A third positional count greater than
zero, `fail=<n>` with `n > 0`, the words `FAIL`, `MISMATCH`, `ERROR`,
`BUILD_FAIL`, and a non-zero exit status are real failures.  `BLOCKED` /
`blocked=<n>` is reported but is not a hard failure, matching the repository
evidence convention.  Group-specific threat models belong in that group's
`NOTES.md`.

## Common build convention

Build from the repository root with the supported `./build/c+` driver in strict
mode, except `auditlog` which is a C99 translation unit compiled directly with
`/usr/bin/clang` against the hardened libc archive.

## Catalogue

### Credential vault — `SECURE_VAULT` (`vault/run.sh`)

| | |
| --- | --- |
| Source | `examples/secure_apps/vault/vault.cp` |
| One-line purpose | In-memory credential vault whose safety comes from typed refusals: store/get/list/peek/delete over fixed-capacity tables with a bounded audit buffer and a 64-byte canary. |
| APIs | `stdlib/string.hp` (`string::len`, `string::substr` into fixed-size storage, measured lengths); `stdlib/error.hp` (`error *`, `error_fail_cat`, `error_reset`, `stdlib_set_error_callback(NULL)` so no secret-bearing callback exists); `__builtin_memcpy` for `argv` slots under `--strict`. |
| Safety refusals | `duplicate-service`, `empty-name`, `empty-secret`, `oversized-name`, `oversized-secret`, `missing-service`, `index-out-of-range`, `vault-full`, `secret-logging-forbidden` (`debug` command), `audit-overflow` (17th append refused, buffer unchanged), `bad-args`, `unknown-command`, `usage` — each prints `VAULT refuse <reason> code <n> cat <name>`. Secrets are emitted only by `get`; the canary is checked on every exit. |
| Build | `./build/c+ build examples/secure_apps/vault/vault.cp --strict --sanitize=none -I. -o examples/secure_apps/vault/vault` |

### Input-validation firewall — `SECURE_FIREWALL` (`firewall/run.sh`)

| | |
| --- | --- |
| Source | `examples/secure_apps/firewall/firewall.cp` |
| One-line purpose | Deterministic allow-list model that contrasts what an unchecked `strcpy` would do with what the checked stdlib layer does for four token policies (`token`, `pathseg`, `sqlsafe`, `text`). |
| APIs | `stdlib/string.hp` (checked length/copy, bounded scans); `stdlib/error.hp` (typed `error *` refusals with stable code/category); fixed compile-time capacities (`FW_SCAN_CAP` 512, `FW_MAX_LEN` 256, `FW_NAIVE_BUF` 32). |
| Safety refusals | Overlong tokens, path-traversal segments, SQL-injection-shaped tokens, embedded/encoded NUL markers (`%00`, `\0`, `\x00`) and control-byte input. Each prints a `FIREWALL verdict=... rule=... code=... errcode=...` line; a `CONTRAST ...` line shows the real consequence on the unchecked path. |
| Build | `./build/c+ build examples/secure_apps/firewall/firewall.cp --strict --sanitize=none -I. -o examples/secure_apps/firewall/firewall` |

### Token-bucket rate limiter — `SECURE_RATELIMIT` (`ratelimit/run.sh`)

| | |
| --- | --- |
| Source | `examples/secure_apps/ratelimit/ratelimit.cp` |
| One-line purpose | Deterministic bounded token bucket: supply capacity, refill and a fixed list of tick/cost pairs; every request is ALLOW/DENY plus a first-class refusal event. |
| APIs | C+ checked scalar parsing helpers over `cplus/cplus.h` (no wall clock — ticks are argv integers); bounded arrays and bounded loops (`RL_MAX_REQUESTS` 64, capacity/refill <= 1,000,000); explicit refusal codes returned as the process exit status. |
| Safety refusals | `RL_ERR_ARGS` (wrong arity), `RL_ERR_PARSE`, `RL_ERR_CAPACITY`, `RL_ERR_REFILL`, `RL_ERR_COUNT`, `RL_ERR_TICK` (negative or decreasing), `RL_ERR_COST`, `RL_ERR_OVERFLOW`. Each prints `REFUSED <code> <NAME> <detail>` and exits with that code. |
| Build | `./build/c+ build examples/secure_apps/ratelimit/ratelimit.cp --strict --sanitize=none -I. -o examples/secure_apps/ratelimit/ratelimit` |

### Audit log — `SECURE_AUDIT` (`auditlog/run.sh`)

| | |
| --- | --- |
| Source | `examples/secure_apps/auditlog/auditlog.c` |
| One-line purpose | Exercise the additive `cplus_violation_log_*` API with synthetic events (code/kind/pointer/size): open, append, re-open, read back, and refuse hostile inputs. |
| APIs | `cplus_violation_log.h` / `libc/src/runtime/violation_log.c`: `cplus_violation_log_open`, `_event`, `_count`, `_close` (safe no-op when already closed); linked with `build/libc/libcplus_hardened.a`; compiled with `-std=c99 -Wall -Wextra -pedantic -pthread -Ilibc/install-include`. |
| Safety refusals | `event_before_open`, `event_after_close` (real `EINVAL`/`EBADF` from the API), `open_null_path`, `open_empty_path` (real `EINVAL`), `event_null_kind`, `event_empty_kind` (real `EINVAL`), and `event_oversized_kind` — an **application-policy** refusal because the backend silently truncates `kind` at 96 bytes (`VIO_KIND_CAP`), which would corrupt an audit record. Seven refusals total. |
| Build | `/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread -Ilibc/install-include examples/secure_apps/auditlog/auditlog.c build/libc/libcplus_hardened.a -o <bin>` |

## Non-claims

- This directory does **not** claim Rust equivalence, a 100%-safe application,
  or a deployable security product. The value is the reproducible refusal
  transcript and the explicit contrast between unchecked and checked paths.
- Results are recorded for **macOS arm64 / Apple clang 16.0.0 (`/usr/bin/clang`)
  only**. No other platform or toolchain is claimed to behave identically.
- A clean build or a local test run is not evidence that the model resists a
  real attacker; the inputs, pointers and credentials are synthetic and the
  workloads are bounded by compile-time constants.
- See [RESULTS.md](RESULTS.md) for the exact final table, toolchain, totals and
  reproduction commands from a real run.
