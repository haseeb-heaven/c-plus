# FUZZ10K — consolidated interpretation

This document interprets the 10,000-scenario-per-group fuzz campaign summarized
in `RESULTS.md`. All counts referenced here come from the **actual aggregate
run** recorded there; nothing is inferred from a build alone and no pass is
fabricated. The campaign is the larger successor to the `ADV1000` campaign
(`tests/adversarial_1000/`), reusing its honesty rules (dynamic discovery,
canonical final line, negative controls, evidence-vs-proof boundary) at 10× the
scenario volume.

## 1. Scope of this campaign

Nine library families are fuzzed, each by a deterministic, fixed-seed harness
owned by a separate agent. The aggregate runner
(`tests/fuzz10k/run_all.sh`) discovers `<group>/run.sh` at run time, executes
each from the repository root under `timeout -k 10 900` with a restricted
`PATH` and `CPLUS_CLANG=/usr/bin/clang`, parses the last canonical `FUZZ10K`
line, and continues after any failure.

| Group | Hostile family exercised | Harness / build mode |
|---|---|---|
| `memstr` | Bounded/checked memory + string APIs (`cplus_mem*_s`, `memcpy/memmove/memset` bounded variants, `strcpy_s/strcat_s/strncpy_s/…`, constraint-handler behaviour) | C, `/usr/bin/clang -std=c11`, safecrt + hardened archives |
| `alloc` | Checked allocation APIs: overflow sizes, `SIZE_MAX`, count overflow, injected realloc failure/atomicity, double free, aligned alloc, canary + metadata state | C, `/usr/bin/clang -std=c11`, both archives, `nm` symbol preflight |
| `containers` | `vector`/`string`/`deque`/`list`/`stack`/`queue`/maps/sets and memory helpers: out-of-range indices, self-append/alias, stale generations, random op sequences vs an independent model, deliberate BREAKING probes | C+ `--strict --sanitize=none`, forked batches, `waitpid` crash/hang containment |
| `format` | Checked format/conversion APIs: forbidden `%n`, truncation/overflow, width/precision matrices, non-literal formats | C, `/usr/bin/clang -std=c99`, both archives, forked subprocess |
| `json_log` | JSON parsing/serialization + `log_sink`: NULL/duplicate keys, deep nesting, malformed/escaped input, sink writes | C+ `--strict --sanitize=none` (JSON flags where applicable) |
| `net_http` | Offline HTTP/URL parsing and bounded response handling: malformed framing, zero caps, invalid/borrowed descriptors — no DNS, proxy or external host | C+ `--default --sanitize=none -lcurl` (see §6 for why not `--strict`) |
| `thread_async` | `thread`/`mutex`/`condvar`/`guard`, libuv `loop`/`timer`, `lifetime_owner`/`borrow`, generation/liveness helpers: double lock/unlock/drop, failed join, double start, stale post-release handles (metadata-only) | C+ `--strict --sanitize=none -luv`, per-scenario `alarm(5)` watchdog |
| `vendored` | All 26 vendored `cplus_v_*` functions | C, `/usr/bin/clang -std=c99`, both archives, forked subprocess |
| `io` | Checked C+ stream-I/O APIs: hostile paths, bounded fixtures, read/write/position cases | C, `/usr/bin/clang -std=c11`, both archives, forked subprocess |

"Hostile" here means inputs chosen from documented pathological spectra:
`0`, `1`, `SIZE_MAX`, `cap`, `cap±1`, `-1`, interior/stale pointers, NULL
handles, mismatched generation tokens, malformed framing, forbidden format
specifiers and random-but-seeded operation sequences — not benign happy-path
inputs. Each scenario is reproducible from its id and a fixed PRNG seed
(e.g. containers `0x1B56C0DE5EED1234` with xorshift64\*, thread_async
`0xF0A7704A5EED5A17` splitmix64); no wall-clock, pid, address or environment
entropy enters the scenario log. Scenario logs and their SHA-256 digests are
written under each group's gitignored `results/` directory (and, for some
groups, at the group top level) as deterministic evidence.

## 2. What crash / hang / corruption detection actually means

The five numeric fields of the canonical line are
`FUZZ10K <group> <scenarios> <crashes> <hangs> <corruptions> <refusals>`.

- **crashes** — a harness process/batch terminated by a signal. Groups that
  fork per batch detect this with `waitpid(..., WNOHANG)` +
  `WIFSIGNALED`/`WTERMSIG` (containers), or an equivalent forked-subprocess
  status check (format, io, vendored). A crash is never silently retried as a
  pass; it is counted and makes the group exit nonzero. The aggregate runner
  treats `crashes > 0` as a real failure.
- **hangs** — a scenario or batch that exceeded an independent deadline.
  Mechanisms in use: a `12 s` per-batch deadline with `SIGKILL` (containers),
  a per-scenario `alarm(5)` watchdog returning status `99` (thread_async), and
  the external `timeout 600` process bound on every group. `hangs > 0` (or an
  aggregate `timeout -k 10 900` expiry, rc `124`/`137`) is a real failure.
- **corruptions** — a guard/invariant check failed while the process was alive.
  Concretely: a 64-byte per-id global sentinel changed, a tracked guard block
  or its outside-the-write-range bytes changed, an independent model
  diverged (`size <= capacity`, element-by-element match), a set of two
  64-bit canaries plus live-block metadata count changed, a sentinel around
  JSON/log/format/IO work buffers changed, or a defined-result contract was
  violated. `corruptions > 0` is a real failure. A group's runner exits
  nonzero when its harness reports any such violation, so the aggregate sees
  both a nonzero runner rc and a nonzero corruption count.
