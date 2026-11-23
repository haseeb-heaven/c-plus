# F-041..F-060 — deterministic format and I/O fuzz configurations

Scope: 20 standalone, self-contained deterministic harnesses for the Safe CRT
format family, bounded stream I/O, line/token input, and numeric parsers.
All files live in `tests/crt/fuzz/` and use the `f100_fmt_` / `f100_io_` prefixes.

## Discipline

- **Fixed-seed LCG** in every harness: `state = state*1664525 + 1013904223 (mod 2^32)`
  (`uint32_t` wrap). Format seeds `0xF100C0DE`, I/O seeds `0xF10010DE`.
- **>=1000 operations per config** (smallest: 1057; largest: 5200; total 38,984).
- **No attacker-controlled formats.** Every printf/scanf format is a literal or
  comes from the fixed, type-matched `f100_specs` catalogue in
  `f100_fmt_common.h`. Only arguments, dmax sizes and tokens are LCG-selected.
- **Bounded destinations with guard bytes** on every snprintf/vsnprintf/sprintf
  call and on the `sscanf_s("%63s")` destination. Truncation boundary sizes
  cover `0..512`; `dmax == 0` with `dest == NULL` is exercised as a legal
  measurement call. Integer/float boundary values include `INT_MIN/MAX`,
  `LLONG_MIN/MAX`, `UINT_MAX`, `ULLONG_MAX`, `SIZE_MAX`, `DBL_MIN/MAX`,
  `DBL_EPSILON`, `-0.0`.
- **stdin fixtures** are generated in-process (no external fixture files):
  empty line, whitespace+CRLF, exact-fit line, 400-byte overlong line, CRLF
  line, and an unterminated EOF line. They are attached with `freopen` and
  replayed 200 times (line reader) / 300 times (tokenizer).
- **Numeric parsers** are differentially checked against the corresponding
  host function (value, end-pointer offset, `ERANGE`), using LCG-generated
  valid and invalid tokens.
- Temp files use `TMPDIR` (default `/tmp`) plus the PID; no user path,
  retail executable, Wine prefix or key binding is hard-coded.
- The violation trap is disabled in `f100_init()` so negative contract probes
  can assert the documented `-1`/`EINVAL` return path instead of `_exit(70)`.
  This is a harness observation choice, not a change to library policy.

## Config inventory

| ID | Harness | Coverage | Ops | Result |
|----|---------|----------|-----|--------|
| F-041 | `f100_fmt_snprintf_trunc.c` | `snprintf_s`, dmax 0..512 x3, NULL dest/dmax=0, guard canaries, truncation NUL | 1571 | pass |
| F-042 | `f100_fmt_snprintf_int.c` | integer boundary values x all integer conversions | 1484 | pass |
| F-043 | `f100_fmt_snprintf_float.c` | DBL_MIN/MAX/EPSILON, +/-1, +/-0 across %f/%e/%g/%a | 1208 | pass |
| F-044 | `f100_fmt_vsnprintf.c` | `vsnprintf_s` va_list path, dmax sweep, would-be-length invariant | 1645 | pass |
| F-045 | `f100_fmt_sprintf.c` | `sprintf_s` / `vsprintf_s`, dmax 0..160 | 1300 | pass |
| F-046 | `f100_fmt_printf.c` | `printf_s` with stdout dup2-redirected to temp file | 1200 | pass |
| F-047 | `f100_fmt_fprintf.c` | `fprintf_s` to tmpfile + read-back; NULL stream/format refusal | 1203 | pass |
| F-048 | `f100_fmt_checked.c` | `snprintf_s_checked` / `sprintf_s_checked`; non-literal and dmax>capacity refusal | 1202 | pass |
| F-049 | `f100_fmt_sscanf.c` | `sscanf_s` bounded %63s and numeric conversions on valid/invalid tokens | 1250 | pass |
| F-050 | `f100_fmt_contract.c` | `%n` refusal, non-literal refusal, NULL format/dest probes | 1057 | pass |
| F-051 | `f100_io_open_close.c` | `fopen_s`/`fclose_s` churn, double-close refusal, NULL args | 1403 | pass |
| F-052 | `f100_io_readwrite.c` | `fwrite_s`/`fread_s` bounded round-trip, bmax clamp, invalid args | 3602 | pass |
| F-053 | `f100_io_readline.c` | bounded line reader over 6-case stdin fixture (incl. overlong/CRLF/EOF) | 1200 | pass |
| F-054 | `f100_io_token.c` | `strtok_s` over stdin token fixture, guarded tail bytes | 3000 | pass |
| F-055 | `f100_io_strtol.c` | `strtol`/`strtoul`, bases 8/10/16, valid+invalid tokens | 2600 | pass |
| F-056 | `f100_io_strtoll.c` | `strtoll`/`strtoull`, bases 0/8/10/16, 64-bit extremes | 2600 | pass |
| F-057 | `f100_io_strtod.c` | `strtod`/`strtof`/`strtold`, valid+invalid float tokens | 3450 | pass |
| F-058 | `f100_io_atoi.c` | `atoi`/`atol`/`atoll`/`atof` differential | 5200 | pass |
| F-059 | `f100_io_fscanf.c` | `fscanf_s` over 1200 numeric records + floats + EOF | 1209 | pass |
| F-060 | `f100_io_roundtrip.c` | `fprintf_s`→`fscanf_s` text round-trip and `fwrite_s`→`fread_s` binary round-trip | 1600 | pass |
| | | **Total** | **38,984** | **20/20** |

