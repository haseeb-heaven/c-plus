# Checked-CRT string examples vs safe Rust

Each of the six checked string examples under `examples/crt/` was compiled and
run as-is, and compared one-by-one against a hand-written **safe Rust**
equivalent (`String`/`str` only; no `unsafe`, no FFI, no raw pointers).

Verdicts are printed by `./compare.sh` as:

```
EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
```

`MATCH` requires byte-identical stdout **and** stderr **and** identical exit
status. `EXAMPLE_COMPARE_NORMALIZED ...` lines are supplementary: they delete
safeclib's `IGNORE CONSTRAINT HANDLER:` diagnostic from the C stdout and repeat
the comparison, isolating the examples' own output.

## Exact commands and toolchains

All commands were run from the repository root unless noted; `ROOT` below is
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.

Toolchains (recorded in `out/toolchains.txt`, timestamp `2026-11-23T05:59:21Z`):

```
clang: Apple clang version 16.0.0 (clang-1600.0.26.6), Target arm64-apple-darwin27.2.0
rustc: rustc 1.100.0 (0ef1bc386 2026-11-09)
timeout: /usr/bin/timeout  (30 s for every build and every run)
```

Original C example (for each `<name>` in the table), the prescribed include
directories plus `stdlib/safe` (needed for `safe_string.h`) and **both**
archives:

```sh
/usr/bin/clang -std=c99 -O2 \
  -I"$ROOT/libc/install-include" \
  -I"$ROOT/third_party/safeclib/include" \
  -I"$ROOT/stdlib/safe" \
  "$ROOT/examples/crt/<name>.c" \
  "$ROOT/build/safe/libcplus_safecrt.a" \
  "$ROOT/build/libc/libcplus_hardened.a" \
  -lm -pthread -o out/c_<name>
```

Rust equivalent:

```sh
rustc -O benchmarks/example_rust_compare/crt_string/<name>.rs -o out/rust_<name>
```

Runs and comparison:

```sh
timeout 30 out/c_<name>    > out/c_<name>.stdout    2> out/c_<name>.stderr
timeout 30 out/rust_<name> > out/rust_<name>.stdout 2> out/rust_<name>.stderr
cmp -s <c.stdout> <rust.stdout>; cmp -s <c.stderr> <rust.stderr>; compare exit codes
```

All six C and Rust builds succeeded with empty build logs (no warnings/errors).
All twelve runs exited 0; all stderr files were empty (0 bytes). No case was
`BLOCKED` — every example was compilable and runnable with the available
toolchain and archives, so there is no missing-symbol/timeout evidence to list.

