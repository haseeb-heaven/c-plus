# Secure apps — aggregate results

Recorded from a **real run** of `sh apps/legacy_examples/secure_apps/run_all.sh` from the
repository root on the toolchain below. This is a snapshot, not a promise: any
group that had not landed at snapshot time would be marked **PENDING** with its
exact reproduction command. Nothing in this file is fabricated or inferred from
a build alone.

## Environment / toolchain

| Item | Value |
| --- | --- |
| Snapshot time (UTC) | `2026-11-23T06:33:37Z` |
| Host | Darwin arm64, macOS 27.2.1 |
| Backend clang (pinned) | `Apple clang version 16.0.0 (clang-1600.0.26.6)`, `Target: arm64-apple-darwin27.2.0`, `/usr/bin/clang` |
| C+ driver | `C+ 0.1.0` / `Language: C+ (C with memory safety, deterministic RAII, lightweight classes)` |
| Rust (context only; not used here) | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| Runner | `/usr/bin/timeout` with `-k 10 300` |
| Child PATH | `/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin` (only tool-providing directories added back) |
| `CPLUS_CLANG` | `/usr/bin/clang` (Homebrew clang deliberately kept off the child PATH) |
| Hardened libc archive | `build/libc/libcplus_hardened.a` (used by `auditlog`) |

Verification performed before the run: `sh -n apps/legacy_examples/secure_apps/run_all.sh`
→ `sh -n: OK`. The aggregate runner uses no Python and no success-override
(`|| true`) masking, and continues after failures.

## Exact final table (verbatim stdout)

```
secure-apps aggregate: timeout=300s (kill after 10s), PATH=/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin, CPLUS_CLANG=/usr/bin/clang

GROUP            TOKEN              EXIT  STATUS       FINAL LINE (verbatim)
-----            -----              ----  ------       ------------------
auditlog         SECURE_AUDIT       0     PASS         SECURE_AUDIT 27 27 0 7
firewall         SECURE_FIREWALL    0     PASS         SECURE_FIREWALL 19 8 0 11
ratelimit        SECURE_RATELIMIT   0     PASS         SECURE_RATELIMIT 19 19 0 15
vault            SECURE_VAULT       0     PASS         SECURE_VAULT 12 12 0 15

secure-agg: runners=4 reported=4 pass=4 blocked=0 fail=0 hard_fail=0
secure-agg: expected tokens: SECURE_VAULT SECURE_FIREWALL SECURE_RATELIMIT SECURE_AUDIT
secure-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/secure_apps/<group>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/secure_apps/final_lines.txt
```

Exact final report lines captured verbatim (also saved in
`build/secure_apps/final_lines.txt`):

```
SECURE_AUDIT 27 27 0 7
SECURE_FIREWALL 19 8 0 11
SECURE_RATELIMIT 19 19 0 15
SECURE_VAULT 12 12 0 15
```

Exact stderr from the same run: **(empty)** — no `BLOCKED`, `PENDING`, or
failure diagnostics were emitted.

## Totals and interpretation

| Metric | Value |
| --- | --- |
| Runners discovered | 4 |
| Runners that emitted a final line | 4 |
| PASS | 4 |
| BLOCKED (reported, not a hard failure) | 0 |
| Real FAIL / FAIL_EXIT / NO_REPORT / BAD_TOKEN | 0 |
| Expected tokens | 4 |
| Tokens reported | 4 of 4 |
| PENDING tokens | 0 |
| `hard_fail` | 0 |
| Aggregate process exit | **0** |

The final-line positional contract is
`TOKEN <scenarios> <passed> <failed> <refused>`. The third field (`failed`) is
zero for every group. For `SECURE_FIREWALL`, `passed=8` counts accepted verdicts
and `refused=11` counts correctly refused hostile tokens, summing to the
`scenarios=19` total; both are expected outcomes, not failures. The aggregate
classifies a row as a failure only on a non-zero exit, a missing/unrecognised
final line, or a nonzero `failed`/`fail=` signal.

Per-group detail (transcripts, build logs) is kept by each group under its own
directory; the aggregate only records the verbatim final line and exit status.

## Reproduction commands

From the repository root:

```sh
# Syntax check first
sh -n apps/legacy_examples/secure_apps/run_all.sh

# Full aggregate run (the exact command recorded above)
sh apps/legacy_examples/secure_apps/run_all.sh

# Individual groups, if a single row needs re-checking
sh apps/legacy_examples/secure_apps/vault/run.sh
sh apps/legacy_examples/secure_apps/firewall/run.sh
sh apps/legacy_examples/secure_apps/ratelimit/run.sh
sh apps/legacy_examples/secure_apps/auditlog/run.sh

# Raw per-runner logs and captured final lines
cat build/secure_apps/final_lines.txt
```

No group was PENDING at snapshot time, so there are no deferred exact commands.
If a group is later removed or its `run.sh` stops landing, the aggregate will
print a `PENDING` row with `PENDING - no runner reported <TOKEN>` and exit 1;
re-run the corresponding `sh apps/legacy_examples/secure_apps/<group>/run.sh` after it
lands.

## Non-claims

- No Rust equivalence is claimed. Rust is not used by these apps; its version
  is recorded only as context for the repository's comparison campaigns.
- No claim of 100% safety, production security, or resistance to a real
  attacker is made. These are bounded deterministic models of a vault,
  firewall, rate limiter and audit log; the credentials, pointers and inputs
  are synthetic and all workloads are bounded by compile-time constants.
- Results apply to **macOS arm64 / Apple clang 16.0.0 (`/usr/bin/clang`) only**.
  SDL3 3.4.18 is the version on this verification host and is referenced by
  the companion `apps/legacy_examples/modern_gui` tree; none of these four secure apps uses
  SDL3. No other platform, toolchain or SDL3 version is claimed to behave
  identically.
- A clean build or local run is not live-deployment success and does not
  substitute for an independent security review of a real implementation.
