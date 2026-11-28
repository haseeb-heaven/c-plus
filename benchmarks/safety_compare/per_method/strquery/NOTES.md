# Per-method memory-safety comparison: C+ checked string-query APIs vs Rust

Scope of this directory only. No source, compiler, build script, or archive was
modified. Files produced here:

| file | role |
| --- | --- |
| `nm_evidence.txt` | timestamped `nm` snapshot proving which symbols exist |
| `harness.c` | deterministic C99 harness using the checked C+ entry points |
| `strquery.rs` | safe-Rust modelling harness (`#![forbid(unsafe_code)]`) |
| `compare.sh` | builds, runs both under `timeout 30`, diffs tokens, prints summary |
| `NOTES.md` | this file |

## 1. Toolchains (exact)

| tool | version / path |
| --- | --- |
| C compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target arm64-apple-darwin27.2.0 |
| C standard | `-std=c99 -O2 -pedantic` |
| Rust compiler | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| Rust flags | `-O`, plus `#![forbid(unsafe_code)]` in source |
| timeout | BSD `timeout` from PATH, 30 s (`STRQUERY_TIMEOUT` overrides) |
| include dirs | `-Ilibc/install-include -Ithird_party/safeclib/include` |
| archives | `build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a`, `-lm -pthread` |

## 2. Archive fingerprint and volatility

The archive is being rebuilt by parallel work in this repository. Evidence was
captured twice; both snapshots are consistent for the symbol set used here.

* snapshot 1: sha256 `031d80636c4646cb2476c18d499c03dcbdb607e624bc9d1b1b1adfd88cbe6460`, mtime `2026-11-23T10:25:01`
* snapshot 2: sha256 `1846c418cc051b684fa2ca4bcf2c1fef742e24235ad16845041ad7959344d2db`, mtime `2026-11-23T10:26:17`
* snapshot 3 (final, in `nm_evidence.txt`): sha256 `374705ddbd488cfbf74455dfcc5e92f2967c0e0c812e0733bcc72f1fedd1f13b`

The byte hash changed three times while this comparison was written; the
**symbol set for all eight requested methods stayed identical** across all
three snapshots. This is stated explicitly so nobody mistakes the hash line in
`nm_evidence.txt` for a permanent archive identity.

`compare.sh` intentionally does **not** trust the snapshot for availability: it
re-runs `nm -g --defined-only "$ARCHIVE"` on every invocation. That is why the
results remain valid across the two rebuilds. `nm_evidence.txt` is the exact,
capturable proof of what the grep returned at snapshot time.

## 3. Symbol verification (`nm` — the archive is the source of truth)

Command shape (METHOD substituted), also recorded verbatim in `nm_evidence.txt`:

```sh
nm -g --defined-only build/safe/libcplus_safecrt.a | \
  grep -E "cplus_(sc|v)_.*METHOD|_(METHOD)([^A-Za-z0-9_]|$)"
```

Mach-O `nm` prefixes C symbols with `_`; `T` means defined text symbol.
Result:

| method | nm result | harness entry point | status |
| --- | --- | --- | --- |
| `strlen_s` | `(no matching symbol -> BLOCKED)` | — | **BLOCKED** |
| `strnlen_s` | `_cplus_sc_strnlen_s`, `_cplus_sc__strnlen_s_chk`, `_cplus_strnlen_s`, `_cplus_upstream_strnlen_s` | `cplus_sc_strnlen_s` | available |
| `strchr_s` | `_cplus_v__strchr_s_chk` | `cplus_v__strchr_s_chk` | available |
| `strrchr_s` | `_cplus_v__strrchr_s_chk` | `cplus_v__strrchr_s_chk` | available |
| `strstr_s` | `_cplus_v__strstr_s_chk` | `cplus_v__strstr_s_chk` | available |
| `strcasestr_s` | `_cplus_v__strcasestr_s_chk` | `cplus_v__strcasestr_s_chk` | available |
| `strspn_s` | `_cplus_v__strspn_s_chk` | `cplus_v__strspn_s_chk` | available |
| `strcspn_s` | `_cplus_v__strcspn_s_chk` | `cplus_v__strcspn_s_chk` | available |

