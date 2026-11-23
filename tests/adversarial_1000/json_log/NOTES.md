# ADV1000 — JSON + logging adversarial scenarios

## Status: EVIDENCE, NOT PROOF

These 1000 deterministic scenarios exercise specific contracts of
`stdlib/json.hp` and `stdlib/log.hp` with fixed-seed inputs. Passing means no
failure was observed for these inputs in this environment. It does not prove
the absence of defects, and it does not extend to code paths not reached here.

## Files (all inside `tests/adversarial_1000/json_log/`)

- `adv1000_json_log.cp` — the harness (C+, `--strict`, `--sanitize=none`).
- `run.sh` — build + two runs + checks + digests; writes only into `results/`.
- `verify_nm.sh` — nm evidence for compiled json/log artifacts in `build/`.
- `coverage.py` — independent recomputation of the fixed-seed parameter map.
- `.gitignore` — ignores `results/` and generated artifacts.
- `results/` — generated evidence (gitignored): canonical output, log files,
  build log, coverage, nm evidence, digests, summary.

No files outside this directory were created or modified. No git add/commit was
performed. No header, compiler, or runtime source was edited.

## Design

- SplitMix64 with fixed seed `0xC0FFEE123456789`; each id 0..999 derives its
  own `(r1,r2,r3)` from the id, so runs are reproducible and order-independent.
  An extra id-dependent mix on `r3` removes a parameter-tuple collision found
  in an earlier draft (`coverage.py` verifies 1000/1000 distinct tuples).
- Ids 0..15 are pinned breakers/boundaries (see table below); ids 16..999 are
  PRNG-generated across 21 scenario kinds.
- Every scenario arms four 32-byte guards around the JSON work buffer and the
  log-message buffer, invokes the API, and checks all guards. A single changed
  byte makes `<sentinel_ok>` 0 and fails the run.
- Results are classified as `ok`, `refused`, `defined`, or `FAIL`. `FAIL` (or a
  bad sentinel) increments the failure count; the binary exits nonzero.
- Each scenario also emits exactly one summary line through a real `log_sink`
  into `results/scenarios.log`. Message-length stress writes go to a separate
  `results/probe.log` so the canonical log stays at exactly 1000 lines.
- The build command's `-lyyjson`/`-Lbuild/ext` produce harmless clang "linker
  input unused" warnings at the lowering stage; the final binary resolves the
  parser (no unresolved yyjson symbols, and large/deep parses succeed).

## Exact commands and results (from repo root)

Build (exact prescribed form):

```sh
./build/c+ build tests/adversarial_1000/json_log/adv1000_json_log.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o tests/adversarial_1000/json_log/results/adv1000_json_log
```

- exit 0.
- Compiler diagnostics (`results/build.log`):
  `clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]`
  and `clang: warning: argument unused during compilation: '-Lbuild/ext'`.
- `nm -u results/adv1000_json_log | grep -i yyjson` → empty (no unresolved
  parser symbol).

Runs:

```sh
cd tests/adversarial_1000/json_log
timeout 120 ./results/adv1000_json_log results/scenarios.log results/probe.log > results/scenarios.out 2>results/run.stderr
timeout 120 ./results/adv1000_json_log results/scenarios2.log results/probe2.log > results/scenarios2.out 2>results/run2.stderr
```

- both exit 0; `results/run.stderr` and `results/run2.stderr` are empty.
- `wc -l results/scenarios.out` → 1001 (1000 `S` lines + 1 final line).
- `wc -l results/scenarios.log` → 1000.
- `grep -c ' FAIL ' results/scenarios.out` → 0.
- `awk '$1=="S" && $6!="1"'` → 0 rows (all sentinels intact).
- Final line: `ADV1000 json_log 1000 626 1000 0`
  (626 refusals, 1000 sentinel-ok, 0 failures).
- Result mix over the 1000 `S` lines: 626 refused / 223 ok / 151 defined
  (the final line is excluded from this breakdown).

Full orchestration:

```sh
bash tests/adversarial_1000/json_log/run.sh
```

- exit 0; `results/summary.txt` reports `build_exit=0 run1_exit=0 run2_exit=0
  canonical_identical_across_runs=1 S_lines=1000 final_lines=1 FAIL_lines=0
  sentinel_not_ok=0 scenarios_log_lines=1000 coverage_exit=0 nm_exit=0
  RUN_ALL_OK=1`.

