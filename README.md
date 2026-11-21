# C+

**Modern C. Rust-grade safety by design. C ABI. Native performance.**

> **Goal:** extend familiar C with **Rust-like compile-time ownership and borrowing**, **C++-style deterministic RAII**, and **optional modern syntax** such as lightweight classes, String and Vector—without requiring C++ inheritance, a garbage collector or a replacement libc.

**Status: experimental.** The full Rust-grade safe-subset guarantee is **not implemented or independently verified yet**. This README distinguishes the desired architecture from the compiler's current behavior. See [STATUS](docs/STATUS.md), [LIMITATIONS](docs/LIMITATIONS.md) and the [roadmap](docs/ROADMAP.md).

## What C+ ships

**C+ is one toolchain with two deliverables:**

1. **The C+ safety compiler (Agent 1):** compile-time ownership, borrowing, alias and lifetime validation; RAII; fail-closed safe mode; Clang/LLVM optimization; unchanged ordinary C syntax.
2. **The C+ hardened standard library (Agent 2):** compiled and installable C standard-library implementations (initially allocation, memory, strings and I/O), compatible public headers, selected checked `*_s` APIs, machine-readable compiler contracts, opt-in logging, example programs, unit tests, fuzz targets and SDK packaging. Reuse appropriately licensed upstream implementations and preserve C ABI/observable C behavior.

The **bundled library is required as a project deliverable**, not merely wrappers around platform libc. Not all ISO C library functions or platforms are implemented yet. Interoperation with system libc remains necessary where fully replacing a platform runtime is unsafe/unsupported. `malloc_s` and related allocation `_s` APIs are **optional C+ extensions**, not standardized Microsoft/Apple CRT functions.

**No automatic magic:** passing a raw `char *` to `strcpy` does not reveal its allocated length to the library. The compiler must establish sound contracts and bounds, use proven dynamic enforcement, or reject the operation in strict safe mode. Full Rust-like safety for arbitrary unchanged C, universal zero overhead, and better-than-C performance are **not current guarantees**.

