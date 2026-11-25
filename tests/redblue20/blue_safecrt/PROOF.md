# BLUE-20 SafeCRT proof — checked layer refuses malformed input with canaries intact

## Result

```
BLUE20 safecrt 2400 2400 0
```

`cases=2400 passed=2400 failed=0`. Exit status 0. Full suite completed in ~8 s,
well inside the 60 s bound. Reproduced identically on two consecutive runs.

## What is proven

The eight checked adapters in the SafeCRT layer are exercised through their
public macro spellings (from `stdlib/safe/safe_memory.h` and
`stdlib/safe/safe_string.h`):

| # | API | checked symbol in `libcplus_safecrt.a` |
|---|-----|----------------------------------------|
| 0 | `memcpy_s`  | `cplus_memcpy_s`  |
| 1 | `memmove_s` | `cplus_memmove_s` |
| 2 | `memset_s`  | `cplus_memset_s`  |
| 3 | `memcmp_s`  | `cplus_memcmp_s`  |
| 4 | `strcpy_s`  | `cplus_strcpy_s`  |
| 5 | `strncpy_s` | `cplus_strncpy_s` |
| 6 | `strcat_s`  | `cplus_strcat_s`  |
| 7 | `strncat_s` | `cplus_strncat_s` |

For each adapter six violation classes are generated:

| # | class | representative input |
|---|-------|----------------------|
| 0 | null           | NULL source (or NULL dest for `memset_s`) |
| 1 | zero           | `dmax == 0` |
| 2 | one-over       | requested length `dmax + 1` (or source string longer than `dmax`) |
| 3 | SIZE_MAX       | `(rsize_t)-1` length |
| 4 | overlap        | source range inside destination range |
| 5 | unterminated   | source data region contains no NUL byte |

Coverage: **8 APIs × 6 classes × 50 fixed-seed rounds = 2400 cases**.

Every case must satisfy all of:

1. The child process survives (no signal — a crash such as a guard-page fault
   or stack smash is counted as a failure). Children are bounded by `alarm(5)`.
2. A constraint violation returns a non-zero error code; a case that the API
   is defined to handle safely returns success.
3. **Canary bytes immediately after the declared capacity are unchanged** in
   both the destination and the source buffer. Destination capacity is
   `[0, cap)`, canary is `[cap, cap+32)`; source data is `[0, cap+8)`, canary
   is `[cap+8, 128)`. A refused operation that over-writes `dmax` bytes past
   the declared capacity corrupts the canary and fails the case.
4. For the two "handled, not refused" overlap cases, the defined result is
   also checked (`memmove_s` yields the correctly shifted bytes; `memcmp_s`
   self-comparison yields `diff == 0`).

## Per-API / per-class expected outcome

Classes that are genuine constraints for an API must be **refused**. Where a
class is not a constraint for that API by design, the case requires **success +
intact canary** instead; this is stated explicitly rather than silently
skipped.

| API | null | zero | one-over | SIZE_MAX | overlap | unterminated |
|-----|------|------|----------|----------|---------|--------------|
| `memcpy_s`  | refuse | refuse | refuse | refuse | refuse | safe binary copy |
| `memmove_s` | refuse | refuse | refuse | refuse | **handled** (correct result) | safe binary copy |
| `memset_s`  | refuse | refuse | refuse | refuse | n/a — single pointer: fill to boundary | n/a — non-NUL binary fill |
| `memcmp_s`  | refuse | refuse | refuse | refuse | **handled** (self-compare equal) | safe binary compare |
| `strcpy_s`  | refuse | refuse | refuse | refuse | refuse | refuse (bounded scan) |
| `strncpy_s` | refuse | refuse | refuse | refuse | refuse | refuse (bounded scan) |
| `strcat_s`  | refuse | refuse | refuse | refuse | refuse | refuse (bounded scan) |
| `strncat_s` | refuse | refuse | refuse | refuse | refuse | refuse (bounded scan) |

Notes on the table:

