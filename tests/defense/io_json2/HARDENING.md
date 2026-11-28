# Agent 2 Phase 5-7 — I/O / JSON hardening

## Finding R-JN-4 (json_parse_file budget bypass) — status: NOT REPRODUCIBLE on current HEAD

The red-team reported (in `tests/redteam/json_net/FINDINGS.md`) that
`json_parse_file` called `yyjson_read_file` directly and skipped the lexical
depth budget, so a 20,000-deep file parsed successfully while the buffer path
refused it. This was tracked as an OPEN policy item in
`docs/security/RED_BLUE_BATTLE_FINAL.md`.

**Verification on current working tree** (`tests/defense/io_json2/repro_budget.cp`,
built with `--default --sanitize=none` because strict mode rejects third-party
`yyjson.h:1201` with a `[CPLUS-ARITH]` division-by-zero false positive):

```
json_parse  depth=16000 file_valid=1 buf_valid=1 file_errcat=0 buf_errcat=0
json_parse  depth=16384 file_valid=1 buf_valid=1 file_errcat=0 buf_errcat=0
json_parse  depth=16385 file_valid=0 buf_valid=0 file_errcat=2 buf_errcat=2
json_parse  depth=20000 file_valid=0 buf_valid=0 file_errcat=2 buf_errcat=2
```

Both paths now refuse depth > 16384 with the same error category (2). The
previously reported bypass is **not present** in this checkout. Possible
explanations: the red-team fixture used a path the budget check did not cover
at the time, or the budget was applied in a parallel edit. No code change was
made by this agent to `json.hp`; the observation is recorded honestly rather
than claiming a fix that was not applied here.

## Other reviewed items (no genuine gap found)

- Checked I/O bindings (`test_safe_iface.c`): fopen/fread/fwrite/fclose with
  null stream, one-over buffer, zero-length, and double-close are refused with
  documented codes; destinations keep their canaries.
- Format `%n` positional bypass and struct-member capacity bypass: fixed by
  the format blue-team (`stdlib/safe/src/safe_format.c`) and verified by
  `tests/redteam/format_io` (353→0 crashes, 706→0 corruptions).
- `http_sink_write` OOB: fixed in `stdlib/http.hp`, verified by fs_net2
  (256→0 failures) and red-team json_net (124→0 corruptions).

## ABI

No ABI-visible change from this agent. Fingerprint MATCH (empty diff), 69/69
compat, 70/70 layouts on the current tree. Scope: macOS arm64 / Apple clang 16.
Evidence for tested inputs, not a proof of full safety.
