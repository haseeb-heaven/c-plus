# `vendored_v` — safe-Rust examples for every vendored `cplus_v_*` function

## Scope and file ownership

Only these four files were created, all under
`benchmarks/rust_examples/vendored_v/`:

| File | Role |
|---|---|
| `vendored.rs` | One safe-Rust example per symbol; prints `<api> <case> <token>`. No `unsafe`, no FFI, no raw pointers. |
| `harness.c` | C99 caller for all 26 symbols; exact signatures from `libc/install-include/cplus_vendor_*.h` (plus two locally declared BOS-aware mem entry points — see below). Prints the same lines. |
| `compare.sh` | Builds both under `/usr/bin/timeout 30`, verifies symbols with `nm`, diffs tokens, prints the summary, exits 1 on real mismatch. |
| `NOTES.md` | This record. |

No source outside this directory was modified; no git add/commit was performed.

## Toolchain and environment (recorded exactly)

```
HOST      = Darwin 27.2.0 arm64
rustc     = rustc 1.100.0 (0ef1bc386 2026-11-09)
            binary: rustc / commit-hash: 0ef1bc386059596dada8fb65b50425aab2ea637a
            commit-date: 2026-11-09 / host: aarch64-apple-darwin
            release: 1.100.0 / LLVM version: 23.1.5
clang     = Apple clang version 16.0.0 (clang-1600.0.26.6)
nm        = llvm-nm, compatible with GNU nm   (/usr/bin/nm)
timeout   = /usr/bin/timeout (BSD-flavoured usage; 30 s used everywhere)
LC_ALL    = C   (pinned inside compare.sh)
```

Static inputs (SHA-256, so the result is reproducible):

```
cb2e59cab90219908ac49fa9cece905beb24db8e2b32629f82a45e27b3394da1  build/safe/libcplus_safecrt.a
d40df57ca75dc9dce581bf7b89b38a871210f6cd11edb8f8690c5534d01650c7  build/libc/libcplus_hardened.a
```

These hashes were captured immediately before the final successful run. This
is a shared working tree with other agents rebuilding `build/`, so the
hardened archive hash may change afterwards; re-run `compare.sh` against the
current archives to reproduce the result (the 26 `cplus_v_*` symbols are in
the unchanged safecrt archive).

Both Rust and C builds completed with **no warnings**.

## `nm` evidence — all 26 symbols verified before any call

Command:

```sh
nm -g build/safe/libcplus_safecrt.a | grep ' T _cplus_v_' | sort
```

Exact output (26 lines; count verified with `... | wc -l` = 26):

```
0000000000000000 T _cplus_v__memchr_s_chk
0000000000000000 T _cplus_v__stpcpy_s_chk
0000000000000000 T _cplus_v__strchr_s_chk
0000000000000000 T _cplus_v__strcmpfld_s_chk
0000000000000000 T _cplus_v__strfirstchar_s_chk
0000000000000000 T _cplus_v__strnset_s_chk
0000000000000000 T _cplus_v__strspn_s_chk
0000000000000000 T _cplus_v__strstr_s_chk
0000000000000000 T _cplus_v_strlcat
0000000000000048 T _cplus_v__strtolowercase_s_chk
00000000000000ec T _cplus_v_strlcpy
0000000000000124 T _cplus_v__strset_s_chk
0000000000000138 T _cplus_v__memccpy_s_chk
0000000000000150 T _cplus_v__strcpyfld_s_chk
000000000000015c T _cplus_v__strlastchar_s_chk
000000000000016c T _cplus_v__strrchr_s_chk
0000000000000190 T _cplus_v__strtouppercase_s_chk
00000000000001fc T _cplus_v__stpncpy_s_chk
000000000000021c T _cplus_v__strcspn_s_chk
0000000000000274 T _cplus_v__strnterminate_s_chk
00000000000002c0 T _cplus_v__strfirstdiff_s_chk
00000000000002fc T _cplus_v__strcasestr_s_chk
00000000000003c4 T _cplus_v_memchr_s
00000000000003cc T _cplus_v_memccpy_s
0000000000000428 T _cplus_v__strlastdiff_s_chk
00000000000005d0 T _cplus_v__strcoll_s_chk
```

