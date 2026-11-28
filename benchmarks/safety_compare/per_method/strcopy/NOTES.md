# Per-method memory-safety comparison: C+ checked copy APIs vs safe Rust

Measured 2026-11-23. Scope: `strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`,
`stpcpy_s`, `stpncpy_s`, one method at a time. All artifacts are confined to
`benchmarks/safety_compare/per_method/strcopy/`.

> **Outcome-token equality is NOT equivalent guarantees.** C+ enforces these
> contracts at **run time** in `build/safe/libcplus_safecrt.a` (return codes
> 400 / 406 and a silent constraint handler installed by the harness). Rust
> makes null references and out-of-bounds writes **unrepresentable in safe
> code** and expresses the same outcomes as `Option`/length checks. Only the
> observed `<method> <case> <token>` records are comparable. Nothing here
> proves the two languages provide the same memory-safety guarantee, nor that
> the Rust model is a formal model of the C+ API.

## Files

- `strcopy_harness.c` — deterministic C99 harness; one method selected via
  `argv[1]`, or all methods with no argument.
- `strcopy.rs` — safe Rust harness (no escape hatches, no raw pointers), same
  CLI and same token vocabulary.
- `compare.sh` — builds both, runs each method separately under `timeout 30`,
  diffs records by `(method, case)`, prints counts, exits nonzero on a real
  mismatch.
- `bin/`, `results/` — generated binaries and captured stdout/stderr/counts
  (not source inputs).

## Toolchains (exact)

- Host: macOS arm64, Darwin 27.2.0 (Apple M2).
- C compiler: `/usr/bin/clang` — `Apple clang version 16.0.0 (clang-1600.0.26.6)`.
- Rust compiler: `rustc 1.100.0 (0ef1bc386 2026-11-09)`.
- C build flags: `-std=c99 -O2 -Ilibc/install-include -Ithird_party/safeclib/include`.
- Rust build flags: `-O` (edition defaults to 2015; single-file program).
- Every compile and every run is wrapped in `timeout 30`; no timeout fired.
- Additional warning checks (not part of `compare.sh`), both clean:
  - `clang -std=c99 -O2 -Wall -Wextra -pedantic ...` → exit 0, no diagnostics.
  - `rustc -O -D warnings strcopy.rs ...` → exit 0, no diagnostics.
  - `grep -n 'unsafe' strcopy.rs` → no matches.

## Exact commands (run from repo root)

```sh
# 1. Symbol verification (see "STP symbol timeline" below).
nm -g build/safe/libcplus_safecrt.a | grep -E 'stpcpy_s|stpncpy_s|cplus_v_stp|stpcpy|stpncpy'
nm -g build/safe/libcplus_safecrt.a | grep -E '^00000000000000[0-9a-f]+ T _cplus_sc_(strcpy|strncpy|strcat|strncat)_s$'
nm -g build/libc/libcplus_hardened.a | grep -E 'stpcpy|stpncpy'

# 2. C harness (exact flags required by the task).
/usr/bin/clang -std=c99 -Ilibc/install-include -Ithird_party/safeclib/include \
  benchmarks/safety_compare/per_method/strcopy/strcopy_harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o benchmarks/safety_compare/per_method/strcopy/bin/strcopy_harness

# 3. Rust harness.
rustc -O benchmarks/safety_compare/per_method/strcopy/strcopy.rs \
  -o benchmarks/safety_compare/per_method/strcopy/bin/strcopy_rust

# 4. One method at a time (example); full matrix is the no-argument form.
timeout 30 benchmarks/safety_compare/per_method/strcopy/bin/strcopy_harness strcpy_s
timeout 30 benchmarks/safety_compare/per_method/strcopy/bin/strcopy_rust   strcpy_s

# 5. Full driver (builds, runs all six methods, diffs, prints summary).
bash benchmarks/safety_compare/per_method/strcopy/compare.sh
```

`compare.sh` additionally passes `-O2` to clang so both harnesses are
optimized consistently; it also installs a no-op safeclib string constraint
handler in the C harness so `ignore_handler_s` diagnostics do not pollute
stdout. Neither changes any token.

## Symbol verification

Checked with `nm -g` on `build/safe/libcplus_safecrt.a`:

```
0000000000000034 T _cplus_sc_strcpy_s
000000000000003c T _cplus_sc_strncpy_s
0000000000000048 T _cplus_sc_strcat_s
0000000000000050 T _cplus_sc_strncat_s
0000000000000000 T _cplus_v__stpcpy_s_chk
00000000000001fc T _cplus_v__stpncpy_s_chk
```

