# fuzz10k / vendored — deterministic 13,000-scenario fuzz of all 26 `cplus_v_*` functions

## Scope

All 26 `cplus_v_*` symbols exported by `build/safe/libcplus_safecrt.a`
(verified with `nm -g --defined-only build/safe/libcplus_safecrt.a | grep -o 'cplus_v_[A-Za-z0-9_]*' | sort -u | wc -l` → `26`):

| # | Symbol | # | Symbol |
|---|--------|---|--------|
| 1 | `cplus_v_memchr_s` | 14 | `cplus_v__stpncpy_s_chk` |
| 2 | `cplus_v_memccpy_s` | 15 | `cplus_v__strnset_s_chk` |
| 3 | `cplus_v__memchr_s_chk` | 16 | `cplus_v__strset_s_chk` |
| 4 | `cplus_v__memccpy_s_chk` | 17 | `cplus_v__strnterminate_s_chk` |
| 5 | `cplus_v__strchr_s_chk` | 18 | `cplus_v__strtolowercase_s_chk` |
| 6 | `cplus_v__strrchr_s_chk` | 19 | `cplus_v__strtouppercase_s_chk` |
| 7 | `cplus_v__strstr_s_chk` | 20 | `cplus_v__strfirstchar_s_chk` |
| 8 | `cplus_v__strcasestr_s_chk` | 21 | `cplus_v__strlastchar_s_chk` |
| 9 | `cplus_v__strspn_s_chk` | 22 | `cplus_v__strfirstdiff_s_chk` |
| 10 | `cplus_v__strcspn_s_chk` | 23 | `cplus_v__strlastdiff_s_chk` |
| 11 | `cplus_v_strlcat` | 24 | `cplus_v__strcmpfld_s_chk` |
| 12 | `cplus_v_strlcpy` | 25 | `cplus_v__strcpyfld_s_chk` |
| 13 | `cplus_v__stpcpy_s_chk` | 26 | `cplus_v__strcoll_s_chk` |

Exact signatures are declared at the top of `fuzz10k_vendored.c`, transcribed
from `libc/install-include/cplus_vendor_{mem,strchr,strstr,spn,stp,set,case,firstlast,field,strl}.h`
(the two internal `memchr`/`memccpy` `_chk` signatures come from the pinned
upstream `third_party/safeclib/src/extmem/*.c` included by `mem_group.c`;
`rsize_t == size_t`, `errno_t == int` at the pin). `restrict` is omitted, which
does not change linkage.

## Determinism

- PRNG: splitmix64 (`rng64()`).
- Fixed seed: **`0xC0FFEE5AF10D5EED`** (`SEED` in the harness).
- Every scenario independently reseeds with `SEED ^ (scenario_index * 0x9E3779B97F4A7C15)`.
  This makes results independent of execution order, including restarts after
  a detected crash/hang.
- Corpus: **26 functions x 500 scenarios = 13,000 distinct scenarios**
  (confirmed per-function counts of exactly 500 each in `scenarios.log`).
- Result digest: FNV-1a 64 over scenario index, function, all generated
  lengths/length-schedules/bos values/NULL flags/content bytes and normalized
  results (pointer addresses are never hashed). Per-scenario digests are
  folded in index order, so the digest is restart-independent.

## Input shapes

- Strings: 0..512 bytes drawn from a bounded 20-byte alphabet that includes
  NUL (twice, so embedded NULs are common), ASCII letters/digits/space,
  control bytes (`0x01`, `0x09`, `0x0A`, `0x0D`, `0x1F`, `0x7F`) and
  non-ASCII bytes (`0x80`, `0xC3`, `0xFF`). Buffers are 1024-byte payloads
  inside 16-byte pre/post canary guards and are always NUL-terminated within
  the object.
- `dmax` / `slen` schedules include `0`, `1`, exact fit, one-over (`1025`,
  `513`), `RSIZE_MAX_STR+1` (`4097`) and `SIZE_MAX`, plus bounded random
  values. Extreme lengths are paired with a *known* destination object size
  so the checked wrapper must refuse (`EOVERFLOW`/`ESLEMAX`) rather than scan
  or write outside the guard. Public `mem*` / `strl*` entry points (no bos
  parameter) are bounded to the real 1024-byte object.
