# Screenshot manifest — captured runtime refusals of the checked stdlib

**These files are captured terminal output from real runs of real binaries.** They are
evidence of **runtime refusal under the tested inputs only**; they are **NOT a proof of
full memory safety, lifetime safety, or thread safety**. The checked layers do not
establish pointer provenance, full ownership, or alias/UAF freedom, and an
uninstrumented caller can bypass them. The transcripts are stored in `examples/`
(`examples/screenshots/captures/`), **not in any README**, per request.

## Reproduction

From the repository root:

```sh
./examples/screenshots/capture.sh
```

The harness rebuilds each existing demo with its documented flags (no source edits),
runs each binary under `/usr/bin/timeout 30`, and writes verbatim stdout+stderr to
`examples/screenshots/captures/*.txt`. All build outputs and temporary files are
confined to `examples/screenshots/captures/tmp/` (also used as `TMPDIR`); no
checked-in demo binary, test output directory, or source file is modified.

Last full run: **2026-11-23T06:28:17Z** — exit `0`,
`CAPTURE_SUMMARY files=6 builds=6 runs=7 failures=0`. Every transcript ended in
`VERDICT: PASS` (all expected tokens found in the captured output).

## Environment

- Captured at (UTC): `2026-11-23T06:28:17Z`
- Toolchain (recorded in every transcript): `C+ 0.1.0 ; Apple clang version 16.0.0 (clang-1600.0.26.6) ; arm64-Darwin`
- Working directory: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
- Timeout wrapper: `/usr/bin/timeout 30`
- Capture method: stdout and stderr of the real binary redirected verbatim into the transcript, between `# --- BEGIN VERBATIM OUTPUT ---` and `# --- END VERBATIM OUTPUT ---` markers.
- Archived static libraries used by the relevant builds: `build/safe/libcplus_safecrt.a`, `build/libc/libcplus_hardened.a`.

**Path shorthand:** below, `$REPO` = `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
and `$CAP` = `$REPO/examples/screenshots/captures`. The commands shown are the exact
commands executed; only these two path prefixes are abbreviated. Each transcript
also carries its own fully expanded `# BUILD CMD:` / `# RUN CMD:` lines.

---

## 1. `captures/unsafe_vs_safe_memcpy.txt`

- **Build command:**
  `./build/c+ build examples/expanded/safety/memcpy_s_demo.cp --strict --sanitize=none -I. -o "$CAP/tmp/memcpy_s_demo" -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a`
- **Run command:** `/usr/bin/timeout 30 $CAP/tmp/memcpy_s_demo`
- **Key captured refusal lines:**
  - `memcpy_s ordinary_one_over OK canary=0x2a corrupted=1 (unguarded subprocess)`
  - `IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max`
  - `memcpy_s checked_one_over REFUSED rc=406 canary=0xa5 intact=1`
- **Safety property demonstrated:** buffer-overflow refusal. The unguarded 9-byte
  copy into an 8-byte field in a forked child corrupts its canary (`0xa5`→`0x2a`,
  child exit 9); the same one-over through `cplus_memcpy_s` is refused with safeclib
  `ESNOSPC = 406` and the parent canary stays `0xa5`. An exact-fit copy still succeeds
  (positive control).

## 2. `captures/bounds_query.txt`

- **Build command:**
  `./build/c+ build examples/expanded/safety/bounds_demo.cp --strict --sanitize=none -I. -o "$CAP/tmp/bounds_demo"`
- **Run command:** `/usr/bin/timeout 30 $CAP/tmp/bounds_demo`
- **Key captured refusal lines:**
  - `bounds one_over REFUSED span_1over=0 span_tail=0 query=-1 bounds_cat=1`
  - `bounds stale_generation REFUSED moved=1 old_key_current=0 old_query=-1 lifecycle_cat=1 fresh_query=1`
- **Safety property demonstrated:** bounds-query refusal. A one-over span fails
  `stdlib_mem_span_ok` and `stdlib_mem_query` returns `-1` (`STDLIB_FAIL`) with
  `STDLIB_CATEGORY_BOUNDS`. After `stdlib_mem_transfer` bumps the generation,
  the old key is no longer current and is refused with `STDLIB_CATEGORY_LIFECYCLE`,
  while a fresh key still succeeds.

## 3. `captures/lifetime_refusal.txt`

- **Build commands:**
  `./build/c+ build examples/expanded/safety/lifetime_demo.cp --strict --sanitize=none -I. -o "$CAP/tmp/lifetime_demo"`
  (plus the already-built `bounds_demo`, rebuilt with the command in §2)
- **Run commands:**
  `/usr/bin/timeout 30 $CAP/tmp/lifetime_demo`
  `/usr/bin/timeout 30 $CAP/tmp/bounds_demo` (re-run to capture the lifecycle-category refusal line)