Reproduce everything (writes only under this directory's `out/`):

```sh
cd benchmarks/example_rust_compare/crt_string
./compare.sh | tee out/compare.stdout
```

## Per-example results

`<handler>` below is the C-only stdout line
`IGNORE CONSTRAINT HANDLER: (NNN) ...` produced by safeclib's default
`ignore_handler_s` (`third_party/safeclib/src/ignore_handler_s.c`, which uses
`sldebug_printf` → `printf`, i.e. **stdout**, not stderr).

| Example | Exact C stdout | Exact Rust stdout | Exit (C/Rust) | Verdict | Normalized verdict | Refusal modeled in Rust |
|---|---|---|---|---|---|---|
| `strcpy_s` | `<handler> (406) strcpy_s: not enough space for src` + `strcpy_s: ok` | `strcpy_s: ok` | 0 / 0 | **DIFFERENT** — stdout differs, stderr identical (C-only safeclib violation-handler line), exits equal | MATCH — `strcpy_s(4,"Too long")` → `Err(NotEnoughSpace)` (`src.len()+1 > dmax`), C reports 406 |
| `strncpy_s` | `strncpy_s: ok` | `strncpy_s: ok` | 0 / 0 | **MATCH** — stdout+stderr+exit identical | MATCH | no refusal case in source; Ok("abc") for `slen=3` |
| `strcat_s` | `<handler> (406) strcat_s: not enough space for src` + `strcat_s: ok` | `strcat_s: ok` | 0 / 0 | **DIFFERENT** — stdout differs, stderr identical (C-only handler line), exits equal | MATCH — second `strcat_s(d,4,"world")` → `Err(NotEnoughSpace)` (`3+5+1 > 4`), C reports 406; dest cleared like safeclib |
| `strncat_s` | `strncat_s: ok` | `strncat_s: ok` | 0 / 0 | **MATCH** — stdout+stderr+exit identical | MATCH | no refusal case in source; Ok("hiwo") for `slen=2` |
| `strcmp_s` | `<handler> (400) strcmp_s: dest is null` + `strcmp_s: ok` | `strcmp_s: ok` | 0 / 0 | **DIFFERENT** — stdout differs, stderr identical (C-only handler line), exits equal | MATCH — `strcmp_s(None,3,...)` → `Err(NullDest)`; success cases return `Ok(-1/0/1)` via out-parameter model |
| `strnlen_s` | `<handler> (400) strnlen_s: str is null` + `strnlen_s: ok` | `strnlen_s: ok` | 0 / 0 | **DIFFERENT** — stdout differs, stderr identical (C-only handler line), exits equal | MATCH — `None` → `0` (`Option`-modeled NULL); note safeclib still invokes its handler on NULL although the documented result is 0 |

Exact raw first lines (verified with `xxd`) are:

```
IGNORE CONSTRAINT HANDLER: (406) strcpy_s: not enough space for src
IGNORE CONSTRAINT HANDLER: (406) strcat_s: not enough space for src
IGNORE CONSTRAINT HANDLER: (400) strcmp_s: dest is null
IGNORE CONSTRAINT HANDLER: (400) strnlen_s: str is null
```

## Totals

```
EXAMPLE_COMPARE_TOTALS                 MATCH=2 DIFFERENT=4 BLOCKED=0
EXAMPLE_COMPARE_NORMALIZED_TOTALS      MATCH=6 DIFFERENT=0
```

Exact comparison output is saved in `out/compare.stdout`; per-program stdout,
stderr, build logs and normalized stdout are in `out/c_<name>.*` and
`out/rust_<name>.*`.

## Explicit non-equivalence statement

The Rust ports are **not byte-for-byte equivalent** to the C examples at the
observable stdout level in 4 of 6 cases. The sole cause is that the C build
links safeclib's default `ignore_handler_s`, which prints a constraint-handler
diagnostic to **stdout** whenever a checked API refuses an operation (or, for
`strnlen_s`, receives NULL); safe Rust has no such global violation handler and
models the same refusals with `Result::Err` / `Option::None`, which produce no
output. After removing that one C-only diagnostic line, all six programs have
identical stdout, identical (empty) stderr and identical exit status 0 — i.e.
the examples' own logic is equivalent, but the observable behavior of the
*linked C runtime* is not reproduced by a safe-Rust implementation.

Further non-equivalences that are intentional and remain even after
normalization (they are properties of the language/runtime, not bugs in the
ports):

* C uses fixed `char[]` byte buffers with a caller-supplied `dmax` and a NUL
  terminator; the ports use owned `String`s with an explicit capacity constant.
  Rust cannot express C pointer provenance, aliasing, lifetimes or out-of-bounds
  writes; the checks here validate *sizes*, not memory safety of arbitrary
  pointers.
* C constraint codes (`ESNULLP=400`, `ESNOSPC=406`, ...) are collapsed into
  typed Rust error enums; the numeric code values are not reproduced.
* C is byte-oriented; the ports use `str::get(..n)` and refuse a byte split
  inside a UTF-8 code point rather than reading arbitrary bytes.
* The C handler is process-global and configurable; the Rust model has no
  equivalent global side channel.

No `BLOCKED` cases: nothing was untestable with the supplied clang/rustc/archives.