See [#21 — compiler](https://github.com/haseeb-heaven/c-plus/issues/21), [#22 — bundled hardened libc](https://github.com/haseeb-heaven/c-plus/issues/22), and [#23 — SDK verification](https://github.com/haseeb-heaven/c-plus/issues/23).

## The design in one picture

```text
                    FAMILIAR C + OPTIONAL MODERN C+
                   .c / .h          .cp / .hp
                          |
            C+ compiler: Clang AST/CFG + lowering
                  |                      |
      SAFE MODE (default)         LEGACY / UNSAFE C
      ownership + borrowing       compatible C/FFI;
      lifetimes + aliasing        no safety guarantee
      RAII + bounds contracts
      reject unproven hazards
                  |                      |
                  +----------+-----------+
                             |
                        Clang / LLVM
                             |
                     optimized native code
                             |
            system libc / native C ABI / C libraries
```

**Critical distinction:** the target is **Rust-comparable memory and thread safety for an explicitly defined safe subset**, not mathematical proof that arbitrary, unchanged C programs are safe. An unknown pointer lifetime or unverified FFI operation must be **rejected in safe mode or require an explicit, audited unsafe boundary**. Rust itself permits unsafe FFI and performs runtime bounds checks for dynamic indices.

## What makes C+ different?

| Project | Primary approach | Key difference from C+'s goal |
| --- | --- | --- |
| **C+ (this project)** | **Target:** Rust-like *static* ownership/borrowing + RAII + C-style syntax + optional classes/Vector + native C ABI | Not there yet. Prioritizes fail-closed safe mode, deterministic cleanup and no mandatory GC. |
| **[Xr0](https://xr0.dev/)** | Verification of existing C using contracts and static proofs | Focuses on verification rather than a modern C language and library layer; current loop/recursion coverage is limited. |
| **[SafeC](https://github.com/safec-org/SafeC)** | Separate C-evolution language claiming compile-time-first memory safety and zero-overhead design | **An overlapping goal, not proof C+ is unique.** Compare supported subset, compiler tests and overhead before claiming an advantage. |
| **[Fil-C](https://fil-c.org/how)** | Memory-safe C/C++ implementation using runtime capability checks and garbage collection | Aims to run much more existing C/C++ unchanged, but uses a different runtime/ABI and adds runtime cost. |
| **[Cforall](https://cforall.uwaterloo.ca/)** | Extends ISO C with modern type, productivity and RAII features | Strong C ergonomics and cleanup, but not an established Rust-style complete static borrow-checking guarantee. |
| **[C3](https://c3-lang.org/)** | Modern C-derived systems language with contracts and native libraries | Similar modern-C ergonomics; not equivalent to Rust's compile-time borrow checker by default. |

**We do not claim C+ is currently safer, faster or more complete than these projects.** The differentiator to *prove* is the combination of strict safe-mode ownership, familiar C interoperability and low runtime cost. Review upstream implementation evidence, not just design statements.

## Safety and performance contract

- **Compile-time first:** Enforce unique ownership, moves, borrowed lifetimes, exclusive mutation, alias invalidation, safe thread transfer and deterministic resource cleanup. If proof is unavailable, **fail closed** in safe mode or require an explicit unsafe boundary.
- **Safe existing libc:** Continue to use ordinary `malloc`, `free`, `memcpy`, `strcpy`, `fopen`, etc. Model their ownership, buffer sizes, failure and lifetime effects through verified contracts. No new required `malloc_s/free_s` API and no libc fork.
- **RAII without a mandatory GC:** Owned resources receive cleanup on supported scope exits; nonlocal control flow that defeats safe cleanup must be rejected or marked unsafe.
- **Near-zero *incremental* overhead where proven:** Static checks and RAII lowering do not inherently require per-access runtime metadata. **Zero runtime overhead is an optimization goal for statically proven cases, not a blanket guarantee.** Dynamic bounds checks, managed allocation bookkeeping and safety instrumentation can cost CPU/memory. Rust also performs dynamic bounds checks.
- **Native ABI:** Preserve C layouts/calling conventions at supported interfaces; external code remains an explicit trust/contract boundary. A native C ABI alone does not make FFI memory-safe.
- **No general C++ frontend:** C+ accepts a C-compatible subset with extensions; it does **not** compile arbitrary C++20/23 code.

## Modern C features (opt-in)

| Feature | C+ model | Current status |
| --- | --- | --- |
| **Classes/methods** | Struct-backed methods and `this->`; no C++ inheritance or virtual machinery | Implemented subset; multi-TU integration still under review |
| **RAII** | `resource struct` with `T_drop(T*)`; compiler inserts cleanup | Implemented subset; control-flow gaps remain |
| **Ownership/borrowing** | Move-only owners, checked borrows and safe lifetime rules | Partial; broad alias/field/container/cross-function verification is P0 |
| **String / Vector / collections** | Optional owned/borrowed native library types | In development on [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |
| **Optional libraries** | File, Result, JSON, logging, GUI, HTTP, async, audio/video, etc. | Experimental, feature expansion deferred until safety milestone passes |

### Example: familiar C + RAII

```c
#include <stdio.h>

resource struct File {
    FILE *handle;
};

void File_drop(File *f) {
    if (f->handle) fclose(f->handle);
}

int main(void) {
    File log = {fopen("log.txt", "w")};
    if (!log.handle) return 1;
    fputs("Hello C+\n", log.handle);
    return 0; // destructor inserted on supported scope exit
}
```

### Example: lightweight C+ class

```c
class Counter {
    int value;

    void add(int n) {
        this->value += n;
    }
};

Counter c = {0};
c.add(5);
```

Classes lower to C structs and functions. String/Vector APIs live in optional `.hp` library modules, **not** in a rewritten libc; check [modern stdlib docs](docs/MODERN_STDLIB.md) and PR #17 for exact supported syntax.

### Target rejection: borrowed alias after free

```c
int *p = malloc(sizeof(int));
if (!p) return 1;
int *alias = p;
free(p);
*alias = 42;  // TARGET: safe-mode compile error
```

**This is a target diagnostic, NOT an example of a fully working guarantee today.** Safe mode must eventually track ownership across aliases, calls, containers and threads; current implementation still has gaps. Where the compiler cannot establish safety, reject or require an explicit unsafe boundary.

## Quick start

Requirements: CMake, a C++20 compiler, LLVM/Clang development packages.

```sh
cmake -S . -B build
cmake --build build --target c+ -- -j4

./build/c+ build demos/A_hello.c -o hello
./hello
```

| Command | Purpose |
| --- | --- |
| `./build/c+ build file.c -o app` | Compile native executable |
| `./build/c+ run file.c -o app` | Build and execute |
| `./build/c+ check file.c` | Parse/check without linking |
| `./build/c+ test` | Automated tests |
| `./build/c+ demos` | Demo suite |
| `./build/c+ bench` | Benchmark suite |
| `--release` | Optimized build; existing checks retained |
| `--legacy-c` | Compatibility path, **not memory-safe** |
| `--emit-c FILE` | Inspect lowered C code |

## Roadmap: one P0 safety milestone, two agents

| Task | Owner | Links |
| --- | --- | --- |
| **Sound safe-mode compiler** — ownership, borrows, alias/field/loop/cross-TU lifetimes, RAII, concurrency and fail-closed FFI | **Agent 1** | [Issue #21](https://github.com/haseeb-heaven/c-plus/issues/21) · [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| **Bundled hardened libc implementation** — real compiled stdlib functions, standard C headers, `*_s` checked APIs, compiler contracts, platform adapters, logging, examples, fuzzing and installable SDK | **Agent 2** | [Issue #22](https://github.com/haseeb-heaven/c-plus/issues/22) · [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |
| **Evidence & merge gate** — safety spec, CI, multi-TU, adversarial tests, performance and peer review | **Both** | [Issue #23](https://github.com/haseeb-heaven/c-plus/issues/23) |

Modern standard-library feature expansion is **P1 / deferred**. Earlier issues were closed as superseded, **not completed**. No open PR should be treated as a completed safety guarantee.

## Verify it

```sh
./tests/run_tests.sh
./demos/run_demos.sh
./benchmarks/run_bench.sh
```

The project must publish actual passing and failing tests, explicit unsupported safe operations, runtime-check overhead, and comparisons against equivalently optimized Clang C and Rust. Test counts and runtime line coverage **are not a percentage of memory safety**.

See [STATUS](docs/STATUS.md), [LIMITATIONS](docs/LIMITATIONS.md), [ARCHITECTURE](docs/ARCHITECTURE.md), [ROADMAP](docs/ROADMAP.md).

## Contributing and license

Contributions: [CONTRIBUTING.md](CONTRIBUTING.md). License: [LICENSE](LICENSE).
