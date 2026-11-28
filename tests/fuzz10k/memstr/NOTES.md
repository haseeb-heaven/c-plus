# FUZZ10K memstr — deterministic fuzz evidence (10,240 scenarios)

## What was verified

Ten checked memory/string APIs in `build/safe/libcplus_safecrt.a`, exercised
through **two families** (22 entry points):

| Family | Symbol prefix | Backend |
|---|---|---|
| `cplus_sc_*_s` | `cplus_sc_memcpy_s` … `cplus_sc_strcmp_s` | safeclib backend, `BOS_UNKNOWN` → all runtime constraint checks active |
| `cplus_*_s` | `cplus_memcpy_s` … `cplus_strcmp_s` | C+ thin adapters (UAF rejection, logging, statistics) delegating to the above |

Operations (11): `memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s`,
`strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `strnlen_s`, `strcmp_s`.

Symbol existence was verified with `nm -g` on the archive (all 22 defined `T`
symbols printed by `run.sh` step 3; the script asserts exactly 22).

## Exact seed and generator

- PRNG: xorshift64*, `SEED = 0xF00D5EEDCAFEBABE` (uint64_t).
- State update: `x ^= x>>12; x ^= x<<25; x ^= x>>27; state = x; return x * 2685821657736338717`.
- The generator drives API selection, family selection, case selection and all
  length/offset/size parameters. **No wall clock, no ASLR-derived addresses.**
- Re-running produces a byte-identical `results/scenarios.log`:
  `SHA-256 = 64e594d639d2506219621128fa9fa2a81a992dfeac29d0c9a9c6f715ba633ac9`.

## Distinct deterministic scenarios

`NSCEN = 10240` (≥10,000). Each scenario is a distinct tuple
`(id, operation, family, hostile-family, D, L, O)` with `D,L ∈ [0,1024]` and
`O ∈ [0,D]`. Scenario `id` is unique and every log line carries it. Coverage
observed in the final log: 22 distinct `family_api` variants, 7 distinct case
families.

Case-family distribution in the final log:

| case | count |
|---|---|
| valid | 4499 |
| huge_n | 1462 |
| null_dest | 1439 |
| null_src | 1092 |
| one_over | 1081 |
| overlap | 408 |
| unterminated | 259 |

## Specific hostile families

1. **`valid`** — in-bounds calls. Destination is canary-guarded; after the call
   the harness verifies the copied/concatenated/zeroed bytes and that bytes
   past `dmax` are unchanged. This detects silent corruption.
2. **`null_dest`** — destination pointer is `NULL` (nonzero length so the
   zero-length shortcut is bypassed). Expected refusal: `ESNULLP (400)`;
   `strnlen_s` returns 0.
3. **`null_src`** — source pointer is `NULL`. Expected refusal: `ESNULLP (400)`.
4. **`one_over`** — declared `dmax` is smaller than the operation length
   (`n = dmax + 1 + [0,63]`, or source string length `dmax` for the string APIs).
   Expected refusal: `ESNOSPC (406)`. Canaries past `dmax` must be intact.
5. **`huge_n`** — length or `dmax` is `SIZE_MAX` (`(rsize_t)-1`), i.e. greater
   than `RSIZE_MAX`. Expected refusal: `ESLEMAX (403)`; `strnlen_s` returns 0.
6. **`overlap`** — source points inside the destination (`dest + 1..8`), for
   the APIs where overlap is undefined (not `memmove_s`). Expected refusal:
   `ESOVRLP (404)`.
7. **`unterminated`** — destination has no NUL in `[0, dmax)` for `strcat_s` /
   `strncat_s`. Expected refusal: `ESUNTERM (407)`.

Per-API applicability is encoded in a matrix; e.g. `memmove_s` never gets the
`overlap` case (overlap is defined for it), `memset_s`/`memzero_s` have no
source argument, `memcmp_s` is read-only, and `strnlen_s` is a bounded count
query (0 on a constraint violation, not a 400-series code).

## Crash / hang / corruption detection

- Every scenario runs in a **forked child**. The parent polls the child's
  result pipe for up to 5000 ms. `poll` timeout → **HANG** (child SIGKILLed).
  `WIFSIGNALED` → **CRASH** (signal number recorded). A normal exit that fails
  to deliver a complete result record is counted as a harness anomaly.
- A child returning a verification mismatch is **CORRUPTION**: broken canary,
  wrong documented refusal code, or wrong destination bytes on a valid call.
- The parent itself never calls an API, so a crashing child cannot kill the
  harness. Exit is nonzero (70) on any crash/hang/corruption.
- Constraint handlers are silenced (IGNORE mode + no-op handlers) and the
  violation trap is disabled, so a refusal stays an observable return code.
- Canaries are 32 bytes past `dmax`; `MAXLEN = 1024`, buffer size 1064.

## Exact commands and results

Environment: macOS (arm64), Apple clang 16.0.0 (`/usr/bin/clang`), branch
`agent/modern-stdlib`. Library archives: `build/safe/libcplus_safecrt.a` and
`build/libcplus_core.a` (the latter linked for runtime symbol completeness;
all 22 APIs under test are defined in the safe CRT archive).

```sh
cd tests/fuzz10k/memstr
./run.sh              # full reproducible pipeline, timeout 600
```

Steps performed by `run.sh` and their outputs:

1. `nm -g build/safe/libcplus_safecrt.a | grep -E ...` → 22 `T` symbols
   (11 `cplus_sc_*_s` + 11 `cplus_*_s`).
2. Build (primary):
   `/usr/bin/clang -O1 -g -std=c99 -pedantic -Wall -Wextra -I<root> ... fuzz10k_memstr.c <safe CRT archive> <core archive> -pthread -o fuzz10k_memstr`
   — compiles with **zero warnings**.
3. `timeout 600 ./fuzz10k_memstr`:
   ```
   FUZZ10K memstr 10240 0 0 0 5741
   ```
   exit **0** → 10,240 scenarios, 0 crashes, 0 hangs, 0 corruptions,
   5,741 documented refusals.
4. `shasum -a 256 results/scenarios.log` →
   `64e594d639d2506219621128fa9fa2a81a992dfeac29d0c9a9c6f715ba633ac9  results/scenarios.log`
   (10,240 `S ` lines; identical on a second run → deterministic).
5. Build + run UBSan replay:
   `-fsanitize=undefined -fno-sanitize-recover=all`, `timeout 600` →
   `FUZZ10K memstr 10240 0 0 0 5741`, exit 0, no diagnostics.
6. Sanity checks: 10,240 scenario lines, 22 unique APIs, 7 unique cases,
   aggregate line matched, both exits zero. `run.sh` exit 0.

## Harness bugs found and fixed (failures were never hidden)

The first run reported 799 verification failures (0 crashes/hangs). Root causes
were **harness bugs**, fixed in `fuzz10k_memstr.c` and re-run:

- The `applicable[][]` row for `A_STRNLEN` (enum index 9) and `A_STRCMP`
  (enum index 10) were swapped. That made the case-choice loop return a case
  with `applicable == 0` and `cas` default to `C_VALID` even when a hostile case
  was selected — producing 126 `null_src` failures where `strnlen_s`/`strcmp_s`
  were called with a `NULL` source and returned garbage. Fixed by ordering the
  rows to match the enum.
- `A_MEMZERO` had no explicit valid-case setup and fell through to the
  `strnlen` branch, so the expected result was a string length instead of 0.
  Fixed with its own branch (whole `[0,cap)` is expected to be zeroed).
- `reset_buffers` originally wrote a NUL at the incoming `slen`, which could
  leave an earlier terminator after `slen` was recomputed — breaking
  `strncpy`/`strncat` content comparisons. Fixed by using a flat `0x41` source
  fill and placing each NUL explicitly in the case that needs it.

After the fixes all three runs (primary, repeat, UBSan) are clean. No library
source was changed and no failure was suppressed: the final harness still
counts and reports any crash/hang/corruption and exits nonzero on any of them.

## Honesty / scope limitation

Fuzzing 10,240 scenarios is **evidence of stability for these inputs, NOT a
proof of full safety.** The checked APIs perform runtime constraint checks
(null pointer, destination capacity, `RSIZE_MAX`, undefined overlap,
termination); they do not prove pointer provenance, ownership, lifetime, or
alias/use-after-free freedom. Those properties remain the caller's and the
compiler's responsibility.

Additional limitation: an AddressSanitizer replay was attempted but is **not**
usable on this host — Apple clang's ASan runtime aborts at process start with
`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 ... (!asan_init_is_running)`
in this forked harness (exit 134, no library stack). Canary guards plus UBSan
provide the memory-safety evidence here; the ASan binary is gitignored and not
part of the passing result.

## Files

- `fuzz10k_memstr.c` — harness (fixed-seed generator, guarded buffers, child
  isolation, verification and result logging).
- `run.sh` — reproducible nm → build → timeout-600 run → UBSan → digest →
  sanity-check pipeline.
- `.gitignore` — ignores generated binaries and `results/`.
- `results/scenarios.log` — 10,240 per-scenario lines (gitignored).
- `results/scenarios.sha256`, `results/stdout.log`, `results/stderr.log`,
  `results/ubsan_stdout.log`, `results/ubsan_stderr.log`,
  `results/run_output.txt` — evidence artifacts (gitignored).
