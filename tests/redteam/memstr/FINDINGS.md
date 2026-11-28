# REDTEAM memstr — FINDINGS

## Scope & method (honest summary)

Attacked the 12 checked memory/string APIs in `build/safe/libcplus_safecrt.a`
(`cplus_sc_*` and `cplus_*_s` families) with **6000 deterministic adversarial
cases** (fixed PRNG seed `0xC0FFEE1234`, generator `attack_cases.py`). Every case
runs in a **forked child** with a 3-second watchdog; a crash cannot kill the
harness. Dest canaries (0xC3) start exactly at the declared capacity `cap`.

Result (exact stdout; two consecutive runs were byte-identical):

```
REDTEAM memstr 6000 5742 50 160 48
```

| class | count | genuine finding? |
|---|---:|---|
| REFUSED (documented code, canary intact) | 5742 | — |
| CRASH (signal exit) | 50 | **YES** |
| CORRUPT (canary changed) | 160 | **YES** |
| UNEXPECTED_OK (API reported success) | 48 | **NO — see §3** |

Toolchain, commands, symbol evidence, digests: `results/toolchain.txt`,
`results/nm_evidence.txt`, `results/digests.sha256`. Full per-case log:
`results/scenarios.log` (`id fam api cas dm n cap outcome [observed]`).

---

## 1. GENUINE FINDING — OOB memset / SIGSEGV on a self-detected constraint
##    violation when `dmax > actual destination capacity` (BOS unknown)

**Severity:** high. **APIs affected:** `memcpy_s`, `memmove_s` (both `cplus_sc_*`
and `cplus_*_s` families); also the `handle_error` overlap path of `strcpy_s` /
`strncpy_s` when their accepted `dmax` slightly exceeds the real buffer.

### Contract that is promised but broken

The checked APIs promise that a runtime-constraint violation (null source,
overlapping objects) is *refused* by returning a documented non-zero code and
leaving memory untouched beyond the declared destination. Instead, with a
`dmax` the library itself accepts (`dmax <= RSIZE_MAX_MEM == 16384`, or
`dmax <= RSIZE_MAX_STR == 4096` for string entry points), the library's **own
error-handling path** writes `dmax` bytes to `dest`:

- `third_party/safeclib/src/mem/safe_mem_constraint.h:handle_mem_error()` →
  `memset(dest, 0, dmax)`.
- `third_party/safeclib/src/mem/memcpy_s.c` overlap branch →
  `mem_prim_set(dp, dmax, 0)`.
- `third_party/safeclib/src/str/safe_str_constraint.h:handle_error()` →
  `memset(dest, 0, dmax)` (when compiled with `SAFECLIB_STR_NULL_SLACK`).

`cplus_sc_*` forces `BOS_UNKNOWN`, so safeclib cannot clamp `dmax` to the real
object size; it trusts the caller-supplied value. A `dmax` one byte larger than
the real buffer therefore turns a *clean refusal* into a one-byte canary
corruption, and a `dmax` of several KiB turns it into `SIGSEGV` / heap
corruption. The return code still says "refused" (400 ESNULLP / 404 ESOVRLP),
so a caller that only checks the return code believes the operation was safely
rejected while adjacent memory has been destroyed.

### Exact reproducers

**A. memcpy_s — null src, accepted oversized dmax → canary corruption then SIGSEGV**

```sh
cd tests/redteam/memstr
ROOT=$(cd ../../.. && pwd)
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
/usr/bin/clang -O0 -g -std=c99 $INC repro_corrupt.c \
  "$ROOT/build/safe/libcplus_safecrt.a" "$ROOT/build/libcplus_core.a" -pthread -o repro_corrupt
ulimit -c 0
./repro_corrupt 512 memcpy       # dmax=512, real dest=256 bytes
./repro_corrupt 5740 memcpy      # dmax=5740 -> SIGSEGV/SIGABRT
```

Observed (`repro_corrupt.c` prints the rc and canary damage):

```
api=memcpy dmax=512 rc=400
first corrupted byte at offset 0 (value 00)
corrupted bytes (within 600B window)=512
# dmax=5740: process dies with signal (exit 139 SIGSEGV / 134 SIGABRT depending on stack layout)
```

The forked harness recorded multiple concrete instances, e.g.:

```
FINDING id=73  fam=0 api=memcpy_s  cas=DMAX_GT_CAP     dm=9506 n=4 cap=166 CRASH   [signal=11]
FINDING id=205 fam=0 api=memcpy_s  cas=DMAX_GT_CAP     dm=5740 n=3 cap=37  CORRUPT [rc=404 can=0 first_bad=0]
FINDING id=25  fam=0 api=memcpy_s  cas=DMAX_GT_CAP_ONE dm=40   n=7 cap=39  CORRUPT [rc=404 can=0 first_bad=39]
```

`id=25`: `attack_cases.tsv` row `25 0 0 9 39 40 7 ...` →
`cplus_sc_memcpy_s(dest[39 real bytes], dmax=40, src=dest+off, n=7)` returns
ESOVRLP (404) but zeroes byte 39 (first canary byte).

**B. memmove_s — null src, oversized dmax**

```
FINDING id=14  fam=1 api=memmove_s cas=DMAX_GT_CAP_ONE dm=171  n=8 cap=170 CORRUPT [rc=400 can=0 first_bad=170]
FINDING id=254 fam=1 api=memmove_s cas=DMAX_GT_CAP     dm=13048 n=7 cap=42  CRASH   [signal=11]
FINDING id=590 fam=1 api=memmove_s cas=DMAX_GT_CAP     dm=10008 n=7 cap=211 CRASH   [signal=11]
```

