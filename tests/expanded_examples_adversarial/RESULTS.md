# Adversarial refusal results — `examples/expanded/*`

Scope: hostile-input testing of the newly added expanded C+ examples. All
created/modified files live only under `tests/expanded_examples_adversarial/`.
No files under `examples/expanded/` were edited. Nothing was staged or
committed by this agent.

## Evidence statement (read this first)

**This is evidence for the specific hostile inputs listed below, not proof of
memory safety, not proof of absence of bugs, and not a guarantee about any
other input.** A `REFUSED` verdict means only that, for that one invocation,
the process either exited nonzero or printed the example's explicit refusal
token, stayed within the `timeout 120` bound, did not terminate on a signal,
and (where the example exposes one) preserved its canary/sentinel token.
Nothing here establishes the examples are formally correct or exhaustively
safe. No passes were fabricated; the two initially mis-specified cases are
disclosed below.

## Exact commands

Builds (per example) are performed by `common.sh:build_one` from the repo root:

```sh
./build/c+ build <repo-relative-source> --strict --sanitize=none -I. -o tests/expanded_examples_adversarial/bin/<name>
# safety/memcpy_s_demo additionally receives after `--`:
#   build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
```

Every individual invocation is bounded:

```sh
timeout 120 <binary> [argv...] <fixture-or-/dev/null> > tests/expanded_examples_adversarial/logs/<example>.<case>.out 2>&1
```

Master command actually executed (final run):

```sh
timeout 120 sh tests/expanded_examples_adversarial/run_all.sh > tests/expanded_examples_adversarial/logs/run3.out 2>&1
# observed exit status: 0
```

Earliest run (`logs/run1.out`) produced `ADV_EXPANDED_TOTAL 87 85 0 2`;
the two `UNEXPECTED_OK` rows were **my test-design errors**, not example
defects:

* `calculator invalid_command` used `1++2`, which is valid under this
  example's documented grammar (`1 + (+2) = 3`, exit 0). Replaced with `foo`.
* `kv_store size_max_value` used a 20-character value, which is within
  `KV_VAL_MAX = 24` and therefore legitimately accepted. Replaced with
  `size_max_key` (20-character key, over `KV_KEY_MAX = 16`) plus a real
  `oversized_value` case (10 KB).

The corrected runs are `logs/run2.out` (110 cases before four more examples
landed) and `logs/run3.out` (final, 110 cases). A reproducibility rerun
(`logs/final.out`) produced byte-identical `ADV_EXPANDED` case lines and the
same `ADV_EXPANDED_TOTAL 110 110 0 0`. All raw stdout+stderr per case is
retained under `logs/`.

## Classification rules

* Exit `>= 128`, or `124`/`137` from `timeout` → `CRASHED`.
* Nonzero exit `1..127` → `REFUSED`.
* Exit 0 with the example's explicit refusal token (`REFUSED`, `REFUSE`,
  `CALC ERROR`, `TODO error`, `BAD_INPUT`, `FAIL`, `usage:`) → `REFUSED`.
* Exit 0 for a self-contained demo with its expected refusal/sentinel token
  → `REFUSED` (these are `SURVIVE` cases: the demo constructs its own hostile
  scenario; hostile argv is injected to check it is ignored without crashing).
* Anything else → `UNEXPECTED_OK`. An additional required-sentinel token
  (`ADV_REQUIRE_TOKEN`) downgrades a would-be `REFUSED` to `UNEXPECTED_OK`
  if the sentinel is missing.

Input families covered: 10 KB strings, SIZE_MAX-looking decimal strings,
empty/whitespace arguments, duplicate keys / ids / cells, out-of-range
indices and ids, invalid commands, truncated/missing paths and one-over files,
10 KB stdin streams, invalid UTF-8 bytes, self-collision move sequences,
double-release / use-after-release / stale-generation scenarios, and missing
(NULL-like) operands.

## Final verbatim output (`logs/run3.out`)

