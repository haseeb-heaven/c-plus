# Per-method memory-safety comparison — format / numeric conversion

Scope: C+ checked format APIs vs. safe-Rust equivalents, one deterministic
token per `(method, case)` pair. Only files in this directory are owned by this
comparison.

## Toolchains (exact)

| Item | Value |
|------|-------|
| C compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin27.2.0` |
| C standard | `-std=c99` |
| Rust compiler | `rustc 1.100.0 (0ef1bc386 2026-11-09)` via `/Users/haseeb-mir/.cargo/bin/rustc` (driver uses `RUSTC=${RUSTC:-rustc}`) |
| Rust flags | `-O` |
| Symbol tool | `/usr/bin/nm -gU` |
| Per-process timeout | `timeout 30` |
| Archives | `build/safe/libcplus_safecrt.a` (424320 B), `build/libc/libcplus_hardened.a` (276128 B) |

## Symbol verification (nm evidence)

Command:

```sh
/usr/bin/nm -gU build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
```

Present symbols (all six methods testable, `blocked = 0`):

```
00000000000003b0 T _cplus_snprintf_s
0000000000000434 T _cplus_snprintf_s_checked
00000000000016dc T _cplus_sprintf_s
0000000000001794 T _cplus_sprintf_s_checked
0000000000000000 T _cplus_vsnprintf_s
0000000000000104 T _cplus_strtol
0000000000000150 T _cplus_strtoul
00000000000001d0 T _cplus_strtoimax
```

Dedicated Annex-K-style conversion symbols are **absent** in both archives:

```sh
/usr/bin/nm -gU build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  | grep -iE 'strtol_s|strtoul_s|strtoimax_s|strtol_checked|strtoul_checked|strtoimax_checked'
# (no output)
```

So `sprintf_s` / `snprintf_s` are exercised through their `_checked` entry
points (`cplus_sprintf_s_checked`, `cplus_snprintf_s_checked`, which are what
the public unprefixed macros expand to); `vsnprintf_s` through
`cplus_vsnprintf_s`; and the three numeric conversions through the thin
platform-backed wrappers `cplus_strtol` / `cplus_strtoul` / `cplus_strtoimax`
(standard `errno == ERANGE` overflow contract). No method is marked BLOCKED.

## Exact build/run commands

From the repository root:

```sh
D=benchmarks/safety_compare/per_method/format

# C+ harness
/usr/bin/clang -std=c99 \
  -Istdlib/safe -Ilibc/install-include -Ithird_party/safeclib/include \
  "$D/cplus_format.c" \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o "$D/.build/cplus_format"
timeout 30 "$D/.build/cplus_format" > "$D/.build/cplus.out"

# Rust harness
rustc -O "$D/format.rs" -o "$D/.build/format_bin"
timeout 30 "$D/.build/format_bin" > "$D/.build/rust.out"