`strlen_s` is blocked intentionally and honestly: no `cplus_sc_*` or `cplus_v_*`
symbol for it exists in `build/safe/libcplus_safecrt.a` (it is not part of the
pinned safeclib API; `strnlen_s` is the corresponding bounded API). No call site
and no token were fabricated for it. Note that vendored *sources* for some of
the `cplus_v_*` methods exist under `libc/src/safecrt_vendor/`, but source files
are not linkable symbols; availability was decided solely by `nm` on the archive.

For the six `cplus_v_*` methods the archive exports the `_chk` entry point, not
a separate public wrapper. The harness calls the `_chk` entry point directly
with `destbos`/`srcbos` set to `BOS_UNKNOWN = (size_t)-1`, which is the same
setting the public wrappers use when the object size is not statically known.

## 4. Exact build commands (run from repository root)

```sh
/usr/bin/clang -std=c99 -O2 -pedantic \
  -Ilibc/install-include -Ithird_party/safeclib/include \
  benchmarks/safety_compare/per_method/strquery/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o harness_c

rustc -O benchmarks/safety_compare/per_method/strquery/strquery.rs -o harness_rs
```

`compare.sh` performs the same builds into a `mktemp -d` directory.

## 5. Execution and case/token contract

`compare.sh` runs each binary with `timeout 30`. Every line is exactly
`<method> <case> <token>`. Six cases per available method:

| case | C+ side | Rust side | token when safe |
| --- | --- | --- | --- |
| `valid` | real checked call with a valid buffer/bound | safe `find`/`rfind`/`find`/`char_indices`/`matches` on a bounded `get` view | `OK:<index-or-count>` |
| `null` | NULL passed as the primary pointer argument | `Option<&str> = None` | `REFUSED` |
| `zero_bound` | `dmax`/`smax` = 0 | bound = 0 | `REFUSED` |
| `one_over_bound` | bound = `RSIZE_MAX_STR + 1` (4097) | bound = 4097 | `REFUSED` |
| `not_found` | no match/NUL inside the bound | same via safe search | `NOTFOUND` or `NOTFOUND:<n>` |
| `canary` | re-read 8 canary bytes after the string region | safe `get(8..16)` compared to expected bytes | `CANARY_OK` |

`one_over_bound` means **one over the policy maximum** (`RSIZE_MAX_STR = 4096`,
from `third_party/safeclib/include/safe_types.h`), i.e. a constraint violation
returning `ESLEMAX`. It does *not* mean one byte past the allocated object.
`strlen_s` contributes 0 cases.

Concrete per-method valid inputs (kept identical on both sides):

| method | valid input | expected token |
| --- | --- | --- |
| `strnlen_s` | `"abc\0"`, smax 8 | `OK:3` |
| `strchr_s` | `"abc"`, dmax 3, `'c'` | `OK:2` |
| `strrchr_s` | `"abac"`, dmax 4, `'a'` (last) | `OK:2` |
| `strstr_s` | `"abcabc"`, dmax 6, needle `"bc"` | `OK:1` |
| `strcasestr_s` | `"aBcDef"`, dmax 6, needle `"CD"` | `OK:2` |
| `strspn_s` | `"aabbxy"`, dmax 6, set `"ab"` | `OK:4` |
| `strcspn_s` | `"abcXY"`, dmax 5, set `"XY"` | `OK:3` |

The Rust harness uses only safe primitives from the permitted set: `len`,
`find`, `rfind`, `matches`, `char_indices`, `get` (plus `to_lowercase` for the
case-insensitive method and `Option` for the NULL model). `strlen_s` is absent
from the Rust output too — there is nothing to compare it against.