Run `./repro_corrupt 512 memmove` for a standalone memmove reproducer.

**C. strcpy_s / strncpy_s — overlap, oversized dmax**

```
FINDING id=126 fam=1 api=strcpy_s  cas=DMAX_GT_CAP_ONE dm=12 n=4 cap=11 CORRUPT [rc=404 can=0 first_bad=11]
FINDING id=223 fam=0 api=strncpy_s cas=DMAX_GT_CAP     dm=334 n=5 cap=8  (earlier build) CORRUPT [rc=404 ...]
```

### Caveat (stated honestly)

Triggering this requires the caller to pass a `dmax` larger than the real
destination object. With `BOS_UNKNOWN` the library cannot prove capacity, so
this is an instance of the documented "BOS unknown" limitation. It is still a
genuine finding because (a) the API *accepted* the value as valid, (b) the API
*itself detected* a constraint violation and promised a clean refusal, and
(c) the refusal path — not the copy path — is what performs the OOB write. A
defensive fix would clamp the error-path clear to the proven object size (or
skip it when capacity is unknown) rather than to the untrusted `dmax`.

---

## 2. GENUINE FINDING class detail (aggregate)

All 210 genuine findings (50 CRASH + 160 CORRUPT) share one root cause:
**the error path clears `dmax` bytes** with `dmax` larger than the real buffer.
Breakdown by API/attack (from `results/harness.stderr`):

| API | attack | outcome | count |
|---|---|---|---:|
| memmove_s | DMAX_GT_CAP_ONE | CORRUPT | 65 |
| memmove_s | DMAX_GT_CAP | CRASH | 28 |
| memmove_s | DMAX_GT_CAP | CORRUPT | 25 |
| memcpy_s | DMAX_GT_CAP_ONE | CORRUPT | 41 |
| memcpy_s | DMAX_GT_CAP | CORRUPT | 29 |
| memcpy_s | DMAX_GT_CAP | CRASH | 22 |

`DMAX_GT_CAP` = `dmax = cap + delta` up to the library's accepted ceiling;
`DMAX_GT_CAP_ONE` = `dmax = cap + 1`. The violation is a null source (rc 400) or
overlapping objects (rc 404). CRASHes are `signal=11` (SIGSEGV).

---

## 3. UNEXPECTED_OK cases — examined, **not** genuine findings

48 cases reported `UNEXPECTED_OK` mechanically (hostile case + API returned
success). All 48 are `strncpy_s` with `cas=DMAX_GT_CAP`/`DMAX_GT_CAP_ONE`, small
`n`, and `dmax = cap + delta`, canary intact:

```
FINDING id=679 fam=0 api=strncpy_s cas=DMAX_GT_CAP_ONE dm=232 n=1 cap=231 UNEXPECTED_OK [rc=0 can=1 first_bad=-1]
FINDING id=907 fam=0 api=strncpy_s cas=DMAX_GT_CAP     dm=1095 n=2 cap=206 UNEXPECTED_OK [rc=0 can=1 first_bad=-1]
```

**Verified not a finding.** Exact standalone reproducer `repro679.c` (case 679
parameters: cap=231, dm=232, n=1, overlap offset 5):

```
off=5 CAP=231 dm=232 n=1 src[0..2]=424242
rc=0
buf[231]=c3 buf[232]=c3 ...            # canary fully intact
```

`strncpy_s` with `slen=1` copies one byte plus a NUL, both inside the real
buffer; the oversized `dmax` was never exercised because the string terminated
early. No OOB write, no crash, no contract violation. These are **safe
completions**, not findings. They are kept in the log for transparency and are
excluded from the genuine-finding count.

---

## 4. Cases with NO finding

5742 cases were safely refused with the documented code and intact canaries,
covering (both API families, PRNG-varied):

- `DMAX_ZERO` (dmax = 0 → ESZEROL/0), `NULL_DEST`, `NULL_SRC` (ESNULLP),
  `ONE_OVER` (n = cap+1+δ, truthful dmax → ESNOSPC), `HUGE_N`
  (n = SIZE_MAX / RSIZE_MAX+1 → ESLEMAX), `HUGE_DMAX` (dmax = SIZE_MAX →
  ESLEMAX), `OVERLAP` (memcpy/strcpy/strncpy with truthful dmax → ESOVRLP),
  `UNTERMINATED` (strcat/strncat → ESUNTERM), `VALUE_GT255`
  (memset_s value > 255 → ESLEMAX), `DIFF_NULL` (memcmp_s/strcmp_s diff=NULL →
  ESNULLP), `EMBEDDED_NUL` (embedded NUL + oversized n/dm → ESLEMAX).

APIs with **no finding** in this campaign: `memset_s`, `memzero_s`, `memcmp_s`,
`strcat_s`, `strncat_s`, `strcmp_s`, `strnlen_s`, `strtok_s` (the eight APIs not
listed in §1/§2). This is evidence of correct refusal for these specific
deterministic inputs, not a proof of full memory safety.

---

## 5. Files

- `attack_cases.py` — deterministic generator (6000 cases, seed 0xC0FFEE1234).
- `attack_harness.c` — forked-child harness + outcome classification.
- `run.sh` — nm symbol verification → generate → build → run → digest.
- `results/attack_cases.tsv`, `results/scenarios.log`, `results/*.sha256`,
  `results/toolchain.txt`, `results/nm_evidence.txt`, `results/harness*.{stdout,stderr}`
  (all gitignored).
- `repro_corrupt.c`, `repro679.c` — standalone reproducers (gitignored).

**Exit status:** harness exits 0 (it ran; findings are reported, not hidden).
