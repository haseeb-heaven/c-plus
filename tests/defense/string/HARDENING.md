# Checked string APIs — blue-team hardening review (agent 7)

Scope: `stdlib/safe/safe_string.h`, `stdlib/safe/src/safe_string.c`, and the new
regression suite in `tests/defense/string/`.

Backing implementation: the `cplus_sc_*` symbols are safeclib 3.9.1 (vendored)
wrapped by `third_party/safeclib/src/cplus_sc_wrappers.c`; those primitives
already perform their own runtime-constraint checks.  This review therefore
focuses on gaps in the **C+ wrapper layer** and reports honestly when a
reviewed item is already covered downstream.

## Summary

| Reviewed item | Genuine gap? | Action |
|---|---|---|
| Null args (dest/src/indicator) | No | Already refused with ESNULLP (400) by safeclib; `_auto` wrappers pre-check NULL. Test `null_args`. |
| Zero-length defined behavior | No | `n==0` no-ops, `strnlen_s(s,0)==0`, empty-src copy/append defined. Test `zero_length`. |
| One-byte-too-small dest | No | safeclib returns ESNOSPC (406); auto maps to NULL+ERANGE. Test `one_small`. |
| **Unterminated source / destination (bounded scan)** | **YES** | Fixed — see below. Tests `unterminated_*`. |
| Overlap / self-append | No | safeclib returns ESOVRLP (404); auto self-append returns NULL. Test `overlap`. |
| `SIZE_MAX` / oversized `smax` | No | safeclib returns 0 with ESLEMAX (403), no hang. Test `sizemax_smax`. |
| Constraint-handler bypass | No | Default handler is `ignore_handler_s`; all checked primitives invoke it. Verified with a counting handler. Test `handler`. |
| Truncation semantics | No | `strncpy_s`/`strncat_s` honour `slen` and NUL-terminate within `dmax`. Test `truncation`. |
| Return-value ambiguity | No (documented) | Codes are positive (400/401/403/404/406) and must be tested `!= 0`; header already says so. `strnlen_s`'s 0-is-ambiguous is inherent to its signature and was not changed. Test `codes`. |

## The one genuine gap: unbounded `strlen()` in the `_auto` wrappers

**Before.** `cplus_strcpy_s_auto()` computed `len = strlen(src)`,
`cplus_strcat_s_auto()` computed `strlen(dest)` *and* `strlen(src)`, and
`cplus_strncat_s_auto()` computed `strlen(dest)` — all before calling the
bounded safeclib primitive.  A caller-supplied buffer with no NUL made
`strlen()` read without limit, past the end of the object.  This is a classic
exploitable over-read (information leak / fault) that occurs *before* any
constraint check can refuse the operation.

`cplus_strncpy_s_auto()` was reviewed and **does not** have this gap: its
fallback dmax is `n+1` and safeclib `strncpy_s` reads at most `slen == n` bytes
from `src`.

**Fix (in `safe_string.c`, file-local/static only).**

* `cplus_safe_str_scan_ceiling(dest, known_dmax)` — smallest provable capacity
  of `dest`: tracked remaining bytes (`cplus_safe_remaining_capacity`) and/or
  the caller's `__builtin_object_size` value, capped at `RSIZE_MAX_STR` (the
  same hard ceiling safeclib enforces).
* `cplus_safe_bounded_strlen(s, ceiling, &out)` — replaces `strlen()` with the
  bounded `cplus_strnlen_s(s, ceiling)` query and fails if no NUL appears
  within the bound.
* `cplus_safe_str_refuse_unterminated(api)` — logs the refusal, records a
  failed statistic, and sets `errno = ERANGE`.

Each affected wrapper now scans at most the destination's provable capacity
and returns `NULL` + `ERANGE` for an unterminated buffer.  A string that is not
terminated within the destination capacity could never have been copied or
appended successfully, so **no valid-input result changes**; only the
malformed case moves from "unbounded read / crash" to "defined refusal".

