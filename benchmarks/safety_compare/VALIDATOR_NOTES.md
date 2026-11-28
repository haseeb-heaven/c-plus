# `validate_results.cp` — benchmark evidence validator notes

Files owned by this task:

- `benchmarks/safety_compare/validate_results.cp` (the validator)
- `benchmarks/safety_compare/VALIDATOR_NOTES.md` (this file)

No other repository files were created or modified by this task. All fixture
files below live in a temporary directory outside the repository; the built
binary `/tmp/val` is outside the repository as well.

## What the program does

`validate_results.cp` is a self-contained C+ program. It reads one or more
evidence files with `stdlib/file.hp`, parses CSV/JSON with simple string
scanning (no Python, no external parser), runs four families of checks, and
prints two machine-readable summary lines:

```
BENCH_BLOCKED <count>
BENCH_VALID <checks> <failures>
```

Each failed check also emits `BENCH_FAIL <tag>` before the summaries. The exit
status is 0 only when `failures == 0`. The program prints a `usage:` line and
one failing check if invoked with no file arguments.

`BENCH_BLOCKED <count>` is a separate, non-failing report of rows whose
`status` is `blocked` or `skipped`. Such rows represent externally blocked
work (in the final evidence: an external C+ runtime/header mismatch,
`CPLUS_EXIT_ARITHMETIC`), not corrupt evidence. They are counted and reported
but never counted as validation failures and never treated as passes.

### Accepted evidence schemas

CSV (`.csv`, or a file whose first non-space byte is not `{`):

```
# any comment lines
# columns: workload,language,runs,checksum,median_s,min_s,max_s,status
modern,cplus_safe,7,2200000200000,0.0140,0.0134,0.0188,ok
modern,rust,7,2200000200000,0.0060,0.0048,0.0105,ok
modern,cplus_safe,0,,0,0,0,blocked
```

The column declaration is taken from a `# columns:` comment when present;
otherwise the first non-comment line is used as the header. The existing
`benchmarks/stdlib/allocator/results/allocator_bench.csv` uses the `# columns:`
form and is readable by the validator.

JSON (`.json`, or a file whose first non-space byte is `{`):

```json
{ "records": [
  {"workload":"modern","language":"cplus_safe","runs_ok":7,
   "checksum":"2200000200000","median_s":0.0140,"status":"ok"},
  {"workload":"modern","language":"rust","runs_ok":7,
   "checksum":"2200000200000","median_s":0.0060,"status":"ok"},
  {"workload":"modern","language":"cplus_safe","runs_ok":0,
   "checksum":"","median_s":0,"status":"blocked"}
] }
```

The record array may be named `records` or `results` (the latter matches the
real `benchmarks/safety_compare/results/summary.json`). Run counts may be
spelled `runs`, `runs_ok`, or `runs_total` (`runs_ok` is preferred). Nested
values such as `artifacts: {...}` are skipped as non-evidence. Strings are
quote/escape aware. Evidence files up to 64 KiB are supported.

`status` is optional and is parsed in **both** formats. Recognised benign
values are exactly `blocked` and `skipped`. `ok`, any other value, and a
missing `status` are all validated strictly.

### Checks performed

1. **Required columns/fields exist.** Both formats must provide `workload`,
   `language`, and `runs`; each record must carry a non-empty value for all
   three. CSV additionally checks that each row's field count matches the
   header. `checksum` and `status` are optional.
2. **Checksums agree across languages.** For each distinct `workload` with at
   least two non-empty `checksum` values coming from at least two distinct
   `language` values, all such checksums must be identical. Workloads with a
   single language or no checksum are skipped (not applicable). Rows with a
   benign status are excluded from these groups entirely.
3. **Run counts >= 7.** Each `runs` value must parse fully as an integer and
   be at least 7.
4. **Finite numeric values.** Every unquoted numeric field (`runs`, `median_s`,
   `p95_s`, `ns_per_op`, ...) is parsed with `strtod()`; the whole token must
   be consumed and `isfinite()` must be true, so `nan`, `inf`, `1e400`, and
   partially parsed tokens fail.

