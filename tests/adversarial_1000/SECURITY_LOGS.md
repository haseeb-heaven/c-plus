# ADV1000 — security-log audit

Audit of the logging evidence produced by the eight adversarial groups present
at the time of the aggregate run recorded in `RESULTS.md`. All digests below
were recomputed from the files on disk **after** that real run. A mismatch is
reported, never silently repaired; no pass is fabricated.

## 1. What was audited

| Group | Canonical scenario log | Lines | Digest (SHA-256) | Digest record |
|---|---|---:|---|---|
| alloc | `alloc/results/scenarios.log` | 1001 (1000 `S` + summary) | `bb3016c7e5fef98446796dfeb946d247dd43a0d97619942cdacf460b3f51a02d` | `scenarios.log.sha256` — matches |
| containers | `containers/results/scenarios.log` | 1000 `S` lines | `4683e88875c126e44e5abe6a28973c751990d995f7e8aefbbf9557bd747dfab3` | `digest.txt` — matches |
| containers (stdout mirror) | `containers/results/raw.log` | 1001 (1000 `S` + summary) | `ddcfc6aea68f153eb0fcd3dc910593f97720b741829ac329a8625bf11630a859` | digest.txt refers to `scenarios.log`, not this file — see §4 |
| fmt_io | `fmt_io/results/scenarios.log` | 1001 (1000 `S` + summary) | `b366a11e13cb32f495e8387658ae591f88d492fc4f876f9ee0ae23190f0141b3` | `scenarios.log.sha256` — matches |
| json_log (canonical stdout) | `json_log/results/scenarios.out` | 1001 (1000 `S` + summary) | `4bb4bb7b8d1e478cf6d1ac5ad15e209cd37dafbae03f39620c785a6357647927` | `digests.txt` — matches; `scenarios2.out` has the **same** digest |
| json_log (sink log, run 1) | `json_log/results/scenarios.log` | 1000 | `e327f4df53d25389fd178a5c2417315d158dd5affd47b723de740e84f5afeadf` | `digests.txt` — matches |
| json_log (sink log, run 2) | `json_log/results/scenarios2.log` | 1000 | `d268e5504ddbf7cf346ad697ca9b3577ff87fc071155af265983e81c462c7e51` | `digests.txt` — matches; differs from run 1 only by wall-clock timestamp — see §4 |
| memstr | `memstr/results/scenarios.log` | 1000 `S` lines | `0059a6ab270ceada106792c2876bc8c58fff3e519c622fcaae16a2fc7c2fbe23` | `scenarios.sha256` — matches |
| memstr (stdout mirror) | `memstr/results/stdout.log` | 1001 | `2fb77bd747b7570f7f24cdeb22ecb503bf65f39632eaa9de76e21040ed0625e2` | recomputed here |
| multimedia | `multimedia/results/scenarios.log` | 1000 `S` lines | `147032458a1c26e153f100309c05fc6a58670c6ad8bdffc13ebf32c18a2251f2` | `scenarios.sha256` — matches |
| multimedia (stdout mirror) | `multimedia/results/scenarios.out` | 1001 | `0c725a8587634bbd3b03c8ec6d6dd499dc56388687e7ee32ac91da7b1b08d68b` | recomputed here |
| net_http | `net_http/results/scenarios.log` | 1000 `S` lines | `df3bb4da9a2e7ea7f18c754449cfa89b77acf919ca62ef9640511c693c9c7bd9` | `digest.sha256` — matches |
| net_http (stdout mirror) | `net_http/results/stdout.log` | 1002 (summary + `ADV1000_DIGEST c0283473327a64ae`) | `b0b552ccd280687c0002236591efb9a0594615455dc804aabd27d12a5c5e0a3b` | recomputed here |
| thread_async | `thread_async/results/scenarios.log` | 1003 (1000 `S` + `ADV1000_DISTINCT` + `ADV1000_DIGEST ffe4b39f52a0f9e5` + summary) | `e1e08f0cba16e74ab3e52c772fec0fd406ec2c2cc85dc853f58ed24c0f72cfbe` | `scenarios.sha256` — matches |

