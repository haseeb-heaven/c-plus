# Per-method memory-safety comparison — field / first-last / classify family

Scope: the 11 assigned methods, compared one method at a time between the C+
checked APIs in `build/safe/libcplus_safecrt.a` and safe-Rust equivalents.

| # | method | status in archive at verification time |
|---|--------|----------------------------------------|
| 1 | `strcmpfld_s`        | AVAILABLE (`cplus_v__strcmpfld_s_chk`) |
| 2 | `strcpyfld_s`        | AVAILABLE (`cplus_v__strcpyfld_s_chk`) |
| 3 | `strcoll_s`          | AVAILABLE (`cplus_v__strcoll_s_chk`) |
| 4 | `strfirstchar_s`     | AVAILABLE (`cplus_v__strfirstchar_s_chk`) |
| 5 | `strlastchar_s`      | AVAILABLE (`cplus_v__strlastchar_s_chk`) |
| 6 | `strfirstdiff_s`     | AVAILABLE (`cplus_v__strfirstdiff_s_chk`) |
| 7 | `strlastdiff_s`      | AVAILABLE (`cplus_v__strlastdiff_s_chk`) |
| 8 | `strisalphanumeric_s`| **BLOCKED** — absent from archive |
| 9 | `strisascii_s`       | **BLOCKED** — absent from archive |
| 10 | `strisdigit_s`       | **BLOCKED** — absent from archive |
| 11 | `strishex_s`         | **BLOCKED** — absent from archive |

The C+ symbols are the `cplus_v_`-prefixed upstream `_chk` entry points emitted
by `libc/src/safecrt_vendor/field_group.c` and `firstlast_group.c` (included
verbatim from pinned `rurban/safeclib` with symbol renames only). No vendor file
was edited by this task.

## Toolchains (exact)

```
$ /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

$ nm --version | head -1
llvm-nm, compatible with GNU nm

$ command -v timeout
/usr/bin/timeout
```

Archive under test: `build/safe/libcplus_safecrt.a`
(observed `Nov 23 10:28:26 2026`, 458520 bytes during final verification).
Parallel agents were rebuilding this archive concurrently; the driver
re-probes it on every run, so results track whatever the archive actually
contains rather than a hard-coded assumption.

## Availability verification (exact commands + evidence)

From the repository root:

```
nm -g build/safe/libcplus_safecrt.a | grep -E 'strcmpfld_s|strcpyfld_s|strcoll_s|strfirstchar_s|strlastchar_s|strfirstdiff_s|strlastdiff_s|strisalphanumeric_s|strisascii_s|strisdigit_s|strishex_s'
```

Exact output (available methods), reproduced verbatim as `nm_available.txt`:

```
0000000000000000 T _cplus_v__strcmpfld_s_chk
0000000000000000 T _cplus_v__strfirstchar_s_chk
0000000000000104 T __strfirstchar_s_uchk
000000000000010c T __strcmpfld_s_uchk
0000000000000150 T _cplus_v__strcpyfld_s_chk
000000000000015c T _cplus_v__strlastchar_s_chk
0000000000000268 T __strlastchar_s_uchk
00000000000002c0 T _cplus_v__strfirstdiff_s_chk
00000000000003dc T __strfirstdiff_s_uchk
0000000000000428 T _cplus_v__strlastdiff_s_chk
0000000000000554 T __strlastdiff_s_uchk
00000000000005d0 T _cplus_v__strcoll_s_chk
00000000000006b8 T __strcoll_s_uchk
```

Exact evidence for each BLOCKED method — all four greps produced **no output,
exit status 1**:

```
$ nm -g build/safe/libcplus_safecrt.a | grep strisalphanumeric_s   # (nothing)
$ nm -g build/safe/libcplus_safecrt.a | grep strisascii_s          # (nothing)
$ nm -g build/safe/libcplus_safecrt.a | grep strisdigit_s          # (nothing)
$ nm -g build/safe/libcplus_safecrt.a | grep strishex_s            # (nothing)
```