A row with `status` `blocked` or `skipped` is exempt from checks 2 and 3 and
does not increment the failure count for any of its checks; it is reported on
the `BENCH_BLOCKED` line instead. Checks 1 and 4 are still evaluated for such
rows (they are counted in `<checks>`), but their failures are suppressed. All
`ok` / unknown / missing-status rows keep every check strict. This change does
not weaken any check on valid rows.

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
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
./build/c+ build benchmarks/safety_compare/validate_results.cp \
  --strict --sanitize=none -o /tmp/val
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
61079d5a59ef02c06172e21d9c7291b82e661d0e5925300b6a20db26a91bbdc1  benchmarks/safety_compare/validate_results.cp
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
modern,scoped,8,2200000200000,0.0044,0.0041,0.0090
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
modern,scoped,7,2200000200000,nan
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

### 3. Positive and negative fixture runs (re-run after the change)

```sh
for f in bench_valid.csv bench_valid.json bench_bad_checksum.csv \
         bench_missing.csv bench_inf.json; do
  echo "=== $f ==="
  /tmp/val "$TD/$f"
  echo "exit=$?"
done
```

Actual output:

```
=== bench_valid.csv ===
BENCH_BLOCKED 0
BENCH_VALID 45 0
exit=0
=== bench_valid.json ===
BENCH_BLOCKED 0
BENCH_VALID 22 0
exit=0
=== bench_bad_checksum.csv ===
BENCH_FAIL record2.runs_ge_7
BENCH_FAIL record3.median_s.finite
BENCH_FAIL checksum_match:modern
BENCH_BLOCKED 0
BENCH_VALID 30 3
exit=1
=== bench_missing.csv ===
BENCH_FAIL csv.column.language
BENCH_FAIL record1.language_present
BENCH_BLOCKED 0
BENCH_VALID 14 2
exit=1
=== bench_inf.json ===
BENCH_FAIL record1.median_s.finite
BENCH_FAIL checksum_match:modern
BENCH_BLOCKED 0
BENCH_VALID 22 2
exit=1
```

All five negative fixtures still fail with exactly the same failure tags and
counts as before the change. They carry no `status` field, so they are
validated strictly. They exercise: mismatched checksum, `runs=6`, `nan`
value, missing `language` column/field, and non-finite `1e400`.

### 4. Real evidence in the repository

```sh
/tmp/val benchmarks/safety_compare/results/summary.json
echo "exit=$?"
```

Actual output (target result):

```
BENCH_BLOCKED 5
BENCH_VALID 192 0
exit=0
```

This is the real, unedited evidence file produced by another agent's
`benchmarks/safety_compare/run_all.sh` in the same working tree. It has 20
records: 15 with `status=ok` (`runs_ok=7`, matching non-empty checksums) and 5
with `status=blocked` (`runs_ok=0`, empty checksum). Before this change the
validator reported `BENCH_VALID 192 5` and exit 1 because it applied the
`runs_ge_7` rule to those 5 externally blocked rows. Now the 5 blocked rows
are reported on `BENCH_BLOCKED 5`, no failure is counted for them, all
applicable cross-language checksum groups still pass, and the exit status is
0. The blocked rows are still not passes — they are reported separately.

To confirm the checksum rule is still evaluated strictly on that real schema,
one checksum was changed in a temporary copy (the repository file was
untouched):

```sh
perl -0pe 's/"checksum":"2200000200000"/"checksum":"0000000000000"/' \
  benchmarks/safety_compare/results/summary.json > "$TD/summary_bad_checksum.json"
/tmp/val "$TD/summary_bad_checksum.json"
echo "exit=$?"
```

Actual output:

```
BENCH_FAIL checksum_match:modern
BENCH_BLOCKED 5
BENCH_VALID 192 1
exit=1
```

Only the checksum failure remains; the 5 blocked rows no longer produce
`runs_ge_7` failures, and the checksum mismatch is still detected.

### 5. Focused status-field tests (CSV and JSON)

New fixtures cover `blocked`, `skipped`, and an unknown status. The exemption
fixture deliberately gives a blocked row `runs=6` and a mismatched checksum
to prove both rules are waived for blocked rows only.

