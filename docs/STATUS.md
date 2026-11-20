# Capability matrix

`Implemented` means code exists in the compiler/runtime. `Tested` means an
automated case exists. `Verified` means the behaviour was actually observed in
the repository's test/demo runs. "partial" is never counted as complete.

| feature | implemented | tested | verified | known limitations |
| --- | --- | --- | --- | --- |
| Memory safety (supported subset) | yes | yes | yes | no UAF detection, no pointer-arithmetic checks, no ASan on this host |
| Compile-time bad array index | yes | yes | yes | constant and simple counted loops only |
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
