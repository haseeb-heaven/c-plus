# C+

## Goal: modern, safer C without abandoning C

> **Cake-style static ownership + C++-style deterministic RAII + Apple-style bounds enforcement + Clang optimization, while retaining existing C source compatibility and the native C ABI.**

> **P0 non-negotiable safety target (not yet achieved):** In C+ safe mode, accepted code in a clearly specified **safe subset** must meet Rust-comparable compile-time ownership, borrow/lifetime and concurrency safety. Unproven memory operations must **fail closed** or be explicitly marked unsafe/FFI; sanitizer warnings/traps alone are not Rust-level proof. Dynamic bounds checks are permitted, as in Rust. Unrestricted legacy C remains a separate compatibility/unsafe mode. Native C ABI does not imply all raw C pointer operations can be safe. See [#1](https://github.com/haseeb-heaven/c-plus/issues/1) and [ROADMAP](docs/ROADMAP.md).

C+ is an **experimental** compiler extension for C. The goal is to make existing C projects easier to maintain and progressively safer: lightweight struct-backed classes, reliable automatic cleanup, ownership/borrowing, checked memory operations and efficient native code. Standard libraries wrap **existing** libc/platform/C libraries; C+ does not replace libc, glibc, Clang, or the C ABI. Real C++ source support is a separate, future frontend concern—not a current compatibility claim.

**This is the design target, not a claim that full memory safety has been achieved.** The current safe subset, verified functionality, unsupported operations and performance costs are documented in [STATUS](docs/STATUS.md), [LIMITATIONS](docs/LIMITATIONS.md) and [the core-safety issue](https://github.com/haseeb-heaven/c-plus/issues/1). Ordinary legacy C can remain usable in compatibility mode, which is **not** memory-safe by default.

### Proven foundations to reuse

| Capability | Project / source | C+ integration goal |
| --- | --- | --- |
| Ownership, borrowed views, nullability | [Cake](https://github.com/thradams/cake) | Flow-sensitive static ownership using Clang AST/CFG |
| Compiler, native optimization and diagnostics | [LLVM/Clang](https://github.com/llvm/llvm-project) | Reuse the existing C toolchain rather than reinvent it |
| Checked-pointer design and C migration | [Checked C + 3C](https://github.com/checkedc/checkedc-clang) | Gradual checked-pointer/migration research |
| Bounds-safe C | [Apple Bounds Safety Clang](https://github.com/swiftlang/llvm-project) · [documentation](https://clang.llvm.org/docs/BoundsSafety/BoundsSafety.html) | Integrate supported `-fbounds-safety`/pointer annotations; do not assume every host Clang supports them |
| Deterministic cleanup and RAII | [Cforall](https://github.com/cforall/cforall) · Clang/C++ semantics | Exactly-once resource cleanup without a mandatory GC |
| Temporal-safety reference | [Fil-C](https://github.com/pizlonator/fil-c) | Study strong UAF protection and runtime/ABI tradeoffs; not a drop-in no-GC solution |
| Native C memory and string operations | [libc / Microsoft Secure CRT](https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt) | Reuse platform libraries, keep the compiler-required runtime minimal |

### Two library workstreams — not three, and not a new libc

| Workstream | What C+ provides | Who works on it |
| --- | --- | --- |
| **1. Safer existing C standard library** | Keep writing **ordinary** `malloc`, `free`, `memcpy`, `strcpy`, `printf`, `fopen`, etc. C+ uses Clang-based bounds, ownership, borrow/lifetime analysis and supported native fortified/checked CRT mechanisms to prevent or diagnose unsafe operations where possible. **No glibc fork, no mandatory `malloc_s`/`free_s` rewrite.** | **Both agents (P0)** — Agent 1 compiler, Agent 2 CRT tests/adapters; [#19](https://github.com/haseeb-heaven/c-plus/issues/19), [#1](https://github.com/haseeb-heaven/c-plus/issues/1), [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| **2. Modern C+ libraries** | Optional struct-backed classes, owned String/Vector/File/Result, **GUI, logging, JSON, graphics, networking, HTTP, async, audio, images and video**. Wrap established libraries and OS APIs with RAII, checked sizes, explicit owners/borrows and errors. | **Agent 2** — [#3](https://github.com/haseeb-heaven/c-plus/issues/3), [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |

**Same C source; better compiler enforcement (target, not yet universally implemented):**

<table><tr><th width="50%">Normal Clang C</th><th width="50%">C+ with native libc (goal)</th></tr><tr><td><pre lang="c"><code>char name[8];
strcpy(name, "Hello World");
/* Potential buffer overflow */</code></pre></td><td><pre lang="c"><code>char name[8];
strcpy(name, "Hello World");
/* Compile-time diagnostic:
   destination too small */</code></pre></td></tr></table>

For safe code, `malloc(size)`, `free(ptr)`, `memcpy` and other existing libc functions **retain their familiar names and platform-defined behavior**. For unknown sizes or pointer lifetimes, runtime checks, annotations or explicit unsafe boundaries may be needed; a compiler cannot infer every bound or lifetime from raw C pointers. Existing optional checked APIs such as Microsoft's `strcpy_s(dst, dst_size, src)` can be used where available, but a separate C+ Safe CRT family is **not** a project requirement.

**Optional modern C+ interfaces (illustrative; available features depend on branch):**

```c
string label = string("Hello C+");
vector values = vector();
values.push_back(42);

/* Modern modules also cover GUI, logging and JSON
   with RAII wrappers over established native C libraries. */
```

**Native implementations and references:** [Apple libc](https://github.com/apple-oss-distributions/Libc), [Apple bounds-safe Clang](https://github.com/swiftlang/llvm-project) ([documentation](https://clang.llvm.org/docs/BoundsSafety/BoundsSafety.html)), [Microsoft Secure CRT](https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt), [LLVM/Clang](https://github.com/llvm/llvm-project), [glibc](https://sourceware.org/git/?p=glibc.git), [musl](https://git.musl-libc.org/cgit/musl). Modern modules reuse [SDL3](https://github.com/libsdl-org/SDL), [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear), [yyjson](https://github.com/ibireme/yyjson), [libcurl](https://github.com/curl/curl), [libuv](https://github.com/libuv/libuv), [miniaudio](https://github.com/mackron/miniaudio), [stb](https://github.com/nothings/stb) and [FFmpeg](https://github.com/FFmpeg/FFmpeg) where appropriate. No blanket promise of proven safety or unchanged performance.

**Current top priority:** Both agents work on [#19 — existing libc/CRT safety](https://github.com/haseeb-heaven/c-plus/issues/19). Agent 1 implements compiler analysis; Agent 2 implements isolated CRT conformance/portability tests and native safety adapters. Modern GUI/JSON/logging feature expansion is paused until this shared milestone is verified. They work on separate branches/PRs to avoid conflicting edits.

### Roadmap and tracking

| Priority | Issue | Work |
| --- | --- | --- |
| P0 | [#1 — Compiler ownership, borrowing, RAII and bounds](https://github.com/haseeb-heaven/c-plus/issues/1) | Agent 1 · [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| P0 | [#19 — **Workstream 1: safer existing libc/CRT**](https://github.com/haseeb-heaven/c-plus/issues/19) | **Both agents** · compiler checks + CRT conformance/platform adapters |
| P1 | [#3 — **Workstream 2: modern C+ libraries, including GUI/logging/JSON**](https://github.com/haseeb-heaven/c-plus/issues/3) | Agent 2 · [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17); former [#20](https://github.com/haseeb-heaven/c-plus/issues/20) scope merged here |
| P0 | [#4 — Multi-TU and RAII linkage](https://github.com/haseeb-heaven/c-plus/issues/4) | Agent 1, coordinate with Agent 2 |
| P1 | [#8 — Tests, fuzzing, CI, benchmarks](https://github.com/haseeb-heaven/c-plus/issues/8) | Both agents |
| P2 | [#18 — Legacy C → modern C+ AST modernizer](https://github.com/haseeb-heaven/c-plus/issues/18) | Later |

**Contribute:** follow [CONTRIBUTING.md](CONTRIBUTING.md), pick an [issue](https://github.com/haseeb-heaven/c-plus/issues), run its tests and open a PR against [`develop`](https://github.com/haseeb-heaven/c-plus/tree/develop). Agent branches/PRs stay separate and require review. See the [detailed roadmap](docs/ROADMAP.md).

```text
.c source (C + a few C+ constructs)
        |
        |  c+ : lex -> lower C+ syntax -> insert cleanup -> plain C
        v
   clang driver : parse / typecheck / optimise / codegen / link
        |
        v
   native executable
```

## Quick start

Build the driver (needs CMake, a C++20 compiler and LLVM/Clang development
packages; pass `-DLLVM_DIR=... -DClang_DIR=...` if they are not auto-detected):

```sh
cmake -S . -B build
cmake --build build --target c+ -- -j4
```

Compile and run a program:

```sh
./build/c+ build demos/A_hello.c -o hello
./hello
# or: ./build/c+ run demos/A_hello.c -o hello
```

## Commands

| command | meaning |
| --- | --- |
| `c+ build file.c [-o out]` | lower and compile to a native executable |
| `c+ run file.c [-o out]` | build, then run it |
| `c+ check file.c` | lower and type-check without linking |
| `c+ test [pattern]` | run the automated test suite |
| `c+ demos` | build and check included demos |
| `c+ bench [pattern]` | compare safe C+, legacy C+ and plain C |

Options: `--release` (`-O2`, safety checks remain on), `--debug` (default),
`--legacy-c` (compatibility mode, **not** memory safe), `--emit-c FILE` (dump
the lowered C), `--keep-temp`, `-Werror`, `--verbose`, and `--` to pass extra
arguments to the C compiler.

## Normal C vs C+: side-by-side

**C+ is still C.** The goal is familiar source code, struct-backed methods, automatic cleanup, stronger static ownership and bounds checks, the **native C ABI**, and Clang-optimized executables. The examples distinguish **existing syntax** from **future safety guarantees**. The full safety contract is tracked in [#1](https://github.com/haseeb-heaven/c-plus/issues/1).

### 1. Memory safety — same existing libc, stronger C+ checks

<table><tr><th width="50%">Normal C</th><th width="50%">C+ — same code, enhanced analysis (goal)</th></tr><tr><td><pre lang="c"><code>int *numbers = malloc(100 * sizeof(int));
if (!numbers) return 1;
numbers[0] = 42;
free(numbers);</code></pre></td><td><pre lang="c"><code>int *numbers = malloc(100 * sizeof(int));
if (!numbers) return 1;
numbers[0] = 42;
free(numbers);
/* Compile-time checks for size,
   lifetime and ownership where provable */</code></pre></td></tr></table>

**No second allocator API needed.** C+ analyzes ordinary libc calls and reuses native CRT behavior. It should reject provably unsafe operations and apply supported checks when necessary. **This is the target safety contract, not a claim every ordinary C allocation is currently verified.** See [#19](https://github.com/haseeb-heaven/c-plus/issues/19) and [#1](https://github.com/haseeb-heaven/c-plus/issues/1).

Optional `managed int *numbers = cplus_new_array(int, 100);` and automatic RAII are **additional language features**, not replacements for libc.

### 2. RAII — manual file closing vs guaranteed cleanup

<table><tr><th width="50%">Normal C</th><th width="50%">C+ — implemented RAII subset</th></tr><tr><td><pre lang="c"><code>int save(void) {
    FILE *f = fopen("log.txt", "w");
    if (!f) return -1;
    if (fprintf(f, "Hello") &lt; 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}</code></pre></td><td><pre lang="c"><code>resource struct File {
    FILE *handle;
};
void File_drop(File *f) {
    if (f-&gt;handle) fclose(f-&gt;handle);
}
int save(void) {
    File f = {fopen("log.txt", "w")};
    if (!f.handle) return -1;
    if (fprintf(f.handle, "Hello") &lt; 0)
        return -1;
    return 0; /* automatically closes */
}</code></pre></td></tr></table>


The compiler generates destructor calls on supported exits, including early returns. Exactly-once cleanup across **all** complex control flow remains under verification.

### 3. Classes — keep the C struct model

<table><tr><th width="50%">Normal C</th><th width="50%">C+ — implemented class syntax</th></tr><tr><td><pre lang="c"><code>typedef struct {
    int value;
} Counter;
void Counter_add(Counter *c, int n) {
    c-&gt;value += n;
}
Counter c = {0};
Counter_add(&amp;c, 5);</code></pre></td><td><pre lang="c"><code>class Counter {
    int value;
    void add(int n) {
        this-&gt;value += n;
    }
};
Counter c = {0};
c.add(5);</code></pre></td></tr></table>


C+ classes lower to **structs and functions**; they do not require the C++ object model, inheritance or virtual dispatch. Use `this->`, not `self->`. Multi-file header linkage is tracked in [#4](https://github.com/haseeb-heaven/c-plus/issues/4).

### 4. Array bounds — similar source, stronger compiler checks

<table><tr><th width="50%">Normal C</th><th width="50%">C+ — bounds-safety target</th></tr><tr><td><pre lang="c"><code>int values[3] = {10, 20, 30};
int index = get_index();
printf("%d", values[index]);
/* Invalid index: undefined behavior. */</code></pre></td><td><pre lang="c"><code>int values[3] = {10, 20, 30};
int index = get_index();
print(values[index]);
/* Goal: reject provably bad indices;
   check uncertain indices if bounded. */</code></pre></td></tr></table>


Current C+ provides **partial** static checks and sanitizer instrumentation. Apple-style counted/sized pointer contracts are an **integration target**, not full existing protection. [Apple Bounds Safety](https://clang.llvm.org/docs/BoundsSafety/BoundsSafety.html).

### 5. Borrow checking — reject dangling aliases

<table><tr><th width="50%">Normal C</th><th width="50%">C+ — intended diagnostic, NOT guaranteed today</th></tr><tr><td><pre lang="c"><code>int *p = malloc(sizeof(int));
if (!p) return 1;
int *q = p;
free(p);
*q = 100; /* use-after-free */</code></pre></td><td><pre lang="c"><code>managed int *p = cplus_new(int);
int *q = p;  /* borrowed alias */
cplus_release(p);
*q = 100;
/* TARGET COMPILE ERROR:
   q may point to freed memory. */</code></pre></td></tr></table>


This is intentionally invalid code. A complete ownership checker should reject the dangling alias by following moves, borrows, lifetimes, escapes and calls. **Current C+ does not catch every UAF**. [Cake](https://github.com/thradams/cake) provides relevant static ownership design ideas.

### 6. Native speed — existing compiler and C ecosystem

```text
Existing C (.c/.h)      Modern C+ (.cp/.hp)
         \                    /
           C+ safety / lowering
           - classes -> C functions
           - RAII cleanup
           - ownership + bounds analysis
                     |
                 Clang/LLVM
            optimize + generate code
                     |
              native executable
              libc + C libraries
```

C+ should **not** reinvent libc, memory-copy functions or native optimizers. Compiler/runtime safety checks may still add overhead; [benchmarks](benchmarks/) must compare equivalent C and C+ workloads.

### Current capability vs goal

| Capability | Today | Target |
| --- | --- | --- |
| C compatibility and native C ABI | Supported subset; `--legacy-c` mode | Broad compatible C source and explicit unsafe FFI |
| Lightweight struct-based classes | Implemented subset | Robust, multi-TU lowering |
| Managed memory and RAII | Implemented subset | Sound ownership and exactly-once cleanup |
| Borrow/lifetime checking | Partial | Flow-sensitive, interprocedural safety |
| Bounds checking | Partial + sanitizers | Apple-style bounds for supported pointer types |
| Use-after-free and data-race protection | Incomplete | Defined, enforced safe subset |
| Native speed | Clang backend; measured overhead | Competitive, reproducibly benchmarked code |
| C → modern C+ conversion | Planned | Optional Clang AST-based modernizer |

See [STATUS](docs/STATUS.md) and [LIMITATIONS](docs/LIMITATIONS.md) before relying on a safety claim. [#18](https://github.com/haseeb-heaven/c-plus/issues/18) tracks automatic migration of legacy C into modern C+.

## Library architecture

**Only two library workstreams:**

1. **Existing libc/CRT, made safer by C+ compiler checks** — study Apple, Microsoft and Clang bounds/fortify/checked-CRT techniques, preserve existing function names and ABI. [Issue #19](https://github.com/haseeb-heaven/c-plus/issues/19).
2. **One modern C+ library** — core String/Vector/File/Result **plus** optional GUI, logging, JSON, HTTP, graphics, audio, video and other modules, all wrapping established native libraries. [Issue #3](https://github.com/haseeb-heaven/c-plus/issues/3).

The third-party integrations previously described as a third pillar are **part of workstream 2**, not a standalone product; [#20](https://github.com/haseeb-heaven/c-plus/issues/20) is consolidated into #3. Existing work is experimental, including unmerged [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17). See [ROADMAP](docs/ROADMAP.md) and [STATUS](docs/STATUS.md).

## Safety model

C+ also has a small borrow checker: a pointer borrowed from a local resource
or managed allocation may be used while the owner is alive, but returning
that borrow or using it after `cplus_move(owner)` is a compile error. See
`docs/LIMITATIONS.md` for what this does **not** cover.

Safe mode (the default) rejects statically known out-of-bounds array indices
at compile time (`-Werror=array-bounds`), so `arr[10]` on an `int[3]` produces
no executable. Indices only known at run time are checked by UBSan
`undefined,bounds,null,alignment` with `-fno-sanitize-recover=all`, so a
detected failure aborts instead of continuing with corrupted state. Runtime ownership errors use distinct
exit codes (see `runtime/include/cplus/cplus.h`). `--legacy-c` disables all of this
and should not be considered safe.

The evidence-backed capability matrix is in [`docs/STATUS.md`](docs/STATUS.md);
the honest limitations are in [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md). The
lowering pipeline is in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), the
compiler-foundation decision in [`docs/FOUNDATION.md`](docs/FOUNDATION.md), the
safety assumptions in [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md), and the
comparison with Fil-C and Goose in [`docs/RELATED.md`](docs/RELATED.md). Work
still required for the full specification is tracked in
[`docs/ROADMAP.md`](docs/ROADMAP.md).

## Verification

```sh
./tests/run_tests.sh      # compiler positive, negative and runtime-fail tests
./demos/run_demos.sh      # sixteen demos
./benchmarks/run_bench.sh # reproducible benchmark table
```

Benchmarks compare safe C+, legacy C+ (same lowering, checks off) and plain
`clang -O2`, so the cost of language features is separated from the cost of
runtime checks. Recorded results are in `docs/LIMITATIONS.md`; rerun on your
own machine for current numbers.

## Layout

- `compiler/Sema/` — C+ lexer, syntax lowering and cleanup insertion
- `compiler/Driver/` — compilation pipeline and backend invocation
- `runtime/` — small C runtime for managed memory and primitives
- `runtime/include/cplus/` — stable C ABI used by generated code
- `tests/`, `demos/`, `benchmarks/` — executable evidence

## Examples

Larger real applications (games, a student database and a macOS system
monitor) are in `examples/`. See [`examples/README.md`](examples/README.md).

## Logging

Compiler and runtime logging are opt-in via `c+ build --log --log-level debug`.
See [`docs/LOGGING.md`](docs/LOGGING.md).

## Contributing

Branching and release workflow: [`CONTRIBUTING.md`](CONTRIBUTING.md).
Development happens on `develop` and feature branches. `main` is the published baseline and should be synchronized only with reviewed `develop` commits. Open [issues](https://github.com/haseeb-heaven/c-plus/issues) for scope and use linked PRs for evidence; do not merge agent branches without review.

## License

See [`LICENSE`](LICENSE).
