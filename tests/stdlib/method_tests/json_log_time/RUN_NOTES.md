# RUN_NOTES — method tests for json / log / time

Scope: 100% of the public callable surface of `stdlib/json.hp`, `stdlib/log.hp`,
and `stdlib/time.hp`, one deterministic case per callable. Every case CALLS the
callable and verifies either its return value or an observable side effect, and
prints exactly `<header> <method> PASS|FAIL`. Each program exits nonzero when
at least one case fails.

## Deliverables

| File | Purpose |
| --- | --- |
| `json_methods.cp` | 34 cases for `stdlib/json.hp` |
| `log_methods.cp` | 16 cases for `stdlib/log.hp` |
| `time_methods.cp` | 6 cases for `stdlib/time.hp` |
| `methods.txt` | one `<header> <method> <PASS\|FAIL>` line per callable |
| `RUN_NOTES.md` | this file |

## Enumeration

All functions/methods declared in the three headers are covered. Constructors
are invoked directly (`json_view(NULL)`, `json(text, &err)`, `json_builder()`,
`log_sink(stream, owns, threshold)`) and verified through the resulting
object's state. Void functions are verified through side effects: `json_free`
frees a real `write()` buffer and accepts NULL; `json::drop` /
`json_builder::drop` / `log_sink::drop` invalidate the owner and are idempotent;
`log_sink::set_level` changes filtering; `time_sleep_ms` advances a monotonic
clock by a bounded amount.

The task annotation listed `json.hp (32), log.hp (14), time.hp (5)`. Those
figures are a strict subset of the declarations actually present: the headers
contain 34 / 16 / 6 callables respectively. The difference is accounted for by
constructors (3 json, 1 log) and void-returning members (2 json `drop`s, 2 log
`set_level`/`drop`, 1 time `time_sleep_ms`). This manifest deliberately tests
the full superset so that no declared callable is left unverified.

C+ lowering notes encountered and handled in the sources:
- the lowering pass uses a flat symbol table, so local variable names are
  globally unique within each test file;
- member calls on temporaries are not lowered, so every root/member value is
  assigned to a local before the next member call.

## Exact commands and results

Run from the repository root. Build command is exactly the one specified in
the task (yyjson triplet as separate operands); each binary is run under
`timeout 30`.

```sh
./build/c+ build tests/stdlib/method_tests/json_log_time/json_methods.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o tests/stdlib/method_tests/json_log_time/json_methods
timeout 30 ./tests/stdlib/method_tests/json_log_time/json_methods
```

build exit 0 (only the compiler's informational
`-lyyjson: 'linker' input unused` / `argument unused during compilation: '-Lbuild/ext'`
warnings), run exit 0, 34 PASS / 0 FAIL:

```
json json_input_within_budget PASS
json json_parse_budget PASS
json json_parse_file PASS
json json_view_of PASS
json json_free PASS
json json_view::json_view PASS
json json_view::valid PASS
json json_view::is_object PASS
json json_view::is_array PASS
json json_view::is_number PASS
json json_view::is_string PASS
json json_view::is_bool PASS
json json_view::get_int PASS
json json_view::get_double PASS
json json_view::get_bool PASS
json json_view::get_string PASS
json json_view::size PASS
json json_view::at PASS
json json_view::member PASS
json json_view::write PASS
json json::json PASS
json json::valid PASS
json json::read_size PASS
json json::value_count PASS
json json::root PASS
json json::drop PASS
json json_builder::json_builder PASS
json json_builder::valid PASS
json json_builder::add_int PASS
json json_builder::add_double PASS
json json_builder::add_bool PASS
json json_builder::add_string PASS
json json_builder::write PASS
json json_builder::drop PASS
RESULT failures 0
```

```sh
./build/c+ build tests/stdlib/method_tests/json_log_time/log_methods.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o tests/stdlib/method_tests/json_log_time/log_methods
timeout 30 ./tests/stdlib/method_tests/json_log_time/log_methods
```

build exit 0 (same informational warnings), run exit 0, 16 PASS / 0 FAIL:

```
log log_level_name PASS
log log_level_parse PASS
log log_open_file PASS
log log_stderr PASS
log log_stdout PASS
log log_sink::log_sink PASS
log log_sink::valid PASS
log log_sink::native PASS
log log_sink::level PASS
log log_sink::set_level PASS
log log_sink::enabled PASS
log log_sink::write PASS
log log_sink::info PASS
log log_sink::warn PASS
log log_sink::failure PASS
log log_sink::drop PASS
RESULT failures 0
```

```sh
./build/c+ build tests/stdlib/method_tests/json_log_time/time_methods.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o tests/stdlib/method_tests/json_log_time/time_methods
timeout 30 ./tests/stdlib/method_tests/json_log_time/time_methods
```

build exit 0 (same informational warnings), run exit 0, 6 PASS / 0 FAIL:

```
time time_now_ns PASS
time time_now_ms PASS
time time_monotonic_ns PASS
time time_monotonic_ms PASS
time time_sleep_ms PASS
time time_format_iso8601 PASS
RESULT failures 0
```

## Determinism and fixtures

- JSON uses two fixed inline fixtures plus a fixed document written to
  `/tmp/cplus_jlt_json_parse_fixture.json` and removed after the case.
- Log files use fixed paths under `/tmp` and are unlinked before/after each
  case; assertions check fixed substrings (level name, message marker,
  `key=value` fields, `file:line`) and the absence of a filtered message.
  Wall-clock timestamps are intentionally not asserted.
- Time getters are checked for non-zero/bounded sanity, mutual consistency
  with a tolerance, and monotonic ordering; `time_format_iso8601` is checked
  against fixed epoch values and deterministic error paths. No exact
  wall-clock value is asserted.
- All three suites were run three times consecutively with identical results
  (34/16/6 PASS, zero failures each run).

## Totals

| Header | Total | Passed | Failed | Skipped |
| --- | --- | --- | --- | --- |
| json | 34 | 34 | 0 | 0 |
| log | 16 | 16 | 0 | 0 |
| time | 6 | 6 | 0 | 0 |
| **all** | **56** | **56** | **0** | **0** |

METHOD_TEST json_log_time 56 56 0 0
