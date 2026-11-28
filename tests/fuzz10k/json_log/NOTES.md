# FUZZ10K — JSON + logging deterministic fuzz (fork-isolated)

## Status: EVIDENCE, NOT PROOF

These scenarios exercise the listed contracts of `stdlib/json.hp`,
`stdlib/log.hp` and the compiled `cplus_json_*` / `cplus_safe_log*`
artifacts with fixed-seed inputs, each isolated in a forked child. Passing
means no failure was observed for these inputs in this environment. It does
not prove the absence of defects, and it does not extend to code paths not
reached here. `--sanitize=none` was used exactly as prescribed.

## Files (all created/modified only inside `tests/fuzz10k/json_log/`)

- `fuzz10k_json_log.cp` — primary harness: 10,000 scenarios, exact prescribed
  build, fork + watchdog + wait4 RSS + canaries, writes `results/scenarios.log`.
- `compiled_probe.cp` — 2,048 scenarios against the compiled symbols in
  `build/safe/libcplus_safecrt.a`; same isolation/verification strategy.
- `run.sh` — build both, run each twice, check determinism + counters, digests.
- `verify_nm.sh` — nm evidence for the compiled artifacts.
- `coverage.py` — independent Python recomputation of the fixed-seed parameter
  map (10,000/10,000 distinct tuples) and output summary.
- `.gitignore` — ignores `results/` and generated artifacts.
- `results/` — generated evidence (gitignored): builds, canonical outputs,
  two `scenarios.log` files, compiled outputs, coverage, nm, digests, summary.

No files outside this directory were created or modified by this task. No
header, compiler, or runtime source was edited. No `git add`/`git commit` was
performed by this task.

## Design

- **Determinism.** SplitMix64 with fixed seed `0xF02A5EED1234ABCD` (primary)
  and `0xC0FFEE0DF02A5EED` (compiled probe). Each id derives its own
  `(kind, r1, r2, r3)` from the seed and id; no wall-clock, PID or address
  enters a scenario or its classification. Ids 0..15 are pinned
  breakers/boundaries; the rest are PRNG-generated across 16 scenario kinds.
  `coverage.py` independently confirms 10,000/10,000 distinct tuples.
- **Fork isolation.** Every scenario runs in `fork()` child. The child sets a
  5-second `alarm()` watchdog and a `RLIMIT_AS` of 512 MiB, arms four
  contiguous 32-byte guards around the JSON and log-message work buffers,
  executes the API, and reports outcome via `_exit()` status. The parent uses
  `wait4()` to obtain the termination signal and peak RSS.
- **Failure classes** (final counters): `crashes` = child killed by a signal
  other than the watchdog; `hangs` = child killed by watchdog `SIGALRM`;
  `corruptions` = canary breach, peak RSS over 256 MiB, or an API result that
  is neither a defined parse nor a defined refusal; `refusals` = defined
  refusals (expected, not failures). Exit is nonzero if
  `crashes + hangs + corruptions > 0`.
- **Bounded generator.** A recursive JSON generator emits null/bool/int/string
  scalars plus arrays/objects, bounded by a nesting limit (terminates) and an
  output cap (128 KiB work buffer). Dedicated builders produce nested arrays
  at depths 0..20000, strings/arrays at lengths 0..128 KiB, and random byte
  blobs. Log messages are 0..4096 bytes. Budgets are random overrides from a
  table including `0, 1, 2, 8, 16, 64, 1024, 16384, 16385, 20000, 25000,
  SIZE_MAX`, plus the header default 16384.
- **Canonical output.** One `S <id> <kind> <result> <sentinel> <memok|memOOM>`
  line per scenario, then the final `FUZZ10K json_log ...` line. Raw RSS is
  deliberately excluded from the canonical stream (it varies with page-fault
  timing); only the bounded-memory verdict is recorded, while the exact peak
  RSS is still checked against the cap in the parent.
- Each primary scenario also writes one line through a real `log_sink` into
  `results/scenarios.log` (default path, overridable by argv[1]).

## Exact commands and results (from repo root)

Build primary (exact prescribed form):

```sh
./build/c+ build tests/fuzz10k/json_log/fuzz10k_json_log.cp --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o tests/fuzz10k/json_log/results/fuzz10k_json_log
```

Build compiled-symbol probe (same base flags plus the compiled archive):

```sh
./build/c+ build tests/fuzz10k/json_log/compiled_probe.cp --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o tests/fuzz10k/json_log/results/compiled_probe -- build/safe/libcplus_safecrt.a
```

Both builds exit 0. Compiler diagnostics are the expected harmless
`-lyyjson / -Lbuild/ext / archive: linker input unused` warnings at the
lowering stage; the final binaries resolve all parser symbols (large/deep
parses succeed and `nm -u` shows no unresolved yyjson symbol).

Runs (each under `timeout 600`):

```sh
cd tests/fuzz10k/json_log
timeout 600 ./results/fuzz10k_json_log results/scenarios.log > results/scenarios.out 2> results/run.stderr
timeout 600 ./results/fuzz10k_json_log results/scenarios2.log > results/scenarios2.out 2> results/run2.stderr
timeout 600 ./results/compiled_probe > results/compiled.out 2> results/compiled.stderr
timeout 600 ./results/compiled_probe > results/compiled2.out 2> results/compiled2.stderr
```

