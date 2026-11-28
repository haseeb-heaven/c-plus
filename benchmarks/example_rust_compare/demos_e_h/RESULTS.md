# Example comparison: demos E–H (original C+ vs safe Rust)

Agent: 1 of 10 parallel agents. Owned files: only this directory
(`benchmarks/example_rust_compare/demos_e_h/`). No source, git index, or other
files were modified.

## Toolchains (exact)

| Tool | Version / path |
|---|---|
| C+ compiler (used) | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/c+` — `C+ 0.1.0`, backend `/usr/bin/clang` (Apple clang 16.0.0, `clang-1600.0.26.6`), target `arm64-apple-macosx15.2.0` |
| clang | `/usr/bin/clang` (`Apple clang version 16.0.0`, `clang-1600.0.26.6`) |
| Rust | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| timeout | `/usr/bin/timeout` (30 s per run) |
| Platform | macOS / darwin, arm64 |

## Why the C+ compiler, not plain `clang -std=c99`

The four demos are `.c` files but contain C+ syntax (`resource struct`,
`managed int *`), so a plain C compile fails. Exact evidence:

```
$ /usr/bin/clang -std=c99 -I runtime/include -include cplus/cplus.h demos/G_borrow.c -o /tmp/plain_g
demos/G_borrow.c:9:1: error: unknown type name 'resource'
demos/G_borrow.c:13:16: error: must use 'struct' tag to refer to type 'Slot'
demos/G_borrow.c:25:5: error: must use 'struct' tag to refer to type 'Slot'
3 errors generated.
```

The supported build is therefore the C+ compiler, which lowers C+ to C and
invokes clang with `-std=gnu17`, the C+ runtime object/archive and the runtime
include directory. Evidence from `c+ build --verbose demos/G_borrow.c`:

```
backend: /usr/bin/clang
  -std=gnu17
  -I/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/../runtime/include
... /var/folders/.../cplus_runtime_san-*.o  /var/folders/.../cplus-*.o ...
```

### Build-environment note (important)

`c-plus-stdlib/build/c+` is present but has a compiled-in
`CPLUS_SOURCE_DIR` pointing at the sibling `c-plus-core` tree while its include
search path resolves to `c-plus-stdlib/runtime/include`; the header/runtime pair
then disagrees:

```
.../c-plus-core/runtime/cplus_runtime.c:144:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
.../c-plus-core/runtime/cplus_runtime.c:259:23: error: use of undeclared identifier 'CPLUS_EXIT_CONVERSION'; did you mean 'CPLUS_EXIT_OWNERSHIP'?
error: failed to compile instrumented C+ runtime
```

Setting `CPLUS_ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` fixes
that mismatch for G/H/F, but this repo's runtime lacks
`__cplus_bounds_check`, which the lowered E source needs:

```
Undefined symbols for architecture arm64:
  "___cplus_bounds_check", referenced from: _main in cplus-*.o
ld: symbol(s) not found for architecture arm64
```

The sibling `c-plus-core/build/c+` (same compiler, matching runtime) builds all
four demos cleanly, so `compare.sh` uses it by default. No files outside this
directory were changed to work around this.

## Exact commands

Build + compare everything (also writes artifacts under `build/`):

```sh
sh benchmarks/example_rust_compare/demos_e_h/compare.sh
```

Equivalent manual commands, per demo `n` in
`E_networking F_concurrency G_borrow H_allocation_borrow`:

```sh
ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
CORE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/c+
B=benchmarks/example_rust_compare/demos_e_h/build

