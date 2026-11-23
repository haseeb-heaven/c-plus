# `validate_results.cp` — benchmark evidence validator notes

Files owned by this task:

- `benchmarks/safety_compare/validate_results.cp` (the validator)
- `benchmarks/safety_compare/VALIDATOR_NOTES.md` (this file)

No other repository files were created or modified by this task. All fixture
files below live in a temporary directory outside the repository; the built
binary `build/validate_results` is an ignored build artifact.

## What the program does

`validate_results.cp` is a self-contained C+ program. It reads one or more
evidence files with `stdlib/file.hp`, parses CSV/JSON with simple string
scanning (no Python, no external parser), runs four families of checks, and
prints one machine-readable summary line:

```
BENCH_VALID <checks> <failures>
```

Each failed check also emits `BENCH_FAIL <tag>` before the summary. The exit
status is 0 only when `failures == 0`. The program prints a `usage:` line and
one failing check if invoked with no file arguments.

### Accepted evidence schemas

CSV (`.csv`, or a file whose first non-space byte is not `{`):

```
# any comment lines
# columns: workload,language,runs,checksum,median_s,min_s,max_s
modern,cplus_safe,7,2200000200000,0.0140,0.0134,0.0188
modern,rust,7,2200000200000,0.0060,0.0048,0.0105
```

The column declaration is taken from a `# columns:` comment when present;
otherwise the first non-comment line is used as the header. The existing
`benchmarks/stdlib/allocator/results/allocator_bench.csv` uses the `# columns:`
form and is readable by the validator.

JSON (`.json`, or a file whose first non-space byte is `{`):

```json
{ "records": [
  {"workload":"modern","language":"cplus_safe","runs":7,
   "checksum":"2200000200000","median_s":0.0140},
  {"workload":"modern","language":"rust","runs":7,
   "checksum":"2200000200000","median_s":0.0060}
] }
```

The record array may be named `records` or `results` (the latter matches the
real `benchmarks/safety_compare/results/summary.json`). Run counts may be
spelled `runs`, `runs_ok`, or `runs_total` (`runs_ok` is preferred so blocked
records with 0 successful runs are not silently treated as passes). Nested
values such as `artifacts: {...}` are skipped as non-evidence. Strings are
quote/escape aware. Evidence files up to 64 KiB are supported.

### Checks performed

1. **Required columns/fields exist.** Both formats must provide `workload`,
   `language`, and `runs`; each record must carry a non-empty value for all
   three. CSV additionally checks that each row's field count matches the
   header. `checksum` is optional.
2. **Checksums agree across languages.** For each distinct `workload` with at
   least two non-empty `checksum` values coming from at least two distinct
   `language` values, all such checksums must be identical. Workloads with a
   single language or no checksum are skipped (not applicable).
3. **Run counts >= 7.** Each `runs` value must parse fully as an integer and
   be at least 7.
4. **Finite numeric values.** Every unquoted numeric field (`runs`, `median_s`,
   `p95_s`, `ns_per_op`, ...) is parsed with `strtod()`; the whole token must
   be consumed and `isfinite()` must be true, so `nan`, `inf`, `1e400`, and
   partially parsed tokens fail.

Byte loads from pointer parameters use `__builtin_memcpy`, the same pattern as
`stdlib/file.hp` and the existing hostile-input probes; this is required
because `--strict` rejects subscripts on pointer parameters whose extent
cannot be proven.

## Exact commands and actual results

All commands below were run from the repository root on branch
`agent/modern-stdlib`. The build was executed under the required 30-second
timeout and produced no stdout/stderr and exit status 0.

### 1. Build (strict, no sanitizer)

```sh
timeout 30 build/c+ build benchmarks/safety_compare/validate_results.cp \
  --strict --sanitize=none -o build/validate_results
echo "build_exit=$?"
```

Result:

```
build_exit=0
```

Source identity at the time of the run:

```sh
shasum -a 256 benchmarks/safety_compare/validate_results.cp
```

```
031a00d6d7ddaaa84d36f467af581f531a489fc5f17e88d3d71984b64751a928  benchmarks/safety_compare/validate_results.cp
```

### 2. Fixture files (temporary, outside the repository)

```sh
TD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/bench_validate_fixtures
mkdir -p "$TD"
cat > "$TD/bench_valid.csv" <<'EOF'
# benchmark evidence: checksum agreement across languages
# columns: workload,language,runs,checksum,median_s,min_s,max_s
modern,cplus_safe,7,2200000200000,0.0140,0.0134,0.0188
modern,cplus_legacy,9,2200000200000,0.0061,0.0052,0.0077
modern,rust,7,2200000200000,0.0060,0.0048,0.0105
modern,goose,8,2200000200000,0.0044,0.0041,0.0090
EOF
cat > "$TD/bench_valid.json" <<'EOF'
{
  "records": [
    {"workload":"modern","language":"cplus_safe","runs":7,"checksum":"2200000200000","median_s":0.0140},
    {"workload":"modern","language":"rust","runs":7,"checksum":"2200000200000","median_s":0.0060}
  ]
}
EOF
cat > "$TD/bench_bad_checksum.csv" <<'EOF'
# columns: workload,language,runs,checksum,median_s
modern,cplus_safe,7,2200000200000,0.0140
modern,rust,6,999,0.0060
modern,goose,7,2200000200000,nan
EOF
cat > "$TD/bench_missing.csv" <<'EOF'
# columns: workload,runs,checksum
modern,7,abc
EOF
cat > "$TD/bench_inf.json" <<'EOF'
{"records": [
  {"workload":"modern","language":"cplus","runs":9,"checksum":"1","median_s":1e400},
  {"workload":"modern","language":"rust","runs":8,"checksum":"2","median_s":0.5}
]}
EOF
```