# Driver (does all of the above plus the diff and summary)
"$D/compare.sh"
```

`-Istdlib/safe` is required in addition to the two include directories named in
the task brief because the checked format declarations live in
`stdlib/safe/safe_format.h` (and the trap toggle in `stdlib/safe/safe_runtime.h`).

The harness calls `cplus_runtime_set_violation_trap(0)` once at startup so that
guarded violations return `-1` rather than `_exit(70)`; otherwise a `%n` case
would kill the process before emitting its token. The `[CPLUS-SAFE] ...`
violation diagnostics are written to **stderr**; stdout contains only tokens.

## Cases and token semantics

| Case | Meaning | C+ observable | Rust observable | Token |
|------|---------|---------------|-----------------|-------|
| `exact_fit` | 5-char `"AB123"` into a 6-byte destination | return 5, buffer `"AB123"` | `write!` into `FixedWriter` cap 5 succeeds | `OK` |
| `null_dest` | destination pointer is null | `!dest && dmax != 0` → `-1` | `Option<&mut [u8]>` is `None` → refused | `REFUSED` |
| `one_short` | 5-char payload with `dmax == 5` (one byte short of a NUL-terminated fit) | bounded copy of `"AB12"`, would-be length 5 | `write!` returns `fmt::Error` when capacity is 4 | `TRUNC` |
| `pct_n` | format contains `%n` | `cplus_fmt_has_percent_n` → `-1` | no `%n` conversion exists → refused | `REFUSED` |
| `canary` | 4-byte guards either side of the destination | guards unchanged after exact-fit write | guards unchanged after safe write | `OK` |
| `valid` (numeric) | parse `"12345"` | value 12345, `errno == 0` | `parse::<i64/u64>()` succeeds | `OK` |
| `overflow` (numeric) | parse 33 nines | `errno == ERANGE` | `parse::<T>()` returns `Err(PosOverflow)` | `OVERFLOW_REFUSED` |

Format methods emit 5 cases each; numeric methods emit 2 cases each; total
21 lines.

## Observed stdout (identical for both harnesses)

```
sprintf_s exact_fit OK
sprintf_s null_dest REFUSED
sprintf_s one_short TRUNC
sprintf_s pct_n REFUSED
sprintf_s canary OK
snprintf_s exact_fit OK
snprintf_s null_dest REFUSED
snprintf_s one_short TRUNC
snprintf_s pct_n REFUSED
snprintf_s canary OK
vsnprintf_s exact_fit OK
vsnprintf_s null_dest REFUSED
vsnprintf_s one_short TRUNC
vsnprintf_s pct_n REFUSED
vsnprintf_s canary OK
strtol valid OK
strtol overflow OVERFLOW_REFUSED
strtoul valid OK
strtoul overflow OVERFLOW_REFUSED
strtoimax valid OK
strtoimax overflow OVERFLOW_REFUSED
```

Observed C+ stderr (3 lines, one per format method's `pct_n` case; not part of
the comparison):

```
[CPLUS-SAFE] vsnprintf_s: forbidden %n specifier in format string
[CPLUS-SAFE] vsnprintf_s: forbidden %n specifier in format string
[CPLUS-SAFE] vsnprintf_s: forbidden %n specifier in format string
```

## Driver summary (exact)

```
$ benchmarks/safety_compare/per_method/format/compare.sh
METHOD_FORMAT 21 21 0 0
$ echo $?
0
```

The driver also verifies every required symbol with `nm` before compiling; a
missing symbol would print `METHOD_FORMAT 0 0 0 6` and exit 2. Mismatch counting
was sanity-checked by perturbing one Rust token: the driver then reported
`21 20 1` with a `MISMATCH strtol overflow ...` diagnostic and a nonzero exit.

## Explicit non-equivalence note

These harnesses compare *observable safety tokens*, not language-level
equivalence:

* **Annex K `sprintf_s` truncation.** C11 Annex K `sprintf_s` is specified to
  fail (constraint handler / `ESNOSPC`) when the output does not fit. The C+
  implementation deliberately aliases `sprintf_s`/`snprintf_s` to bounded
  `vsnprintf` semantics (see `stdlib/safe/src/safe_format.c`), so a
  one-byte-too-small destination yields `TRUNC` (in-bounds, NUL-terminated),
  **not** `REFUSED`. Safe Rust's `write!` to a fixed-capacity writer likewise
  reports an error instead of writing out of bounds, so both sides emit
  `TRUNC`. The task brief's "one-byte-too-small → REFUSED" expectation does
  not hold for these particular APIs; the token records observed behaviour.
* **Null destinations.** Safe Rust cannot construct a null `&mut [u8]`; the
  `REFUSED` token is modelled with `Option::None`. This is a behavioural model
  of the C+ guard, not a literal null-pointer test.
* **`%n`.** Rust's `format!` grammar has no `%n` conversion at all (the `%`
  character is literal), so there is no write-through-pointer surface to
  compare; the refusal token is produced by a literal-format check in the
  Rust harness.
* **Numeric conversions.** `_cplus_strtol/_cplus_strtoul/_cplus_strtoimax` are
  thin platform-backed wrappers, not dedicated `strtol_s`-style checked APIs
  (no such symbols exist in either archive). Their overflow contract is the
  standard `errno == ERANGE`; `str::parse::<T>()` is a distinct API whose
  overflow is an `Err`. Both are represented by `OVERFLOW_REFUSED`.
* **Width.** `intmax_t` / `long` / `unsigned long` are 64-bit on the target
  (LP64 macOS arm64); the Rust side uses `i64` / `u64` accordingly.
* **Determinism.** No randomness, wall-clock, thread scheduling, or live-game
  state is involved; re-running the driver yields the same 21 tokens.
