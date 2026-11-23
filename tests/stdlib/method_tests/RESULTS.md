# Consolidated results — per-method suites

All commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch
`agent/modern-stdlib`, with `CPLUS_CLANG=/usr/bin/clang`, `./build/c+`
(Apple clang 16) and a restricted `PATH`
(`$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin`).

## Headline

**518 methods invoked · 518 passed · 0 failed · 0 skipped.**

The numbers below are the exact final `METHOD_TEST <group> <total> <passed>
<failed> <skipped>` lines verified by the coordinating parent session, one per
sibling group. They agree with the non-comment line count of every sibling
`methods.txt` (87+47+63+28+37+56+127+73 = 518).

## Consolidated table

| group | total methods | passed | failed | skipped | final line |
|---|---:|---:|---:|---:|---|
| `alloc_mem` | 87 | 87 | 0 | 0 | `METHOD_TEST alloc_mem 87 87 0 0` |
| `str_vec` | 47 | 47 | 0 | 0 | `METHOD_TEST str_vec 47 47 0 0` |
| `collections` | 63 | 63 | 0 | 0 | `METHOD_TEST collections 63 63 0 0` |
| `file_io` | 28 | 28 | 0 | 0 | `METHOD_TEST file_io 28 28 0 0` |
| `thread_async` | 37 | 37 | 0 | 0 | `METHOD_TEST thread_async 37 37 0 0` |
| `json_log_time` | 56 | 56 | 0 | 0 | `METHOD_TEST json_log_time 56 56 0 0` |
| `multimedia` | 127 | 127 | 0 | 0 | `METHOD_TEST multimedia 127 127 0 0` |
| `net_http` | 73 | 73 | 0 | 0 | `METHOD_TEST net_http 73 73 0 0` |
| **TOTAL (8 groups)** | **518** | **518** | **0** | **0** | — |

## SKIPPED is not a pass

A `skipped` count is **not** added to `passed`. A skipped method has not been
invoked or verified, so it cannot support a coverage claim. Any group reporting
a non-zero `skipped` value must give a per-method reason in its `RUN_NOTES.md`
(e.g. an unavailable external facility), and the aggregate runner treats the
group as incomplete. In this run **skipped = 0 for every group**, so all 518
reported methods were actually executed and their results verified.

## Exact reproduction commands

Aggregate runner (restricted PATH, `CPLUS_CLANG=/usr/bin/clang`, `timeout 300`
per group, continues after a failure, exits nonzero on any real FAIL):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/stdlib/method_tests/run_all.sh 2>&1
```

Authoritative API-coverage inventory (static call-site scan, exit 0):

```sh
sh tests/stdlib/run_coverage.sh 2>&1
# headers=24 covered=411 total=414; worst 87.0%
```

Per-group commands (verbatim forms recorded in each group's `RUN_NOTES.md`;
each group's notes also record build logs, run logs and determinism repeats):

```sh
# alloc_mem — 87
timeout 30 ./build/c+ build tests/stdlib/method_tests/alloc_mem/all_methods.cp \
    --strict --sanitize=none \
    -o tests/stdlib/method_tests/alloc_mem/bin/all_methods
timeout 30 tests/stdlib/method_tests/alloc_mem/bin/all_methods | tail -1

# str_vec — 47
sh tests/stdlib/method_tests/str_vec/run.sh | tail -1

# collections — 63
./build/c+ build tests/stdlib/method_tests/collections/collections_methods.cp \
    --strict --sanitize=none \
    -o tests/stdlib/method_tests/collections/collections_methods
timeout 30 tests/stdlib/method_tests/collections/collections_methods | tail -1

# file_io — 28 (three programs: 13 + 7 + 8)
for n in file io fs; do
  timeout 30 ./build/c+ build tests/stdlib/method_tests/file_io/test_$n.cp \
      --strict --sanitize=none \
      -o tests/stdlib/method_tests/file_io/bin/$n
  timeout 30 tests/stdlib/method_tests/file_io/bin/$n
done

# thread_async — 37 (21 thread + 16 async)
./build/c+ build tests/stdlib/method_tests/thread_async/test_thread_methods.cp \
    --strict --sanitize=none -o build/thread_method_test
timeout 30 build/thread_method_test
./build/c+ build tests/stdlib/method_tests/thread_async/test_async_methods.cp \
    --strict --sanitize=none -I/opt/homebrew/include -L/opt/homebrew/lib -luv \
    -o build/async_method_test
timeout 30 build/async_method_test

# json_log_time — 56 (34 json + 16 log + 6 time)
./build/c+ build tests/stdlib/method_tests/json_log_time/json_methods.cp \
    --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o tests/stdlib/method_tests/json_log_time/json_methods
timeout 30 ./tests/stdlib/method_tests/json_log_time/json_methods
./build/c+ build tests/stdlib/method_tests/json_log_time/log_methods.cp \
    --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o tests/stdlib/method_tests/json_log_time/log_methods
timeout 30 ./tests/stdlib/method_tests/json_log_time/log_methods
./build/c+ build tests/stdlib/method_tests/json_log_time/time_methods.cp \
    --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o tests/stdlib/method_tests/json_log_time/time_methods
timeout 30 ./tests/stdlib/method_tests/json_log_time/time_methods

# multimedia — 127
sh tests/stdlib/method_tests/multimedia/run_all.sh | tail -1

# net_http — 73
sh tests/stdlib/method_tests/net_http/run_all.sh | tail -1
```

## Reconciliation with the 411/414 coverage figure

The 518 method cases are **not** the same unit of accounting as the 411 (or
414) entries enumerated by `run_coverage.sh`; see `METHODS_AUDIT.md` §5. In
short: the authoritative parser skips every `stdlib_*` name, omits two headers
entirely, and uses single-line signature regexes, while the sibling manifests
also count constructors, `void` members and additive free helpers. No bare
"100% of 411" claim is made here: the verified statement is **518/518 method
cases passed, 0 skipped**, alongside the authoritative tool's unchanged
**411/414** static call-site result.