(`ar t` lists `v_stp_group.o` as the object supplying the two STP workers.)
`build/libc/libcplus_hardened.a` also has `0000000000000264 T _cplus_stpcpy`
and `0000000000000384 T _cplus_stpncpy`, but those are the **unsafe** variants
and are deliberately not used here.

The STP workers are called directly with `destbos = srcbos = (size_t)-1`
(`BOS_UNKNOWN`), as documented in `libc/install-include/cplus_vendor_stp.h`.
Success is reported via `*errp == 0` and a pointer to the terminating NUL;
refusals via `*errp == 400` (ESNULLP) or `406` (ESNOSPC) and a NULL return.

### STP symbol timeline (why nothing is BLOCKED in the final result)

- First check (10:2x): `libc/src/safecrt_vendor/stp_group.c` did not exist;
  `nm -g ... | grep stp` returned nothing, so STP cases were initially emitted
  as `BLOCKED` with the exact reason "vendor object not compiled".
- A concurrent agent then added `stp_group.c` and rebuilt the archive
  (`v_stp_group.o`, mtime 10:28:26). Repeated `nm` checks returned the same two
  `_chk` symbols twice, so they were treated as available and **tested for
  real** rather than leaving fabricated BLOCKED rows.
- Final summary therefore reports `METHOD_STRCOPY_BLOCKED 0`. The driver still
  supports and counts `BLOCKED` records if a checked symbol disappears again;
  it never converts them into matches.

## Cases and tokens

Seven cases per method (42 records total). Tokens: `OK` (EOK), `REF_NULL`
(ESNULLP 400), `REF_NOSPC` (ESNOSPC 406 / truncation / bounded unterminated
source), `CANARY_INTACT` (refusal left the 0xA5 guard beyond `dmax` untouched),
`CANARY_CORRUPT` (would indicate a bounds bug — never observed).

| case | strcpy_s | strncpy_s | strcat_s | strncat_s | stpcpy_s | stpncpy_s |
|---|---|---|---|---|---|---|
| valid_exact_fit | OK | OK | OK | OK | OK | OK |
| null_dest | REF_NULL | REF_NULL | REF_NULL | REF_NULL | REF_NULL | REF_NULL |
| null_src | REF_NULL | REF_NULL | REF_NULL | REF_NULL | REF_NULL | REF_NULL |
| one_byte_too_small | REF_NOSPC | REF_NOSPC | REF_NOSPC | REF_NOSPC | REF_NOSPC | REF_NOSPC |
| unterminated_bounded | REF_NOSPC | OK | REF_NOSPC | OK | REF_NOSPC | OK |
| truncation_dmax | REF_NOSPC | OK | REF_NOSPC | OK | REF_NOSPC | OK |
| canary_refusal | CANARY_INTACT | CANARY_INTACT | CANARY_INTACT | CANARY_INTACT | CANARY_INTACT | CANARY_INTACT |

Both harnesses produce exactly this table (C output in `results/*.c.out`,
Rust output in `results/*.rs.out`). The `OK` rows for `strncpy_s`, `strncat_s`
and `stpncpy_s` on `unterminated_bounded` / `truncation_dmax` are the real
per-method difference: the `_n` variants honor an explicit `slen` bound and
NUL-terminate the truncated result, while the unbounded variants refuse.

For the canary case the C side allocates `unsigned char buf[32]`, fills it with
`0xA5`, sets `dmax` below the required size, and checks bytes `[dmax..32)` after
the refusal. The Rust side models the guard as a trailing `"ZZZZZZZZ"` sentinel
in a `String`, verifies it with `get(start..)` and `to_owned()` after the
refusal, and reports `CANARY_INTACT`. These are logically equivalent checks of
"the refusal did not touch out-of-bounds storage", not equivalent memory
models.

## Driver output (verbatim, final run)

```
METHOD_STRCOPY_STRCPY_S 7 7 0
METHOD_STRCOPY_STRNCPY_S 7 7 0
METHOD_STRCOPY_STRCAT_S 7 7 0
METHOD_STRCOPY_STRNCAT_S 7 7 0
METHOD_STRCOPY_STPCPY_S 7 7 0
METHOD_STRCOPY_STPNCPY_S 7 7 0
METHOD_STRCOPY 42 42 0
METHOD_STRCOPY_BLOCKED 0
```

Exit status: 0. Two consecutive driver runs produced byte-identical output;
no `MISMATCH`, `MISSING_C`, or `MISSING_RUST` records were emitted
(`results/mismatch_details.txt` is empty). A real token difference or missing
record makes `compare.sh` exit 1; a build/run failure makes it exit 2/3.