`compare.sh` re-checks each expected API with
`grep -q " T _${api}$"` against this `nm -g` output. An API with no matching
`T` symbol is reported `BLOCKED <api>` and the script stops before building or
printing any token for it (never fabricated). Verified live with a stub `nm`
that removed `cplus_v__strcoll_s_chk`:

```
BLOCKED cplus_v__strcoll_s_chk: no ' T _cplus_v__strcoll_s_chk' in nm -g .../libcplus_safecrt.a
RUST_EXAMPLES vendored_v 0 0 0 0
```

(exit status 0 — blocked is not a failure; only a real mismatch or build/run
failure is nonzero.)

## Exact build and run commands

From the repository root:

```sh
# C caller (clang_O2); includes come from the checked-in install headers and
# the pinned safeclib headers needed by cplus_vendor_mem.h.
/usr/bin/clang -O2 -std=c99 -Wall -Wextra \
  -Ilibc/install-include -Ithird_party/safeclib/include \
  benchmarks/rust_examples/vendored_v/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -o <tmp>/harness

# Safe-Rust examples
rustc -O benchmarks/rust_examples/vendored_v/vendored.rs -o <tmp>/vendored

# Bounded runs
/usr/bin/timeout 30 <tmp>/harness  > <tmp>/c.tokens
/usr/bin/timeout 30 <tmp>/vendored > <tmp>/r.tokens
diff -u <tmp>/c.tokens <tmp>/r.tokens     # no output

# One-command driver (does all of the above plus the nm checks)
benchmarks/rust_examples/vendored_v/compare.sh
```

`compare.sh` writes its binaries and captured streams to a `mktemp -d`
directory and removes it via `trap` on exit; it never writes into the repo.

## Real-run result

```
$ benchmarks/rust_examples/vendored_v/compare.sh; echo EXIT=$?
RUST_EXAMPLES vendored_v 26 6 156 0
EXIT=0
```

26 APIs × 6 cases each = 156 records; **matched = 156, mismatched = 0**.
`cmp` on the two token files is byte-identical. Mismatch detection was also
verified by temporarily changing one Rust token: the driver printed
`RUST_EXAMPLES vendored_v 26 6 133 46`, a unified diff, and **exited 1**; the
source was then restored and the clean run above repeated.

### Case matrix (what each case exercises)

| Case label | Meaning |
|---|---|
| `valid` | Normal successful call; token is the found index `i`, span/count `n`, buffer bytes (lower-case hex), terminator index `p`, return value `r`, or comparison sign `s`. |
| `null` | NULL pointer argument (dest/src/out-parameter, modelled as `Option::None` in Rust); token `E:NULL` (or `r=0` for the size-returning strl*/strnterminate APIs). |
| `zero` | Zero length (`dmax`/`dsize`); `E:ZERO` for checked APIs, `r=...` for strl*/strnterminate. |
| `over` | One-over bound. Where a known object size is supplied (`destbos = sizeof(buf)`) this is `dmax = bos + 1` → `E:OVER` (Darwin `EOVERFLOW` = 84); where only `BOS_UNKNOWN` is available it is `RSIZE_MAX_STR/MAX_MEM + 1` → `E:MAX`; for `memccpy_s` it is `n > dmax` → `E:NOSPC`; for `strnset_s`, `n > dmax` → `E:NOSPC`; for `strlcpy`/`strlcat` it is the one-over exact-fit truncation boundary (return value shows truncation). |
| `edge` | Not-found (`E:NOTFND`), no-difference (`E:NODIFF`), empty input, early NUL, exact-fit/unterminated, or `n == 0` behaviour. |
| `canary` | A sentinel byte just outside the operation's bounded work area; every record ends `;canary=ok`, i.e. the sentinel was unchanged in both the real C call and the safe-Rust model. |

### Exact output (both streams, 156 lines)