All digest values above were produced with `shasum -a 256`. Where a group's
digest record names `results/scenarios.log` and that file holds 1000 `S` lines,
the 1001-line stdout mirror is a *different* artifact and has a different,
independently recomputed digest; this is a scope distinction, not a mismatch.

## 2. Per-group log format

| Group | Scenario line format (verbatim shape) | Examples / notes |
|---|---|---|
| alloc | `S <id> <api> <case> <rc> <canary_ok> <state_ok>` | `S 1 malloc_s overflow_size_max 22 1 1`; id `1..1000`; `<rc>` is the observed errno/return. |
| containers | `S <id> <api> <case> <result> <canary_ok>` | `S 0 vec.at last pass 1`; id `0..999`; `result ∈ {pass, refuse, FAIL}`. |
| fmt_io | `S <id> <api> <case> <result> <canary_ok>` | `S 1 snprintf_s TRUNC TRUNC 1`; id `1..1000`. |
| json_log | `S <id> <api> <case> <result> <sentinel_ok>` in canonical stdout; sink log prefixes an RFC3339 timestamp and `INFO` level: `<ts> INFO  S ...` | `S 0 json ctor_null refused 1` / `2026-11-23T05:52:02Z INFO  S 0 json ctor_null refused 1`. |
| memstr | `S <id> <api> <case> <rc> <canary_ok>` | `S 1 cplus_sc_memcpy_s valid 0 1`. |
| multimedia | `S <id> <api> <case> <result> <sentinel_ok>` | `S 0 window zero_dims refused 1`; id `0..999`; literal `sentinel_ok` token in the summary. |
| net_http | `S <id zero-padded> <api> <case> <result> <sentinel_ok>` | `S 0000 http_sink_write zero_cap_000 REFUSED 1`; id `0000..0999`. |
| thread_async | `S <id> <api> <case> <result> <state_ok>` | `S 1 lifetime valid_rw defined 1`; plus `ADV1000_DIGEST` / `ADV1000_DISTINCT` trailer lines. |

Every group terminates with one canonical aggregation line beginning with
`ADV1000`; the aggregate runner extracts the last well-formed such occurrence
(stripping a `final=` prefix where a runner uses one) and compares it with the
copy stored at `build/adversarial_1000/<group>.final`.

## 3. Fixed-seed determinism

| Group | Fixed seed / generator (from group `NOTES.md` and harness source) | Determinism evidence |
|---|---|---|
| alloc | 64-bit LCG `BASE_SEED = 0xC0FFEEAD1000`, reseeded per id with `id * 0x9E3779B97F4A7C15`; no time/stdin | `cmp` of `scenarios.log` against a rerun is byte-identical (same digest `bb3016…`). |
| containers | xorshift64* reseeded from scenario id | Two runs produced byte-identical stdout (`cmp` clean). |
| fmt_io | Numerical Recipes LCG `0xAD1000F1`; six fixed draws per scenario | Two full runs produced the identical SHA-256 digest (`b366a1…`). |
| json_log | SplitMix64 `0xC0FFEE123456789`; id-derived | `scenarios.out` and `scenarios2.out` share digest `4bb4bb…` (byte-identical canonical output). |
| memstr | compile-time xorshift64* seed | Two independent runs produced byte-identical `scenarios.log` (`0059a6…`). |
| multimedia | fixed SplitMix64 plus id; no clock/PID/address | Documented in group `NOTES.md`; fixed 50 kinds, 1000 distinct ids. |
| net_http | xorshift64* `0x243F6A8885A308D3`; no clock/PID | Two consecutive runs byte-identical; scenario-log digest `df3bb4…` stable. |
| thread_async | single fixed `ADV_SEED` driving SplitMix64 | Two bounded runs byte-identical stdout (`cmp` returned 0). |

Independent spot-check during this audit: the already-built binaries for
`alloc`, `fmt_io`, `containers` and `json_log` were re-run into a scratch
directory (without touching group sources). All produced the same canonical
digest as the group's recorded run, with the single documented exception of
`json_log`'s timestamped sink log (§4).

