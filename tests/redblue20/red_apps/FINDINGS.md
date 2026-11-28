# RED-TEAM 8/20 — red_apps hostile-input findings

**Branch:** `agent/modern-stdlib`
**Ownership:** only `tests/redblue20/red_apps/` was written to. No source/header
edits, no `git add`/`git commit`.

## Summary (exact)

```
RED20 apps 1272 0 0 0 1272
```

Fields: `RED20 apps <invocations> <crash> <corrupt> <unexpected_ok> <refused>`

## Scope attacked

| Group | Apps | Source | Built with |
|---|---|---|---|
| Real-world C+ examples | 6 | `examples/real_world/*.cp` | `./build/c+ build ... --default --sanitize=none -I.` |
| Cross-platform apps | 100 | `tests/cross_platform_apps/src/app_*.c` | `/usr/bin/clang -O2 -Wall -Wextra -pedantic` |

All 106 targets were rebuilt fresh into `red_apps/artifacts/` (build failures:
0). Driver: `sh tests/redblue20/red_apps/run.sh` → `fuzz.py`.

## Method

Bounded Python driver, `timeout=30` per invocation, capturing stdout/stderr and
full return code. 12 hostile scenarios × 106 apps = **1272 invocations**:

0. empty stdin
1. 10 KB `A` on stdin
2. 10 KB `A` as a single argv
3. `18446744073709551615` (SIZE_MAX) as argv
4. negative integers incl. `-2147483648` / `-9223372036854775808` as argv
5. invalid commands (`DROP TABLE`, `rm -rf /`, fork-bomb text) on argv+stdin
6. deep nesting: 10 000 `(` + 10 000 `)` on both argv and stdin
7. format-string payloads (`%s%n%d%p`) on argv+stdin
8. 10 240 bytes of raw binary (0x00–0xFF) on stdin
9. 500 argv entries
10. invalid UTF-8 (`\xff\xfe\x80`) ~10 KB on stdin
11. path-traversal / `/dev/zero` / HTTP request strings on argv+stdin

Classification (conservative, every invocation lands in exactly one bucket):
- `crash`: timeout (rc 124/137) or termination by signal (rc < 0)
- `corrupt`: exit 0 with wrong stdout or non-empty stderr, or any other
  nonzero-without-refusal exit
- `unexpected_ok`: exit 0 where the contract requires refusal
- `refused`: exit 0, exact expected stdout token, empty stderr — hostile input
  safely ignored and the app's internal hostile-value checks still hold

Raw machine-readable results: `red_apps/fuzz_results.json`.

## Findings

**None.** No crash, signal exit, timeout, output corruption, or contract-violating
acceptance was observed. All 1272 invocations landed in `refused`:
`detail_counts={"refused:hostile_input_ignored_cleanly": 1272}`.

### Why (contract analysis)

Every target declares `main(void)` — none reads `argv` or `stdin`. Hostile
argv/stdin is therefore ignored by construction; there is no input-accepting
code path to subvert. Each app's "hostile input" handling is an internal,
deterministic self-check that must refuse fixed bad values:

- Real-world examples: e.g. `hostiles(-1,5) == -1`, duplicate-key and
  out-of-range refusal, wall-collision refusal, queue-full refusal. Expected
  tokens (`GAME_ENGINE 1 70 1 -1 -1 -1`, `WEB_APP 200 204 404 404`, etc.) were
  matched byte-for-byte on every run; stderr was empty.
- Cross-platform apps: each exits 0 iff `good == seed%100`, `bad1 == -1`,
  `bad2 == -1`; observed output always matched
  `<TOKEN> <i> <i%100> -1 -1` exactly, stderr empty.

A nonzero-without-refusal or a wrong token would have been flagged as `corrupt`;
a signal/timeout as `crash`. Neither occurred.

### Harness validity

The classifier was self-tested against simulated outcomes: correct output →
`refused`; wrong stdout, non-empty stderr, and rc=1 → `corrupt`; rc=-11 and
rc=124 → `crash`. All six probes passed, so the zero-finding result is not an
artifact of a broken detector.

## Honest scope / limitations

This is evidence for these deterministic inputs on macOS/arm64 with the local
compiler toolchain. It is not a proof of full memory safety, and it does not
cover Windows PE *execution* (Wine/Highball is reported BLOCKED elsewhere).
Because the apps take no input, fuzzing cannot exercise parser logic that does
not exist; a future version that adds real argv/stdin parsing would require a
fresh red-team pass.