**Residual limitation (honest).** When the destination capacity is unknown
(`known_dmax == (size_t)-1` and the pointer is not in the tracked allocator),
the scan ceiling falls back to `RSIZE_MAX_STR`.  An unterminated `src` that is
a *separate* small object could then still be scanned past its end — bounded
by `RSIZE_MAX_STR` rather than unbounded as before.  Full closure would need a
source-capacity parameter or mandatory strict capacity mode; neither is an
ABI-neutral change, so this is documented rather than silently altered.

## ABI preservation rationale

* No function signature, symbol name, struct layout, enum value, or public
  macro form was changed.  The header edit is comments only.
* The three new helpers are `static` in `safe_string.c`, so they are not
  exported.  `nm -gU build/safe/libcplus_safecrt.a` shows no new
  `cplus_str*` / `cplus_sc_str*` symbols.
* ABI fingerprint: the layout (`L`) and function-signature (`F`) sections are
  byte-identical to `tests/abi/fingerprints/safe_layer_baseline.txt`, and the
  exported `cplus_str*`/`cplus_sc_str*` symbol set for the safe archive is
  byte-identical.  (Other `_cplus_v_*` symbols that differ from that baseline
  were added by other agents to unrelated sources and are not from this work.)

## Exact build and run commands

On this host the Homebrew `clang` defaults to a missing `MacOSX27.sdk`, so the
tests select `/usr/bin/clang` (overridable with `CC`/`CPLUS_CLANG`).

```
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/defense/string/run.sh
```

Equivalent explicit steps (all with a 30 s per-case budget):

```
CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh
/usr/bin/clang -O1 -g -std=c11 \
  -Istdlib/safe -Ithird_party/safeclib/include \
  tests/defense/string/test_string_hardening.c \
  build/safe/libcplus_safecrt.a -lpthread -lm \
  -o build/safe/test_string_hardening
timeout 30 build/safe/test_string_hardening <case>
```

Cases: `null_args zero_length one_small overlap sizemax_smax truncation codes
handler unterminated_src_strcpy unterminated_src_strcat
unterminated_dest_strcat unterminated_dest_strncat auto_macro_valid`.

The four `unterminated_*` cases place a 32-byte non-NUL buffer flush against a
`PROT_NONE` guard page (`mmap` + `mprotect`), so a pre-fix unbounded read
faults deterministically.  Post-fix, the bounded scan touches only those 32
bytes and the call is REFUSED.

## Before / after results

**Before** (library built from the unmodified `safe_string.c`): 9 behavioural
cases PASS; every exploit reproduction crashes.

```
PASS null_args
PASS zero_length
PASS one_small
PASS overlap
PASS sizemax_smax
PASS truncation
PASS codes
PASS handler
FAIL unterminated_src_strcpy  (exit 138)   # Bus error: 10
FAIL unterminated_src_strcat  (exit 138)   # Bus error: 10
FAIL unterminated_dest_strcat (exit 138)   # Bus error: 10
FAIL unterminated_dest_strncat (exit 138)  # Bus error: 10
PASS auto_macro_valid
```

**After** (hardened `safe_string.c`, full suite, each case under `timeout 30`):

```
PASS null_args
PASS zero_length
PASS one_small
PASS overlap
PASS sizemax_smax
PASS truncation
PASS codes
PASS handler
PASS unterminated_src_strcpy
PASS unterminated_src_strcat
PASS unterminated_dest_strcat
PASS unterminated_dest_strncat
PASS auto_macro_valid
```

The safeclib `IGNORE CONSTRAINT HANDLER:` lines in the raw logs are the default
`ignore_handler_s` reporting the expected 400/401/403/404/406 codes; they are
not failures.  The `handler` case swaps in a counting handler and asserts it
is invoked on a 401 violation, demonstrating there is no constraint-handler
bypass for the checked primitives.
