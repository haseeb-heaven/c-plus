# Per-method memory-safety comparison — I/O (`fopen_s`, `fread_s`, `fwrite_s`, `fclose_s`, `tmpfile_s`)

Owner: `benchmarks/safety_compare/per_method/io/` only. No files outside this
directory were created or modified; no `git add`/`git commit`; no compiler or
runtime sources were touched.

## 1. Method availability — `nm` evidence

Archives inspected (in `$ROOT = /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`):

* `build/safe/libcplus_safecrt.a` (424320 bytes, 2026-11-23 10:22)
* `build/libc/libcplus_hardened.a` (276128 bytes, 2026-11-23 10:11)

Command and result:

```sh
$ nm -g --defined-only build/safe/libcplus_safecrt.a \
    | grep -E 'cplus_f(open|close|read|write)_s|cplus_tmpfile_s'
000000000000033c T _cplus_fclose_s
0000000000000000 T _cplus_fopen_s
0000000000000544 T _cplus_fread_s
0000000000000618 T _cplus_fwrite_s

$ nm -g --defined-only build/libc/libcplus_hardened.a \
    | grep -E '_cplus_(fopen|fclose|fread|fwrite|tmpfile|fseek|ftell|remove|rename|rewind)$'
000000000000016c T _cplus_fclose
00000000000000fc T _cplus_fopen
0000000000000494 T _cplus_fread
0000000000000530 T _cplus_fwrite
0000000000000000 T _cplus_fseek
000000000000004c T _cplus_ftell
00000000000000b4 T _cplus_remove
00000000000000e8 T _cplus_rename
0000000000000290 T _cplus_rewind
000000000000024c T _cplus_tmpfile
```

`tmpfile_s` is **absent**. The hardened archive exposes only the *unchecked*
`cplus_tmpfile`; the checked archive exposes no tmpfile entry at all:

```sh
$ nm build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a 2>/dev/null \
    | grep 'tmpfile_s' || echo ABSENT
ABSENT: no symbol containing 'tmpfile_s' in either archive
```

**`tmpfile_s` → BLOCKED (evidence above).** It is counted in the `blocked`
field of the summary and is deliberately not emitted as a `<method> <case>
<token>` line by either harness. `compare.sh` re-derives this value at runtime
with the same `nm` test (currently `BLOCKED=1`) rather than hard-coding it.

Semantics of the four available methods were read from
`stdlib/safe/src/safe_io.c` (not from a header, because these symbols have no
installed header declaration; the harness declares the four prototypes itself):

* `cplus_fopen_s(name, mode)` → `NULL` + `EINVAL` on NULL name/mode; else `fopen`.
* `cplus_fclose_s(stream)` → `EOF` on NULL, on closed/double-close, or when a
  close-marker cannot be recorded; else `0`.
* `cplus_fread_s(buf, bmax, size, count, stream)` → `0` on NULL
  buf/stream or zero size/count; clamps `count = min(count, bmax/size)`.
* `cplus_fwrite_s(...)` → identical guard/clamp behaviour to `fread_s`.

## 2. Toolchains (exact)

```sh
$ /usr/bin/clang --version | head -1
Apple clang version 16.0.0 (clang-1600.0.26.6)
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)
$ command -v timeout
/usr/bin/timeout
$ uname -m
arm64
```

## 3. Exact commands

Everything is driven by `benchmarks/safety_compare/per_method/io/compare.sh`
(`chmod +x` applied). From the repo root:

```sh
benchmarks/safety_compare/per_method/io/compare.sh
```

Equivalent manual commands (these are what the driver runs, with `$ROOT`
expanded and `timeout 30` wrapping each build/run):

```sh
cd "$ROOT"

timeout 30 /usr/bin/clang -std=c99 \
  -I"$ROOT/libc/install-include" \
  -I"$ROOT/third_party/safeclib/include" \
  "$ROOT/benchmarks/safety_compare/per_method/io/io_harness.c" \
  "$ROOT/build/safe/libcplus_safecrt.a" \
  "$ROOT/build/libc/libcplus_hardened.a" \
  -lm -pthread \
  -o "$ROOT/benchmarks/safety_compare/per_method/io/results/io_harness"

timeout 30 rustc -O \
  "$ROOT/benchmarks/safety_compare/per_method/io/io.rs" \
  -o "$ROOT/benchmarks/safety_compare/per_method/io/results/io_rust"

timeout 30 "$ROOT/benchmarks/safety_compare/per_method/io/results/io_harness" \
  > "$ROOT/benchmarks/safety_compare/per_method/io/results/c.out"
timeout 30 "$ROOT/benchmarks/safety_compare/per_method/io/results/io_rust" \
  > "$ROOT/benchmarks/safety_compare/per_method/io/results/rust.out"

diff -u .../results/c.out .../results/rust.out   # empty on pass
```

The C compile also passes `-Wall -Wextra` when run standalone (no warnings).
The Rust source compiles warning-free under `rustc -O`.

## 4. Exact outputs

