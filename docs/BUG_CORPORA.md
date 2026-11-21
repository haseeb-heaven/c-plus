# User-provided bug corpora

Two sets of 52 memory-bug examples are kept as reproducible corpora:

- `tests/metrics/bugs52/` — first set (UAF, double/invalid free, bounds, RAII,
  ownership, libc contracts). Run with `python3 tests/metrics/check_52.py`.
- `tests/metrics/bugs52b/` — second set (dangling structures, threads,
  provenance, allocators, variadics, tagged pointers, cleanup attributes).
- `tests/metrics/compile_time_bug_catalog.md` — 100-entry catalog with
  detected/gap status.

Current results (DEFAULT / STRICT):

| corpus | rejected | remaining gaps |
| --- | --- | --- |
| bugs52 | 37 / 39 of 52 | leaks on exceptional paths, scope escapes, field ownership, uninitialized heap reads, container/arena cases |
| bugs52b | 14 / 20 of 52 | TOCTOU/data races, deep recursion, mmap/file-size faults, dynamic-library lifetime, variadic ABI, tagged pointers, cleanup attributes |

## Honest scope

Many examples are **not statically provable without rejecting valid C**:
TOCTOU races between threads, `setenv` invalidating `getenv` pointers,
recursion depth, `mmap`/SIGBUS from file truncation, environment lifetime,
tagged pointers, deep-graph cycles, and format-string ABI mismatches are
runtime or platform properties. Returning a pointer to a `static` buffer,
relational comparison of unrelated pointers, and `restrict` overlap are legal
C constructs that are only bugs in context.

The compiler rejects the soundly provable subset, accepts valid C with
**0 false positives** on the 83-program safe corpus, and documents the rest as
gaps rather than claiming false guarantees. Leak and possible-null FILE*
findings are warnings (not errors) because the linear plain-C scan cannot
prove them on every path.
