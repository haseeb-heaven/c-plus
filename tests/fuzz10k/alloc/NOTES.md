# fuzz10k/alloc — exact run record

## Files owned by this task

- `fuzz_alloc.c` — deterministic scenario generator + forked runner (only source file)
- `run.sh` — nm preflight, build, `timeout 600` run, SHA-256 recording
- `.gitignore` — ignores `results/` (generated evidence, never committed)
- `results/` — generated logs, hashes, binaries, nm dumps (gitignored)

No files outside `tests/fuzz10k/alloc/` were created or modified by this task.
No `git add`/`git commit` was run by this task. Git status at handoff:
`fuzz_alloc.c`, `run.sh`, and `.gitignore` were already tracked/staged by the
parent session; `NOTES.md` is untracked (the parent session may commit it).
Generated `results/` is gitignored.

## Toolchain / date / host

```
date                    2026-11-23 ~12:25 IST
compiler                /usr/bin/clang --version
                        Apple clang version 16.0.0 (clang-1600.0.26.6)
                        Target: arm64-apple-darwin27.2.0
compile flags           -std=c11 -O1 -g -Wall -Wextra -pedantic
                        -I<root>/libc/install-include -I<root>
link                    build/safe/libcplus_safecrt.a
                        build/libc/libcplus_hardened.a -lm -pthread
timeout                 /usr/bin/timeout 600
hasher                  shasum -a 256 (Apple)
fixed PRNG              splitmix64, SEED = 0x46555a5a31304b30 ("FUZZ10K0")
```

Both archives were verified with `nm -gU` before every run (run.sh aborts if
any required symbol is missing). Final pinned artifact hashes:

```
f7f7efe91d257e6b71251a30c79571df854804b0bb23abc6bd5d209d96a397c1  build/safe/libcplus_safecrt.a
918ec219d78e4eead5f3e8f5eb5d50175c44705fd409bef8a37b8c073d9262f9  build/libc/libcplus_hardened.a
```

Symbols confirmed present (T = defined text) in **both** archives:

- safecrt: `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `free_checked_s`,
  `aligned_alloc_s`, `cplus_safe_ptr_is_live`, `cplus_safe_ptr_size`,
  `cplus_safe_ptr_was_freed`, `cplus_safe_alloc_get_meta`,
  `cplus_safe_ptr_check_generation`, `cplus_safe_alloc_live_count`,
  `cplus_safe_alloc_live_bytes`, `cplus_safe_alloc_seq`,
  `cplus_safe_alloc_fail_after`, `cplus_safe_alloc_fail_reset`.
- hardened: `cplus_malloc`, `cplus_free_checked`, `cplus_alloc_is_live`,
  `cplus_alloc_size`, `cplus_alloc_live_count`,
  `cplus_alloc_double_free_count`, `cplus_alloc_fail_after`,
  `cplus_alloc_fail_reset`.

Full nm dumps: `results/nm_safecrt.txt`, `results/nm_hardened.txt`.

> Note: other parallel agents rebuilt both archives several times during this
> session. The final 10,000-scenario run reported below was executed against
> the two hashes above; intermediate runs used earlier builds but produced the
> same counters and an identical scenario log SHA-256.

## Exact commands (from repository root)

```sh
# preflight + build + full run + hash (single entry point)
tests/fuzz10k/alloc/run.sh

# what run.sh effectively executes:
nm -gU build/safe/libcplus_safecrt.a    > tests/fuzz10k/alloc/results/nm_safecrt.txt
nm -gU build/libc/libcplus_hardened.a   > tests/fuzz10k/alloc/results/nm_hardened.txt
/usr/bin/clang -std=c11 -O1 -g -Wall -Wextra -pedantic \
  -Ilibc/install-include -I. tests/fuzz10k/alloc/fuzz_alloc.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/fuzz10k/alloc/results/fuzz_alloc
/usr/bin/timeout 600 tests/fuzz10k/alloc/results/fuzz_alloc \
  tests/fuzz10k/alloc/results/scenarios.log 10000
shasum -a 256 tests/fuzz10k/alloc/results/scenarios.log \
  > tests/fuzz10k/alloc/results/scenarios.log.sha256

# determinism repeat (independent full run, different log path)
timeout 600 tests/fuzz10k/alloc/results/fuzz_alloc \
  tests/fuzz10k/alloc/results/scenarios.repeat.log 10000
shasum -a 256 tests/fuzz10k/alloc/results/scenarios.first.log \
              tests/fuzz10k/alloc/results/scenarios.repeat.log

# negative control (deliberately wrong assertion in a gitignored copy)
sed 's/cplus_safe_ptr_size(q) == want, "malloc_s metadata size wrong"/cplus_safe_ptr_size(q) == want + 1, "MUTANT wrong-size assertion"/' \
  tests/fuzz10k/alloc/fuzz_alloc.c \
  > tests/fuzz10k/alloc/results/fuzz_alloc_mutant.c
# ... compile same as above, run with 120 scenarios ...

# UBSan subset
/usr/bin/clang -std=c11 -O1 -g -fsanitize=undefined \
  -fno-sanitize-recover=undefined -Ilibc/install-include -I. \
  tests/fuzz10k/alloc/fuzz_alloc.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/fuzz10k/alloc/results/fuzz_alloc_ubsan