## Determinism evidence (SHA-256, `shasum -a 256`)

Canonical output is byte-identical across the two runs; the file-backed logs
differ only because `log.hp` prefixes each line with a wall-clock timestamp.

```
4bb4bb7b8d1e478cf6d1ac5ad15e209cd37dafbae03f39620c785a6357647927  results/scenarios.out
4bb4bb7b8d1e478cf6d1ac5ad15e209cd37dafbae03f39620c785a6357647927  results/scenarios2.out
4051588d836528975f48710955c4b79c98c67d282fdacd369b863bd53eec60fb  results/scenarios.log
31680750c0fc66028a0a653c6be707c19f103f96409143ff036da7fddd5d9af5  results/scenarios2.log
```

Recorded in `results/digests.txt`.

## Coverage evidence (`coverage.py` → `results/coverage.txt`)

```
scenarios=1000 distinct_parameter_tuples=1000
depth_scenarios=216 depth_min=0 depth_max=20000 depth_0=True depth_16384=True depth_16385=True depth_20000=True
length_scenarios=143 length_min=0 length_max=2097152 length_0=True length_2097152=True
msg_scenarios=42 msg_min=0 msg_max=4096 msg_0=True msg_4096=True
budget_override_values=0 1 2 8 16 64 1024 16384 16385 20000 25000 18446744073709551615
COVERAGE_OK=1
```

All 21 kinds are represented (per-kind counts are in `results/coverage.txt`).

## Pinned breaker cases (ids 0..15, observed results)

| id | case | expected / observed |
|----|------|---------------------|
| 0 | `json(NULL, err)` | refuses: invalid doc, silent error by documented contract |
| 1 | `json_parse_budget(NULL, 16, ...)` | refused |
| 2 | zero length | refused |
| 3 | `SIZE_MAX` length | refused |
| 4 | truncated 16385-deep prefix | refused |
| 5 | 16385-level nesting, default budget (16384) | refused |
| 6 | 16384-level nesting, default budget | ok |
| 7 | 20000-level nesting, budget 16384 | refused |
| 8 | 20000-level nesting, budget `SIZE_MAX` | ok |
| 9 | 2 MiB document | ok |
| 10 | null `FILE*` sink write | refused |
| 11 | `log_open_file(NULL, ...)` | refused |
| 12 | `log_level_parse(NULL, ...)` | refused |
| 13 | 0-byte log message | ok |
| 14 | 4096-byte log message | refused (bounds) |
| 15 | `json_input_within_budget(NULL, ...)` | refused |

None crashed, hung, or perturbed a guard byte.

## Compiled-artifact verification (nm only)

`bash verify_nm.sh > results/nm_evidence.txt` (exit 0) confirmed:

- `build/safe/w_safe_json.o`: `cplus_json_parse`, `cplus_json_free`,
  `cplus_json_get_int`, `cplus_json_get_str`.
- `build/safe/w_safe_log.o`: `cplus_safe_log`, `cplus_safe_log_set_level`,
  `cplus_safe_log_get_level`.
- `build/safe/libcplus_safecrt.a`: the same symbols plus
  `cplus_safe_stat_record`.
- `build/ext/libyyjson.a`: `yyjson_read_opts`, `yyjson_val_write*`.

These compiled artifacts were **verified by nm, not exercised by the 1000
scenarios**; the scenarios call the header-only `stdlib/json.hp` and
`stdlib/log.hp` APIs, which is the primary requested scope.

## Honest limitations

- `log_sink::write` uses a 512-byte line buffer. Message-length expectations
  use conservative margins: <=400 bytes expect success, >=600 expect a bounds
  refusal, and 401..599 accept either defined result. The exact truncation
  boundary is not asserted because it depends on the timestamp prefix width.
- Guard bytes detect writes outside the two work buffers; they do not detect
  all forms of corruption, and no sanitizer was used (`--sanitize=none` as
  prescribed).
- `json()` uses `strlen`; embedded-NUL cases are therefore exercised through
  `json_parse_budget` explicit lengths, not through the constructor.
- Wall-clock timestamps make `scenarios.log` non-reproducible byte-for-byte;
  only the canonical `S ...` stream is deterministic.
- A successful build/run is evidence for these 1000 inputs only, not proof of
  absence of defects.
