# C+

**Familiar C. Automatic safety analysis. Hardened libc+. Native performance.**

> **Goal:** make standard C simpler to keep safe: infer ownership, lifetimes, aliasing and buffer contracts automatically, diagnose dangerous code early, and ship a real hardened libc+ alongside the compiler. Preserve familiar C syntax and performance rather than recreate Rust's developer-facing borrow restrictions. Optional RAII, struct-backed classes, String and Vector stay opt-in.

**Status: experimental.** Compile-time safety is a goal, not a proven Rust-equivalent guarantee. Compatibility/diagnostic mode is not guaranteed memory-safe; strict safe mode must reject operations it cannot prove or soundly check. This README distinguishes the desired architecture from the compiler's current behavior. See [STATUS](docs/STATUS.md), [LIMITATIONS](docs/LIMITATIONS.md) and the [roadmap](docs/ROADMAP.md).

## What C+ ships

**C+ is one toolchain with two deliverables:**

1. **The C+ safety compiler (Agent 1):** automatic ownership/lifetime/alias/bounds analysis; low-friction C-centric diagnostics; opt-in RAII; strict fail-closed mode; default compatibility/diagnostic mode; Clang/LLVM optimization; unchanged ordinary C syntax.
2. **The C+ hardened standard library (Agent 2):** compiled and installable C standard-library implementations (initially allocation, memory, strings and I/O), compatible public headers, selected checked `*_s` APIs, machine-readable compiler contracts, opt-in logging, example programs, unit tests, fuzz targets and SDK packaging. Reuse appropriately licensed upstream implementations and preserve C ABI/observable C behavior.

The **bundled library is required as a project deliverable**, not merely wrappers around platform libc. Not all ISO C library functions or platforms are implemented yet. Interoperation with system libc remains necessary where fully replacing a platform runtime is unsafe/unsupported. `malloc_s` and related allocation `_s` APIs are **optional C+ extensions**, not standardized Microsoft/Apple CRT functions.

**No automatic magic:** passing a raw `char *` to `strcpy` does not reveal its allocated length to the library. The compiler must establish sound contracts and bounds, use proven dynamic enforcement, or reject the operation in strict safe mode. Full Rust-like safety for arbitrary unchanged C, universal zero overhead, and better-than-C performance are **not current guarantees**.