`results/c.out` and `results/rust.out` are byte-identical (18 lines):

```
fopen_s null_path REFUSED
fopen_s null_mode REFUSED
fopen_s valid_open OK
fread_s valid_read OK
fread_s null_buf REFUSED
fread_s null_stream REFUSED
fread_s zero_bound REFUSED
fread_s one_over_bound REFUSED
fread_s canary_intact OK
fwrite_s valid_write OK
fwrite_s null_buf REFUSED
fwrite_s null_stream REFUSED
fwrite_s zero_bound REFUSED
fwrite_s one_over_bound REFUSED
fwrite_s canary_intact OK
fclose_s valid_close OK
fclose_s double_close REFUSED
fclose_s null_stream REFUSED
```

Driver result (three consecutive runs, identical):

```
METHOD_IO 18 18 0 1   (exit 0)
METHOD_IO 18 18 0 1   (exit 0)
METHOD_IO 18 18 0 1   (exit 0)
```

Fields: `cases=18`, `matched=18`, `mismatched=0`, `blocked=1` (`tmpfile_s`).

**Negative control** (temporarily flipped one Rust token `REFUSED`→`OK`, then
restored the source):

```
MISMATCH c=[fread_s null_buf REFUSED] rust=[fread_s null_buf OK]
METHOD_IO 18 17 1 1   (exit 1)
```

So a real token mismatch does make the driver exit nonzero.

## 5. Harness design / determinism

* C harness uses a real named file (unique per PID, removed before exit) for
  `fopen_s valid_open`, and a real `tmpfile()` stream seeded with 16 `0x11`
  bytes for all `fread_s`/`fwrite_s` probes. No mocks, no fixed absolute paths,
  no network, no clock dependence.
* Token derivation is from **observed** values (return codes and canary
  bytes), not hard-coded. `emit_ok` prints `OK` only when the operation
  succeeded; `emit_refused` prints `REFUSED` only when the guard actually
  held. A canary clobber or missing bound therefore flips a token and is
  caught as a real mismatch by `compare.sh`.
* Canary = 4 bytes of `0x5A` on each side of an 8-byte payload buffer; only
  the two guard regions are checked (the payload region is intentionally
  non-canary). `one_over_bound` requests `count = bmax + 1` with `size = 1`;
  the checked API clamps to `bmax`, so the canary remains intact.
* Rust harness uses only safe std APIs: `File::open` / `OpenOptions`,
  `read_exact` into a bounds-checked `&mut [u8]` slice of a `Vec<u8>`,
  `write_all`, and `Drop` via `Option::take` for the double-close model.
  There is no `unsafe` block, no raw pointer, and no FFI in `io.rs`.
* Three repeated runs of each harness and of the driver produced identical
  output (no ordering or naming nondeterminism in the tokens).

## 6. Explicit non-equivalence note

The identical tokens do **not** mean the two languages enforce safety by the
same mechanism:

1. **Clamping vs. pre-refusal.** `cplus_fread_s`/`cplus_fwrite_s` clamp
   `count` to `bmax / size` and perform a bounded operation; the Rust harness
   refuses an oversized request *before* issuing any I/O because safe slicing
   makes the oversized slice impossible. Both are memory-safe and both print
   `REFUSED`, but the C+ API still touches the stream, whereas the Rust side
   does not.
2. **NULL / mode have no Rust analogue.** Rust has no NULL pointers and
   `File::open` has no mode string. The `null_path`, `null_mode`, `null_buf`
   and `null_stream` cases are modelled with `Option` (= absent) and a
   small mode-string whitelist; they represent "no valid input supplied"
   rather than an actual C NULL dereference attempt. The C+ API genuinely
   receives NULL and returns `EINVAL`-style failure.
3. **Double close.** `cplus_fclose_s` keeps a bounded cache of recent closes
   and refuses a second `fclose` on a possibly freed `FILE*` (a real UAF
   guard). Safe Rust cannot express this aliasing at all; `Option::take`
   models it at the type level (second `take()` yields `None`). The token
   matches, but no host close on a dangling handle is ever attempted in Rust.
4. **tmpfile_s.** Rust has no direct `tmpfile_s` equivalent either; the
   method is BLOCKED on the C+ side by symbol absence, so no token comparison
   exists for it.
5. **Scope.** These probes cover the listed guard/bound cases only. They are
   not a proof of full Annex-K conformance, thread-safety, or equivalence of
   error numbers (`errno` is not printed); the comparison is of the
   observable safety token for each named case.

## 7. Files

* `io_harness.c` — C99 deterministic checked-I/O probe (18 cases).
* `io.rs` — Rust safe-API counterpart (18 cases, no `unsafe`).
* `compare.sh` — build/run/diff driver and `METHOD_IO ...` summary.
* `NOTES.md` — this file.
* `results/c.out`, `results/rust.out`, `results/diff.txt`,
  `results/io_harness`, `results/io_rust`, `results/*.err` — generated
  artifacts (regenerated on every driver run).