```
cplus_v_memchr_s valid ok:i=4
cplus_v_memchr_s null E:NULL
cplus_v_memchr_s zero E:ZERO
cplus_v_memchr_s over E:MAX
cplus_v_memchr_s edge E:NOTFND
cplus_v_memchr_s canary ok:i=3;canary=ok
cplus_v__memchr_s_chk valid ok:i=2
cplus_v__memchr_s_chk null E:NULL
cplus_v__memchr_s_chk zero E:ZERO
cplus_v__memchr_s_chk over E:OVER
cplus_v__memchr_s_chk edge E:NOTFND
cplus_v__memchr_s_chk canary ok:i=3;canary=ok
cplus_v_memccpy_s valid ok:61626358aaaaaaaa
cplus_v_memccpy_s null E:NULL
cplus_v_memccpy_s zero E:ZERO
cplus_v_memccpy_s over E:NOSPC
cplus_v_memccpy_s edge ok:00aaaaaaaaaaaaaa
cplus_v_memccpy_s canary ok:6162aaaa5aaaaaaa;canary=ok
cplus_v__memccpy_s_chk valid ok:61626358aaaaaaaa
cplus_v__memccpy_s_chk null E:NULL
cplus_v__memccpy_s_chk zero E:ZERO
cplus_v__memccpy_s_chk over E:OVER
cplus_v__memccpy_s_chk edge ok:00aaaaaaaaaaaaaa
cplus_v__memccpy_s_chk canary ok:6162aaaa5aaaaaaa;canary=ok
cplus_v__strchr_s_chk valid ok:i=1
cplus_v__strchr_s_chk null E:NULL
cplus_v__strchr_s_chk zero E:ZERO
cplus_v__strchr_s_chk over E:OVER
cplus_v__strchr_s_chk edge E:NOTFND
cplus_v__strchr_s_chk canary ok:i=3;canary=ok
cplus_v__strrchr_s_chk valid ok:i=5
cplus_v__strrchr_s_chk null E:NULL
cplus_v__strrchr_s_chk zero E:ZERO
cplus_v__strrchr_s_chk over E:OVER
cplus_v__strrchr_s_chk edge E:NOTFND
cplus_v__strrchr_s_chk canary ok:i=3;canary=ok
cplus_v__strstr_s_chk valid ok:i=6
cplus_v__strstr_s_chk null E:NULL
cplus_v__strstr_s_chk zero E:ZERO
cplus_v__strstr_s_chk over E:OVER
cplus_v__strstr_s_chk edge E:NOTFND
cplus_v__strstr_s_chk canary ok:i=2;canary=ok
cplus_v__strcasestr_s_chk valid ok:i=6
cplus_v__strcasestr_s_chk null E:NULL
cplus_v__strcasestr_s_chk zero E:ZERO
cplus_v__strcasestr_s_chk over E:OVER
cplus_v__strcasestr_s_chk edge E:NOTFND
cplus_v__strcasestr_s_chk canary ok:i=2;canary=ok
cplus_v__strspn_s_chk valid ok:n=6
cplus_v__strspn_s_chk null E:NULL
cplus_v__strspn_s_chk zero E:ZERO
cplus_v__strspn_s_chk over E:MAX
cplus_v__strspn_s_chk edge ok:n=0
cplus_v__strspn_s_chk canary ok:n=3;canary=ok
cplus_v__strcspn_s_chk valid ok:n=3
cplus_v__strcspn_s_chk null E:NULL
cplus_v__strcspn_s_chk zero E:ZERO
cplus_v__strcspn_s_chk over E:MAX
cplus_v__strcspn_s_chk edge ok:n=0
cplus_v__strcspn_s_chk canary ok:n=3;canary=ok
cplus_v_strlcpy valid r=3;bytes=61626300aaaaaaaa
cplus_v_strlcpy null r=0
cplus_v_strlcpy zero r=4;bytes=aaaaaaaaaaaaaaaa
cplus_v_strlcpy over r=4;bytes=61626300aaaaaaaa
cplus_v_strlcpy edge r=0;bytes=00aaaaaaaaaaaaaa
cplus_v_strlcpy canary r=3;bytes=616263005aaaaaaa;canary=ok
cplus_v_strlcat valid r=4;bytes=6162636400aaaaaa
cplus_v_strlcat null r=0
cplus_v_strlcat zero r=2;bytes=616200aaaaaaaaaa
cplus_v_strlcat over r=6;bytes=6162636400aaaaaa
cplus_v_strlcat edge r=2;bytes=616200aaaaaaaaaa
cplus_v_strlcat canary r=4;bytes=616263005aaaaaaa;canary=ok
cplus_v__stpcpy_s_chk valid ok:p=5;bytes=68656c6c6f00aaaa
cplus_v__stpcpy_s_chk null E:NULL
cplus_v__stpcpy_s_chk zero E:ZERO
cplus_v__stpcpy_s_chk over E:OVER
cplus_v__stpcpy_s_chk edge ok:p=0;bytes=00aaaaaaaaaaaaaa
cplus_v__stpcpy_s_chk canary ok:p=3;bytes=61626300aaaa5aaa;canary=ok
cplus_v__stpncpy_s_chk valid ok:p=5;bytes=776f726c6400aaaa
cplus_v__stpncpy_s_chk null E:NULL
cplus_v__stpncpy_s_chk zero E:ZERO
cplus_v__stpncpy_s_chk over E:MAX
cplus_v__stpncpy_s_chk edge ok:p=0;bytes=00aaaaaaaaaaaaaa
cplus_v__stpncpy_s_chk canary ok:p=3;bytes=61626300aa5aaaaa;canary=ok
cplus_v__strset_s_chk valid ok:xxxxxxx
cplus_v__strset_s_chk null E:NULL
cplus_v__strset_s_chk zero E:ZERO
cplus_v__strset_s_chk over E:MAX
cplus_v__strset_s_chk edge ok:7a7a0043444546
cplus_v__strset_s_chk canary ok:xxxx;canary=ok
cplus_v__strnset_s_chk valid ok:yyydefg
cplus_v__strnset_s_chk null E:NULL
cplus_v__strnset_s_chk zero E:ZERO
cplus_v__strnset_s_chk over E:NOSPC
cplus_v__strnset_s_chk edge ok:abcdefg
cplus_v__strnset_s_chk canary ok:wwcd;canary=ok
cplus_v__strnterminate_s_chk valid r=7;bytes=4142434445464700
cplus_v__strnterminate_s_chk null r=0
cplus_v__strnterminate_s_chk zero r=0
cplus_v__strnterminate_s_chk over r=0
cplus_v__strnterminate_s_chk edge r=3;bytes=6162630065666768
cplus_v__strnterminate_s_chk canary r=3;bytes=616263004141415a;canary=ok
cplus_v__strtolowercase_s_chk valid ok:abc-xyz
cplus_v__strtolowercase_s_chk null E:NULL
cplus_v__strtolowercase_s_chk zero E:ZERO
cplus_v__strtolowercase_s_chk over E:MAX
cplus_v__strtolowercase_s_chk edge ok:61620043444546
cplus_v__strtolowercase_s_chk canary ok:abcd;canary=ok
cplus_v__strtouppercase_s_chk valid ok:ABC-DEF
cplus_v__strtouppercase_s_chk null E:NULL
cplus_v__strtouppercase_s_chk zero E:ZERO
cplus_v__strtouppercase_s_chk over E:MAX
cplus_v__strtouppercase_s_chk edge ok:ABCdef
cplus_v__strtouppercase_s_chk canary ok:ABCD;canary=ok
cplus_v__strfirstchar_s_chk valid ok:i=2
cplus_v__strfirstchar_s_chk null E:NULL
cplus_v__strfirstchar_s_chk zero E:ZERO
cplus_v__strfirstchar_s_chk over E:OVER
cplus_v__strfirstchar_s_chk edge E:NOTFND
cplus_v__strfirstchar_s_chk canary ok:i=2;canary=ok
cplus_v__strlastchar_s_chk valid ok:i=3
cplus_v__strlastchar_s_chk null E:NULL
cplus_v__strlastchar_s_chk zero E:ZERO
cplus_v__strlastchar_s_chk over E:OVER
cplus_v__strlastchar_s_chk edge E:NOTFND
cplus_v__strlastchar_s_chk canary ok:i=2;canary=ok
cplus_v__strfirstdiff_s_chk valid ok:i=3
cplus_v__strfirstdiff_s_chk null E:NULL
cplus_v__strfirstdiff_s_chk zero E:ZERO
cplus_v__strfirstdiff_s_chk over E:OVER
cplus_v__strfirstdiff_s_chk edge E:NODIFF
cplus_v__strfirstdiff_s_chk canary ok:i=2;canary=ok
cplus_v__strlastdiff_s_chk valid ok:i=5
cplus_v__strlastdiff_s_chk null E:NULL
cplus_v__strlastdiff_s_chk zero E:ZERO
cplus_v__strlastdiff_s_chk over E:OVER
cplus_v__strlastdiff_s_chk edge E:NODIFF
cplus_v__strlastdiff_s_chk canary ok:i=0;canary=ok
cplus_v__strcmpfld_s_chk valid ok:s=1
cplus_v__strcmpfld_s_chk null E:NULL
cplus_v__strcmpfld_s_chk zero E:ZERO
cplus_v__strcmpfld_s_chk over E:OVER
cplus_v__strcmpfld_s_chk edge ok:s=0
cplus_v__strcmpfld_s_chk canary ok:s=1;canary=ok
cplus_v__strcpyfld_s_chk valid ok:48454c4c4f000000
cplus_v__strcpyfld_s_chk null E:NULL
cplus_v__strcpyfld_s_chk zero E:ZERO
cplus_v__strcpyfld_s_chk over E:OVER
cplus_v__strcpyfld_s_chk edge ok:aaaaaaaaaaaaaaaa
cplus_v__strcpyfld_s_chk canary ok:41420000005aaaaa;canary=ok
cplus_v__strcoll_s_chk valid ok:s=-1
cplus_v__strcoll_s_chk null E:NULL
cplus_v__strcoll_s_chk zero E:ZERO
cplus_v__strcoll_s_chk over E:OVER
cplus_v__strcoll_s_chk edge ok:s=0
cplus_v__strcoll_s_chk canary ok:s=0;canary=ok
```