See [#21 — compiler](https://github.com/haseeb-heaven/c-plus/issues/21), [#22 — bundled hardened libc](https://github.com/haseeb-heaven/c-plus/issues/22), and [#23 — SDK verification](https://github.com/haseeb-heaven/c-plus/issues/23).

## Design rule: feel like C, not Rust

**No mandatory borrow annotations, lifetime syntax, ownership qualifiers, `defer`, `move`, or `*_s` function renaming.** Accept valid aliases when the compiler can prove their use safe; do not imitate Rust's specific borrow restrictions where a less restrictive sound proof works. Existing standard C remains syntactically valid, but **strict safe mode may reject unverified programs**. The regular compatibility/diagnostic mode flags bugs and warns about unverified hazards without promising Rust-grade safety. Automatic RAII must not silently change existing C cleanup behavior. The bundled **libc+** implementation and verified contracts complement, rather than replace, compiler analysis.

## The design in one picture

```text
                 EXISTING C SOURCE + OPTIONAL C+ FEATURES
                               |
                       C+ CLANG FRONTEND
                               |
              +----------------+----------------+
              |                                 |
       DEFAULT COMPATIBILITY               STRICT SAFE
       + DIAGNOSTICS                        MODE
       standard C accepted                 proof / sound check
       proven defects flagged              or reject
       unverified hazards warned           no silent unsafe pass
              |                                 |
              +----------------+----------------+
                               |
                   SHARED LIBC+ CONTRACTS
              ownership | bounds | lifetime | effects
                               |
                    CLANG / LLVM NATIVE CODE
                               |
                 HARDENED LIBC+ + PLATFORM CRT
              installed headers, libraries, native ABI
```

**Critical distinction:** Rust-comparable memory and thread safety is a **target for the strictly verified subset**, not a claim that every unchanged C program is safe. Normal/default mode prioritizes compatibility and usable diagnostics, not a universal safety guarantee. An unknown pointer lifetime or unverified FFI operation must be **rejected in safe mode or require an explicit, audited unsafe boundary**. Rust itself permits unsafe FFI and performs runtime bounds checks for dynamic indices.

## What makes C+ different?

| Project | Primary approach | Key difference from C+'s goal |
| --- | --- | --- |
| **C+ (this project)** | **Target:** inferred static ownership/lifetimes, optional RAII, familiar C syntax, bundled hardened libc+ and native ABI without Rust-style user-facing complexity | Not there yet. Prioritizes fail-closed safe mode, deterministic cleanup and no mandatory GC. |
| **[Xr0](https://xr0.dev/)** | Verification of existing C using contracts and static proofs | Focuses on verification rather than a modern C language and library layer; current loop/recursion coverage is limited. |
| **[SafeC](https://github.com/safec-org/SafeC)** | Separate C-evolution language claiming compile-time-first memory safety and zero-overhead design | **An overlapping goal, not proof C+ is unique.** Compare supported subset, compiler tests and overhead before claiming an advantage. |
| **[Fil-C](https://fil-c.org/how)** | Memory-safe C/C++ implementation using runtime capability checks and garbage collection | Aims to run much more existing C/C++ unchanged, but uses a different runtime/ABI and adds runtime cost. |
| **[Cforall](https://cforall.uwaterloo.ca/)** | Extends ISO C with modern type, productivity and RAII features | Strong C ergonomics and cleanup, but not an established Rust-style complete static borrow-checking guarantee. |
| **[C3](https://c3-lang.org/)** | Modern C-derived systems language with contracts and native libraries | Similar modern-C ergonomics; not equivalent to Rust's compile-time borrow checker by default. |

**We do not claim C+ is currently safer, faster or more complete than these projects.** The differentiator to *prove* is the combination of strict safe-mode ownership, familiar C interoperability and low runtime cost. Review upstream implementation evidence, not just design statements.

## Safety and performance contract

- **Compile-time first, C ergonomics:** infer pointer validity, ownership, aliases, lifetimes and safe resource transitions; accept demonstrably safe ordinary C aliases. Strict mode fails closed when a sound proof/check is unavailable; default compatibility mode warns but cannot claim guaranteed safety. Do not require Rust-style borrow syntax.
- **Safe existing libc:** Continue to use ordinary `malloc`, `free`, `memcpy`, `strcpy`, `fopen`, etc. Model their ownership, buffer sizes, failure and lifetime effects through verified contracts. No new required `malloc_s/free_s` API. **Ship an actual selectable hardened libc+ implementation**, with explicit platform CRT fallback and no unsafe system-libc replacement.
- **Opt-in RAII without a mandatory GC:** Resource types with defined cleanup contracts get deterministic destruction on supported exits; never silently insert cleanup into arbitrary legacy C if that changes behavior.
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
    fputs("Hello C+\\n", log.handle);
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

## Roadmap: simple C, two cooperating components

| Task | Owner | Links |
| --- | --- | --- |
| **Low-friction compiler safety** — infer ownership, aliases, lifetimes and bounds for ordinary C; default diagnostics + separately verified fail-closed strict mode; optional RAII | **Agent 1** | [Issue #21](https://github.com/haseeb-heaven/c-plus/issues/21) · [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| **Bundled hardened libc implementation** — real compiled stdlib functions, standard C headers, `*_s` checked APIs, compiler contracts, platform adapters, logging, examples, fuzzing and installable SDK | **Agent 2** | [Issue #22](https://github.com/haseeb-heaven/c-plus/issues/22) · [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |
| **Evidence & merge gate** — safety spec, CI, multi-TU, adversarial tests, performance and peer review | **Both** | [Issue #23](https://github.com/haseeb-heaven/c-plus/issues/23) |

First prove the compiler + hardened libc+ allocation/memory/string vertical slice on unchanged C; then expand the library and analysis. Modern standard-library feature expansion is **P1 / deferred**. Earlier issues were closed as superseded, **not completed**. No open PR should be treated as a completed safety guarantee.

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

## AI Usage Disclaimer

C+ was implemented with AI tools, including OpenAI Codex and Google Gemini. Heaven created and directed the language design, vision, syntax, feature requirements, and technical decisions. Heaven also manually tested the project and guided development. AI-generated code was reviewed and tested; AI assistance does not replace human authorship or responsibility.