## 6. Exact observed output

C and Rust produced byte-identical token streams (42 lines each). Representative
lines:

```
strnlen_s valid OK:3
strnlen_s null REFUSED
strnlen_s zero_bound REFUSED
strnlen_s one_over_bound REFUSED
strnlen_s not_found NOTFOUND:5
strnlen_s canary CANARY_OK
strcasestr_s valid OK:2
strspn_s valid OK:4
strcspn_s not_found NOTFOUND:0
```

`compare.sh` output (clean run, exit status 0):

```
BLOCKED strlen_s (no cplus_sc_*/cplus_v_* symbol in build/safe/libcplus_safecrt.a; see nm_evidence.txt)
DETAIL strlen_s 0 0 0 1
DETAIL strnlen_s 6 6 0 0
DETAIL strchr_s 6 6 0 0
DETAIL strrchr_s 6 6 0 0
DETAIL strstr_s 6 6 0 0
DETAIL strcasestr_s 6 6 0 0
DETAIL strspn_s 6 6 0 0
DETAIL strcspn_s 6 6 0 0
METHOD_STRQUERY 42 42 0 1
```

Driver failure mode was verified by temporarily changing one Rust token to
`OK:102`; the driver printed `MISMATCH strnlen_s valid c='OK:3' rust='OK:102'`,
reported `METHOD_STRQUERY 42 41 1 1`, and exited 1. The source was restored and
the clean run reconfirmed (exit 0). Structural guards also fail on a missing
line, a `CANARY_BAD` token, or a `BAD_ACCEPTED`/`BAD_RESULT` token, so a shared
wrong answer on both sides cannot silently pass.

## 7. EXPLICIT NON-EQUIVALENCE STATEMENT

**Token equality here is not safety equivalence, not semantic equivalence, and
not a proof of correctness.** It is equality of printed tokens for a fixed set
of deterministic inputs.

* The C+ functions are *runtime-checked* C APIs. They return an error code (or 0
  for `strnlen_s`) and silently rely on the caller to inspect it. Passing a NULL
  result pointer, a stale pointer, a buffer that is not NUL-terminated within
  `dmax`, or a `dmax` larger than the real object is still undefined behaviour.
  These harnesses deliberately pass `BOS_UNKNOWN`, which **disables the
  object-size check**; safety here rests only on the `dmax`/`slen` policy
  constraints. An over-large `dmax` below `RSIZE_MAX_STR` would overread.
* Rust's safety is *static and structural*: `Option<&str>` makes NULL
  unrepresentable in the valid case, references cannot dangle, and `get(..n)`
  is bounds-checked. Misuse yields a compile error or a bounded panic, not a
  silent out-of-bounds read. The `REFUSED` tokens are modelling choices in safe
  Rust, not a runtime constraint-handler mechanism.
* The data models differ. C operates on NUL-terminated byte buffers; Rust
  `&str` is UTF-8 and has no terminating NUL. Indices are byte offsets in both,
  but Rust's UTF-8 validity rule and multi-byte case folding mean a non-ASCII
  case-insensitive search would not map to the same indices — this comparison
  uses ASCII inputs only, so the mapping is exact here. `strcasestr_s` uses
  ASCII lowercasing on both sides for the same reason.
* The C `canary` case checks that these particular calls did not write past the
  tested region. It says nothing about the general absence of memory errors in
  the checked runtime. The Rust canary is trivially intact because safe code
  cannot write out of bounds.
* `strlen_s` is BLOCKED, so six of the eight requested methods were compared,
  not eight. No result is asserted for the blocked method.

Anyone citing this comparison should quote the summary as:
`METHOD_STRQUERY 42 42 0 1` with the explicit caveat that this means
"42 observable tokens matched; 1 method blocked; no equivalence claim".

## 8. Reproduce

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
benchmarks/safety_compare/per_method/strquery/compare.sh
```