## 4. Digest scope and the one honest caveat

* `json_log` writes each scenario to a real `log_sink`; the sink prefixes an
  RFC3339 wall-clock timestamp. Consequently `scenarios.log` and
  `scenarios2.log` have different SHA-256 digests (`e327f4…` vs `d268e5…`)
  even though their 1000 semantic records are identical after stripping the
  `<timestamp> INFO  ` prefix. The **canonical** stdout logs are byte-identical
  (`4bb4bb…`). The runner records `canonical_identical_across_runs=1` and
  `RUN_ALL_OK=1`; this audit does not claim byte-level digest stability for a
  timestamped file.
* `containers/digest.txt` authenticates `results/scenarios.log` (1000 `S`
  lines), while `results/raw.log` additionally carries the summary line. Both
  digests were recomputed and are reported separately in §1.
* No other digest mismatch was found.

## 5. Negative controls — refusals are real, canaries/sentinels intact

The integrity field in each final line is the independent negative control:
a refusal is only counted as acceptable if the post-call guard check also
passed. On the aggregate run every group reported full integrity and zero
failures:

| Group | Integrity control | Recorded value | What it proves in this run |
|---|---|---|---|
| alloc | live heap canary bytes + allocation-metadata state, checked after each call | `canary_ok=1000`, `state_ok=1000` | No guard or metadata corruption across 1000 calls, including pinned breaking ids 1–4 (`SIZE_MAX`, count overflow, injected realloc failure with atomicity, double free). |
| containers | 64-byte global sentinel + prefix/suffix in-block canaries | `canary_ok=1000` | BREAKING-family out-of-range/stale/overflow probes refused without touching adjacent memory (`BREAKING` family present in source). |
| fmt_io | 16-byte canaries before and after every data region | `canary_ok=1000` | Forbidden `%n` catalogue refused before conversion; truncation/overflow cases left guards intact. |
| json_log | four byte guards around JSON and log work buffers | `sentinel_ok=1000` | A single changed guard byte fails the case; none changed. |
| memstr | canary placed at declared capacity (`dmax`/`cap`) | `canary_ok=1000` | Hostile lengths returned documented safeclib refusal codes (400/402/403/404/406/407) without writing past the declared bound. |
| multimedia | byte guards around work buffers + pre-seeded out-parameter sentinels | literal `sentinel_ok` token | One changed guard byte fails the case; SDL dummy drivers and null backends used throughout. |
| net_http | guards around bounded response/slist buffers | `sentinel_ok=1000` | Offline URL parsing, malformed framing and invalid/borrowed descriptors refused without guard damage; no DNS/proxy/external host. |
| thread_async | two-value canary + stdlib live-block metadata count | `state_ok=1000` | Stale handles queried via metadata only (never dereferenced); all worker threads joined under a watchdog. |

Refusal totals from the same run: alloc 644, containers 614, fmt_io 654,
json_log 626, memstr 541, multimedia 720, net_http 660, thread_async 750
(**5209** refusals in total). These counts are from the captured final lines in
`RESULTS.md`, not estimates.

## 6. Evidence — NOT proof

**This is EVIDENCE of refusal under the tested scenarios. It is NOT a proof of
full memory safety, formal correctness, or 100% security.** Each harness
exercises a fixed, reproducible set of public API contracts with deterministic
adversarial inputs and reports what was observed. Passing means that, for
these 1000 inputs per group, the tested entry points returned the documented
result or refusal and the local guards survived. It cannot establish the
absence of defects outside the generated space (other sizes, interleavings,
addresses, configurations, concurrent schedules, compiler/runtime states), nor
does it verify the compiler, the C runtime, or third-party libraries as a
whole. Several groups additionally note specific limits (e.g. the allocator's
bounded recent-free cache, generation-based stale-alias detection, and
non-sanitized default archives). Treat the numbers as regression evidence, not
as a safety guarantee.
