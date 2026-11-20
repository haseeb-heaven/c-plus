# Capability matrix

`Implemented` means code exists in the compiler/runtime. `Tested` means an
automated case exists. `Verified` means the behaviour was actually observed in
the repository's test/demo runs. "partial" is never counted as complete.

| feature | implemented | tested | verified | known limitations |
| --- | --- | --- | --- | --- |
| Memory safety (supported subset) | yes | yes | yes | no UAF detection, no pointer-arithmetic checks, no ASan on this host |
| Compile-time bad array index | yes | yes | yes | constant and simple counted loops only |
| Runtime bounds/null/alignment checks | yes | yes | yes | UBSan-based; aborts on first violation |
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
./tests/run_tests.sh                         # 32 tests
./demos/run_demos.sh                         # 14 demos
./benchmarks/run_bench.sh                    # performance table
```
