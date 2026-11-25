# C source compatibility — original C compiles unchanged, with and without the checked layer

## Claim under test

Ordinary, legacy ISO C source using `malloc`/`calloc`/`free`, `memcpy`/`memset`,
`strcpy`/`strcat`, `printf`/`fprintf`, and `FILE*` (`tmpfile`/`fread`/`fwrite`)
must compile **byte-for-byte unchanged** under `/usr/bin/clang -std=c99` both
when the checked layer is absent and when it is force-included with
`-include stdlib/safe/safe_c.h` and linked against the safe archives. The two
resulting executables must produce **byte-identical stdout** and **exit 0** on
identical (empty) inputs.

This directory contains 11 realistic legacy C sources. Sources 01–10 are
parity cases. Source 11 is a deliberate local-buffer-overflow case used to
demonstrate a checked refusal in a separate subprocess.

**No source file is edited between builds.** The very same `.c` path is handed
to the compiler in every mode; only the compile/link flags change.

## Environment (observed, not assumed)

```
$ /usr/bin/clang --version
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0
```

Archives linked by the checked/declarations-only builds (present in the tree):

```
build/safe/libcplus_safecrt.a
build/libc/libcplus_hardened.a
```

Runner: `sh tests/abi/c_source_compat/run.sh` (every artifact it writes stays
under `tests/abi/c_source_compat/out/`). Full machine-generated command log:
`tests/abi/c_source_compat/out/RESULTS.txt`.

## Exact commands

For a source named `<name>` (run from the repository root):

```
# 1. PLAIN — ordinary system libc, no layer
/usr/bin/clang -std=c99 tests/abi/c_source_compat/<name>.c \
  -o tests/abi/c_source_compat/out/<name>.plain

# 2. CHECKED — force-include the ordinary-C mapping header, link both archives
/usr/bin/clang -std=c99 -include stdlib/safe/safe_c.h \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/abi/c_source_compat/<name>.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/abi/c_source_compat/out/<name>.checked

# 3. DECLARATIONS-ONLY — same header linked in, CPLUS_SAFE_C_DISABLE suppresses
#    all macros, so ordinary libc spellings remain in effect
/usr/bin/clang -std=c99 -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/abi/c_source_compat/<name>.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/abi/c_source_compat/out/<name>.disabled
```

Each binary is then executed as its own subprocess with no arguments and no
stdin, with stdout/stderr captured separately:

```
tests/abi/c_source_compat/out/<name>.plain    > out/<name>.plain.stdout    2> out/<name>.plain.stderr
tests/abi/c_source_compat/out/<name>.checked  > out/<name>.checked.stdout  2> out/<name>.checked.stderr
tests/abi/c_source_compat/out/<name>.disabled > out/<name>.disabled.stdout 2> out/<name>.disabled.stderr
```

Acceptance for 01–10: all three compile (rc 0), all three exit 0,
`cmp -s` confirms plain == checked and plain == disabled stdout, and all three
stderr files are empty. SHA-256 of each stdout is recorded as byte-identity
evidence.

## Parity results (01–10)

All 30 compiles (10 sources × 3 modes) returned rc=0. All 30 runs exited 0
with empty stderr. For every row the three stdout SHA-256 hashes were equal
(only one hash is shown because all three matched):

| # | Source | APIs exercised | exit (plain/checked/disabled) | stdout sha256 (all 3 identical) | stdout (checked) |
|---|--------|----------------|-------------------------------|---------------------------------|------------------|
| 01 | `01_record_pack.c` | malloc, memset, strcpy, memcpy, free, printf | 0/0/0 | `7857c9d6e6ef53b21bdab1c667ee868333b82ebeb5f6c6e7a68f5706bfc4e608` | `record_pack: id=42 year=1998 tag=ALPHA` |
| 02 | `02_name_builder.c` | strcpy, strcat, printf | 0/0/0 | `7521d13a294011b898f6601e2bcbc36c57a2c099cf5d8f880b4f41dfe65ce8e8` | `name_builder: Ada Lovelace` |
| 03 | `03_token_table.c` | malloc, strcpy, printf, free | 0/0/0 | `a2b60a33870094e8dae18277ed09427c9a72ca91f82f424b9e407470b963d69b` | `token_table: 0 alpha` … `3 delta` |
| 04 | `04_stream_roundtrip.c` | tmpfile, fwrite, rewind, fread, malloc, memset, printf, fclose | 0/0/0 | `e22726f2f1a251064a7be5cd4626f4c6cd8e366a25e3fb46d74fca4726ec9a53` | `stream_roundtrip: got=32 text=[legacy line one / legacy line two /]` |
| 05 | `05_matrix_copy.c` | malloc, memset, memcpy, printf, free | 0/0/0 | `cdda60c0f411615ba76b60420dfea6a92054d163c5474792e55eefdb60997e20` | `matrix_copy: sum=78 last=12` |
| 06 | `06_string_table.c` | malloc, strcpy, printf, free | 0/0/0 | `ff744d1d04b1c5028f95642739ee86a70835f4ccb96e2fa7ef237bca62b8f433` | `string_table: 3 west` … `0 north` |
| 07 | `07_packet_build.c` | malloc, memset, memcpy at interior pointers, printf, free | 0/0/0 | `9223abb4f8caacf11cb394d4fbf1638b29b9ff83c71a125abcc4420fbc209a23` | `packet_build: ABCD 1234 PING` |
| 08 | `08_inventory.c` | memset, strcpy, strcat, printf | 0/0/0 | `48ac17e91e076225cddf4c071582a02594750fb310e21e16dd2a60b9e298d89b` | `inventory: sku=X-100 desc=widget - blue (box of 12)` |
| 09 | `09_config_parse.c` | strchr, memcpy, strcpy, printf | 0/0/0 | `0cbf0ad10bce4ebcc5ed8600dd4ec63452b3c2e7d7b6875fcef3c995caaca2cc` | `config_parse: [host] => [localhost]` … |
| 10 | `10_report.c` | calloc, strcpy, fprintf(stdout), free | 0/0/0 | `fd5f9123a98468596f9ebf78af26f34a6274c9b8088ec52a68b486f6ba269f13` | `report: 1 alpha`, `2 beta`, `3 gamma` |

