# FUZZ10K / io — deterministic fuzz of the checked stream-I/O APIs

## Scope

Exactly the six requested symbols, verified present as global text symbols in
`build/safe/libcplus_safecrt.a` with `nm -gU` before every build:

```
0000000000000000 T _cplus_fopen_s
000000000000033c T _cplus_fclose_s
0000000000000544 T _cplus_fread_s
0000000000000618 T _cplus_fwrite_s
0000000000000918 T _cplus_fprintf_s
0000000000001094 T _cplus_fscanf_s
```

(`cplus_fprintf_s_checked` / `cplus_fscanf_s_checked` also exist but are out of
scope; the harness calls the six requested prefix functions directly.)

## Files

- `fuzz10k_io.c` — deterministic scenario generator, forked runner, and all
  per-scenario assertions.
- `run.sh` — symbol check, build, bounded temp dir, `timeout 600` run, digest,
  shape check, and summary.
- `NOTES.md` — this record.
- `results/` — gitignored by the root `.gitignore` rule
  `tests/fuzz10k/**/results/`; holds `scenarios.log`, its `.sha256`, the
  stdout/stderr captures, the compiled binary, and small debug/control artifacts.

No files outside `tests/fuzz10k/io/` were created or modified, and nothing was
staged or committed.

## Exact commands (run from the repository root)

```sh
# symbol verification (also executed at the top of run.sh)
nm -gU build/safe/libcplus_safecrt.a | \
  grep -E '^[0-9a-f]+ T _cplus_(fopen_s|fclose_s|fread_s|fwrite_s|fprintf_s|fscanf_s)$'

# compile (both archives, in this order)
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -Wno-unused-parameter \
  -Wno-format-nonliteral -Wno-format-security \
  -Istdlib/safe -Ilibc/install-include -Ilibc \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/fuzz10k/io/fuzz10k_io.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/fuzz10k/io/results/fuzz10k_io

# full run
sh tests/fuzz10k/io/run.sh
```

Compiler: Apple clang 16.0.0 (`/usr/bin/clang`, arm64-apple-darwin).
Link line uses **both** archives: `libcplus_safecrt.a` then
`libcplus_hardened.a`, matching `tests/adversarial_1000/fmt_io/run.sh`.

## Determinism

- Fixed seed `FUZZ_SEED = 0x46555A5A3130304BULL` (ASCII `FUZZ100K`).
- Per-scenario stream: `r = FUZZ_SEED + id * 0x9E3779B97F4A7C15`, then
  splitmix64 draws. No `time()`, `getpid()`, `getenv()`, `rand()`, or temp-path
  text participates in any decision or in the log.
- `api = id % 6` (round-robin, exactly 2,000 scenarios per API); all other
  parameters (case, `bmax` 0..4096, element size 0..8, count mode, payload
  length 0..4096, values) are PRNG-derived.
- Count modes: normal (PRNG), zero, one-over (`bmax/size + 1`), and huge
  (`(size_t)-1`). Element size 0 and NULL path/mode/stream/buffer force
  documented refusals. Payloads are pseudo-random binary bytes, 0..4096.
- Duplicate decision tuples are checked with an open-addressed 32K fingerprint
  set; `FUZZ10K_DISTINCT 12000` is emitted and verified.

## Isolation, canaries, and bounds

- Every scenario runs in a fresh `fork()`ed child. The child arms `alarm(5)`
  and `_Exit(99)` on SIGALRM (hang). The parent classifies WIFSIGNALED as a
  crash, exit 99 as a hang, exit 1 as a corruption, and exit 0 as a pass.
- The wrapper adds a process-wide `/usr/bin/timeout 600`.
- All read/write buffers are `malloc(bmax + 64)` with 32-byte `0x5A` guard
  bands before and after the declared `bmax` body; both guards are checked
  after every call. Read buffers are prefilled with `0xA5`, and bytes past the
  region stdio may legitimately touch (including a partial final item at EOF)
  must remain `0xA5`, which proves no over-read/write past the clamped bound.
- File fixtures are created only in a `mktemp -d "$TMPDIR/fuzz10k_io.XXXXXX"`
  directory, removed on exit; the parent also `unlink()`s each fixture. No
  home/user path is referenced anywhere.
- File integrity: fixtures are read back with raw stdio using a cap of
  `MAX_BUF + 16 = 4112`; a file larger than the cap is a failure, so an
  unbounded write cannot pass. Writes must match the exact expected prefix;
  reads and scans must leave the fixture byte-identical.
- The library's violation trap is deliberately disabled in the child
  (`cplus_runtime_set_violation_trap(0)`) so documented refusals (`%n`,
  unbounded `%s`, double close, NULL args) are observable as defined return
  values (`-1` / `EOF` / `0`) instead of the library's default exit(70). This
  is a harness observation choice, not a library change.

## Results — verified real runs

Final command: `sh tests/fuzz10k/io/run.sh` (exit 0, ~18 s wall clock, well
inside `timeout 600`).

```
FUZZ10K io 12000 0 0 0 9116
scenario lines = 12000 ; distinct trailers = 1 ; summary lines = 1
FUZZ10K_DISTINCT 12000
digest: ee083050e118487c1a9fcc845ec5d3ee1bac2786796a7980e2b228e9467c2ddc
```

Counts: **12,000 scenarios, 0 crashes, 0 hangs, 0 corruptions, 9,116 refusals**.
The scenario log contains 12,000 `S` lines plus two trailers; 5,893 outcomes
are defined results (`D`) and 6,107 are documented refusals (`R`).

Per-API distribution (2,000 scenarios each):

| API       | defined | refusal |
|-----------|---------|---------|
| fopen_s   |     620 |    1380 |
| fclose_s  |     392 |    1608 |
| fread_s   |    1103 |     897 |
| fwrite_s  |    1113 |     887 |
| fprintf_s |    1527 |     473 |
| fscanf_s  |    1138 |     862 |

Reproducibility: a direct re-run of the compiled binary produced byte-identical
`scenarios.log` with the same SHA-256. One early wrapper run briefly observed a
partial log because `shasum` raced the file's final flush; `sync` was added to
`run.sh` after the harness, and the subsequent wrapper run (run 3) reported the
stable digest above with a clean shape check. The recorded digest is the final,
verified one.

Negative control (120-scenario variant with
`cplus_runtime_set_violation_trap(1)` restored): `FUZZ10K io 120 0 0 11 88`,
exit status 1. The harness therefore does detect unhandled violations rather
than silently passing.

## Evidence, not proof

This is **evidence for the tested inputs, not a proof of safety**. The 12,000
scenarios exercise a deterministic, PRNG-derived slice of the input space for
the six in-scope APIs; they cannot cover every possible argument combination,
filesystem state, libc implementation, or platform. A clean result means no
crash, hang, canary violation, unbounded transfer, or file-integrity failure
was observed for these specific scenarios on this build and host. It does not
establish the absence of defects outside this corpus, and it is not a
substitute for formal verification or review of the library implementation.
