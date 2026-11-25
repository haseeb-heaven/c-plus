# Real-world utilities — aggregate results

Recorded from a **real run** of `sh apps/legacy_examples/utilities/run_all.sh` from the
repository root on the toolchain below. This is a snapshot, not a promise: no
result here is inferred from a build alone, and a category that had not landed
would be marked **PENDING** with its exact reproduction command. Nothing in
this file is fabricated.

## Environment / toolchain

| Item | Value |
| --- | --- |
| Snapshot (start / end UTC) | `2026-11-23T06:33:36Z` / `2026-11-23T06:34:11Z` |
| Host | Darwin arm64, macOS 27.2.1 (Darwin Kernel 27.2.0, `RELEASE_ARM64_T8112`) |
| Backend clang (pinned) | `Apple clang version 16.0.0 (clang-1600.0.26.6)`, `Target: arm64-apple-darwin27.2.0`, `/usr/bin/clang` |
| C+ driver | `C+ 0.1.0` / `Language: C+ (C with memory safety, deterministic RAII, lightweight classes)` / `Backend: host Clang/LLVM` |
| Rust (not invoked) | `rustc 1.100.0 (0ef1bc386 2026-11-09)` is present for the runner's tool-directory discovery loop; **no utility invokes it** |
| Runner | `/usr/bin/timeout` with `-k 10 300` |
| Child PATH | `/usr/bin:/bin:/usr/sbin:/sbin` plus only the directories that actually provide `c+`, `rustc`, `perl`, `awk` → `/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin` |
| `CPLUS_CLANG` | `/usr/bin/clang` (Homebrew clang deliberately kept off the child PATH) |
| `LC_ALL` | `C` |
| Build mode used by every category | `--strict --sanitize=none -I.` (security tools additionally link the archived checked libraries) |

Verification performed before/around the snapshot:
`sh -n apps/legacy_examples/utilities/run_all.sh` → `sh -n: OK`. Two consecutive real runs
produced **byte-identical stdout and byte-identical (empty) stderr**; the
aggregate process exited `0` both times. The aggregate uses no Python and no
success-override (`|| true`) masking, and continues after failures.

## Exact final table (verbatim stdout)

```
utilities aggregate: timeout=300s (kill after 10s), PATH=/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin, CPLUS_CLANG=/usr/bin/clang

CATEGORY         TOKEN          EXIT  STATUS       FINAL LINE (verbatim)
--------         -----          ----  ------       ------------------
data_tools       UTIL_DATA      0     PASS         UTIL_DATA 5 5 0 20
dev_tools        UTIL_DEV       0     PASS         UTIL_DEV 5 18 0 16
file_tools       UTIL_FILE      0     PASS         UTIL_FILE 5 27 0 17
net_tools        UTIL_NET       0     PASS         UTIL_NET 5 5 0 55
security_tools   UTIL_SECURITY  0     PASS         UTIL_SECURITY 5 5 0 12
text_tools       UTIL_TEXT      0     PASS         UTIL_TEXT 5 5 0 26

util-agg: runners=6 reported=6 pass=6 blocked=0 fail=0 hard_fail=0
util-agg: expected tokens: UTIL_FILE UTIL_TEXT UTIL_DATA UTIL_SECURITY UTIL_DEV UTIL_NET
util-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/utilities/<category>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/utilities/final_lines.txt
```

Exact stderr from the same run: **empty** (zero bytes).

Exact final report lines captured verbatim (also saved in
`build/utilities/final_lines.txt`):

```
UTIL_DATA 5 5 0 20
UTIL_DEV 5 18 0 16
UTIL_FILE 5 27 0 17
UTIL_NET 5 5 0 55
UTIL_SECURITY 5 5 0 12
UTIL_TEXT 5 5 0 26
```

Field meaning: `<tools> <passed> <failed> [refused]`. Field 2 is 5 for every
category; field 3 counts passed units (whole tools in `data_tools`,
`net_tools`, `security_tools`; individual checks in `dev_tools`, `file_tools`,
`text_tools`); field 4 is the failure count (all zero here); field 5 is the
informational count of refused hostile inputs.

## Totals and interpretation

| Metric | Value |
| --- | --- |
| Utilities catalogued | 30 (5 in each of 6 categories) |
| Runners discovered | 6 |
| Runners that emitted a final line | 6 |
| PASS | 6 |
| BLOCKED (reported, not a hard failure) | 0 |
| Real FAIL / FAIL_EXIT / NO_REPORT / BAD_TOKEN | 0 |
| Expected tokens | 6 |
| Tokens reported | 6 of 6 |
| PENDING tokens | 0 |
| Passed units (sum of field 3) | 65 |
| Refused hostile inputs (sum of field 5) | 146 |
| `hard_fail` | 0 |
| Aggregate process exit | **0** |

The high refusal totals are the expected result of the demos, not failures:
each hostile case is asserted to produce a specific stable refusal code, and
the per-category `run.sh` checks exit status, exact stdout and empty stderr.
The dev-tools `UTIL_DEV 5 18 0 16` and file-tools `UTIL_FILE 5 27 0 17` rows
count individual checks rather than whole tools; see those categories'
`NOTES.md` for the case-by-case transcript.