- NULL is supplied randomly for destination, source and out-parameters
  (all checked wrappers validate out-parameters and return `ESNULLP`).
- Destinations are canary-guarded; canaries are checked after every call,
  including refusals. A forked child publishes canary/corruption counts and
  an FNV digest through `mmap(MAP_SHARED|MAP_ANON)`.

## Handler / subprocess setup

- `cplus_runtime_set_violation_trap(0)` — refusals return error codes instead
  of `_exit(70)`.
- `cplus_sc_set_mem_constraint_handler_s_wrapper(silent_handler)` and
  `cplus_sc_set_str_constraint_handler_s_wrapper(silent_handler)` — no-op
  handlers that only increment a refusal counter, so stdout stays clean.
- A forked child runs scenarios; the parent polls its published progress
  every 10 ms. A signal death is a **crash**, a nonzero child exit is a
  **failure**, and 400 consecutive polls (4 s) without progress is a
  **hang** (child `SIGKILL`ed). On any of these the parent records the
  scenario, advances to the next one and forks a fresh child so the entire
  corpus still runs. Any crash, hang or corruption makes the parent exit 1.

## Exact commands (from repo root)

```sh
# build + run (timeout 600, /usr/bin/clang -std=c99, both archives):
sh tests/fuzz10k/vendored/run.sh
```

`run.sh` expands to:

```sh
cd tests/fuzz10k/vendored
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -Wno-unused-parameter \
  fuzz10k_vendored.c \
  ../../build/libc/libcplus_hardened.a \
  ../../build/safe/libcplus_safecrt.a \
  -lm -pthread -o bin/fuzz10k_vendored
timeout 600 ./bin/fuzz10k_vendored
```

Both archives are required: `libcplus_safecrt.a` alone leaves
`_cplus_sc_upstream_memrchr_s` unresolved (used by `v_strchr_group.o`).
Build warnings: none.

## Results (real runs, not fabricated)

Final clean run stdout:

```
FUZZ10K vendored 13000 0 0 0 7175
```

`digest.txt`:

```
seed=0xc0ffee5af10d5eed
functions=26
scenarios=13000
digest=a5f9a37fc06413a9
crashes=0
hangs=0
corruptions=0
refusals=7175
```

Verification performed:

1. Two independent full runs produced byte-identical `digest.txt`
   (`a5f9a37fc06413a9`) and stdout → deterministic replay.
2. `scenarios.log` contains exactly 13,000 lines, 500 per function, with
   `canary_ok=1` on all 13,000; zero `CORRUPTION` records.
3. Of the 3,330 checked-function scenarios with `dmax > 1024`, 3,317
   refused; the remaining 13 are all `cplus_v__strcpyfld_s_chk` with
   `slen == 0`, which returns `EOK` early before any access (defined result,
   canaries intact).
4. Failure-path check: a temporary copy of the harness with one forced
   canary failure (built/run only under gitignored `bin/`) printed
   `FUZZ10K vendored 13000 0 0 1 7175`, logged `CORRUPTION 7 ...`, and
   exited **1** — confirming the nonzero-on-failure contract. The clean
   run was then restored; the probe was deleted.

## Evidence, not proof

These results are **empirical evidence, not proof**. They show that under
this harness — this PRNG seed, these input shapes, this build, this platform
and these runtime settings — none of the 26 vendored functions crashed,
hung, corrupted a guard, or wrote outside its guarded object across 13,000
scenarios. They do **not** constitute a formal correctness proof: other
inputs, seeds, build configurations, locales, object sizes or environments
may still expose faults. In particular, a refusal count is an observed
constraint-handler invocation count, not a specification of which inputs
must be refused.

## Files (all under `tests/fuzz10k/vendored/`)

- `fuzz10k_vendored.c` — harness source.
- `run.sh` — build + `timeout 600` runner.
- `NOTES.md` — this record.
- `digest.txt` — tracked evidence digest/counts.
- `.gitignore` — ignores `scenarios.log`, `bin/`, `logs/`.
- `scenarios.log` (gitignored) — per-scenario CSV + corruption records.
- `logs/`, `bin/` (gitignored) — run captures and compiled binaries.
