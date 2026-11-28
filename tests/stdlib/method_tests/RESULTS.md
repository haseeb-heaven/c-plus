# Consolidated results — per-method suites

All commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch
`agent/modern-stdlib`, with `CPLUS_CLANG=/usr/bin/clang` and `./build/c+`
(Apple clang 16). The runner uses a restricted PATH
(`$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin`) plus the one directory that
actually contains `pkg-config` (`/opt/homebrew/bin`) so the multimedia suite can
discover SDL3/FFmpeg flags; no alternate compiler is introduced because
`CPLUS_CLANG` is pinned to `/usr/bin/clang`.

## Headline

**518 methods invoked · 518 passed · 0 failed · 0 skipped.**

## Exact final run (stdout)

Command:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh -n tests/stdlib/method_tests/run_all.sh          # rc=0
timeout 1200 sh tests/stdlib/method_tests/run_all.sh 2>/tmp/run_final_err.txt
# exit 0
```

Exact stdout of that run:

```
| group | total methods | passed | failed | skipped |
|---|---:|---:|---:|---:|
| alloc_mem | 87 | 87 | 0 | 0 |
| collections | 63 | 63 | 0 | 0 |
| file_io | 28 | 28 | 0 | 0 |
| json_log_time | 56 | 56 | 0 | 0 |
| multimedia | 127 | 127 | 0 | 0 |
| net_http | 73 | 73 | 0 | 0 |
| str_vec | 47 | 47 | 0 | 0 |
| thread_async | 37 | 37 | 0 | 0 |
| **TOTAL (8 groups)** | **518** | **518** | **0** | **0** |

Note: SKIPPED methods are NOT passes; each needs an explicit reason in its group RUN_NOTES.md.
```

Exact final stderr line: `aggregate: ok (518 methods passed, 0 skipped)`.

Each group's final line (captured by the runner, not copied from documentation):

```
METHOD_TEST alloc_mem 87 87 0 0
METHOD_TEST str_vec 47 47 0 0
METHOD_TEST collections 63 63 0 0
METHOD_TEST file_io 28 28 0 0
METHOD_TEST thread_async 37 37 0 0
METHOD_TEST json_log_time 56 56 0 0
METHOD_TEST multimedia 127 127 0 0
METHOD_TEST net_http 73 73 0 0
```

These match the non-comment line counts of the eight sibling `methods.txt`
manifests exactly: 87+47+63+28+37+56+127+73 = **518**.

## SKIPPED is not a pass

A `skipped` count is **never** added to `passed`. A skipped method has not been
invoked or verified, so it cannot support a coverage claim. Any group reporting
`skipped > 0` must give a per-method reason in its `RUN_NOTES.md`, and the
aggregate runner flags incomplete reporting. In this run **skipped = 0 for
every group**, so all 518 reported methods were actually executed with their
results verified at runtime.

## Runner behaviour / reproduction

- Discovers a group by its `<group>/RUN_NOTES.md`.
- Uses the group's `run.sh`/`run_all.sh`/`compare.sh` when present (str_vec,
  net_http, multimedia); otherwise it **rebuilds and runs every non-helper
  `*.cp`** in the group (`./build/c+ build … --strict --sanitize=none`, with
  `-Ithird_party/yyjson -Lbuild/ext -lyyjson` for json sources and
  `-I/opt/homebrew/include -L/opt/homebrew/lib -luv` for async sources), then
  derives the counts from the programs' own `PASS`/`FAIL` lines. No count is
  taken from a documented final line; nothing is fabricated.
- Every group runs under `timeout 900` (multimedia performs seven builds).
- Continues after a failure; exits nonzero on any real FAIL, timeout, build
  error, malformed final line, or missing group.

Authoritative API-coverage inventory (static call-site scan, exit 0):

```sh
sh tests/stdlib/run_coverage.sh 2>&1
# headers=24 covered=411 total=414; worst 87.0%
```

Per-group build/run commands are recorded verbatim in each group's
`RUN_NOTES.md`; the groups that ship a script can be reproduced directly with:

```sh
sh tests/stdlib/method_tests/str_vec/run.sh          # 47/47
sh tests/stdlib/method_tests/net_http/run_all.sh     # 73/73
sh tests/stdlib/method_tests/multimedia/run_all.sh   # 127/127
```

## Reconciliation with the 411/414 coverage figure

The 518 verified method cases are **not** the same accounting unit as the 411
(or 414) entries enumerated by `run_coverage.sh`; see `METHODS_AUDIT.md` §5.
The authoritative parser skips every `stdlib_*` name, omits two headers
entirely, and uses single-line signature regexes, while the sibling manifests
also count constructors, `void` members and additive free helpers. No bare
"100% of 411" claim is made: the verified statement is **518/518 method cases
passed, 0 skipped**, alongside the authoritative tool's unchanged **411/414**
static call-site result.