```
ADV_EXPANDED calculator oversized_expr REFUSED 1
ADV_EXPANDED calculator size_max_number REFUSED 1
ADV_EXPANDED calculator empty_expr REFUSED 1
ADV_EXPANDED calculator whitespace_expr REFUSED 1
ADV_EXPANDED calculator div_zero REFUSED 1
ADV_EXPANDED calculator invalid_command REFUSED 1
ADV_EXPANDED calculator truncated_input REFUSED 1
ADV_EXPANDED calculator oversized_stdin REFUSED 1
ADV_EXPANDED calculator no_input REFUSED 1
ADV_EXPANDED todo oversized_title REFUSED 1
ADV_EXPANDED todo oversized_command REFUSED 1
ADV_EXPANDED todo size_max_id REFUSED 1
ADV_EXPANDED todo empty_command REFUSED 1
ADV_EXPANDED todo whitespace_id REFUSED 1
ADV_EXPANDED todo duplicate_key REFUSED 1
ADV_EXPANDED todo out_of_range_index REFUSED 1
ADV_EXPANDED todo invalid_command REFUSED 1
ADV_EXPANDED todo null_like_args REFUSED 1
ADV_EXPANDED todo truncated_path REFUSED 1
ADV_EXPANDED todo post_hostile_integrity REFUSED 1
ADV_EXPANDED file_tools one_over_file REFUSED 1
ADV_EXPANDED file_tools copy_one_over REFUSED 1
ADV_EXPANDED file_tools cmp_one_over REFUSED 1
ADV_EXPANDED file_tools truncated_path REFUSED 1
ADV_EXPANDED file_tools oversized_command REFUSED 1
ADV_EXPANDED file_tools size_max_path REFUSED 1
ADV_EXPANDED file_tools invalid_command REFUSED 1
ADV_EXPANDED file_tools null_like_args REFUSED 2
ADV_EXPANDED text_tools missing_file REFUSED 1
ADV_EXPANDED text_tools oversized_path REFUSED 1
ADV_EXPANDED text_tools size_max_path REFUSED 1
ADV_EXPANDED text_tools invalid_command REFUSED 1
ADV_EXPANDED text_tools bad_operand_count REFUSED 1
ADV_EXPANDED text_tools null_like_args REFUSED 2
ADV_EXPANDED_SUBTOTAL cli cases=34 refused=34 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED game2048 oversized_direction REFUSED 2
ADV_EXPANDED game2048 invalid_command REFUSED 2
ADV_EXPANDED game2048 empty_arg REFUSED 2
ADV_EXPANDED game2048 size_max_number REFUSED 2
ADV_EXPANDED game2048 whitespace_arg REFUSED 2
ADV_EXPANDED game2048 stdin_dash_with_extra REFUSED 2
ADV_EXPANDED maze oversized_direction REFUSED 2
ADV_EXPANDED maze invalid_command REFUSED 2
ADV_EXPANDED maze empty_arg REFUSED 2
ADV_EXPANDED maze size_max_number REFUSED 2
ADV_EXPANDED maze whitespace_arg REFUSED 2
ADV_EXPANDED maze stdin_dash_with_extra REFUSED 2
ADV_EXPANDED tictactoe oversized_move REFUSED 2
ADV_EXPANDED tictactoe invalid_command REFUSED 2
ADV_EXPANDED tictactoe size_max_number REFUSED 2
ADV_EXPANDED tictactoe empty_arg REFUSED 2
ADV_EXPANDED tictactoe whitespace_arg REFUSED 2
ADV_EXPANDED tictactoe self_collision REFUSED 2
ADV_EXPANDED tictactoe oversized_stdin REFUSED 2
ADV_EXPANDED snake_core hostile_argv_self_collision REFUSED 0
ADV_EXPANDED snake_core size_max_argv REFUSED 0
ADV_EXPANDED snake_cli oversized_scenario REFUSED 2
ADV_EXPANDED snake_cli invalid_command REFUSED 2
ADV_EXPANDED snake_cli empty_scenario REFUSED 2
ADV_EXPANDED snake_cli size_max_scenario REFUSED 2
ADV_EXPANDED snake_cli self_collision_stream REFUSED 1
ADV_EXPANDED_SUBTOTAL games cases=26 refused=26 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED kv_store oversized_key REFUSED 0
ADV_EXPANDED kv_store size_max_key REFUSED 0
ADV_EXPANDED kv_store oversized_value REFUSED 0
ADV_EXPANDED kv_store empty_key REFUSED 0
ADV_EXPANDED kv_store invalid_utf8_key REFUSED 0
ADV_EXPANDED kv_store duplicate_key REFUSED 0
ADV_EXPANDED kv_store missing_operand REFUSED 0
ADV_EXPANDED kv_store invalid_command REFUSED 0
ADV_EXPANDED kv_store out_of_range_key REFUSED 0
ADV_EXPANDED kv_store null_like_operand REFUSED 0
ADV_EXPANDED kv_store oversized_stdin REFUSED 0
ADV_EXPANDED kv_store post_hostile_integrity REFUSED 0
ADV_EXPANDED table_query oversized_field REFUSED 0
ADV_EXPANDED table_query size_max_predicate REFUSED 0
ADV_EXPANDED table_query empty_field REFUSED 0
ADV_EXPANDED table_query invalid_command REFUSED 0
ADV_EXPANDED table_query duplicate_id REFUSED 0
ADV_EXPANDED table_query out_of_range_index REFUSED 0
ADV_EXPANDED table_query null_like_operand REFUSED 0
ADV_EXPANDED table_query invalid_utf8_field REFUSED 0
ADV_EXPANDED table_query post_hostile_integrity REFUSED 0
ADV_EXPANDED safety_demo oversized_argv REFUSED 0
ADV_EXPANDED safety_demo size_max_argv REFUSED 0
ADV_EXPANDED safety_demo empty_argv REFUSED 0
ADV_EXPANDED safety_demo null_like_argv REFUSED 0
ADV_EXPANDED_SUBTOTAL database cases=25 refused=25 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED engine_loop oversized_token REFUSED 2
ADV_EXPANDED engine_loop empty_token REFUSED 2
ADV_EXPANDED engine_loop invalid_command REFUSED 2
ADV_EXPANDED engine_loop size_max_token REFUSED 2
ADV_EXPANDED engine_loop whitespace_token REFUSED 2
ADV_EXPANDED engine_loop null_like_no_args REFUSED 2
ADV_EXPANDED ecs oversized_argv REFUSED 0
ADV_EXPANDED ecs size_max_argv REFUSED 0
ADV_EXPANDED ecs null_like_argv REFUSED 0
ADV_EXPANDED resource_cache oversized_argv REFUSED 0
ADV_EXPANDED resource_cache size_max_argv REFUSED 0
ADV_EXPANDED resource_cache empty_argv REFUSED 0
ADV_EXPANDED resource_cache null_like_argv REFUSED 0
ADV_EXPANDED_SUBTOTAL engine cases=13 refused=13 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED memcpy_s_demo oversized_argv REFUSED 0
ADV_EXPANDED memcpy_s_demo size_max_argv REFUSED 0
ADV_EXPANDED memcpy_s_demo empty_argv REFUSED 0
ADV_EXPANDED memcpy_s_demo null_like_argv REFUSED 0
ADV_EXPANDED bounds_demo oversized_argv REFUSED 0
ADV_EXPANDED bounds_demo size_max_argv REFUSED 0
ADV_EXPANDED bounds_demo empty_argv REFUSED 0
ADV_EXPANDED bounds_demo null_like_argv REFUSED 0
ADV_EXPANDED lifetime_demo oversized_argv REFUSED 0
ADV_EXPANDED lifetime_demo size_max_argv REFUSED 0
ADV_EXPANDED lifetime_demo empty_argv REFUSED 0
ADV_EXPANDED lifetime_demo null_like_argv REFUSED 0
ADV_EXPANDED_SUBTOTAL safety cases=12 refused=12 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED_TOTAL 110 110 0 0
```

