# Capability matrix

`Implemented` means code exists in the compiler/runtime. `Tested` means an
automated case exists. `Verified` means the behaviour was actually observed in
the repository's test/demo runs. "partial" is never counted as complete.

| feature | implemented | tested | verified | known limitations |
| --- | --- | --- | --- | --- |
| Memory safety (supported subset) | yes | yes | yes | general heap bounds, arbitrary aliases and complete temporal safety remain unproven; ASan is opt-in/backend-dependent |
| Compile-time bad array index | yes | yes | yes | constant and simple counted loops only |
| Compile-time null pointer dereference | yes | yes | yes | Clang AST constant-initializer tracking |
| Compile-time use-after-free (direct + simple alias) | yes | yes | yes | Clang AST release-state/alias tracking; branch-conservative |
| Runtime bounds/null/alignment checks | yes | yes | yes | UBSan-based; aborts on first violation |
| Return-stack-address / uninitialized checks | yes | yes | yes | Clang warnings promoted to errors |
| Pointer-leak / raw realloc / strict-aliasing checks | yes | yes | yes | token-level static pass |
| String-literal write / sizeof decay / format security | yes | yes | yes | Clang warnings promoted to errors |
| Unsequenced / shift / sign-compare checks | yes | yes | yes | Clang warnings promoted to errors |
| memcpy overlap / direct data-race checks | yes | yes | yes | token-level static pass (conservative) |
| Pointer arithmetic / pointer compare / signal / ctype / I/O checks | yes | yes | yes | static pass + promoted Clang warnings |
| setjmp/longjmp, NaN, VLA sizeof, macro, zero-alloc, signal-flag checks | yes | yes | yes | static pass + promoted Clang warnings |
| C23-specific unsafe constructs rejected / auto made qualifier-safe | yes | yes | yes | static pass + __typeof__ lowering |
| C2y-style defer / countof / if-declarations / 0o literals | partial | yes | yes | see docs/C2Y.md; no full C2y backend |
| fork/restrict/bit-field/fflush/VLA/defer-control checks | yes | yes | yes | static pass |
| Double/invalid release detection | yes | yes | yes | stale pointer after address reuse may be misattributed |
| RAII: scope exit / return / break / continue | yes | yes | yes | `goto`/`longjmp`/`exit` bypass cleanup |
| RAII reverse destruction order | yes | yes | yes | one owner per declaration |
| Ownership transfer (`cplus_move`, return) | yes | yes | yes | resource locals only |
| Structured borrow checking | partial | yes | yes | no aliases in containers/fields; no lifetime annotations |
| Classes: fields + methods | yes | yes | yes | no virtual dispatch, inheritance or access control |
| Class constructors | yes | yes | yes | no return type, no early return |
| Class destructors + RAII | yes | yes | yes | destructor must tolerate zeroed handle |
| Lightweight `class` sugar (`p.m()`) | yes | yes | yes | receiver class must be visible in the same TU |
| Managed allocation + `.len` | yes | yes | yes | `.len` folds when allocation visible; pointer args fall back to side table |
| Fixed array `.len` | yes | yes | yes | compile-time literal |
| C compatibility / C ABI / libc + POSIX | yes | yes | yes | C17 + GNU extensions; legacy C not safe |
| `.cp` / `.hp` extensions | yes | yes | yes | `.c`/`.h` still accepted; header lowering per TU |
| `.hp` standard library layer | yes | yes | yes | thin wrappers over libc/POSIX; not a libc rewrite |
| Native executable generation | yes | yes | yes | host Clang driver; macOS arm64 verified here |
| `--legacy-c` mode | yes | yes | yes | explicitly **not** memory safe |
| Format-free `print` / `scan` | yes | yes | yes | no struct reflection; printer state is not thread-safe |
| Default function arguments | yes | yes | yes | same translation unit only |
| Range-based `for` | yes | yes | yes | simple lvalue ranges with `.len` only |
| Safe concurrency (mutex + TLS + threads) | partial | yes | yes | no race detection; feature is experimental |
| Networking demo (sockets) | yes | yes | yes | uses existing POSIX socket API |
| Reproducible benchmarks | yes | yes | yes | 8 microbenchmarks, local machine only |
| GC / managed object graph | no | — | — | out of scope; scope-based allocator instead |
| Full Rust-style borrow checker | no | — | — | roadmap |
| Data-race detection / Sendable types | no | — | — | roadmap |
| Incremental build cache / dependency tracking | no | — | — | roadmap |
| Linux x86-64 / ARM64 targets verified | no | — | — | no Linux runner in this environment |
| Windows x86-64 target | no | — | — | roadmap |
| Production backend/database/game-engine frameworks | no | — | — | target workloads, not claims |

