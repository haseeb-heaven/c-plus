# Per-method runtime test matrix

The runner `tests/methods/run_methods.sh` rebuilds both static libraries
(`build/libc/libcplus_hardened.a` and `build/safe/libcplus_safecrt.a`), then
compiles and runs each standalone C99 test in `tests/methods/` in the fixed
order below. Each test prints `methods_<group>: <N>/<N> checks ok` and exits 0
when all of its checks pass. Capture files (build log, run log, binary) live in
`build/methods/`, and each run is bounded by a 30-second timeout.

## Layers

- **SafeCRT** (`safecrt` variant): default build, links SafeCRT + libc. This is
  the checked/`_s` entry-point path.
- **Plain libc** (`plain` variant): built with `-DPLAIN_LIBC`, links libc only.
  This is the ordinary-C wrapper path. A group that does not define a
  `PLAIN_LIBC` path is reported as `n/a` in its plain column and is **not**
  counted as a failure. A source file that has not been authored yet is
  reported as `SKIP ... (file not present)` during parallel work and is also
  not counted as a failure.

Only variants that are actually exercised count toward the pass/fail totals;
`n/a` and `SKIP` lines do not.

## Matrix

| Method group | Source file | Layer(s) | What is checked (normal / boundary / zero / NULL / overflow / logging parity) | Evidence command | Current result |
|---|---|---|---|---|---|
| alloc | `tests/methods/methods_alloc.c` | SafeCRT, plain libc | malloc/calloc/realloc/free: normal allocation, zero-size requests, size-overflow (`SIZE_MAX`, `n*size`), boundary reuse, NULL/invalid pointers, refusal-vs-logging parity | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| memory | `tests/methods/methods_memory.c` | SafeCRT, plain libc | memset/memcpy/memmove/memcmp: exact fits, zero length, one-past-end boundary, NULL buffers, overlapping regions, destination-size overflow, refusal logging | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| string | `tests/methods/methods_string.c` | SafeCRT, plain libc | strcpy/strncpy/strcat/strlen/strnlen family: exact and empty strings, bounded truncation, embedded NUL, NULL pointers, overflow, constraint-handler/logging parity | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| stdio | `tests/methods/methods_stdio.c` | SafeCRT, plain libc | formatted I/O and fread/fwrite: normal read/write, zero-length transfers, size*count overflow, NULL buffers, truncation boundaries, error logging | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| conversion | `tests/methods/methods_conversion.c` | SafeCRT (libc-only entry points) | numeric/string conversion: normal values, min/max boundaries, zero, invalid input, overflow/errno, logging parity where a checked entry point exists | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| format | `tests/methods/methods_format.c` | SafeCRT, plain libc | sprintf_s/snprintf/vsnprintf family: normal formatting, exact-fit and truncated boundaries, zero width/precision, NULL output, overflow, constraint-handler/logging parity | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| wchar | `tests/methods/methods_wchar.c` | SafeCRT (libc-only entry points) | wide-string and multibyte routines: normal conversions, zero length, boundary/buffer limits, NULL pointers, overflow, logging parity where applicable | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| math_time | `tests/methods/methods_math_time.c` | SafeCRT (libc-only entry points) | math and time helpers: normal values, domain/range boundaries, zero and signed zero, NaN/Inf, overflow/errno, logging parity where applicable | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |
| logging | `tests/methods/methods_logging.c` | SafeCRT (libc-only entry points) | `cplus_log` and level filtering: normal emit, level boundaries, zero/NULL message handling, long-message overflow, cross-layer parity of emitted records | `sh tests/methods/run_methods.sh` | _pending — fill after run_ |

The "what is checked" column names the intended categories; the source file is
authoritative for the exact cases and counts. Groups listed as "libc-only entry
points" are compiled in the default (SafeCRT+libc) build but exercise libc
functions and therefore report `n/a` for the plain variant unless the source
also defines a `PLAIN_LIBC` path.

## Interpretation — scope and limits

- **These are runtime outcome tests, not a Rust-equivalence proof.** A passing
  row means the measured behaviour of this build matched the test's expectations
  (normal results, controlled refusal, or correct logging) on this host. It
  does not establish language-level memory safety or parity with Rust; see
  `CPLUS_RUST_CONFORMANCE.md` and `RUSTLIKE_METHOD_MATRIX.md` for the separate
  conformance discussion.
- **Raw-C UB cases are skipped/documented, not executed.** Operations that are
  undefined by ISO C (for example `strnlen(NULL, n)`, overlapping raw `memcpy`,
  or out-of-bounds pointer arithmetic) cannot be made safe by a library wrapper
  and are recorded as documented gaps rather than run in a way that would
  produce nondeterministic crashes. The runner reports only deterministic,
  defined outcomes.
- A `FAIL` line is a real failure of that group/variant (compile error, nonzero
  exit, timeout, or crash). `n/a` and `SKIP` lines are informational and never
  fail the suite.

## Refreshing results

After the per-method source files are in place:

```sh
sh -n tests/methods/run_methods.sh
sh tests/methods/run_methods.sh
```

Replace each `_pending — fill after run_` cell with the `ok`/`FAIL` line (or
`n/a` / `SKIP` note) produced for that group and variant, and record the final
`method tests: <P> passed, <F> failed` line with the compiler and host used.