## Notes on specific verdicts

* `kv_store`, `table_query`, `safety_demo`, `ecs`, `resource_cache` and the
  three `safety/*_demo` programs return exit 0 for handled refusals; their
  contract is the explicit `REFUSED`/`REFUSE` token. Verdicts above are based
  on those tokens, with canary/summary sentinels required where available.
* `memcpy_s_demo` intentionally forks a child that corrupts *its own* canary;
  that is a deliberate subprocess case. The parent's `parent_canary OK`
  sentinel is required in every case, and the parent never exited on a signal.
* `snake_core` and `snake_cli self_collision_stream` are containment checks:
  hostile argv / a 10 KB direction stream must not crash the bounded core.
* `truncated.db` is generated but not used as a named case because these
  examples' documented contracts treat an individual malformed record as a
  skipped record, not as a process-level refusal; asserting a nonzero exit
  there would be a fabricated requirement. Truncated/one-over *files* are
  covered via `file_tools` (`one_over_file`, `copy_one_over`, `cmp_one_over`).

## Files

`common.sh`, `cli.sh`, `games.sh`, `database.sh`, `engine.sh`, `safety.sh`,
`run_all.sh`, `RESULTS.md`, generated `fixtures/`, built `bin/`, and raw
`logs/` — all under `tests/expanded_examples_adversarial/`.