## Reproduction commands

From the repository root:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib

sh -n apps/legacy_examples/utilities/run_all.sh     # syntax check
sh apps/legacy_examples/utilities/run_all.sh        # full aggregate; exit 0 expected
```

Run a single category directly (exact command per token):

| Token | Category | Exact command |
| --- | --- | --- |
| `UTIL_FILE` | `file_tools` | `sh apps/legacy_examples/utilities/file_tools/run.sh` |
| `UTIL_TEXT` | `text_tools` | `sh apps/legacy_examples/utilities/text_tools/run.sh` |
| `UTIL_DATA` | `data_tools` | `sh apps/legacy_examples/utilities/data_tools/run.sh` |
| `UTIL_SECURITY` | `security_tools` | `sh apps/legacy_examples/utilities/security_tools/run.sh` |
| `UTIL_DEV` | `dev_tools` | `sh apps/legacy_examples/utilities/dev_tools/run.sh` |
| `UTIL_NET` | `net_tools` | `sh apps/legacy_examples/utilities/net_tools/run.sh` |

Each category owns its own build commands and per-case evidence in its `NOTES.md`;
the aggregate does not rebuild examples itself. Generated artifacts from the
snapshot are at `build/utilities/<category>.log` (full runner output),
`build/utilities/final_lines.txt` (verbatim final lines),
`build/utilities/aggregate.stdout` and `build/utilities/aggregate.stderr`.

## PENDING categories: none at snapshot time

All six expected tokens were reported by a landed `run.sh`, so no row was
fabricated and no `PENDING` row was needed. The integrity rule is still active:
if a `<category>/run.sh` is removed or fails to print its token, the aggregate
prints `(missing:<token>) ... PENDING`, writes
`util-agg: PENDING <token> - rerun after that category's run.sh lands` to
stderr, and exits 1. Recovery command for any such token:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh apps/legacy_examples/utilities/<category>/run.sh   # use the table above for <category>
sh apps/legacy_examples/utilities/run_all.sh          # refresh the whole table
```

## Screenshot / captured-evidence pointer

Separate from the 30 utilities, a safety-demo campaign captured real terminal
transcripts and rendered SVG evidence under [`apps/legacy_examples/screenshots/`](../screenshots/):

- Manifest with reproduction commands and exact observed lines:
  [`apps/legacy_examples/screenshots/MANIFEST.md`](../screenshots/MANIFEST.md).
- Captured terminal transcripts (verbatim stdout+stderr of real binaries under
  `/usr/bin/timeout 30`):
  `apps/legacy_examples/screenshots/captures/unsafe_vs_safe_memcpy.txt`,
  `bounds_query.txt`, `lifetime_refusal.txt`, `sync_double_destroy.txt`,
  `database_safety_refusals.txt`, `stdc_hostile_refusals.txt`.
- Rendered SVGs: `apps/legacy_examples/screenshots/images/unsafe_vs_safe_memcpy.svg`,
  `bounds_query.svg`, `lifetime_refusal.svg`, `sync_double_destroy.svg`.
- Harnesses: `apps/legacy_examples/screenshots/capture.sh` and
  `apps/legacy_examples/screenshots/images/render.sh`.

That campaign's last full run was recorded as
`2026-11-23T06:28:17Z`, exit `0`,
`CAPTURE_SUMMARY files=6 builds=6 runs=7 failures=0`, with every transcript
ending in `VERDICT: PASS`. These transcripts document the checked-stdlib
safety demos, **not** the 30 utilities in this catalogue; the utilities' own
per-case observed output is in each category's `NOTES.md` and in
`build/utilities/<category>.log`.

## Explicit non-claims

- **No Rust-equivalence claim.** `rustc` is present and discovered for the
  PATH loop, but no utility or aggregate step invokes it. Nothing here claims
  C+ and Rust are equivalent languages or have equivalent safety guarantees.
- **No 100% safety claim.** Every `REFUSED` line is runtime evidence for the
  exact tested input under `--sanitize=none`, not a formal proof of memory,
  lifetime or thread safety. The checked layers do not establish pointer
  provenance or full ownership and can be bypassed by callers that do not use
  them. High refusal counts are not a safety proof.
- **macOS arm64 only.** Evidence was produced on Darwin arm64 with Apple clang
  16.0.0. No other OS or architecture is claimed; a build alone is never
  presented as a cross-platform result.
- **Not production software.** These are practical teaching utilities with
  fixed caps and fixed fixtures. Parser names (`jsonfmt`, `uriparse`,
  `urlcheck`), security names (`checksum`, `pwdcheck`) and network names do not
  imply production parsers, cryptographic hashes, authentication or live
  network clients.
- **A passing build is not a result.** Only the observed final lines and
  per-case transcripts above are claimed, for the fixed inputs tested.
- **PENDING is not a pass.** A category without a landed runner is recorded as
  PENDING with its exact reproduction command, never with an assumed result.
