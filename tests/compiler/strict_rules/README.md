# Strict-safe subset conformance suite (spec v0.1)

Permanent, self-contained suite for
`docs/compiler/strict_safe_subset.md`. It owns only files in this directory
plus the generated matrix `docs/compiler/strict_rules_matrix.json`.

## Layout

- `rules.json` — source of truth: rule IDs, mechanisms, fixture expectations.
- `fixtures/pos_*.c` / `neg_*.c` — one positive and one negative fixture per rule.
- `run_strict_rules.py` — runner; hard timeouts, exact exit-status and diagnostic
  substring assertions, no skips, no `|| true`.
- `results/summary.json`, `results/summary.txt` — full execution evidence.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/strict_rules/run_strict_rules.py
```

Exit code is 0 only when every fixture met its assertion. Timeout
(`CPLUS_STRICT_TIMEOUT`, default 45 s) is always a FAIL. `check` rules use
`check --strict --sanitize=none`; `BOUNDS-DYNAMIC-RUNTIME` builds and executes
the binary to exercise the runtime guard.

The runner generates `docs/compiler/strict_rules_matrix.json`: `status` is
`VERIFIED` only for rules whose two fixtures were actually executed and
passed. `false_positives` and `false_negatives` are recorded separately with
concrete fixture names, reasons and observed exit codes.

## Adversarial addition (per-rule mutations / near-misses)

The base suite above is unchanged. A compact, table-driven addition covers a
representative five-rule subset (`ARITH-OVERFLOW`, `BOUNDS-FIXED`,
`LIFETIME-UAF`, `LIFETIME-DOUBLE-FREE` / `OWNER-DOUBLE-FREE`, `THREAD-RACE`):

- `adversarial.json` — one MUTATION (a cast/alias/equivalent-form bypass that
  must be rejected with an exact diagnostic substring) and one NEAR-MISS (a
  legal boundary form that must be accepted) per rule.
- `fixtures/mut_*.c`, `fixtures/near_*.c` — the ten extra fixtures.
- `run_adversarial.py` — reuses the base runner's execution/assertion
  conventions; writes only `results/summary_adversarial.json` and
  `results/summary_adversarial.txt`.

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/strict_rules/run_adversarial.py
```

Measured with the existing `build/c+`: 5 rules covered, 10/10 cases PASS
(5 mutations rc=1, 5 near-misses rc=0), 0 FAIL, 0 timeouts, 0 false positives,
0 false negatives. The exact commands, exit codes and counts are recorded in
`results/summary_adversarial.txt` / `.json`.
