# ADV1000 / memstr — adversarial safe-refusal evidence

**Branch:** `agent/modern-stdlib`
**Owner directory (only files touched):** `tests/adversarial_1000/memstr/`
No compiler, runtime, header, or other repository files were created or modified.
No `git add` / commit was performed.

## Scope

All 13 checked memory/string APIs in `build/safe/libcplus_safecrt.a`, in **both**
shipped families (26 variants total):

- `cplus_sc_*` — safeclib-backed wrappers (`BOS_UNKNOWN`, so every check is a
  runtime constraint check) — `third_party/safeclib/src/cplus_sc_wrappers.c`
- `cplus_*_s` — intel-safestringlib-backed adapters — `stdlib/safe/src/`

APIs: `memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s`, `strcpy_s`,
`strncpy_s`, `strcat_s`, `strncat_s`, `strnlen_s`, `strcmp_s`, `strtok_s`,
`strerror_s`.

## Files

| File | Purpose |
|---|---|
| `adv1000_memstr.c` | Deterministic 1000-scenario generator/checker (C99) |
| `run.sh` | Reproducible nm-verify → build → run → digest → sanity pipeline |
| `results/scenarios.log` | The 1000 `S ...` lines (**gitignored**) |
| `results/scenarios.sha256` | SHA-256 digest of the log (**gitignored**) |
| `results/stdout.log`, `results/stderr.log`, `run_output.txt` | Raw run output (**gitignored**) |
| `.gitignore` | Ignores all generated artifacts |
| `NOTES.md` | This evidence record |

## Exact commands and results

Run from the repository root:

```sh
timeout 150 sh tests/adversarial_1000/memstr/run.sh > tests/adversarial_1000/memstr/run_output.txt 2>&1
```

Exit code: **0**. Result summary from `run_output.txt`:

```
ADV1000 memstr 1000 541 1000 0
scenario lines=1000 unique APIs=26 unique cases=7
```

### 1. Symbol verification (`nm`)

Command inside `run.sh`:

```sh
nm -g build/safe/libcplus_safecrt.a | grep -E ' T _cplus_(sc_)?(memcpy_s|memmove_s|memset_s|memcmp_s|memzero_s|strcpy_s|strncpy_s|strcat_s|strncat_s|strnlen_s|strcmp_s|strtok_s|strerror_s)$'
```

All 26 symbols are present as global `T` definitions (13 `cplus_*_s` + 13
`cplus_sc_*`); e.g.

```
0000000000000000 T _cplus_memcpy_s
0000000000000000 T _cplus_sc_memcpy_s
00000000000005f4 T _cplus_strerror_s
0000000000000070 T _cplus_sc_strerror_s
...
```

### 2. Build (`/usr/bin/clang -std=c99`, both archives)

```sh
/usr/bin/clang -O2 -std=c99 -pedantic -Wall -Wextra \
  -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  adv1000_memstr.c \
  build/safe/libcplus_safecrt.a build/libcplus_core.a \
  -pthread -o adv1000_memstr
```

Clean build, no warnings. "Both archives" = the checked CRT archive plus the
C+ core archive. All 26 entry points under test resolve from
`libcplus_safecrt.a` (verified by `nm` above); the core archive is linked for
runtime symbol completeness only.

### 3. Run under `timeout 120`

```sh
timeout 120 ./adv1000_memstr > results/stdout.log 2> results/stderr.log
exit=0
```

Aggregate (stdout and stderr):

```
ADV1000 memstr 1000 541 1000 0
```

Meaning: 1000 scenarios, **541** hostile cases refused with the documented
code, **1000/1000** canary checks OK, **0** failures.

### 4. Digest and determinism

```sh
shasum -a 256 results/scenarios.log
# 0059a6ab270ceada106792c2876bc8c58fff3e519c622fcaae16a2fc7c2fbe23  results/scenarios.log
```

Two independent runs produced byte-identical `scenarios.log` (verified with
`cmp`). The PRNG is xorshift64* seeded with the compile-time constant
`0x5EED1234ABCD5678`; there is no wall clock, `time()`, or ASLR-dependent
input.

### 5. Coverage histograms

Case counts (all 7 case types present):

```
 459 valid          156 null_dest        96 null_src
 101 one_over        30 overlap          20 unterminated
 138 huge_n
```

Observed `<rc>` values on hostile cases:

```
 240 → 400 (ESNULLP)     12 → 402 (ESLEMIN)    126 → 403 (ESLEMAX)
  30 → 404 (ESOVRLP)     89 → 406 (ESNOSPC)     20 → 407 (ESUNTERM)
  24 → 0   (strnlen_s bounded-count refusals)
```

All 26 API variants appear; each scenario line has the form
`S <id> <api> <case> <rc> <canary_ok>` with `canary_ok = 1` throughout.

## Documented refusal-code matrix (empirically confirmed)

Codes are the safeclib constraint codes from
`third_party/safeclib/include/safe_lib_errno.h` (also mirrored by the intel
backend). They were discovered with throwaway probes and cross-checked against
the function sources.

| Case | Setup | Expected rc |
|---|---|---|
| `valid` | In-bounds / terminated inputs | `0` (count for `strnlen_s`, token for `strtok_s`) |
| `null_dest` | dest pointer = NULL, non-zero length | `400` ESNULLP (`strnlen_s` returns `0`) |
| `null_src` | src pointer = NULL, non-zero length | `400` ESNULLP |
| `one_over` | `n = dmax + 1 + (L % 63)` (up to 63 bytes past the declared capacity); string src unterminated within `dmax` so the bound check fires | `406` ESNOSPC (`strerror_s` with `dmax` 1..3 → `402` ESLEMIN) |
| `huge_n` | length/`dmax`/`*dmaxp` = `SIZE_MAX` | `403` ESLEMAX (`strnlen_s` returns `0`) |
| `overlap` | src placed inside dest; source length > offset | `404` ESOVRLP |
| `unterminated` | no NUL in dest within `dmax` | `407` ESUNTERM |

Per-function applicability (`applicable[][]` in the source):

- `overlap` is only asserted for `memcpy_s`, `strcpy_s`, `strncpy_s`. It is
  *defined* for `memmove_s` and is not checked by the read-only `memcmp_s`
  (empirically rc=0 in both families), so asserting 404 there would be wrong.
- `unterminated` is only asserted for `strcat_s` / `strncat_s`, the APIs whose
  dest string must be NUL-terminated. `strncpy_s`/`strcmp_s` with `BOS_UNKNOWN`
  do not surface ESUNTERM for the tested shapes.
- `one_over` is not asserted for `strcmp_s`/`memzero_s` (no length/space
  constraint of that shape), and `null_src` not for single-pointer APIs.
- `strnlen_s` returns a count, not an `errno_t`; its refusals are the documented
  `0` return. `strtok_s` returns a pointer; refusals are normalized from the
  `errno` value the checked API sets (`400`/`403`). Both are noted here so the
  `<rc>` column remains numeric and comparable. `strtok_s` unterminated-dest is
  deliberately **not** generated: that path writes a terminator at index
  `*dmaxp`, which would (correctly) trip the canary and is a separate finding.

## BREAKING cases (unchecked equivalent would corrupt memory)

`one_over` scenarios are the deliberate "break the API" inputs. For example
`S 79 cplus_sc_memcpy_s one_over 406 1` and
`S 92 cplus_memcpy_s one_over 406 1`: with a declared destination capacity of
`dmax`, the checker passes `n = dmax + 1 + (L % 63)` — up to 63 bytes beyond the
declared capacity. An unchecked `memcpy` into, e.g., `char[8]` with `n = 64`
would smash the canary; here the checked API returns `406` (ESNOSPC) and every
canary byte after `dmax` is unchanged (`canary_ok = 1`). The same pattern is
applied to `strcpy_s`/`strncpy_s`/`strcat_s`/`strncat_s` with a source whose
length is at least `dmax`, so the truncation check fires rather than a short
source succeeding by accident.

Refusal is always demonstrated as a documented non-zero error code (400/402/
403/404/406/407) with intact canaries; the process exits **70** (EX_SOFTWARE)
on any mismatch or canary violation, and **0** only when all 1000 pass. No
memory corruption was observed.

## Honesty / limitations

**1000 scenarios passing is EVIDENCE of safe refusal for these specific
deterministic inputs; it is NOT a proof of full memory safety.** These checked
APIs enforce runtime constraints (null, declared bounds, max size, overlap,
termination). They do **not** establish pointer provenance, ownership,
lifetime, or alias/use-after-free safety; those remain the caller's and the
compiler's responsibility. Canaries cover only the bytes immediately after the
declared destination capacity in this harness; they do not detect every
possible memory-safety defect. No claim is made about live-game or production
behavior from this result alone.