```sh
cat > "$TD/status_blocked.csv" <<'EOF'
# columns: workload,language,runs,checksum,median_s,status
modern,cplus,7,abc,0.01,ok
modern,rust,7,abc,0.02,ok
modern,scoped,0,,0,blocked
modern,fish,0,,0,skipped
EOF
cat > "$TD/status_exempt.csv" <<'EOF'
# columns: workload,language,runs,checksum,status
modern,cplus,7,abc,ok
modern,rust,7,abc,ok
modern,scoped,6,XYZ,blocked
EOF
cat > "$TD/status_unknown.csv" <<'EOF'
# columns: workload,language,runs,checksum,status
modern,cplus,6,abc,weird
EOF
cat > "$TD/status_blocked.json" <<'EOF'
{"results": [
  {"workload":"modern","language":"cplus","runs_ok":7,"checksum":"abc","status":"ok"},
  {"workload":"modern","language":"rust","runs_ok":7,"checksum":"abc","status":"ok"},
  {"workload":"modern","language":"scoped","runs_ok":0,"checksum":"","status":"blocked"},
  {"workload":"modern","language":"fish","runs_ok":0,"checksum":"","status":"skipped"}
]}
EOF
for f in status_blocked.csv status_exempt.csv status_unknown.csv status_blocked.json; do
  echo "=== $f ==="
  /tmp/val "$TD/$f"
  echo "exit=$?"
done
```

Actual output:

```
=== status_blocked.csv ===
BENCH_BLOCKED 2
BENCH_VALID 37 0
exit=0
=== status_exempt.csv ===
BENCH_BLOCKED 1
BENCH_VALID 27 0
exit=0
=== status_unknown.csv ===
BENCH_FAIL record1.runs_ge_7
BENCH_BLOCKED 0
BENCH_VALID 14 1
exit=1
=== status_blocked.json ===
BENCH_BLOCKED 2
BENCH_VALID 32 0
exit=0
```

`status=blocked` and `status=skipped` work in both CSV and JSON. An unknown
status (`weird`) is not a waiver: its `runs=6` still fails. The exemption
fixture confirms a blocked row is excluded from both the `runs_ge_7` check
and the checksum group even when its own values would violate both.

### 6. Other existing evidence and invocation modes

The existing real CSV was also checked (it legitimately lacks a `language`
column, so failures are expected, not suppressed):

```sh
/tmp/val benchmarks/stdlib/allocator/results/allocator_bench.csv
echo "exit=$?"
```

Actual output (18 per-record failures plus the column failure; 19 `BENCH_FAIL`
lines in total):

```
BENCH_FAIL csv.column.language
BENCH_FAIL record1.language_present
... (record2 .. record18.language_present) ...
BENCH_BLOCKED 0
BENCH_VALID 242 19
exit=1
```

No-argument behaviour:

```sh
/tmp/val
echo "exit=$?"
```

```
usage: /tmp/val <evidence.csv|evidence.json> [more ...]
BENCH_FAIL arguments_present
BENCH_BLOCKED 0
BENCH_VALID 1 1
exit=1
```

Multiple files in one invocation aggregate counts, e.g.
`/tmp/val "$TD/bench_valid.csv" "$TD/bench_valid.json"` printed
`BENCH_BLOCKED 0` and `BENCH_VALID 67 0` with exit 0. `BENCH_BLOCKED` is
aggregated the same way as `BENCH_VALID`.

## Honest limitations

- This validator checks the *shape and internal consistency* of an evidence
  file. It does not re-run the benchmarks, verify timestamps, or prove that
  the numbers were measured rather than written by hand.
- The JSON parser is intentionally narrow: it handles the flat record objects
  and nested object skipping used by the two schemas above, not arbitrary
  JSON (no duplicate-key detection, no unicode escape decoding beyond
  `\n`/`\t`/`\r`, no nested arrays of records).
- A workload with fewer than two languages reporting a checksum produces no
  checksum check; that is "not applicable", not a pass.
- A `blocked`/`skipped` row is exempt from the `runs>=7` and checksum rules
  and from failure counting. This is intentional: the benchmark driver
  reports these separately as externally blocked, not as evidence corruption.
  The validator does not verify that the blocker reason is genuine; it trusts
  the `status` field. Only the exact lowercase strings `blocked` and `skipped`
  are recognised; other spellings are validated strictly.
- The 64 KiB evidence cap and 512-record cap are fixed; larger files fail the
  `file_open` / capacity checks and are reported, not truncated silently.
- No live-game or cross-machine claim is made. All results above are local
  command output from the branch and environment described here.
