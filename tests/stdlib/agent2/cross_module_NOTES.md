# cross_module — cross-module regression test (agent 2)

## Owned files

- `tests/stdlib/agent2/cross_module.cp`
- `tests/stdlib/agent2/cross_module.stdin`
- `tests/stdlib/agent2/cross_module_NOTES.md` (this file)

No headers, existing tests, or other agents' files were modified. No git
add/commit was performed.

## What the single program covers

| # | Required combination | Checks / tags |
|---|----------------------|---------------|
| 1 | file -> string via `file::write_bytes`, `file::read`, `stdlib/io` `read_line` | `cm_write_*`, `cm_file_read`, `cm_file_into_string`, `cm_io_stdin_line`, `cm_io_into_string`, `cm_io_file_*` |
| 2 | parse buffer with `stdlib/json` (yyjson) | `cm_json_valid`, `cm_json_root`, `cm_json_int_*`, `cm_json_array_ints` |
| 3 | parsed ints into `intmap` + `vector` | `cm_store_intmap`, `cm_store_vector`, `cm_store_sizes` |
| 4 | grow both containers while strings are allocated | `cm_growth_ok`, `cm_growth_sizes`, `cm_growth_telemetry`, `cm_growth_strings_alive` |
| 5 | drop containers before strings | `cm_drop_containers`, `cm_strings_survive_containers`, `cm_drop_strings` |
| 6 | file error via `stdlib/error`, assert category + code | `cm_file_error_status`, `cm_file_error_category_code`, `cm_file_error_message` |
| 7 | `fs_exists` gates an open | `cm_fs_present`, `cm_fs_absent`, `cm_gated_open` |
| 8 | second operation after induced alloc failure; recovery | `cm_oom_first`, `cm_oom_atomic`, `cm_oom_telemetry`, `cm_oom_recovered` |
| 9 | repeated failed/valid cycles + telemetry balance | `cm_cycles_ok`, `cm_cycles_balance` |
| 10 | no double release via allocator telemetry | `cm_idem_first_drop`, `cm_idem_second_drop`, `cm_global_balance` |

Plus `cm_fs_clean_start`, `cm_cleanup_remove`, `cm_cleanup_absent`: the
PID-unique file (`/tmp/cplus_agent2_cross_<pid>.json`) is created, gated, read,
and asserted removed. Growth keys come from a fixed-seed LCG
(`0xC0FFEEu`, `a=1664525`, `c=1013904223`), so stdout is fully deterministic.
No stale pointer is ever dereferenced; json views are dropped before their
document and are not used afterwards.

## Exact commands (run from the repository root)

`ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`

STRICT --sanitize=none (run FIRST):

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/cross_module.cp" \
  --strict --sanitize=none \
  -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
  -o "$TMPD/cross_module_strict_none"
timeout 30 "$TMPD/cross_module_strict_none" \
  < "$ROOT/tests/stdlib/agent2/cross_module.stdin" > "$TMPD/strict_none.out"
```

Default:

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/cross_module.cp" \
  -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
  -o "$TMPD/cross_module_default"
timeout 30 "$TMPD/cross_module_default" \
  < "$ROOT/tests/stdlib/agent2/cross_module.stdin" > "$TMPD/default.out"
```

UBSan:

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/cross_module.cp" \
  --strict --sanitize=undefined \
  -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
  -o "$TMPD/cross_module_ubsan"
timeout 30 "$TMPD/cross_module_ubsan" \
  < "$ROOT/tests/stdlib/agent2/cross_module.stdin" > "$TMPD/ubsan.out"
```

The `-Ithird_party/yyjson -Lbuild/ext -lyyjson` flags are required by
`stdlib/json.hp`; without them the build fails at link time. The harness
(`run_all.sh`, which auto-detects json includes) supplies them. Harness check
used (with the result directory redirected so other agents' artifacts were
not touched by `--clean`):

```sh
AGENT2_RESULTS="$TMPD/results" \
  sh tests/stdlib/agent2/run_all.sh --test cross_module --clean
