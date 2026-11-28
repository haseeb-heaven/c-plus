# Rust examples vs. checked C file/stdio APIs — `benchmarks/rust_examples/io`

## What this is

Four files:

| File | Purpose |
| --- | --- |
| `io.rs` | Safe-Rust stdlib examples (`File`, `OpenOptions`, `read_exact`, `write_all`, `BufReader`, `Drop`; **no `unsafe`**). Emits `<api> <case> <token>` on **stderr**. |
| `harness.c` | C99 harness calling every checked API for the same cases; emits the same tokens on **stderr**. Links both archives at once. |
| `compare.sh` | Builds both under `timeout 30`, diffs the token streams, prints the summary line, exits nonzero only on a real mismatch. |
| `NOTES.md` | This file. |

Tokens are a small normalized vocabulary: `ok`, `closed`, `null` (null/invalid input rejected), `zero` (zero-length request), `clamped` (buffer cap / one-over), `roundtrip` (binary write→read match), `refused` (double close refused).

## API → symbol mapping (verified with `nm`, not assumed)

The public names in the task (`fopen_s`, `fprintf_s`, …) are **unprefixed macros** in
`stdlib/safe/safe_io.h` / `safe_format.h`. The real globally-exported symbols are the
`cplus_*` ones. The harness calls those symbols directly with
`CPLUS_SAFE_NO_UNPREFIXED_MACROS` defined, so no macro rewriting is involved.

Checked APIs covered (**13**):

* Safe CRT family — `build/safe/libcplus_safecrt.a`:
  `fopen_s → cplus_fopen_s`, `fclose_s → cplus_fclose_s`,
  `fread_s → cplus_fread_s`, `fwrite_s → cplus_fwrite_s`,
  `fprintf_s → cplus_fprintf_s`, `fscanf_s → cplus_fscanf_s`,
  `printf_s → cplus_printf_s`, `scanf_s → cplus_scanf_s`.
* Hardened libc family — `build/libc/libcplus_hardened.a`:
  `cplus_fopen`, `cplus_fclose`, `cplus_fread`, `cplus_fwrite`,
  `cplus_tmpfile` (this is the `tmpfile` entry point — **present**, as `cplus_tmpfile`).

### nm evidence (exact output from this checkout)

```
$ nm -g build/safe/libcplus_safecrt.a | grep -E ' T _cplus_(fopen_s|fclose_s|fread_s|fwrite_s|fprintf_s|fscanf_s|printf_s|scanf_s)$' | sort -u
0000000000000000 T _cplus_fopen_s
000000000000033c T _cplus_fclose_s
0000000000000544 T _cplus_fread_s
0000000000000614 T _cplus_printf_s
0000000000000618 T _cplus_fwrite_s
0000000000000918 T _cplus_fprintf_s
0000000000000c60 T _cplus_scanf_s
0000000000001094 T _cplus_fscanf_s

$ nm -g build/libc/libcplus_hardened.a | grep -E ' T _cplus_(fopen|fclose|fread|fwrite|tmpfile)$' | sort -u
00000000000000fc T _cplus_fopen
000000000000016c T _cplus_fclose
000000000000024c T _cplus_tmpfile
0000000000000494 T _cplus_fread
0000000000000530 T _cplus_fwrite
```

The two archives define **no overlapping global symbols** in this set, so linking both
into one harness is safe:

```
$ comm -12 <(nm -g --defined-only build/safe/libcplus_safecrt.a | awk '{print $3}' | sort -u) \
           <(nm -g --defined-only build/libc/libcplus_hardened.a | awk '{print $3}' | sort -u) | wc -l
0
```

## Cases (43 `(api, case)` pairs)

Required categories and where they appear:

* **valid read/write** — every API's `valid` case.
* **null stream / path** — `fopen_s`/`cplus_fopen` `null-path`; `fclose_s`/`cplus_fclose`,
  `fread_s`/`cplus_fread`, `fwrite_s`/`cplus_fwrite`, `fprintf_s`, `fscanf_s`
  `null-stream`; `fread_s`/`fwrite_s`/`cplus_fread`/`cplus_fwrite` `null-buf`;
  `printf_s`/`scanf_s` `null-format`.
* **zero / one-over buffer** — `zero` (zero-length request) on all read/write
  functions; `one-over` on the Annex-K-style `fread_s`/`fwrite_s` (`bmax` clamps a
  `count` that exceeds `bmax/size`). The hardened `cplus_fread`/`cplus_fwrite` have
  no `bmax` parameter, so they get `zero` + overflow-guard behavior instead.
* **binary roundtrip** — `binary` on `fread_s`, `fwrite_s`, `cplus_fread`,
  `cplus_fwrite`; `write-read` on `cplus_tmpfile` (`BIN = 00 01 02 03 ff fe 80 7f`).