Commands that produced the "verified" column:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c+_lang
ctest --test-dir build --output-on-failure   # 2/2 suites
./tests/run_tests.sh                         # 100 tests
./demos/run_demos.sh                         # 16 demos
./benchmarks/run_bench.sh                    # performance table
```

## Agent 1 hardening status (branch `agent/core-safety`, commit pending)

### Implemented and verified compile-time checks
- Constant/loop/multidimensional array bounds; heap allocation bounds including
  `malloc`, `calloc(count,size)`, `realloc`, alias copies, explicit casts,
  conflicting call-site extents and heap-factory returns.
- Mandatory regression: `malloc(1); p[9]=42` is rejected in DEFAULT and STRICT
  with `[CPLUS-BOUNDS] array index 9 exceeds heap allocation 'p' of 1 bytes`.
- Runtime-valued signed add/sub/mul, division/remainder by zero, `INT_MIN/-1`,
  shift count/left-shift validity, narrowing conversions and pointer formation
  are enforced natively in generated C (no UBSan dependency).
- UAF/double-free for 10 named single-TU shapes, including cast aliases,
  helper-returned aliases, released pointers passed to callees and runtime-index
  elements; invalid free, realloc patterns, RAII/move/ownership diagnostics.

### Missing checks (not claimed)
- General heap-pointer bounds (name-keyed extents, last-write-wins across
  multiple call sites is now fail-closed but still not expression-keyed).
- Arbitrary alias lifetimes, indirect calls, integer round-trips, heap address
  reuse, threads and cross-TU ownership; complete temporal safety.
- Sound data races, container/field element lifetimes, full pointer provenance.

### Test results (exact commands and counts)
```
tests/run_tests.sh                     -> 109 passed, 0 failed
tests/core/run.sh                      -> 152 passed, 0 failed
ctest --test-dir build --output-on-failure -> 2/2
tests/compiler/agent1/run_all.sh --full   -> 159 total, 156 PASS, 0 FAIL, 3 SKIP
tests/compiler/agent1/run_bounds_matrix.sh -> 13 PASS, 0 FAIL
tests/compiler/agent1/run_native_matrix.sh -> 24 PASS, 0 FAIL, 0 BLOCKED
tests/metrics/uaf_evidence_run.py      -> 12/12 (10 static rejects, 2 safe controls)
CLANG_BIN=/usr/bin/clang tests/metrics/measure.py
  -> 90/90 safe DEFAULT and STRICT, 0/0 false positives, 97/107 STRICT detection
tests/compiler/agent1/perf_run.sh      -> no config regressed >2x
```

### False-positive cases found and fixed
- `quarantine_probe.c`: `cplus_check_live` was treated as a dereference by the
  released-argument rule; liveness queries are now exempted.
- STRICT 89/90 during that regression; restored to 90/90 with 0 FP.

### Known false negatives
Indirect calls, function-pointer frees, `uintptr_t` round-trips, arena/container
elements, cross-TU ownership, races beyond the direct-global heuristic, and
arithmetic in macro/header locations or wider than 64 bits.

### Performance
`tests/compiler/agent1/results_perf/summary.txt`: safe_release vs
legacy_c_release is 1.0x–1.5x across five programs; no >2x regression. Debug
is ~3x–31x slower than release, as expected for `-O0`.