Supporting files: `f100_fmt_common.h`, `f100_io_common.h` (LCG, canary buffers,
fixed spec/value tables, fixture generators, temp-path helper) and
`f100_fmt_run.sh` (aggregate build+run driver).

## Exact commands and results

Driver (what was recorded as the final result):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/crt/fuzz/f100_fmt_run.sh
```

Per-config command issued by the driver (template; `<name>` = harness basename):

```sh
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra \
  -Wno-format-nonliteral -Wno-format-security -Wno-unused-function \
  -fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero \
  -Istdlib/safe -Ilibc/install-include -Ilibc \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/crt/fuzz/<name>.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o <temp-bin>
timeout 30 <temp-bin>
```

Archives linked (pre-existing, unmodified):
`build/safe/libcplus_safecrt.a` and `build/libc/libcplus_hardened.a`.

Final driver output (`20 passed, 0 failed`) — each line also shows the
self-reported operation count and failure count:

```
ok   F-041 f100_fmt_snprintf_trunc: CONFIG F-041 ops=1571 fail=0
ok   F-042 f100_fmt_snprintf_int: CONFIG F-042 ops=1484 fail=0
ok   F-043 f100_fmt_snprintf_float: CONFIG F-043 ops=1208 fail=0
ok   F-044 f100_fmt_vsnprintf: CONFIG F-044 ops=1645 fail=0
ok   F-045 f100_fmt_sprintf: CONFIG F-045 ops=1300 fail=0
ok   F-046 f100_fmt_printf: CONFIG F-046 ops=1200 fail=0
ok   F-047 f100_fmt_fprintf: CONFIG F-047 ops=1203 fail=0
ok   F-048 f100_fmt_checked: CONFIG F-048 ops=1202 fail=0
ok   F-049 f100_fmt_sscanf: CONFIG F-049 ops=1250 fail=0
ok   F-050 f100_fmt_contract: CONFIG F-050 ops=1057 fail=0
ok   F-051 f100_io_open_close: CONFIG F-051 ops=1403 fail=0
ok   F-052 f100_io_readwrite: CONFIG F-052 ops=3602 fail=0
ok   F-053 f100_io_readline: CONFIG F-053 ops=1200 fail=0
ok   F-054 f100_io_token: CONFIG F-054 ops=3000 fail=0
ok   F-055 f100_io_strtol: CONFIG F-055 ops=2600 fail=0
ok   F-056 f100_io_strtoll: CONFIG F-056 ops=2600 fail=0
ok   F-057 f100_io_strtod: CONFIG F-057 ops=3450 fail=0
ok   F-058 f100_io_atoi: CONFIG F-058 ops=5200 fail=0
ok   F-059 f100_io_fscanf: CONFIG F-059 ops=1209 fail=0
ok   F-060 f100_io_roundtrip: CONFIG F-060 ops=1600 fail=0
F-041..F-060: 20 passed, 0 failed
```

Determinism check (same compile/link flags, each binary executed twice, stdout
and stderr compared byte-for-byte):

```
determinism: 20 identical, 0 differing
```

Sanitizer/assertion scan over all 20 runs (grep for `runtime error`,
`sanitizer`, `undefined behavior`, `F100 FAIL`):

```
configs with sanitizer/assertion markers: 0
```

All runs completed well inside the 30-second timeout (no timeout events).
Toolchain: Apple clang 16.0.0 (`/usr/bin/clang`, arm64-apple-darwin).

## Sanitizer findings policy

UBSan here is a **runtime observation instrument only**. A clean UBSan run is
not evidence that the C+ compiler's static enforcement pass accepted or
rejected anything, and a sanitizer finding would not be reported as a C+
enforcement failure. No sanitizer findings were observed in these 20 configs.
Static/literal-format enforcement lives in the compiler pass and is out of
scope for these harnesses; F-048/F-050 only exercise the library's runtime
defense-in-depth return paths.

## Defects and repros

**No product (library) defect was found by these 20 configurations.** Three
defects appeared in the harnesses themselves during development and were
fixed before the final run; they are recorded for transparency:

1. F-060 record misalignment (harness bug). Re-generating records with
   `gen_record()` in the read loop advanced the LCG, so read lengths differed
   from write lengths and bytes from the next record bled in. Repro on the
   original file: `F100 FAIL ... binary record mismatch`, with
   `w=[-8843/44300980c4286edf/428.500]` vs
   `r=[8533/c3d22408f6f309f/716.50077]`. Fix: generate all 400 records once
   into `recs[]` and reuse that array in both directions.
2. F-060 stale update-stream reads (harness bug). Missing `fflush(f)` before
   `rewind(f)` on the `"wb+"` stream produced `fscanf_s record mismatch` on
   every record. Fix: `fflush(f)` before each `rewind(f)`.
3. F-046 build failure (harness bug): `f100_tmp_path` was defined only in the
   I/O common header. Fix: add an equivalent helper to `f100_fmt_common.h`.

Known backend quirk (pre-existing, not a new finding): `cplus_strtok_s` may
return the last token again instead of `NULL` at exhaustion; F-054 caps the
loop at 64 calls, matching `tests/crt/unit/test_strfam_strtok_s.c`.

## Limitations

- These are deterministic, in-process harnesses, not libFuzzer targets (the
  host lacks a usable libFuzzer runtime). They sample fixed schedules, not all
  inputs; a clean run is not a proof of absence of bugs.
- The differential numeric oracle is the same platform's host CRT, so a
  platform-wide misdefinition shared by both wrappers would not be detected.
- Temporary stdout/stdin redirection and temp files are process-local; no
  live-game or injected-DLL behavior is claimed.