- All four runs exit 0; stderr files contain only the compiled probe's own
  SAFECRT log lines (its `cplus_safe_log` cases), no crashes or sanitizer text.
- `wc -l results/scenarios.out` → 10001 (10,000 `S` lines + 1 final).
- `wc -l results/scenarios.log` → 10000.
- `wc -l results/compiled.out` → 2049 (2,048 `C` lines + 1 final).
- `grep -cE 'CRASH|HANG|CORRUPT|OOM|FAIL'` → 0 in both canonical streams.
- Sentinel-not-intact rows → 0 in both streams.
- Primary final line: `FUZZ10K json_log 10000 0 0 0 4168`
  (10,000 scenarios, 0 crashes, 0 hangs, 0 corruptions, 4,168 refusals).
- Compiled final line: `COMPILED json_log 2048 0 0 0 770`
  (2,048 scenarios, 0 crashes, 0 hangs, 0 corruptions, 770 refusals).
- Primary result mix: 3,932 ok / 1,900 defined / 4,168 refused.
- Wall-clock: ~10.5 s for the 10,000-scenario primary run.

Full orchestration:

```sh
bash tests/fuzz10k/json_log/run.sh
```

Final `results/summary.txt`: `main_build_exit=0 compiled_build_exit=0
main_run1_exit=0 main_run2_exit=0 compiled_run1_exit=0 compiled_run2_exit=0
main_deterministic=1 compiled_deterministic=1 main_S_lines=10000
compiled_C_lines=2048 scenarios_log_lines=10000 bad_main_lines=0
bad_compiled_lines=0 bad_sentinel_main=0 bad_sentinel_compiled=0
coverage_exit=0 nm_exit=0 RUN_ALL_OK=1`.

## Determinism evidence (SHA-256, `shasum -a 256`)

The canonical streams are byte-identical across the two runs. The file-backed
`scenarios.log` files differ only because `log.hp` prefixes each line with a
wall-clock timestamp.

```
f77db2e7886f189180a8bbb0cf233c4c8a4a2f5706176756980c640175ee8f72  results/scenarios.out
f77db2e7886f189180a8bbb0cf233c4c8a4a2f5706176756980c640175ee8f72  results/scenarios2.out
3167303943c137f1e5f840124d4063af32112c2fb6512b0c2753eb3733abd718  results/scenarios.log
bdfa4c7d0c7f596b23aa20db63729cb73ce7b469c18186ed41fcd986984b4431  results/scenarios2.log
77d2bdaf1122a0018f65a173852d9b365c8515d1ec3e031a38463a2dca25d38a  results/compiled.out
77d2bdaf1122a0018f65a173852d9b365c8515d1ec3e031a38463a2dca25d38a  results/compiled2.out
```

Recorded in `results/digests.txt`.

## Coverage evidence (`coverage.py` → `results/coverage.txt`)

```
scenarios=10000 distinct_parameter_tuples=10000
kinds_seen=16 kind_ids=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
S_lines=10000 final_lines=1 kinds_in_output=16
result_mix=defined:1900 ok:3932 refused:4168
final=FUZZ10K json_log 10000 0 0 0 4168
COVERAGE_OK=1
```

All 16 kinds are represented. Depth range 0..20000, length range 0..128 KiB,
log message range 0..4096, null-text/key/message paths, and the budget table
above are all reached by the generator/builders.

## Compiled-artifact verification (`verify_nm.sh` → `results/nm_evidence.txt`)

Exit 0 confirmed `cplus_json_parse`, `cplus_json_free`, `cplus_json_get_int`,
`cplus_json_get_str`, `cplus_safe_log{,_set_level,_get_level}` in
`build/safe/libcplus_safecrt.a`/`w_safe_json.o`, and the yyjson parser backend
in `build/ext/libyyjson.a`. Unlike the ADV1000 suite, these symbols are not
only verified by nm: `compiled_probe.cp` links and actually exercises them
across 2,048 forked scenarios (parse valid/malformed/null/deep/large, get-int,
get-str, free, safe-log set/get/emit), all with sentinel and memory checks.

## Honest limitations

- The 5-second watchdog and 256 MiB RSS cap are generous; a pathological
  allocation well under 256 MiB would not be flagged as a memory violation.
  The canaries detect writes outside the two contiguous work buffers, not all
  forms of corruption.
- Message-length expectations for `log_sink::write` use conservative margins
  (<=400 bytes expect success, >=600 expect a bounds refusal, 401..599 accept
  either) because the exact truncation boundary depends on the timestamp width.
- `json()` uses `strlen`; embedded-NUL inputs are therefore exercised through
  `json_parse_budget` explicit lengths, not the constructor.
- The compiled parser's status-code contract (0 valid, 1 null args, 2 parse
  error) was recovered from nm + a link-and-run probe, not from a public header.
- Canonical stdout is deterministic; the file-backed log is not byte-reproducible
  due to wall-clock timestamps.
- A successful build/run is evidence for these inputs only, not proof of the
  absence of defects.
