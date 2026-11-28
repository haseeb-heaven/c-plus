# Checked-CRT format examples — C99 vs safe-Rust behaviour comparison

Scope: the four checked-CRT format examples under `examples/crt/`:
`printf_s.c`, `snprintf_s.c`, `sprintf_s.c`, `vsnprintf_s.c`.

For each example the original C99 program was compiled and linked against the
**real** checked-CRT archives and run under a 30 s timeout; a self-contained
**safe-Rust** equivalent (no `unsafe`, no FFI) was compiled with `rustc -O` and
run under the same timeout. stdout was compared byte-for-byte with `cmp`, and
the process exit statuses were compared.

Files in this directory (all outputs/build artifacts stay under `.build/`):

| File | Purpose |
| --- | --- |
| `printf_s.rs`, `snprintf_s.rs`, `sprintf_s.rs`, `vsnprintf_s.rs` | one safe-Rust equivalent per example |
| `compare.sh` | builds, runs, compares, prints `EXAMPLE_COMPARE …` lines |
| `.build/` | C/Rust binaries, build logs, captured stdout/stderr, evidence probes |

## Toolchains (exact versions, captured on this host)

```
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

$ /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

$ command -v timeout
/usr/bin/timeout
  (BSD-style usage: timeout [-f|-k|-p|-s|-v] <duration> <command> [args];
   `--version` is not supported. `timeout 30 <cmd>` works and was used.)
```

Archives linked into the C side (both were present and used):

```
build/safe/libcplus_safecrt.a     (458520 bytes)
build/libc/libcplus_hardened.a    (295592 bytes)
```

Required exported symbols were confirmed present:

```
$ nm -g build/safe/libcplus_safecrt.a | grep -E 'cplus_(printf_s|snprintf_s|sprintf_s|vsnprintf_s)$'
0000000000000614 T _cplus_printf_s
00000000000003b0 T _cplus_snprintf_s
00000000000016dc T _cplus_sprintf_s
0000000000000000 T _cplus_vsnprintf_s
```

## Exact reproduction commands

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
bash benchmarks/example_rust_compare/crt_format/compare.sh
```

`compare.sh` performs, for each `<name>` in
`printf_s snprintf_s sprintf_s vsnprintf_s`:

```sh
# C side (original, unmodified source in examples/crt/)
/usr/bin/clang -std=c99 -O2 \
  -Ilibc/install-include -Ithird_party/safeclib/include -Istdlib/safe \
  examples/crt/<name>.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -o benchmarks/example_rust_compare/crt_format/.build/<name>.c.bin

# Rust side (safe Rust, no unsafe/FFI)
rustc -O benchmarks/example_rust_compare/crt_format/<name>.rs \
  -o benchmarks/example_rust_compare/crt_format/.build/<name>.rs.bin

# Run both under the 30 s limit, capture stdout/stderr/exit
timeout 30 .build/<name>.c.bin  > .build/<name>.c.out  2> .build/<name>.c.err
timeout 30 .build/<name>.rs.bin > .build/<name>.rs.out 2> .build/<name>.rs.err

# Compare
cmp -s .build/<name>.c.out .build/<name>.rs.out   # byte-for-byte stdout
# plus equality of the two exit statuses
```

### Deviation from the task-specified include flags (with evidence)

The task specified only `-Ilibc/install-include -Ithird_party/safeclib/include`.
That alone does **not** build these examples, because `safe_format.h` is not in
either directory; it lives in `stdlib/safe/`:

```
$ /usr/bin/clang -std=c99 -Ilibc/install-include -Ithird_party/safeclib/include \
    examples/crt/printf_s.c <both archives> -o <out>
examples/crt/printf_s.c:2:10: fatal error: 'safe_format.h' file not found
    2 | #include "safe_format.h"
      |          ^~~~~~~~~~~~~~~