## Implementation notes / honest caveats

- **`harness.c` calls the real symbols.** All signatures are transcribed from
  `libc/install-include/cplus_vendor_{case,field,firstlast,mem,set,spn,stp,strchr,strl,strstr}.h`.
  `cplus_v__memchr_s_chk` and `cplus_v__memccpy_s_chk` are emitted by
  `v_mem_group.o` (confirmed by `nm`) but deliberately absent from the public
  mem header, so they are declared locally with the exact upstream
  `errno_t`/`rsize_t` (== `int`/`size_t`) signatures, exactly as
  `tests/libc/test_vendor_mem.c` does.
- **Constraint chatter is suppressed.** The pinned safeclib default handler
  prints `IGNORE CONSTRAINT HANDLER: ...` to stdout. `harness.c` installs
  `cplus_sc_set_str_constraint_handler_s` and
  `cplus_sc_set_mem_constraint_handler_s` with a quiet handler first, so the
  only stdout lines are the records shown above.
- **Error codes are symbolic in the tokens.** `400/401/403/404/406/408/409`
  map to `E:NULL/E:ZERO/E:MAX/E:OVLP/E:NOSPC/E:NODIFF/E:NOTFND`.
  `EOVERFLOW` is platform-valued (84 on this Darwin host, 75 where errno.h
  lacks it), so both sides map it to `E:OVER` and the C side uses the actual
  `EOVERFLOW` macro from `<errno.h>`.
- **No live-game / performance claim.** This is a token-equivalence harness,
  not a benchmark and not a safety proof.
- **Non-equivalence statement.** Matching tokens do **not** mean the Rust
  examples are equivalent implementations of the vendored C functions. The
  C side executes the verbatim upstream rurban/safeclib bodies (symbol-prefix
  renames only) with their runtime-constraint handlers, object-size (`BOS`)
  checks, locale-dependent `strcoll`, and byte-level buffer semantics. The
  Rust side is an independent safe model using `str`/slice methods,
  iterators, `chars`/ASCII case conversion, `Option` (for NULL) and `Result`
  (for constraint outcomes); it deliberately avoids `unsafe` and FFI, so it
  cannot observe or reproduce C undefined behaviour, allocator/locale
  details, or the exact safeclib error-handler side effects. The comparison
  demonstrates that these *specific inputs* produce the *same observable
  token* in both implementations. It is not evidence of general semantic
  equivalence, memory safety of either implementation, or of behaviour for
  untested inputs. Comparison signs (`s=...`) are used for the two comparison
  APIs precisely to avoid over-claiming locale/magnitude equality.