"$CORE" build "$ROOT/demos/$n.c" -o "$B/orig_$n"
rustc -O benchmarks/example_rust_compare/demos_e_h/$n.rs -o "$B/rust_$n"
timeout 30 "$B/orig_$n" >"$B/$n.orig.out" 2>"$B/$n.orig.err"; echo "orig rc=$?"
timeout 30 "$B/rust_$n" >"$B/$n.rust.out"  2>"$B/$n.rust.err";  echo "rust rc=$?"
```

For E only, canonicalization used for the comparison:

```sh
sed -E 's/127\.0\.0\.1:[0-9]+/127.0.0.1:PORT/g' | LC_ALL=C sort
```

## Per-example results

| Example | Original stdout (exit) | Rust stdout (exit) | Comparison | Verdict |
|---|---|---|---|---|
| E_networking | `client received: HTTP/1.0 200 OK\r` / `listening on 127.0.0.1:<ephemeral>` / `server received: GET / HTTP/1.0\r` / `server closing connection` (0) | same 4 lines and CRs, different ephemeral port and line order (0) | normalized: port → `PORT`, lines sorted | **MATCH** |
| F_concurrency | `counter=150000` (0) | `counter=150000` (0) | exact stdout + exit | **MATCH** |
| G_borrow | `borrowed value: 42` / `owner still alive` / `drop slot 42` (0) | identical (0) | exact stdout + exit | **MATCH** |
| H_allocation_borrow | `len=4 total=100` (0) | identical (0) | exact stdout + exit | **MATCH** |

Raw observed E outputs (5 consecutive original runs, and the Rust run) show the
port changing on every run and the client line preceding the buffered server
lines in C vs following them in Rust:

```
# original (redirected stdout)
client received: HTTP/1.0 200 OK\r
listening on 127.0.0.1:51849
server received: GET / HTTP/1.0\r
server closing connection

# Rust (line-buffered stdout)
listening on 127.0.0.1:51967
server received: GET / HTTP/1.0\r
client received: HTTP/1.0 200 OK\r
server closing connection
```

Both normalize to:

```
client received: HTTP/1.0 200 OK\r
listening on 127.0.0.1:PORT
server closing connection
server received: GET / HTTP/1.0\r
```

## Totals

- Examples tested: **4**
- MATCH: **4** (E normalized; F/G/H exact)
- DIFFERENT: **0**
- BLOCKED: **0** (no example required live external network or nondeterministic
  timing left unresolved: E uses a real loopback listener with a self-contained
  client thread and a bounded `join`, F uses a bounded 3-thread join.)

## Explicit non-equivalence statement

1. **Mechanisms are not equivalent, only observable behavior is.** C+
   implements RAII/borrow/allocation safety with **runtime metadata**:
   compiler-generated `cplus_cleanup` records and `cplus_scope_cleanup` for
   `resource` destructors (G), and allocator-tracked length plus
   `cplus_release` for `managed` allocations (H). Rust uses **compile-time**
   ownership, lifetimes and static drop elaboration, plus `Vec`'s
   in-value length and `Drop`. The safe-Rust versions have no `unsafe` and no
   FFI; their networking/threading uses only safe `std` APIs.
2. **F's safety guarantees differ.** The C+ compiler emits
   `[CPLUS-BORROW] stack borrow passed to thread` and
   `[CPLUS-FFI] multiple threads mutate shared global 'shared_counter' without
   synchronization` warnings — static verification of locked shared mutation
   is explicitly on the C+ roadmap. Rust's `Arc<Mutex<i64>>` and `MutexGuard`
   make the same mistake a compile-time error. The numeric result is identical.
3. **E cannot be byte-compared raw.** `bind(..., 0)` picks an OS-assigned port,
   and the original's forked, unbuffered `dprintf` child vs block-buffered
   `printf` server gives a different line order than Rust's line-buffered
   threads. These are properties of the demo, not of the language; both were
   canonicalized before comparison. Every message (including the trailing
   `\r` left by `strchr(reply, '\n')` truncation), the HTTP exchange, and the
   exit code agree.
4. All four Rust programs compile cleanly with `rustc -O` (empty build logs)
   and exit 0 with empty stderr.

## Machine-readable output

```
EXAMPLE_COMPARE E_networking MATCH normalized ephemeral port + client/server line order; same 4 messages, CRs and exit; raw stdout differs only by those two inherent nondeterminism sources
EXAMPLE_COMPARE F_concurrency MATCH exact stdout + exit; 3 threads x 50,000 locked increments, counter=150000
EXAMPLE_COMPARE G_borrow MATCH exact stdout + exit; static Rust borrow/Drop vs C+ runtime cleanup metadata
EXAMPLE_COMPARE H_allocation_borrow MATCH exact stdout + exit; Vec length/Drop vs C+ managed-allocation length metadata
```