No `field_group.c`/`firstlast_group.c` classification translation unit
(e.g. a `class_group.c` exporting `cplus_v__stris*_chk`) existed in
`libc/src/safecrt_vendor/` at verification time, and no `cplus_v_*stris*`
symbol exists in the archive. These four methods are therefore reported as
BLOCKED in `METHOD_FIELD ... <blocked>`; **no result tokens are fabricated for
them** — they are compiled out of both harnesses by the driver. If a later
rebuild adds those symbols, `compare.sh` detects them automatically and the
guarded test paths in both harnesses (already present and compile-checked)
become active.

## Exact build / run commands

All commands run from
`benchmarks/safety_compare/per_method/field/` unless a path says otherwise.
`$ROOT` is the repository root; `$AVAIL` is the comma-separated list of
methods the driver found in the archive.

C harness (clang, C99, archives, 30 s timeout):

```
/usr/bin/clang -std=c99 \
  -I$ROOT/libc/install-include \
  -I$ROOT/third_party/safeclib/include \
  -DHAVE_STRCMPFLD_S -DHAVE_STRCPYFLD_S -DHAVE_STRCOLL_S \
  -DHAVE_STRFIRSTCHAR_S -DHAVE_STRLASTCHAR_S \
  -DHAVE_STRFIRSTDIFF_S -DHAVE_STRLASTDIFF_S \
  field_harness.c $ROOT/build/safe/libcplus_safecrt.a -o field_harness
timeout 30 ./field_harness > c.out
```

Rust harness (safe-only, rustc -O, 30 s timeout):

```
rustc -O field.rs -o field
timeout 30 ./field "$AVAIL" > rust.out
```

`AVAIL` for the final run:

```
strcmpfld_s,strcpyfld_s,strcoll_s,strfirstchar_s,strlastchar_s,strfirstdiff_s,strlastdiff_s
```

Driver:

```
./compare.sh
```

with exit status 0 (no mismatches).

## Token vocabulary and cases

For every available method the C harness prints exactly six deterministic
lines `<method> <case> <token>`:

| case | meaning | C token source | Rust token source |
|------|---------|----------------|-------------------|
| `valid` | valid input produces the expected result | checked `_chk` return code + output | safe iterator/slice result |
| `null` | NULL pointer input | constraint handler fired / rc != 0 -> `REFUSED` | `Option<&[u8]> = None` -> `REFUSED` |
| `zero` | zero length (`dmax == 0`) | constraint violation -> `REFUSED` | `safe_prefix(.., 0)` -> `None` -> `REFUSED` |
| `oneover` | length one past the destination object | `CHK_DEST_OVR` / `ESNOSPC` -> `REFUSED` | bounds check `len > capacity` -> `None` -> `REFUSED` |
| `delimiter` | NUL/embedded-NUL handling | result matches field/string NUL semantics | `split` at NUL (or byte-include for field APIs) |
| `canary` | bytes beyond the field are untouched | explicit guard-byte check -> `CANARY_INTACT` | bounds-checked writes cannot reach guard -> `CANARY_INTACT` |

Result tokens: `CMP_LT`, `COPY_OK`, `POS_<n>`, `DIFF_<n>`, `DELIM_OK`,
`CANARY_INTACT`. Diagnostic tokens `BAD` / `CANARY_CORRUPT` never appeared in
the final run (verified with `grep -cE 'BAD|CORRUPT' c.out` -> `0`).

## Exact outputs

`c.out` and `rust.out` are byte-identical (42 lines each); `diff -u` is empty.
Verbatim content (both files):