```

Note: `--clean` deletes every file in the results directory, so the override
was used to stay inside strict file ownership. Result: `PASS=1 FAIL=0
BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=1`, harness exit 0.

## Per-mode results

| Mode | Build rc | Run rc | Final stdout line | stderr | Verdict |
|------|----------|--------|-------------------|--------|---------|
| strict / sanitize=none | 0 | 0 | `CROSS_MODULE 46 0` | empty | PASS |
| default | 0 | 0 | `CROSS_MODULE 46 0` | empty | PASS |
| strict / sanitize=undefined (UBSan) | 0 | 0 | `CROSS_MODULE 46 0` | empty | PASS |

All three stdout files are byte-identical (`diff` clean). 47 stdout lines:
46 `<tag> <0|1>` check lines, all `1`, then the final line. Exit code is 0 in
every mode. Running without the `.stdin` fixture makes exactly
`cm_io_stdin_line` and `cm_io_into_string` fail (`CROSS_MODULE 46 2`, exit 1),
confirming the fixture is exercised. No `/tmp/cplus_agent2_cross_*.json`
file remains after any run.

The harmless compiler warnings `argument unused during compilation: '-L...'`
/ `'-lyyjson: linker input unused'` come from the C+ lowering's intermediate
clang invocations; the final link uses both flags (json parses successfully).

## Exact stdout (all three modes)

```
cm_fs_clean_start 1
cm_io_stdin_line 1
cm_io_into_string 1
cm_write_open 1
cm_write_bytes 1
cm_write_flush 1
cm_write_close 1
cm_fs_present 1
cm_fs_absent 1
cm_gated_open 1
cm_file_read 1
cm_file_into_string 1
cm_io_file_open 1
cm_io_file_read 1
cm_io_file_into_string 1
cm_json_valid 1
cm_json_root 1
cm_json_int_a 1
cm_json_int_b 1
cm_json_int_c 1
cm_json_int_d 1
cm_json_array_ints 1
cm_store_intmap 1
cm_store_vector 1
cm_store_sizes 1
cm_growth_ok 1
cm_growth_sizes 1
cm_growth_telemetry 1
cm_growth_strings_alive 1
cm_drop_containers 1
cm_strings_survive_containers 1
cm_drop_strings 1
cm_file_error_status 1
cm_file_error_category_code 1
cm_file_error_message 1
cm_oom_first 1
cm_oom_atomic 1
cm_oom_telemetry 1
cm_oom_recovered 1
cm_cycles_ok 1
cm_cycles_balance 1
cm_idem_first_drop 1
cm_idem_second_drop 1
cm_cleanup_remove 1
cm_cleanup_absent 1
cm_global_balance 1
CROSS_MODULE 46 0
```

## Observed defects / findings

No defect in the stdlib headers or the C+ runtime was observed. Two
test-side issues were found and fixed **in the test only** (minimal repro
included so they are not reintroduced):

1. **`cplus_move` on a temporary is rejected in STRICT mode.**
   Minimal repro:
   ```c++
   json d = json(NULL, &e);
   d = cplus_move(json("{}", &e));   // error: cannot take the address of an rvalue
   ```
   `cplus_move` expands to `memset(&(x), 0, sizeof(x))` and needs an lvalue.
   Fix: declare the owner directly at its point of use instead of
   pre-declaring and moving a temporary (`json cm_doc = json(...)`,
   `file cm_writer = file(...)`). No header change needed.

2. **Telemetry window / constructor hoisting.** Minimal repro: declaring an
   owned `string` at function top and calling `stdlib_telemetry_reset()` as
   the first statement produced `DBG 46 50 17 0 0` — more frees than allocs.
   `--emit-c` showed why: the lowering emits owned-locals' constructors at
   the top of their block, i.e. **before** the first executable statement, so
   the constructors' `stdlib_alloc` calls fell outside the window while their
   later `stdlib_release` calls were counted inside it. Fix: all owned locals
   now live in an inner block that starts after `stdlib_telemetry_reset()` /
   `stdlib_telemetry_set_enabled(1)`, with the final balance snapshot taken
   before that block ends. The ownership_violations==0 and
   lifecycle_failures==0 assertions are retained. This is a documented
   lowering behaviour, not a library defect.

A third, non-issue: a JSON root-size assertion initially expected 4 members;
the fixture has 5 (`a`, `b`, `c`, `nested`, `arr`). Corrected to 5.