Evidence files: `out/<name>.{plain,checked,disabled}.{stdout,stderr}` plus the
per-mode `.build.log` files. The `cmp -s` equality checks and SHA-256 values are
in `out/RESULTS.txt`.

## Checked refusal demo (source 11)

`11_overflow_refusal.c` deliberately copies 40 bytes into an 8-byte stack
buffer. An adjacent `guard[64]` array absorbs the spill so the unchecked build
does not crash — it silently corrupts adjacent stack memory (undefined
behavior) and keeps going. Compiled and run in all three modes as above.

Observed, exact results:

```
# checked binary, run as its own subprocess
$ tests/abi/c_source_compat/out/11_overflow_refusal.checked
rc=70
stdout: overflow_refusal: before guard0=5A
stderr: [CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 40 > capacity 8, ESNOSPC 406)

# plain binary (no layer) — raw overflow, no refusal
$ tests/abi/c_source_compat/out/11_overflow_refusal.plain
rc=0
stdout: overflow_refusal: before guard0=5A
        overflow_refusal: after guard0=49 buf0=41
stderr: (empty)

# declarations-only (CPLUS_SAFE_C_DISABLE) — macros suppressed, same raw overflow
$ tests/abi/c_source_compat/out/11_overflow_refusal.disabled
rc=0
stdout: overflow_refusal: before guard0=5A
        overflow_refusal: after guard0=49 buf0=41
stderr: (empty)
```

Interpretation: with the layer force-included, the ordinary `memcpy(d, s, 40)`
call is remapped to the capacity-checking wrapper. `__builtin_object_size(d, 1)`
proves the destination is 8 bytes, so the write is refused **before any byte is
written**; the wrapper emits the exact `[CPLUS-SAFE] ... ESNOSPC 406` line on
stderr and terminates that subprocess with `_exit(70)` (a defined exit, not a
signal). The `before` line is the only stdout produced. With no layer — or
with `CPLUS_SAFE_C_DISABLE` — the raw copy overwrites `guard[0]` (`0x5A` →
`0x49`) and the program reports success. The runner asserts `rc == 70` and an
exact `grep -Fx` match of the diagnostic line; this assertion passed.

Raw captures: `out/11_overflow_refusal.{checked,plain,disabled}.{stdout,stderr}`.

## `CPLUS_SAFE_C_DISABLE` declarations-only mode

All 10 parity sources plus source 11 were also compiled with
`-DCPLUS_SAFE_C_DISABLE` on the command line while still force-including
`stdlib/safe/safe_c.h` and linking both archives. This parses the header but
suppresses every macro mapping (`CPLUS_SAFE_C_ENABLE` is not defined), so the
ordinary libc spellings survive. Observed: parity sources 01–10 produced
stdout identical to the plain build and exited 0; source 11 behaved exactly
like the plain build (raw overflow, rc 0). This demonstrates the opt-out is a
true declarations-only mode with no behavior change and no silent
interposition.

## Reproduce

```
sh tests/abi/c_source_compat/run.sh
# exit 0 means PASS=11 FAIL=0; detailed evidence in out/RESULTS.txt
```

## Honesty notes / limits

- Every number, hash, exit code and diagnostic in this file was produced by
  the commands above and is also present verbatim in `out/RESULTS.txt` and the
  captured `.stdout`/`.stderr` files. Nothing is simulated or fabricated.
- Parity is shown for these 11 representative sources, not for all possible C
  programs. The checked layer is runtime diagnosis with provenance for its own
  allocator plus `__builtin_object_size`; it is not a proof of arbitrary pointer
  safety. Sources that deliberately write outside a proven capacity are
  expected to be refused (source 11) and are therefore excluded from the
  stdout/exit parity claim.
- Logging is off by default in both libraries, which is why checked-build
  stderr is empty for the parity cases; the only stderr line observed is the
  deliberate refusal in source 11.
- The checked refusal uses `memcpy` because the memory wrappers call
  `cplus_runtime_violation` (diagnostic + `_exit(70)`). The `strcpy` auto
  wrapper refuses the same class of overflow by returning `NULL`/`ERANGE`
  without a process-level diagnostic; that distinct behavior is not conflated
  with the `memcpy` evidence here.
