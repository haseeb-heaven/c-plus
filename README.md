# C+

C+ is ordinary C with a deliberately small set of additions: deterministic
resource cleanup, lightweight struct-based classes and managed allocations.

It is **a feature layer over an existing open-source C toolchain**, not a new
language runtime and not a rewrite of C. The `c+` driver lowers the C+
additions to plain C, then hands that C to the host Clang driver for parsing,
type checking, optimisation, code generation and linking. Programs are normal
native executables and interoperate with libc, POSIX and any C library.

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
| `c+ demos` | build and check all six demos |
| `c+ bench [pattern]` | compare safe C+, legacy C+ and plain C |

Options: `--release` (`-O2`, safety checks remain on), `--debug` (default),
`--legacy-c` (compatibility mode, **not** memory safe), `--emit-c FILE` (dump
the lowered C), `--keep-temp`, `-Werror`, `--verbose`, and `--` to pass extra
arguments to the C compiler.

## What C+ adds

A resource has a destructor and is destroyed deterministically when it leaves
scope — including on `return`, `break` and `continue`:

```c
#include <stdio.h>
#include "cplus/cplus.h"

resource struct File {
    FILE *handle;
};

void File_drop(File *self) {
    if (self->handle != NULL) {
        fclose(self->handle);
        self->handle = NULL;
    }
}

File File_open(const char *path) {
    File f;
    f.handle = fopen(path, "w");
    return f;                  /* ownership moves to the caller */
}

int main(void) {
    File log = File_open("app.log");   /* closed at scope exit */
    fprintf(log.handle, "hello\n");
    return 0;
}
```

A class is a struct plus functions; `p.m(x)` and `p->m(x)` are sugar for
`T_m(&p, x)` and `T_m(p, x)`:

```c
class Counter {
    int value;

    void add(int n) { self->value += n; }

    int get(void) { return self->value; }
};

int main(void) {
    Counter c = {0};
    c.add(5);
    printf("%d\n", c.get());
}
```

Fixed arrays carry a compile-time length; allocations made through the C+
allocator carry a tracked length too. `auto` infers types from the
initialiser:

```c
int values[] = {10, 30, 50};
auto total = 0;
for (auto i = 0; i < values.len; i++)
    total += values[i];   /* values.len is a literal 3 */
```

```c
int *heap = (int *)cplus_alloc(sizeof(int) * 3);
printf("%zu\n", heap.len);   /* 3, from the allocation's size table */
```

Out-of-bounds accesses that can be proven wrong at compile time are errors:
both `values[10]` and `for (i = 0; i < 5; i++) values[i]` fail to compile in
safe mode. Indices only known at run time are checked by UBSan.

Managed memory is released at scope exit:

```c
managed Buffer *b = cplus_new(Buffer);
b->size = 1024;                /* no free() needed */
```

Ordinary `.c` files need no changes and compile unchanged. See
[`docs/LANGUAGE.md`](docs/LANGUAGE.md) for the full syntax, and the `demos/`
directory for complete programs.

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
exit codes (see `include/cplus/cplus.h`). `--legacy-c` disables all of this
and should not be considered safe.

The honest capability table, including what is *not* implemented, is in
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md). The lowering pipeline is described
in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and a comparison with Fil-C
and Goose is in [`docs/RELATED.md`](docs/RELATED.md).

## Verification

```sh
./tests/run_tests.sh      # 24 positive, compile-fail and runtime-fail tests
./demos/run_demos.sh      # nine demos
./benchmarks/run_bench.sh # reproducible benchmark table
```

Benchmarks compare safe C+, legacy C+ (same lowering, checks off) and plain
`clang -O2`, so the cost of language features is separated from the cost of
runtime checks. Recorded results are in `docs/LIMITATIONS.md`; rerun on your
own machine for current numbers.

## Layout

- `src/Sema/` — C+ lexer, syntax lowering and cleanup insertion
- `src/Driver/` — compilation pipeline and backend invocation
- `src/Runtime/` — small C runtime for managed memory and primitives
- `include/cplus/` — stable C ABI used by generated code
- `tests/`, `demos/`, `benchmarks/` — executable evidence

## License

See [`LICENSE`](LICENSE).
