# Wide format contract (`cplus_*` wide formatted I/O)

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23
**Finding addressed:** `AUDIT_numeric_wide.md` F01 (HIGH) and F11 (MEDIUM).

This document states the *real* safety boundary for the wide formatted-I/O
wrappers in `libc/src/wchar/wchar_format.c`. It deliberately makes **no claim
of compile-time format checking** on this host: none is performed (see §3).

## 1. Source of truth and delegation

All twelve ISO C17 wide formatted-I/O wrappers are thin, namespaced delegates
to the identically named host functions; the `v`-forms pass the caller's
`va_list` straight through:

| Wrapper | Host backend |
| --- | --- |
| `cplus_wprintf` / `cplus_vwprintf` | `vwprintf` |
| `cplus_fwprintf` / `cplus_vfwprintf` | `vfwprintf` |
| `cplus_swprintf` / `cplus_vswprintf` | `vswprintf` |
| `cplus_wscanf` / `cplus_vwscanf` | `vwscanf` |
| `cplus_fwscanf` / `cplus_vfwscanf` | `vfwscanf` |
| `cplus_swscanf` / `cplus_vswscanf` | `vswscanf` |

Format parsing, conversions, locale handling, buffering, return counts and
error reporting belong to the host C library. This layer adds only the
defined-UB input guards (NULL format / destination / stream) documented in
the source file. It does not interpose, replace or redefine any host symbol.

## 2. The two tiers

### Tier 1 — standard, unchanged (`cplus_swprintf`, `cplus_vswprintf`, …)

These wrappers retain exact standard semantics for backward compatibility.
In particular **`%n` is NOT detected, rejected or neutralised**: doing so
would silently change standard format behaviour. A caller who passes
`L"%n"` to `cplus_swprintf` gets the host's `%n` write-back, as before.
Format validity — including that each conversion matches the number and type
of the supplied arguments — and destination capacity remain entirely the
caller's responsibility. **Never pass an untrusted format string to Tier 1.**

### Tier 2 — opt-in checked sibling (`cplus_swprintf_checked`)

```c
int cplus_swprintf_checked(wchar_t *s, size_t n, const wchar_t *fmt, ...);
```

Additional behaviour, before any `va_start` or host call:

1. The existing NULL guards apply (`!fmt`, or `n > 0 && !s`).
2. The format is scanned for a conversion whose specifier is `n`.
   When found, the function returns `-1` with `errno = EINVAL` **without
   writing to `s` and without consuming the `va_list`**.
3. Otherwise it delegates to host `vswprintf` and returns the host result.

The scan respects `%%` (a literal percent is not a conversion) and consumes
flags, width, precision, `*`, length modifiers (`hh h l ll j z t L q`) and
POSIX positional `N$` specifiers, so all of these are refused:

```
%n   %hn   %hhn   %ln   %lln   %jn   %zn   %tn
%1$n   %2$hn   %*n   %*1$n   %05n   "prefix %d %n"   %%%n
```

while these are allowed and formatted normally:

```
L"%%n"        -> prints the literal text "%n"
L"%d %ls"     -> ordinary wide formatting
```

**What Tier 2 is NOT:** it is a `%n` deny-list, not a general format
validator. It does not check argument count/type agreement, buffer capacity,
huge width/precision (F19), or any other malformed-format case. Those remain
caller responsibilities exactly as in Tier 1. Tier 2 is a deliberate
"trusted-format helper" for callers who want the dangerous write-back
conversion rejected; it does not make an untrusted format safe in general.

Logging is opt-in and must not affect the contract: the refusal sets `errno`
*after* the `cplus_libc_log` call, so an enabled logger (whose `write(2)` may
clobber `errno`, F16) cannot change the return value or `errno`. Logging ON
and OFF are parity-equivalent.

## 3. Compiler format attributes — honest statement (F11)

The source annotates the variadic wrappers with
`__attribute__((format(wprintf, …)))` / `format(wscanf, …)` macros and
mirrors the header pattern. On this host (**Apple Clang 16**) the wide
attribute kinds are **not implemented**: Clang emits `-Wignored-attributes`
and ignores them, so the diagnostic is suppressed locally and these
annotations are **documentary only — zero static format checking occurs**.
The narrow kinds (`format(printf, …)` / `format(scanf, …)`) cannot be applied
to a `const wchar_t *` format parameter (Clang rejects "format argument not
a string type"), so they are not used on these wide signatures either.

Consequently:

* There is **no compile-time checking** of wide format strings on this host,
  for either tier. The Tier-2 `%n` check is a **runtime** scan.
* Do not describe any `cplus_*` wide wrapper as "compiler-checked" or
  "statically validated". The format is caller-trusted, full stop.
* A future toolchain that implements the wide attribute kinds could add
  diagnostics for literal formats; that is a toolchain change, not a property
  of this library, and is not claimed here.

## 4. Verification evidence

* `tests/libc/test_wchar_format.c`: Tier-1 backward-compatibility test
  (`L"ab%n"` writes `2` through the sentinel, as the host does); Tier-2
  `%n` refusals across length/positional/escaped variants with a sentinel
  `int *`, asserting `-1`, `errno == EINVAL`, sentinel unchanged and the whole
  destination buffer unchanged; normal Tier-2 formatting; `%%n` accepted as a
  literal; and logging ON/OFF parity for every refusal.
* Build: `sh tests/libc/build_libc.sh` (static `libcplus_hardened.a`).
* Run: `sh tests/libc/run_libc_tests.sh` (compiles and runs every
  `tests/libc/test_*.c`, including `test_wchar_format.c`).
* Sanitizer: the same test compiled and linked with `-fsanitize=undefined`
  against an instrumented `libcplus_hardened.a`.
* These are local build/harness results only; no live-game or production
  runtime claim is made.
