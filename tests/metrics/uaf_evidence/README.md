# Raw UAF evidence suite

Deterministic, 12-case evidence for the documented limitation
"raw malloc/free use-after-free is not detected in default builds"
(`docs/LIMITATIONS.md`, capability table and classic-bug table row 2).

Run from the repository root:

```sh
python3 tests/metrics/uaf_evidence_run.py
```

No compiler rebuild is performed. At startup the runner copies `build/c+` to
`tests/metrics/uaf_evidence/out/cplus-under-test` and pins source lookup with
`CPLUS_ROOT`, so a concurrent rebuild of `build/c+` cannot change the compiler
mid-run. The SHA-256 of the snapshot is recorded in both reports. All outputs
are written only under `tests/metrics/uaf_evidence/out/` (per-case logs, ASan
binaries, `report.txt`, `report.json`).

Optional environment overrides: `CPLUS_BIN`, `CPLUS_ASAN_CLANG`,
`CPLUS_SDKROOT`, `CPLUS_CHECK_CLANG`.

## What the runner proves

1. **Static stage** — `c+ check --strict --sanitize=none` is run on every case.
   Six cases are rejected with exact, named C+ diagnostics
   (`[CPLUS-LIFETIME]`, `[CPLUS-OWNER]`, `[CPLUS-ALIAS]`). The other six are
   accepted; these are the static-analysis gaps (callee parameter, cast alias,
   returned alias, runtime array index, and the two safe controls).
2. **Runtime stage** — each accepted case is built with
   `c+ build --default --sanitize=address` using
   `CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang` and
   `-- -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`, then run.
   The four unsafe accepted cases must print both
   `ERROR: AddressSanitizer: heap-use-after-free` and its `SUMMARY:` line.
   The two safe controls must exit 0 with no sanitizer diagnostic.
3. A nonzero exit status alone is **never** treated as proof: every assertion
   is on an exact diagnostic substring.

## Case inventory

| case | static STRICT result | runtime result |
| --- | --- | --- |
| `direct_uaf` | reject, `[CPLUS-LIFETIME]` | not built |
| `alias_uaf` | reject, `[CPLUS-LIFETIME]` | not built |
| `field_uaf` | reject, `[CPLUS-LIFETIME]` | not built |
| `array_ptr_uaf` | reject, `[CPLUS-LIFETIME]` | not built |
| `double_free` | reject, `[CPLUS-OWNER]` | not built |
| `use_after_realloc` | reject, `[CPLUS-ALIAS]` | not built |
| `interproc_uaf` | reject, `[CPLUS-LIFETIME]` (released pointer passed to callee) | not built |
| `cast_uaf` | reject, `[CPLUS-LIFETIME]` (cast-transparent alias) | not built |
| `returned_alias_uaf` | reject, `[CPLUS-LIFETIME]` (helper-return alias) | not built |
| `array_index_uaf` | reject, `[CPLUS-LIFETIME]` (runtime index into released element) | not built |
| `safe_free_null_guard` | compile (accepted) | clean, no diagnostic |
| `safe_alias_before_free` | compile (accepted) | clean, no diagnostic |