1 error generated.
```

`-Istdlib/safe` was therefore added verbatim to every C build. No source file
under `examples/` was modified. This is a build-environment correction, not a
behavioural change.

## Exact results (verbatim `compare.sh` output)

```
EXAMPLE_COMPARE printf_s MATCH stdout identical, exit=0
EXAMPLE_COMPARE snprintf_s MATCH stdout identical, exit=0
EXAMPLE_COMPARE sprintf_s MATCH stdout identical, exit=0
EXAMPLE_COMPARE vsnprintf_s MATCH stdout identical, exit=0
TOTAL match=4 different=0 blocked=0 of 4
```

Script exit status: `0`.

## Per-example table

| Example | C stdout (exact bytes) | Rust stdout (exact bytes) | C exit | Rust exit | Truncation / refusal modelled | Result |
| --- | --- | --- | --- | --- | --- | --- |
| `printf_s` | `printf_s: ok\n` | `printf_s: ok\n` | 0 | 0 | No truncation (stream API). `%n` refusal modelled by `has_percent_n` guard; format is literal and safe. Return value (`n = rendered.len()`) checked > 0. | **MATCH** |
| `snprintf_s` | `snprintf_s: ok\n` | `snprintf_s: ok\n` | 0 | 0 | Truncation exercised: `b[8]`/`"%s","hi"` → n=2, `b="hi"`; `b`/dmax=4/`"toolong"` → n=7 (would-be length), stored `"too"` + NUL at index 3. `%n` refusal modelled in the sink. | **MATCH** |
| `sprintf_s` | `sprintf_s: ok\n` | `sprintf_s: ok\n` | 0 | 0 | Truncation exercised: `b[16]`/`"%s-%d",("id",7)` → n=4, `b="id-7"`; `small[4]`/`"toolong"` → n=7 (would-be length), `"too"` + NUL at `small[3]`. `%n` refusal modelled. | **MATCH** |
| `vsnprintf_s` | `vsnprintf_s: ok\n` | `vsnprintf_s: ok\n` | 0 | 0 | No truncation in the example (16-byte dest for `"42"` → n=2, `b="42"`). Variadic wrapper modelled with `format_args!` + `fmt::Arguments`. `%n` refusal modelled. | **MATCH** |

`od -An -tx1c` confirmed the captured stdout is identical byte-for-byte in all
four cases (each is the ASCII success line plus one trailing `\n`). All eight
stderr captures (C and Rust, four examples) were **0 bytes**. All C and Rust
build logs were **0 bytes** (clean builds; the Rust sources emit no warnings).

### Supporting evidence for the modelled truncation values

An evidence probe compiled with the same flags and archives (`.build/probe_trunc.c`)
prints the values the C APIs actually returned in the truncation cases:

```
snprintf_s n=2 b=[hi]
snprintf_s n=7 b=[too]
sprintf_s n=4 s=[id-7]
sprintf_s n=7 small=[too] small[3]=0
```

These are exactly the values the Rust bounded sink asserts (`would_be` of 7 for
`"toolong"`, NUL placed at `dmax-1 = 3`).

### Supporting evidence for refusal semantics (not exercised by the 4 examples)

`.build/probe_refuse.c` calls `cplus_printf_s("bad %n format\n")`:

```
stdout: before
stderr: [CPLUS-SAFE] printf_s: forbidden %n specifier in format string
exit:   70
```

The exit 70 comes from `cplus_runtime_violation()` with the default
`g_violation_trap = 1` (`stdlib/safe/src/safe_runtime.c:35-60`). No example
under comparison contains `%n`, a non-literal format, a null destination, or
an out-of-bounds `dmax`, so **no case in this task was BLOCKED** and no refusal
branch was exercised by the compared programs. The Rust code models refusal as
a `-1` return with no write; it cannot reproduce the stderr diagnostic or the
exit-70 trap (see non-equivalence statement).

## Totals

```
MATCH      4  (printf_s, snprintf_s, sprintf_s, vsnprintf_s)
DIFFERENT  0
BLOCKED    0
TOTAL      4
```

## Explicit non-equivalence statement

**A `MATCH` result means only that the observable process behaviour compared
here — captured stdout bytes and exit status — was identical. It does NOT mean
the Rust program is semantically or API-equivalent to the checked-CRT C call.
The two implementations are deliberately non-equivalent in the following ways:**

1. **No checked `_s` API in Rust.** The C side exercises real `cplus_*_s`
   functions that validate arguments, refuse non-literal formats (CWE-134),
   reject `%n`, check destination bounds/UAF, log metadata and record stats.
   The Rust side uses `format!`/`write!` and a hand-written safe sink; it does
   not call any checked-CRT code and does not link the archives.
2. **Format-string safety mechanism differs.** Rust's `format!` rejects malformed
   format strings at compile time; the C API's protection is partly compile-time
   (`__builtin_constant_p` + format attribute) and partly a runtime violation
   path. The Rust `%n` guard is a *model* of the C runtime check, not the check
   itself.
3. **No C variadics / `va_list`.** `vsnprintf_s.rs` uses `format_args!` and
   `std::fmt::Arguments` passed through a generic function; the C example uses
   a real `va_start`/`va_list`/`va_end` wrapper.
4. **No NUL-terminated buffers in safe Rust.** The bounded sink explicitly
   reserves a byte and stores NUL to mimic C `vsnprintf`; Rust strings/`[u8]`
   carry their length and would not need this.
5. **Would-be length is computed, not returned by libc.** The Rust sink sums
   `write_str` lengths and clamps the stored bytes to `dmax-1`; C delegates both
   to the platform `vsnprintf`.
6. **Refusal/trap semantics differ.** On refusal the C API calls
   `cplus_runtime_violation`, which writes to stderr and, with the default trap
   enabled, `_exit(70)`. The Rust model just returns `-1` and has no global trap,
   handler, logging, or statistics. This branch is unexercised by these four
   examples.
7. **`sprintf_s` here is not Annex K.** In this repository `cplus_sprintf_s` is
   a bounded alias of the vsnprintf behaviour (truncates and reports the
   would-be length). The Rust equivalent models that alias, not a true Annex K
   `sprintf_s` that would refuse/trap on insufficient capacity.
8. **Stream sink differs.** C `cplus_printf_s` writes via `vprintf` to `FILE*`
   stdout; Rust uses `print!` on `std::io::Stdout`. The bytes happened to match,
   but buffering and error-reporting behaviour are not the same.
9. **Scope of evidence.** These results do not validate the C archives, the
   Win32/x86 build path, or live injected-game behaviour; they only compare the
   stdout/exit behaviour of four small standalone examples against a safe-Rust
   behavioural model.