```
strcmpfld_s valid CMP_LT
strcmpfld_s null REFUSED
strcmpfld_s zero REFUSED
strcmpfld_s oneover REFUSED
strcmpfld_s delimiter DELIM_OK
strcmpfld_s canary CANARY_INTACT
strcpyfld_s valid COPY_OK
strcpyfld_s null REFUSED
strcpyfld_s zero REFUSED
strcpyfld_s oneover REFUSED
strcpyfld_s delimiter DELIM_OK
strcpyfld_s canary CANARY_INTACT
strcoll_s valid CMP_LT
strcoll_s null REFUSED
strcoll_s zero REFUSED
strcoll_s oneover REFUSED
strcoll_s delimiter DELIM_OK
strcoll_s canary CANARY_INTACT
strfirstchar_s valid POS_2
strfirstchar_s null REFUSED
strfirstchar_s zero REFUSED
strfirstchar_s oneover REFUSED
strfirstchar_s delimiter DELIM_OK
strfirstchar_s canary CANARY_INTACT
strlastchar_s valid POS_5
strlastchar_s null REFUSED
strlastchar_s zero REFUSED
strlastchar_s oneover REFUSED
strlastchar_s delimiter DELIM_OK
strlastchar_s canary CANARY_INTACT
strfirstdiff_s valid DIFF_2
strfirstdiff_s null REFUSED
strfirstdiff_s zero REFUSED
strfirstdiff_s oneover REFUSED
strfirstdiff_s delimiter DELIM_OK
strfirstdiff_s canary CANARY_INTACT
strlastdiff_s valid DIFF_4
strlastdiff_s null REFUSED
strlastdiff_s zero REFUSED
strlastdiff_s oneover REFUSED
strlastdiff_s delimiter DELIM_OK
strlastdiff_s canary CANARY_INTACT
```

Driver output (stdout after the four BLOCKED diagnostics on stderr):

```
METHOD_FIELD 42 42 0 4
```

Interpretation: 42 case lines compared, 42 token matches, 0 mismatches,
4 methods BLOCKED (absent from the archive). `compare.sh` exits 0; it exits
nonzero only on a real token mismatch or a harness build/run failure.

## EXPLICIT NON-EQUIVALENCE NOTE

This is a **safety-outcome comparison, not a semantic/ABI equivalence proof**.
The two harnesses are deliberately NOT the same program:

* The C harness calls the real checked functions in `libcplus_safecrt.a`
  (upstream `rurban/safeclib` bodies) and observes their runtime-constraint
  behaviour. The Rust harness calls **no C function at all**: it is written in
  safe Rust only (`Option`, bounds-checked slices, `split`, `split_whitespace`,
  `chars`, iterator `cmp`/`position`/`rposition`, `char::is_alphanumeric`,
  `is_ascii`, `is_ascii_digit`, `is_ascii_hexdigit`), with zero `unsafe` blocks.
* "REFUSED" on the Rust side is a *modelled* outcome: safe Rust has no raw
  pointer that can be NULL, so NULL is represented as `Option::None`, and
  zero/one-over requests as lengths rejected by a capacity check. It shows
  that safe Rust refuses the same unsafe situations, **not** that it returns
  the same `errno_t`/constraint codes as the C API.
* The `canary` case is checked explicitly in C (guard bytes read back). In
  Rust it is a structural guarantee of bounds-checked writes, not a runtime
  canary; the token means "no out-of-bounds write is expressible", not "an
  identical guard region was inspected".
* `strcoll_s` is locale-sensitive (`LC_COLLATE`). The C harness pins
  `setlocale(LC_ALL, "C")`; the Rust side uses `str::cmp` on the NUL-prefix.
  These agree only for the ASCII inputs used here. Non-ASCII/locale-specific
  collation is **not** covered and is not claimed equivalent.
* Field APIs (`strcmpfld_s`, `strcpyfld_s`) treat NUL as an ordinary byte;
  string APIs stop at NUL. The delimiter cases exercise exactly that
  distinction with fixed ASCII inputs; broader Unicode/embedded-NUL corpora
  are out of scope.
* Tokens are coarse. They assert the safety-relevant result category
  (accept/refuse, direction/position, guard intact), not full return-code,
  locale, or multi-byte semantics.

Consequently a 42/42 token match supports the narrow claim that, for the
enumerated cases, safe-Rust idioms and the C+ checked APIs exhibit the same
memory-safety outcomes. It is not evidence of general behavioural, locale,
or ABI equivalence between the two languages' facilities.

## Files

* `field_harness.c` — C99 harness (guarded per method via `-DHAVE_*`)
* `field.rs` — safe-Rust harness (`rustc -O`, no `unsafe`)
* `compare.sh` — availability probe, build/run both, diff tokens, summary
* `NOTES.md` — this file
* `c.out`, `rust.out`, `token.diff`, `c.err`, `rust.err`, `*_compile.err` —
  generated evidence from the last `compare.sh` run
* `nm_available.txt` — verbatim `nm -g ... | grep ...` capture for available
  methods (exact addresses, no transcription)