- `memmove_s` and `memcmp_s` intentionally accept overlap (that is their
  contract), so those cases verify safe handling instead of refusal.
- `memset_s` takes a single pointer, so overlap and unterminated-source do not
  apply; the corresponding slots verify a full-capacity fill right up to the
  canary boundary and a non-NUL binary fill, both with canaries intact.
- `strcpy_s` / `strcat_s` have no explicit length argument. Their SIZE_MAX case
  uses a terminator-free source longer than `dmax`, which is the string-API
  equivalent of an unbounded length request and is refused (406).

Observed refusal codes (recorded during harness development, test with
`!= 0` per the API contract): null `400`, zero `401` (string APIs / `memset_s`
`406`), one-over `406`, SIZE_MAX `403` (string copy/append `406`), overlap
`404`, unterminated string source `406`.

## Method

- **Fork per case.** `main()` forks once per case; the child runs one
  deterministic case and `_exit`s 0 (PASS) or 1 (FAIL). A crash or hang is
  therefore isolated to a single case (`alarm(5)` bounds a hang; `waitpid`
  status is checked). The parent tallies and prints
  `BLUE20 safecrt <cases> <passed> <failed>`.
- **Fixed seed.** `xorshift32` seeded with `0x5EED1234`, mixed with the case
  index via `0x9E3779B9`. Capacity (16..31), fill byte and overlap offset are
  derived only from that state, so the suite is fully reproducible.
- **Bounded timeout.** `run.sh` wraps the whole harness with `timeout 60`.
- **Archives linked.** `build/safe/libcplus_safecrt.a` (checked layer) followed
  by `build/libc/libcplus_hardened.a` (hardened libc). `run.sh` verifies with
  `nm -g` that all 8 adapters and the 3 support symbols exist before building,
  and fails closed otherwise (see `out/symbols.txt`).
- **Quiet diagnostics.** The safeclib constraint handlers are replaced with a
  no-op and the violation trap is disabled so refusals do not print banners;
  the constraint codes and canary behaviour are unaffected.

## Exact evidence artifacts

- `out/run.log` — the two-line run output (header + `BLUE20 safecrt 2400 2400 0`).
- `out/build.log` — clean compile (empty on success).
- `out/symbols.txt` — `nm` symbol verification for all 11 required symbols.
- `out/proof` — the built fork-per-case harness.

Environment: Darwin 27.2.0 arm64; Apple clang 16.0.0 (clang-1600.0.26.6);
branch `agent/modern-stdlib`, commit `7edf2c6`.
Archive SHA-256:
`libcplus_safecrt.a = 7352b9f4fa8ad6c5b6842704546e8655fd306974e433d2acebd0fe014b9aac3a`,
`libcplus_hardened.a = bbbde45840699f0c39ef912b59640af0910ebe1d34186110be3e89389d6d3331`.

## Negative control (harness is not vacuous)

A temporary copy of `proof.c` with one deliberate canary write (`D[cap] = 0`)
in every case was built and run with 64 cases:

```
BLUE20 safecrt 64 0 64
```

with `FAIL ... canary=0` diagnostics for every case. This confirms the
canary check actually detects corruption, so the 2400/2400/0 result reflects
the checked layer's behaviour rather than a pass-through harness.

## Reproduce

```sh
cd <repo-root>
sh tests/redblue20/blue_safecrt/run.sh          # 2400 cases, prints BLUE20 line
CASES=4800 sh tests/redblue20/blue_safecrt/run.sh  # optional larger run
```

## Honest limits

- Canaries detect writes past the *declared* capacity; they are not a proof of
  pointer provenance, ownership, lifetime, or alias/UAF safety. This suite
  exercises runtime constraint checks only.
- The declared `dmax` is truthful in every case, so safeclib's error-path
  zeroing stays inside the declared capacity. A caller that lies about `dmax`
  to an *untracked* destination remains outside this proof's scope; the
  tracked-capacity / guard-page defence for that case is a separate concern.
- No source or header was modified; only files under
  `tests/redblue20/blue_safecrt/` were created.