### 3. Positive and negative fixture runs

```sh
for f in bench_valid.csv bench_valid.json bench_bad_checksum.csv \
         bench_missing.csv bench_inf.json; do
  echo "=== $f ==="
  build/validate_results "$TD/$f"
  echo "exit=$?"
done
```

Actual output:

```
=== bench_valid.csv ===
BENCH_VALID 45 0
exit=0
=== bench_valid.json ===
BENCH_VALID 22 0
exit=0
=== bench_bad_checksum.csv ===
BENCH_FAIL record2.runs_ge_7
BENCH_FAIL record3.median_s.finite
BENCH_FAIL checksum_match:modern
BENCH_VALID 30 3
exit=1
=== bench_missing.csv ===
BENCH_FAIL csv.column.language
BENCH_FAIL record1.language_present
BENCH_VALID 14 2
exit=1
=== bench_inf.json ===
BENCH_FAIL record1.median_s.finite
BENCH_FAIL checksum_match:modern
BENCH_VALID 22 2
exit=1
```

These exercise each required property: valid CSV/JSON pass; the bad-checksum
fixture fails the `modern` checksum comparison, the `runs=6` count, and the
`nan` value; the missing-column fixture fails the `language` column/field;
the JSON fixture fails the non-finite `1e400` value and its checksum mismatch.

### 4. Real evidence in the repository

```sh
build/validate_results benchmarks/safety_compare/results/summary.json
echo "exit=$?"
```

Actual output:

```
BENCH_FAIL record2.runs_ge_7
BENCH_FAIL record5.runs_ge_7
BENCH_FAIL record12.runs_ge_7
BENCH_FAIL record17.runs_ge_7
BENCH_FAIL record19.runs_ge_7
BENCH_VALID 192 5
exit=1
```

This is a real, unedited evidence file produced by another agent's
`benchmarks/safety_compare/run_all.sh` in the same working tree. The 5
failures are the records whose `runs_ok` is 0 (blocked/failed builds in that
harness); the validator does not reclassify those as passes. All applicable
cross-language checksum groups in that file passed.

To confirm the checksum rule is actually evaluated on that real schema, one
checksum was changed in a temporary copy (the repository file was untouched):

```sh
perl -0pe 's/"checksum":"2200000200000"/"checksum":"0000000000000"/' \
  benchmarks/safety_compare/results/summary.json > "$TD/summary_bad_checksum.json"
build/validate_results "$TD/summary_bad_checksum.json"
echo "exit=$?"
```

Actual output:

```
BENCH_FAIL record2.runs_ge_7
BENCH_FAIL record5.runs_ge_7
BENCH_FAIL record12.runs_ge_7
BENCH_FAIL record17.runs_ge_7
BENCH_FAIL record19.runs_ge_7
BENCH_FAIL checksum_match:modern
BENCH_VALID 192 6
exit=1
```

The existing real CSV was also checked (it legitimately lacks a `language`
column, so failures are expected, not suppressed):

```sh
build/validate_results benchmarks/stdlib/allocator/results/allocator_bench.csv
echo "exit=$?"
```

Actual output (18 per-record failures plus the column failure; the full list
is 19 `BENCH_FAIL` lines):

```
BENCH_FAIL csv.column.language
BENCH_FAIL record1.language_present
... (record2 .. record18.language_present) ...
BENCH_VALID 242 19
exit=1
```

### 5. No-argument behaviour

```sh
build/validate_results
echo "exit=$?"
```

```
usage: build/validate_results <evidence.csv|evidence.json> [more ...]
BENCH_FAIL arguments_present
BENCH_VALID 1 1
exit=1
```

Multiple files in one invocation aggregate counts, e.g.
`build/validate_results "$TD/bench_valid.csv" "$TD/bench_valid.json"` printed
`BENCH_VALID 67 0` with exit 0.

## Honest limitations

- This validator checks the *shape and internal consistency* of an evidence
  file. It does not re-run the benchmarks, verify timestamps, or prove that
  the numbers were measured rather than written by hand.
- The JSON parser is intentionally narrow: it handles the flat record objects
  and nested object skipping used by the two schemas above, not arbitrary
  JSON (no duplicate-key detection, no unicode escape decoding beyond
  `\\n`/`\\t`/`\\r`, no nested arrays of records).
- A workload with fewer than two languages reporting a checksum produces no
  checksum check; that is "not applicable", not a pass.
- The 64 KiB evidence cap and 512-record cap are fixed; larger files fail the
  `file_open` / capacity checks and are reported, not truncated silently.
- No live-game or cross-machine claim is made. All results above are local
  command output from the branch and environment described here.