- **refusals** — *expected* negative-control outcomes: the API rejected a
  hostile input with a documented result code instead of proceeding. Refusals
  are **not** failures; they are the primary positive evidence that bounds
  and liveness checks fire. The aggregate runner deliberately does not fail a
  group for a high refusal count.

The aggregate runner also fails (with status `NO_FINAL_LINE`, `MALFORMED`,
`TIMEOUT`, `RC=<n>` or `LOW_N`) when a group produces no canonical line, a
malformed one, times out, exits nonzero without accounting for it, or reports
fewer than 10,000 scenarios. Zero discovered groups is exit `2`, explicitly
"NOT a pass". Every failure is recorded to stderr and the table, then the
runner continues to the next group.

## 3. Negative controls

Two independent negative controls make the counts meaningful:

1. **Refusal + integrity gating.** In every group a refusal is only counted as
   acceptable if the *post-call integrity check also passed*. A guard byte
   that changed, a stale handle that was dereferenced, or a model mismatch is
   reported as a corruption, not hidden inside the refusal count. This is why
   a group can show a large refusal count and still be a PASS.
2. **Deliberate breaking probes.** Several harnesses include explicit
   known-bad families that are expected to be refused without guard damage —
   e.g. containers' `BREAKING` family (out-of-range / stale / overflow probes),
   alloc's pinned `SIZE_MAX` / count-overflow / injected-realloc-failure /
   double-free ids, and format's forbidden `%n` catalogue. These prove the
   detection path is live rather than vacuous.

Where a group reports a nonzero corruption count (see `RESULTS.md`), that is a
genuine finding: the negative control fired and the aggregate correctly marks
the group failed. It is not suppressed.

## 4. Evidence — NOT proof

**This is EVIDENCE of stability for the tested inputs. It is NOT a proof of
full memory safety, formal correctness, or 100% security.** Passing N
scenarios means only that, for these fixed, reproducible inputs and this
specific build/host, the tested entry points returned the documented result
or refusal and the local guards survived. It cannot establish the absence of
defects outside the generated space — other sizes, interleavings, addresses,
configurations, concurrent schedules, compiler/runtime states — nor does it
verify the compiler, the C runtime, the allocator archives, or third-party
libraries (libcurl, libuv, yyjson) as a whole. Groups additionally carry their
own documented limits (e.g. bounded recent-free caches, generation-based
stale-alias detection, non-sanitized default archives). Treat the numbers in
`RESULTS.md` as regression evidence, not as a safety guarantee.

## 5. Sanitizer limits (why detection is guard-based, not ASan-based)

All nine groups build in a non-sanitized mode
(`--sanitize=none`, or C with no `-fsanitize=*`) and rely on the in-harness
canaries, sentinels, watchdogs and fork containment described in §2. This is
forced by the host toolchain, not a preference:

- **ASan is unsupported here.** The C+ driver's compile-and-run probe rejects
  `address` with `error: sanitizer 'address' is not supported by the selected
  backend (/usr/bin/clang); detected by a compile-and-run probe`. The same
  applies to `leak`, `thread`, `memory` and `cfi`. Independently,
  `tests/crt/asan/run_asan.sh` documents that Apple clang 16 on macOS arm64
  fails at process start (`sanitizer_malloc_mac.inc:189`). No ASan pass is
  claimed anywhere in this campaign.
- **Canonical UBSan is blocked by an external `CPLUS_EXIT_ARITHMETIC`
  blocker.** `./build/c+ build <any>.cp --strict --sanitize=undefined` fails
  while rebuilding the C+ runtime:
  `.../c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared
  identifier 'CPLUS_EXIT_ARITHMETIC'`. This reproduces with untouched tests
  and belongs to the compiler/runtime integration, outside `stdlib/*.hp` and
  outside this campaign. UBSan evidence elsewhere in the repository was only
  obtainable via a post-`--` Clang-flag workaround / `-DCPLUS_EXIT_ARITHMETIC=110`
  shim — not canonical mode — and is therefore not used to qualify these
  results.

Consequently, "zero crashes / hangs / corruptions" in `RESULTS.md` means "no
violation observed by the harness's own detectors", **not** "ASan-clean" or
"UBSan-clean".

## 6. Host and toolchain scope

| Field | Value |
|---|---|
| OS / arch | macOS, `arm64` (Apple silicon), Darwin 27.2.0 |
| Backend compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin27.2.0` |
| C+ driver | `<root>/build/c+`, used in `--strict --sanitize=none` (and `--default --sanitize=none -lcurl` for net_http) |
| Selected C libraries | safecrt archive `build/safe/libcplus_safecrt.a`, hardened libc `build/libc/libcplus_hardened.a` |
| Aggregate bound | `timeout -k 10 900` per group; restricted `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin`; `CPLUS_CLANG=/usr/bin/clang`; `LC_ALL=C` |

**Scope note:** these results were produced on **macOS arm64 with Apple clang
16 only**. They do not transfer to other compilers (GCC, MSVC, other clang
versions), other architectures (x86-64), or other operating systems (Linux,
Windows). A different host may have different sanitizer support, ABI, library
archive contents and runtime behaviour; re-run the campaign there before
making any claim about that target.