- **Key captured refusal lines:**
  - `lifetime stale_token_refused REFUSED released=1 retired=1 not_live=1 read=-1 cat=1 untouched=1 write=-1 wcat=1`
  - `lifetime double_release_refused REFUSED init=1 first=1 second=-1 double_free_cat=1 live_stable=1`
  - `bounds stale_generation REFUSED moved=1 old_key_current=0 old_query=-1 lifecycle_cat=1 fresh_query=1`
- **Safety property demonstrated:** lifetime refusal. A stale borrow token is
  refused from metadata only (`CPLUS_EXIT_USE_AFTER_FREE`) and the destination
  sentinel is untouched; a second `lifetime_owner_release` is refused with
  `CPLUS_EXIT_DOUBLE_FREE` while the live-block count stays stable. The re-run of
  `bounds_demo` supplies the explicit `lifecycle_cat=1` (`REFUSED ... lifecycle`) line.

## 4. `captures/sync_double_destroy.txt`

- **Build command:**
  `/usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -I. -Ilibc/install-include examples/expanded/safety/sync_demo.c build/libc/libcplus_hardened.a -pthread -o "$CAP/tmp/sync_demo"`
- **Run command:** `/usr/bin/timeout 30 $CAP/tmp/sync_demo`
- **Key captured refusal lines:**
  - `sync mtx_destroy REFUSED init_destroy=0 double=22 (EINVAL=22)`
  - `sync cnd_destroy REFUSED init_destroy=0 double=22 (EINVAL=22)`
  - `sync thrd_join REFUSED join=0 result=42 double=22 (EINVAL=22)`
  - `sync null_handles REFUSED mtx_NULL=22 cnd_NULL=22 thrd_zero=22 (EINVAL=22)`
- **Safety property demonstrated:** synchronization-handle refusal. A second
  destroy/join of a mutex, condition variable, or thread handle returns `EINVAL`
  (22) without re-entering pthread; NULL mutex/condition handles and an all-zero
  thread handle are refused with `EINVAL` without dereference.

## 5. `captures/database_safety_refusals.txt`

- **Build command:**
  `./build/c+ build examples/expanded/database/safety_demo.cp --strict --sanitize=none -I. -o "$CAP/tmp/database_safety_demo"`
- **Run command:** `/usr/bin/timeout 30 $CAP/tmp/database_safety_demo`
- **Key captured refusal lines:**
  `SAFETY oversized_key REFUSED 101`, `SAFETY null_value REFUSED 108`,
  `SAFETY overflowing_sum REFUSED 101`, `SAFETY duplicate_key REFUSED 108`,
  `SAFETY delete_missing REFUSED 108`, ending in `SAFETY_SUMMARY 5 5 5 0`.
- **Safety property demonstrated:** application-level input validation with an
  intact canary (`SAFETY FINAL_CANARY ok`) and unchanged store
  (`SAFETY FINAL_STORE 1`) after five refused hostile operations.

## 6. `captures/stdc_hostile_refusals.txt`

- **Build command:**
  `/usr/bin/clang -O0 -fno-builtin -std=c99 -Wall -Wextra -Wno-format-nonliteral -Wno-fortify-source -Wno-builtin-memcpy-chk-size -I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include -include stdlib/safe/safe_c.h tests/abi/stdc_safety/hostile.c build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread -o "$CAP/tmp/hostile_checked"`
- **Run command:**
  `/usr/bin/timeout 30 $CAP/tmp/hostile_checked $CAP/tmp/hostile-out`
- **Key captured refusal lines:**
  - `[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 17 > capacity 16, ESNOSPC 406)`
  - `stdc_safety memcpy null REFUSED 1`
  - `stdc_safety strcpy huge REFUSED 1`
  - `stdc_safety free null REFUSED 1`
  - `STDC_SAFETY 64 48 64 0` (64 cases, 48 refusals, 64 intact canaries, 0 failures)
- **Safety property demonstrated:** hostile ordinary-C inputs (null, small, huge)
  across 16 APIs are refused by the force-included checked layer, with all 64
  canaries intact. This is the source of the literal `[CPLUS-SAFE] ... ESNOSPC 406`
  refusal line cited in the task request.

---

## Verification performed

- `sh -n examples/screenshots/capture.sh` — syntax OK.
- `./examples/screenshots/capture.sh` — exit `0`; 6 builds OK, 7 binary runs, all
  exit `0` under `timeout 30`; all 6 per-file token checks PASS.
- Each transcript was inspected: the refusal lines under the BEGIN/END markers
  match the literal program output observed from direct runs of the demos.
- `examples/screenshots/captures/.gitignore` ignores `tmp/` so intermediate
  binaries/logs are not committed; the `*.txt` transcripts are the evidence files.

No transcript was fabricated: every refusal line shown was emitted by a freshly
built binary during the harness run recorded above.
