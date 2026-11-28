# Memory model gate

`sh tests/memory_model/run.sh` is a shell-only aggregate of the evidence that
separates two allocation models. It runs three sub-gates and prints one line:

```text
SCOPED_MEM_OK SCOPED_MEM_TEST 8 8 0
BENCH_SCOPED_OK BENCH_SCOPED 4 4 0
GOOSE_ARENA_OK PHASE arena <checksum>
MEMORY_MODEL_TEST 3 3 0
```

The gate exits nonzero if any case fails. Logs land in `build/memory_model/`.
It needs the same untracked build artifacts as the other scoped-memory gates:
`build-alpha/c+`, `build/c+` and `build/libc/libcplus_hardened.a`, plus
`goose` on `PATH`.

## What the three cases prove

1. **C+ datastack / scoped memory, no per-object heap on hot paths.** The
   `SCOPED_MEM_TEST` runner builds and executes the facade over the VM bump
   stack, arena and slot pool: one-store rewind, debug 0xDD poison, release
   one-store semantics, chained spill blocks, cross-TU generation uniqueness,
   16-byte alignment and stale-view retirement. It is invoked with
   `CPLUS_BIN=build-alpha/c+` so the alpha compiler is the one under test.
2. **Bump versus checked heap, measured end to end.** The bump benchmark
   verifier checks the fast bucket, the one-store release, the >=2x loop ratio
   and `BENCH_OK`; the absolute heap-time bucket is deliberately not gated
   because wall-clock varies with host load.
3. **Goose arena phase.** `benchmarks/six_way/portable.goose` is copied to
   `/tmp` with only its scale literal rewritten from 100 to 1 (sed, no
   Python), then `goose run` must emit `PHASE arena` followed by an exact
   decimal checksum. This is outcome/correctness evidence, not a timing race.

## Two categories, not one ranking

| Category | Model | Examples here |
|---|---|---|
| Scoped datastack (no heap) | Stack/arena/pool regions reserved up front; allocation is a cursor bump; release is a single store; views are retired at scope exit. No per-object `malloc`/`free` in the hot loop. | C+ `stdlib/datastack.hp`, `stdlib/arena.hp`, `stdlib/scoped_mem.hp`; Goose arena loop in `portable.goose`; the bump arms of both benchmark runners. |
| Dynamic heap | Each live object owns an allocation from the general allocator, with per-object allocate/free bookkeeping and lifetime managed by the owner (RAII/drop/GC-free explicit ownership). | checked C+ heap and the heap arms of the benchmark; C `malloc`/`free`; Rust `Box`/`Vec` (RAII drops); Zig allocator calls; Goose `[>..]` growth on the dynamic-heap path. |

These are allocation *models*, not language speed rankings. C+ deliberately
keeps a checked heap as the ABI backing allocator: "no heap" is an opt-in
source policy (`CPLUS_NO_HEAP`) for hot paths, not a global guarantee. See
`docs/stdlib/LIBRARY_MEMORY_MODEL_AUDIT.md` for the full boundary.

## Measured allocation latency (ns/op)

Reported from the committed benchmark evidence in `proof/benchmarks/README.md`:

| Allocation path | ns/op | Source |
|---|---:|---|
| C+ scoped bump | 30 | `BENCH bump_ns_per_op` |
| C+ checked heap | 328 | `BENCH heap_ns_per_op` (~10.9x slower) |
| Goose bump / arena | 61 | Goose allocation evidence pair |
| Goose dynamic heap | 163 | Goose allocation evidence pair (~2.7x slower) |

These are single-host, workload-specific figures; the deterministic gates
assert bucketed verdicts and exact checksums rather than raw wall-clock
thresholds. No equivalence with Rust, Zig, Fil-C or TrapC is claimed.