timeout 300 tests/fuzz10k/alloc/results/fuzz_alloc_ubsan \
  tests/fuzz10k/alloc/results/ubsan.final.log 300
```

## Scenario model and coverage

10,000 **distinct** scenarios (open-addressed dedup over the full parameter
tuple; all 10,000 accepted as unique). `index % 12` cycles uniformly through
12 operation families, so each family ran 623–624 times:

`MALLOC`, `CALLOC`, `RGROW`, `RSHRINK`, `ALIGNED`, `FREE_DOUBLE`,
`FREE_INVALID`, `ZERO`, `INJ_SAFECRT`, `INJ_HARDENED`, `OVERFLOW`,
`REALLOC_INVALID`.

Parameter ranges / special values:

- random sizes `0..65536`, random counts `0..4096`, random alignment `1..256`
- boundary variants: `0,1,2,7,8,...,65536`; `SIZE_MAX`, `SIZE_MAX-1`,
  `RSIZE+1` (`RSIZE=SIZE_MAX>>1`), `SIZE_MAX/2+1`; alignments `0,1,2,3,5,7,...`
- 474 overflow-boundary variants, 453 special-boundary variants, 6,550 uniform
- injected failure: `fail_after(0..8)` on ~25% of scenarios (latching ENOMEM),
  plus dedicated latch-sequence scenarios with `fail_after/reset` on **both**
  archives and sequence/double-free counter assertions
- free paths: `free_s`, `free_checked_s`, double free, stack pointer, interior
  pointer, already-freed pointer
- zero-size: `malloc_s(0)`, `calloc_s(0,x)`, `calloc_s(x,0)`, `realloc_s(p,0)`,
  `aligned_alloc_s(a,0)`
- realloc grow/shrink always writes a position-dependent canary and verifies
  `min(old,new)` bytes survive; on refusal it verifies original pointer,
  byte size, elem size, generation, `was_freed` state and all canary bytes.

Per scenario (in a forked child, `alarm(3)` + 5 s parent watchdog) the harness
checks: no crash/hang, refusal-or-defined-success with expected errno, metadata
size/elem/count/generation/liveness, zero-fill for `calloc_s`, canary intact,
and that SafeCRT live-count/live-bytes and hardened live-count return to the
baseline observed before the scenario.

## Real results (final run against the pinned archives above)

```
FUZZ10K alloc 10000 0 0 0 35988
harness exit code: 0
```

| metric | value |
|---|---|
| scenarios (distinct) | 10000 |
| crashes (WIFSIGNALED, non-SIGALRM) | 0 |
| hangs (alarm exit 124 / watchdog SIGKILL / SIGALRM) | 0 |
| corruptions (child check failure) | 0 |
| refusals (defined EINVAL/EOVERFLOW/ENOMEM/double-free) | 35988 |
| `CHILD=OK` lines | 2930 |
| `CHILD=REFUSED` lines | 4547 |

Scenario log: `results/scenarios.log` (10,003 lines: header, distinct count,
10,000 scenario lines, summary).

```
SHA-256(results/scenarios.log) =
0d4ef528a2462a1c367350ce68894f76e035ec7d9c6799c14e2f478e210f4581
```

Determinism: two independent full runs produced the identical log SHA-256
(`results/scenarios.first.log` and `results/scenarios.repeat.log`); `diff -q`
reported no differences.

Negative control: a copy with one deliberately falsified assertion
(`cplus_safe_ptr_size(q) == want + 1`) reported
`FUZZ10K alloc 120 0 0 10 425`, exit code **1** — the checks are live, not
vacuous.

UBSan subset (300 scenarios, `-fno-sanitize-recover=undefined`): `FUZZ10K alloc
300 0 0 0 1065`, exit 0, empty stderr (`results/ubsan.final.stderr`).

## Honest caveats / corrections

1. An earlier run reported 4 corruptions, all `CALLOC` with `size=0` and a
   non-zero count. Reading `stdlib/safe/src/safe_alloc.h` and `safe_alloc.c`
   showed the specified behavior is a defined **1-byte** block when *either*
   factor is zero. The harness expectation was wrong; it was corrected to
   require `count=elem=1, total=1` for that case. Every run after that fix is
   clean. The 4 findings were not library bugs.
2. AddressSanitizer was attempted (`-fsanitize=address,undefined`) but failed
   to initialize on this macOS host
   (`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`), before any
   scenario ran. ASan results are therefore **not** claimed as evidence; only
   UBSan results are reported.
3. Other agents rebuilt the archives concurrently; hash-pinning and a final
   full re-run were performed as described above.

## Evidence, not proof

This is **evidence from a bounded, deterministic fuzz campaign, not proof of
correctness**. It covers 10,000 generated parameter tuples against one toolchain
and one pair of static archives on one host. The metadata tables have bounded
recent-free caches and the harness cannot observe every alias, every thread
interleaving, or arbitrary host heap states; a passing run does not exclude a
bug outside these scenarios. "No crash/hang/corruption" here means no child
exited by signal, no child hit the alarm/watchdog, and no assertion in this
harness failed. Live-game or production behavior is not established by this
run.