* **close / double-close refusal** — `fclose_s` and `cplus_fclose` each have
  `valid → closed` and `double-close → refused`. `cplus_tmpfile` also has `close`.
* `printf_s`/`scanf_s` use stdout/stdin: the harness calls `cplus_printf_s` /
  `cplus_scanf_s` (which target the standard streams); Rust uses `println!` /
  `stdin().read_line()`. `compare.sh` feeds both an identical `io 42\n` on stdin
  and discards stdout, diffing only stderr.

## Exact commands / toolchains

Environment (this checkout):

```
rustc 1.100.0 (0ef1bc386 2026-11-09)
cargo 1.100.0 (3acc79c0c 2026-11-05)
Apple clang version 16.0.0 (clang-1600.0.26.6), target arm64-apple-darwin
/usr/bin/timeout present
```

One-command reproduction from the repo root:

```sh
bash benchmarks/rust_examples/io/compare.sh
```

Equivalent manual steps:

```sh
rustc --edition 2021 benchmarks/rust_examples/io/io.rs -o /tmp/io
cc -std=c99 -D_POSIX_C_SOURCE=200112L -Istdlib/safe \
   benchmarks/rust_examples/io/harness.c \
   build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
   -lpthread -o /tmp/harness
printf 'io 42\n' | /tmp/io       >/tmp/io.out   2>/tmp/io.err
printf 'io 42\n' | /tmp/harness  >/tmp/h.out    2>/tmp/h.err
diff -u /tmp/io.err /tmp/h.err
```

Both builds and both runs are wrapped in `timeout 30` inside `compare.sh`.

## Result (real run, this checkout)

```
== token diff (rust stderr vs harness stderr) ==
token streams identical
== summary ==
RUST_EXAMPLES io 13 43 43 0 0
```

`apis=13`, `cases=43`, `matched=43`, `mismatched=0`, `blocked=0`; `compare.sh`
exits `0`. `io.rs` compiles cleanly with no `unsafe`, and `harness.c` compiles
with `-Wall` producing no warnings.

Blocked-path checks (verified with temp copies; no repo files touched):

* Missing archive → `MISSING ARCHIVE`, `BLOCKED: …`, and
  `RUST_EXAMPLES io 13 0 0 0 2` (exit 0).
* Archive present but required symbols absent → one `BLOCKED: absent symbol _…`
  line per missing symbol plus nm evidence, e.g.
  `RUST_EXAMPLES io 13 0 0 0 9` (exit 0).

A blocked run is **not** counted as a mismatch; only differing token lines make
`compare.sh` exit nonzero.

## Non-equivalence statement

**Matching tokens demonstrate surface-level outcome parity for the tested cases,
not semantic or safety equivalence between safe Rust and the checked C APIs.**

* The C APIs are ordinary unsafe C functions with runtime guards: a NULL stream
  or buffer is detected at call time and the function returns a failure value.
  Safe Rust makes those states **unrepresentable** (no null `File`/`&[u8]`, format
  strings checked at compile time). The `null` tokens for these cases therefore
  mean "rejected by C at runtime / prevented by Rust at construction time" — the
  same label, different mechanisms.
* Double close is **prevented by Rust's ownership + `Drop`** (a second close does
  not compile; the value is moved), whereas `cplus_fclose_s`/`cplus_fclose`
  accept an already-closed `FILE*` and refuse it via a bounded closed-address
  marker cache *before* dereferencing it. The `refused` token means the same
  observable outcome, not the same implementation.
* `fread_s`/`fwrite_s` clamp `count` to `bmax/size` (Annex-K-style). Safe Rust has
  no `bmax` argument; the `clamped` examples model the cap by using a smaller
  destination slice / writing a shorter slice, which is a hand-written analogue,
  not an automatic library behavior. The hardened `cplus_fread`/`cplus_fwrite`
  have no `bmax` at all and are checked only for NULL and multiplication overflow.
* `cplus_tmpfile()` returns an anonymous, auto-unlinked `FILE*`; the Rust
  examples use a uniquely named temp file under `std::env::temp_dir()` with
  `Drop` closing it. Lifetime/unlink semantics differ even though the
  `valid`/`write-read`/`close` tokens match.
* `printf_s`/`scanf_s` in Rust map to `println!`/`stdin().read_line()` with
  manual parsing; they are not the C `printf`/`scanf` variadic machinery, and
  this harness only exercises a fixed, literal format with a fixed stdin line.
* The C libraries also perform additional checks (UAF tracking, `%n` refusal,
  non-literal-format refusal, unbounded-`%s` refusal, capacity failure with
  `errno=EMFILE`) that are outside these cases and have no direct Rust token here.

Consequently this benchmark should be read as: *"for these specific cases, the
safe-Rust stdlib produces the same normalized outcome as the checked C API,"* not
*"the two are interchangeable."* It is a behavioral smoke comparison, not a
proof of equivalence.
