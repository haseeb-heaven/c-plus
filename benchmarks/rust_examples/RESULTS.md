# Rust safe-library examples — aggregate results

This file records the **exact output of a real run** of `run_all.sh` over every
`<group>/compare.sh` discovered under `benchmarks/rust_examples/`. No numbers
below are synthesized.

## Toolchains (pinned, exact versions)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

CMD: timeout --help | head -1
Usage: timeout [-f | --foreground] [-k time | --kill-after time] [-p | --preserve-status] [-s signal | --signal signal]  [-v | --verbose] <duration> <command> [arg ...]

CMD: uname -m
arm64
```

`run_all.sh` forces `CPLUS_CLANG=/usr/bin/clang` (Apple clang 16, **not** the
Homebrew clang that is first on the caller's PATH) and runs each group with a
restricted PATH. The PATH used for the recorded run was the runner's default:

```
/usr/bin:/bin:/Users/haseeb-mir/.cargo/bin:/Library/Frameworks/Python.framework/Versions/3.12/bin:/usr/local/bin:/usr/bin
```

Each `compare.sh` was invoked as `timeout 300 <group>/compare.sh` with
`CPLUS_CLANG=/usr/bin/clang` and `CLANG=/usr/bin/clang` exported. Individual
group harnesses use their own shorter internal timeouts (typically 30 s).

## Reproduction commands

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/rust_examples/run_all.sh
```

Optional overrides:
`RUST_EXAMPLES_PATH` (child PATH), `RUST_EXAMPLES_LOGDIR` (log destination),
`TIMEOUT` (timeout binary).

Exact symbol-surface enumeration used by `ALL_APIS.md`:

```sh
nm -g build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
nm -g --defined-only build/safe/libcplus_safecrt.a | awk '$2=="T"{print $3}' | sort -u | grep '^_cplus_' | wc -l   # 167
nm -g --defined-only build/libc/libcplus_hardened.a  | awk '$2=="T"{print $3}' | sort -u | grep '^_cplus_' | wc -l   # 783 at task baseline; 805 after a parallel rebuild
```

A parallel agent rebuilt `build/libc/libcplus_hardened.a` mid-session (mtime
11:13 local), adding 22 exported `_cplus_*` symbols (including
`_cplus_mtx_destroy_checked`, `_cplus_cnd_destroy_checked`,
`_cplus_thrd_join_checked`, `_cplus_alloc_checked_free`,
`_cplus_violation_log_*`, and C23 math wrappers). The full delta is listed in
`ALL_APIS.md`. The table below was re-run **after** that rebuild and is
byte-identical to the pre-rebuild run; the complete current 972-symbol surface
(167 + 805) is enumerated in `ALL_APIS.md`.

## Real run — captured final lines (verbatim)

```
RUST_EXAMPLES alloc 21 77 53 0 24
RUST_EXAMPLES format 15 10 58 0 0
RUST_EXAMPLES io 13 43 43 0 0
RUST_EXAMPLES json_log 8 6 18 0 0
RUST_EXAMPLES runtime_diag 10 43 43 0 2
RUST_EXAMPLES safecrt_memstr 21 126 126 0
RUST_EXAMPLES search 7 55 55 0 1
RUST_EXAMPLES threading 10 10 8 0 2
RUST_EXAMPLES vendored_v 26 6 156 0
```

## Real run — aggregate table (exact)

```
group             apis  cases  matched  mismatched  blocked status          exit
------            ----  -----  -------  ----------  ------- ------          ----
alloc               21     77       53           0       24 PASS_WITH_BLOCKS     0
format              15     10       58           0        0 PASS               0
io                  13     43       43           0        0 PASS               0
json_log             8      6       18           0        0 PASS               0
runtime_diag        10     43       43           0        2 PASS_WITH_BLOCKS     0
safecrt_memstr      21    126      126           0        - BAD_REPORT         0
search               7     55       55           0        1 PASS_WITH_BLOCKS     0
threading           10     10        8           0        2 PASS_WITH_BLOCKS     0
vendored_v          26      6      156           0        - BAD_REPORT         0
TOTAL              131    376      560           0       29 FAIL_GROUPS=2      -
```

Runner summary lines from the same run:

```
groups run: 9 | blocked (reported, NOT passes): 29 | real mismatches: 0 | failing groups: 2
note: PASS_WITH_BLOCKS is a partial result -- blocked cases are reported and are NOT counted as passes.
RESULT: FAIL
```

Runner exit status: **1**.

### Headline counts

- **9 groups discovered and run** (all completed; the runner continued past every failure).
- **0 real mismatches** across all groups; **0 build failures** observed.
- **29 BLOCKED cases** reported — reported, **never counted as passes**. GROUP
  statuses: 5 `PASS_WITH_BLOCKS`, 2 `PASS`, 2 `BAD_REPORT`.
- Runner exits nonzero because two groups did **not** emit the required final
  line. There were no behavioral mismatches.

## Caveats that must be read with the table

1. **Two reporting-contract violations (`BAD_REPORT`).** The required final line
   is exactly `RUST_EXAMPLES <group> <apis> <cases> <matched> <mismatched> <blocked>`.
   `safecrt_memstr/compare.sh:157` and `vendored_v/compare.sh:160` print only
   five payload fields (no `<blocked>`), e.g.
   `RUST_EXAMPLES safecrt_memstr 21 126 126 0` and
   `RUST_EXAMPLES vendored_v 26 6 156 0`. The runner captures these verbatim,
   displays `-` in the `blocked` column, marks the group `BAD_REPORT`, and fails
   the overall run. Their zero `<mismatched>` fields are still recorded as zero —
   no behavioral failure is being hidden.
2. **Inconsistent `cases` semantics in two groups.** `format` reports
   `cases=10, matched=58` and `json_log` reports `cases=6, matched=18`: their
   `cases` is a count of *distinct case names* while `matched` is a count of
   *emitted token lines*. The runner emits an informational `WARN` for these;
   it is **not** treated as a mismatch. Other groups use `cases` as total
   emitted cases (sometimes including BLOCKED, sometimes excluding it).
   Compare raw streams inside each group's own `out/`/`results/` directory
   before drawing cross-group conclusions.
3. A `PASS` or `PASS_WITH_BLOCKS` status means only that the C-checked-API
   harness and the safe-Rust example produced matching outcome tokens for the
   cases they both emitted. See the non-equivalence statement below.

## Non-equivalence of outcome tokens

**Matching outcome tokens are NOT equivalent safety guarantees.** Each C
harness observes a checked library's runtime outcome (return code / refusal
token) for a specific input; each Rust example uses safe idioms (slices,
`Option`, compile-time bounds checks, no `unsafe`, no FFI) whose token is made
to match that observation. A token match demonstrates that the same input class
is *recognised*, not that the two implementations provide the same memory-safety
guarantees. In particular:

- Rust makes null pointers and out-of-range lengths unrepresentable, so C
  refusal tokens are modelled with guards/`Option` rather than by reproducing
  the C handler/constraint machinery.
- A C refusal token does not imply the Rust code needed a run-time guard at all.
- The C archives' diagnostics, handlers, and global state are not reproduced by
  the Rust examples; `BLOCKED` cases are exactly where a correspondence could
  not be established without fabricating a result.

Treat every MATCH as a behavioural correspondence for the enumerated case set
only. The full symbol-level accounting is in `ALL_APIS.md`; it explicitly does
not claim 100% API coverage.
