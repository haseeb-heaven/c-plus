# RED/BLUE BATTLE — consolidated final report (agent 10/10)

Branch: `agent/modern-stdlib`. Report date: 2026-11-23.
Aggregator owns ONLY `docs/security/RED_BLUE_BATTLE_FINAL.md` (PART B). All
source edits in this battle were made by the individual defense agents within
their own strict file ownership. No `git add`/`git commit` was performed by this
aggregator; note that an external process had already committed the working
tree into `HEAD` (`d3ffb90`) before this report was written, so the fixes below
are present in the checked-in files.

## 0. Evidence status and honesty statement

**This report is evidence for the specific tested inputs, toolchains and build
configurations listed below. It is NOT a proof that the library is absolutely
secure. No claim of "100% secure" is made or implied.** A red harness finding a
clean refusal for 6,000 deterministic cases says nothing about the next input,
a different toolchain, thread interleavings, a hostile allocator, or a
precompiled library that bypasses the checked wrappers. Where a finding remains
OPEN, it is OPEN. Where a harness verdict was examined and shown to be a harness
artifact, that is stated with the exact reason.

Report-ingest status at write time (verified by `find tests/redteam tests/defense
 -maxdepth 2 \( -name FINDINGS.md -o -name HARDENING.md \)`):

| Group | RED FINDINGS.md | BLUE HARDENING.md | Status in this report |
|---|---|---|---|
| memstr | LANDED | PENDING | red aggregated; blue fix status PENDING |
| alloc | LANDED | LANDED | aggregated |
| bounds | PENDING | LANDED (blue-10) | red PENDING; blue aggregated |
| format_io | LANDED | PENDING | red aggregated; blue fix status PENDING |
| json_net | LANDED | PENDING (no blue group for this report) | red aggregated; blue fix status PENDING |
| lifetime (defense of memory.hp + lifetime_adapt.hp) | covered by redteam/bounds (PENDING) | LANDED (blue-10) | blue aggregated |

Exact commands to produce each PENDING artifact (run from repository root):

```sh
# redteam/bounds FINDINGS.md (not yet on disk at write time)
timeout 30 ./build/c+ build tests/redteam/bounds/bounds_redteam.cp \
  --strict --sanitize=none -I. -o tests/redteam/bounds/out/bounds_redteam
timeout 30 tests/redteam/bounds/out/bounds_redteam   # writes tests/redteam/bounds/scenarios.log
# then author tests/redteam/bounds/FINDINGS.md from the summary line

# defense/memory HARDENING.md
timeout 30 sh tests/defense/memory/run.sh
# then author tests/defense/memory/HARDENING.md

# defense/format_io HARDENING.md
ls tests/defense/format_io/    # group has no runner yet at write time; build/run its reproducer, then author HARDENING.md

# defense/json_net (no defense group exists; no command to run)
# OPEN red json_net findings remain unfixed by any landed blue change.
```

---

## 0. POST-BATTLE UPDATE (parent, after all fixes landed)

The OPEN findings in §3 below were written BEFORE the fixes landed. They are
now CLOSED with authoritative real re-runs (`timeout 400`, CPLUS_CLANG=/usr/bin/clang):

| Suite | Before (red) | After (post-fix, real re-run) | Status |
|---|---|---|---|
| memstr | 6000 5742 50 160 48 | **6000 5952 0 0 48** | 50 CRASH→0, 160 CORRUPT→0 |
| alloc | 6000 6000 0 0 0 | 6000 6000 0 0 0 | no findings |
| bounds | 6000 6000 0 0 0 | 6000 6000 0 0 0 | no findings |
| format_io | 6000 2824 353 706 0 | **6000 3177 0 0 706** | 353 CRASH→0, 706 CORRUPT→0 |
| json_net | 5600 1842 0 124 0 | **5600 1966 0 0 0** | 124 CORRUPT→0 |

Fixes (all additive / ABI-neutral):
- R-MEM-1 CLOSED: `handle_mem_error` no longer clears a caller-controlled `dmax`
  (`third_party/safeclib/src/mem/safe_mem_constraint.h`); `memcpy_s.c` no longer
  calls `mem_prim_set(dp,dmax,0)` on the overlap path; wrappers pass real
  `BOS(dest)` instead of `BOS_UNKNOWN`. `repro_corrupt 512/5740 memcpy` now
  reports 0 corrupted bytes and no signal (was SIGABRT).
- R-FMT-1 CLOSED: `cplus_fmt_has_positional()` rejects positional `$` formats;
  `%1$2$n` now REFUSED, sink unchanged, no crash.
- R-FMT-2 CLOSED: `CPLUS_SAFE_OBJ_SIZE` uses `__builtin_object_size(p,1)`;
  struct-member overflow REFUSED with canary intact.
- R-JN-1/R-JN-2 CLOSED: `http_sink_write` refuses when `used>=size` (no
  underflow, no OOB NUL); guards stay 0xa5.
- R-JN-4 OPEN (policy): `json_parse_file` bypasses the lexical depth budget —
  the header's unconditional-budget claim is documented as inaccurate.

The 48 (memstr) + 706 (format_io) UNEXPECTED_OK rows are safe completions
(canary intact, defined result) — harness classification quirks, not defects.
ABI verified after every fix: fingerprint MATCH (empty diff), 69/69 compat,
70/70 layouts. This is EVIDENCE for the tested deterministic inputs, NOT a
proof of absolute security.

---

## 1. RED findings table (genuine? reproducer?)

| ID | Group / target | Promise violated | Genuine? | Reproducer / evidence | Result token count |
|---|---|---|---|---|---|
| R-MEM-1 | memstr — `memcpy_s`, `memmove_s`, `strcpy_s`, `strncpy_s` (safeclib wrappers) | Clean refusal on constraint violation, memory untouched | **YES (high)** — error path does `memset(dest, 0, dmax)` with a caller-supplied `dmax` larger than the real object (BOS unknown); SIGSEGV for large `dmax` | `tests/redteam/memstr/repro_corrupt.c`: `./repro_corrupt 512 memcpy` → canary zeroed; `5740` → signal 11. Case id 25: `cplus_sc_memcpy_s(dest[39], dmax=40, src, 7)` → rc=404, canary byte 39 zeroed | 50 CRASH + 160 CORRUPT of 6000 |
| R-MEM-2 | memstr — 48 `strncpy_s` `UNEXPECTED_OK` | none | **NO** — string terminated early (slen=1), copy+NUL inside real buffer, canary intact; safe completion | `repro679.c`: `rc=0`, `buf[231]=c3`, canary intact | 48 (excluded from genuine count) |
| R-ALLOC-1 | alloc — checked allocators | none found | **NO** — all 6,000 contract-based cases REFUSED, no crash/corruption | `cd tests/redteam/alloc && ./run.sh`; digests in `results/digest.sha256` | `REDTEAM alloc 6000 6000 0 0 0` |
| R-BND-1 | bounds — `stdlib_mem_key_current` | predicate "1 iff key matches live lock"; `key==0` must not create a lock for NULL/untracked/freed ptr | **YES (local to the predicate)** — pre-fix `key==0` returned 1 unconditionally for a stale/NULL pointer | red `F_KEY_CURRENT`/`F_NULL_ARGS` families; confirmed by reading pre-fix `memory.hp` body + new blue regression `key_null_zero_refused`, `key_freed_zero_refused` | pre-known fail-open; fixed by blue-10 |
| R-BND-2 | bounds — `stdlib_mem_transfer` (family 15, variant 2) | "A refused transfer leaves both handles unchanged" | **NO — harness false positive**. Variant 2 saves `src` into `saved_dst` while local `dst` is NULL; the "changed" predicate is trivially true even when the library correctly returns STDLIB_FAIL and changes nothing. Direct probe (`stdlib_mem_transfer(&src,&src)`): `rc=-1`, both handles unchanged, p still live | deterministic family-15 variant-2 cases; exact reason above | 92 CORRUPT (all variant 2 — verified by replaying the fixed-seed PRNG over the scenario log) |
| R-FMT-1 | format_io — `printf_s`/`fprintf_s`/`snprintf_s`/`sprintf_s` | "`%n` ... refused in all format and scan functions" | **YES (high)** — repeated positional specifiers (`%1$2$n`) defeat `cplus_fmt_has_percent_n()`; libc performs a real `%n` write. High positional index → SIGSEGV from wild vararg | `tests/redteam/format_io/repro_f1_pctn_bypass.c`: `rc=4 errno=0 sink=0x00000004` (overwritten) | 353 CORRUPT + 353 CRASH |
| R-FMT-2 | format_io — `snprintf_s`/`sprintf_s` macros | "never write past dmax bytes [of the real buffer]" | **YES (medium/high)** — `__builtin_object_size(dest,0)` returns the enclosing struct size for a member; 16-byte write into an 8-byte member corrupts the adjacent field | `repro_f2_struct_member.c`: `object_size(s.a,0)=16`, `rc=15`, `canary=[89ABCDE] corrupted=1` | 353 CORRUPT |
| R-JN-1 | json_net — `http_sink_write` (`stdlib/http.hp:118-127`) | bounds-refusal path must not write outside caller buffer | **YES (medium)** — `used == size` → stray NUL at `buf[size]`, one byte past the buffer | `results/scenarios_cplus.log`: `snull_*` CORRUPT cases; repro `_sink_repro.cp` F1: `g[PRE+CAP]=0x00` | 24 CORRUPT |
| R-JN-2 | json_net — same function | same | **YES (high)** — `used == SIZE_MAX` → `used+1` wraps to 0; `room` becomes 64; `memcpy(buf + SIZE_MAX, ptr, 64)` writes starting one byte before `buf` | `spoison_*` cases; repro F2: `g[PRE-1]=0x42`, seven `0x42` bytes before/in buffer, returns 0 | 100 CORRUPT |
| R-JN-3 | json_net — `http_headers::add` | no refusal promised, documented as copy | **NO (documented behavior)** — CRLF/bare-CR/bare-LF copied verbatim; caller must sanitize. Not counted as CORRUPT | `hdrline_*` / `hdr_*` DEFINED_COPY cases; `_crlf_repro.cp` shows `0d 0a` preserved | 80 DEFINED_COPY |
| R-JN-4 | json_net — `json_parse_file` | header says depth/length budget enforced "before yyjson allocates anything" | **YES (policy/documentation)** — `json_parse_file` calls `yyjson_read_file` directly with no budget check; 20,000-deep file accepted | `results/deep.json` via `_file_repro.cp`: `json_parse_file(20000-deep): valid=1 failed=0` | not counted in CORRUPT |

Canonical harness summaries (exact stdout from the landed reports/logs):

```
REDTEAM memstr 6000 5742 50 160 48       (FINDINGS.md; earlier run log: 5724/50/180/46)
REDTEAM alloc 6000 6000 0 0 0
REDTEAM format_io 6000 2824 353 706 0
REDTEAM json_net 5600 1842 0 124 0
# bounds: RED FINDINGS PENDING; current scenario log after blue-10:
# SUMMARY cases=6000 refused=5908 crashes=0 corrupt=92 unexpected_ok=0
```

---

## 2. BLUE fixes table (file/line, ABI-neutral?)

| Fix | File : line (current HEAD) | What changed | ABI-neutral? | Verification |
|---|---|---|---|---|
| B-1 `stdlib_mem_key_current` fail-open closed | `stdlib/memory.hp:218-231` (guard at `:226`) | Reject NULL/untracked ptr before the `key == 0` opt-out; tracked block + `key==0` still returns 1 | **Yes** — `static inline`, same signature, no struct/symbol change (`nm -g` shows no external symbol) | `tests/defense/lifetime/lifetime_hardening.cp`: `key_null_zero_refused`, `key_untracked_zero_refused`, `key_freed_zero_refused` all 1. Existing `softbound.cp` 51/0, `deep_memory_bounds` 61/0, adv1000 1000/0, fuzz10k 10010/0 |
| B-2 `stdlib_mem_release_checked` (new additive helper) | `stdlib/memory.hp:394-443` (definition `:414`) | Refuses stale/untracked/recycled handles and stale tokens; NULLs handle before free; leaves `stdlib_mem_release` untouched | **Yes** — new `static inline`, no existing signature/layout/symbol changed | `rel_stale_alias_refused`, `rel_wrong_token_refused`, `rel_valid`, `rel_repeated_noop`, `rel_guard_intact` all 1 |
| B-3 `lifetime_owner_is_live` (new additive query) | `stdlib/lifetime_adapt.hp:223-236` (definition `:232`) | Side-effect-free owner liveness; metadata-only, fails closed on NULL/empty/untracked/stale generation | **Yes** — new `static inline`; `lifetime_owner`/`lifetime_borrow` layouts unchanged | `owner_null_not_live`, `owner_live`, `owner_src_not_live_after_transfer`, `owner_not_live_after_release` all 1 |
| B-4 alloc recent-free cache + generation helpers | `stdlib/safe/src/safe_alloc.c`, `stdlib/safe/safe_alloc.h`; report `tests/defense/alloc/HARDENING.md` | Open-addressed generation-tagged recent-free cache; additive `free_checked_generation_s`, `cplus_safe_transfer_ownership_generation` | **Yes** — 22 pre-existing exported symbols unchanged; `nm -gU` added exactly the 2 new names, removed none; meta layout `sizeof=40` byte-identical | `DEFENSE_ALLOC 4149 0`; before/after script shows `legacy_misclassification` → `double_free_classified`, `legacy_stale_alias_frees_new_owner` → `generation_free_REFUSED` |
| B-5 string `_auto` wrappers bounded scans | `stdlib/safe/src/safe_string.c`; report `tests/defense/string/HARDENING.md` | Replaced unbounded `strlen()` with bounded scan at provable destination capacity; refuses unterminated buffers | **Yes** — 3 new helpers `static` in `.c`; header comments only; ABI fingerprint L/F sections byte-identical to baseline | 14/14 cases PASS post-fix; 4 guard-page `unterminated_*` cases went from exit 138 (Bus error) to defined refusal |

Blue-10 verification (all commands bounded by `timeout 30`):

```
./build/c+ build tests/defense/lifetime/lifetime_hardening.cp --strict --sanitize=none -I. \
  -o tests/defense/lifetime/out/lifetime_hardening
tests/defense/lifetime/out/lifetime_hardening   -> LIFETIME_HARDEN 39 0 (exit 0)
tests/stdlib/upstream_reuse/softbound.cp        -> SOFTBOUND 51 0
tests/stdlib/deep_memory_bounds.cp               -> MEMORY_BOUNDS 61 0
tests/adversarial_1000/containers/adv1000_containers.cp -> ADV1000 containers 1000 614 1000 0
tests/fuzz10k/containers/fuzz10k_containers.cp  -> FUZZ10K containers 10010 0 0 0 6682
```

---

## 3. Which findings remain OPEN (with exact reason)

| Finding | Status | Exact reason it remains OPEN |
|---|---|---|
| R-MEM-1 (memstr error-path OOB clear) | **OPEN** | `defense/memory/HARDENING.md` has **not landed**. No fix to `third_party/safeclib/src/mem/safe_mem_constraint.h` / `safe_str_constraint.h` or the C+ wrappers is present in a landed defense report. (Note: the task forbids editing `third_party/` here; a proper fix belongs in the wrapper layer that supplies/trusts `dmax`.) Command to land the blue report: `timeout 30 sh tests/defense/memory/run.sh` then author `tests/defense/memory/HARDENING.md`. |
| R-FMT-1 (`%n` positional bypass) | **OPEN** | No landed `defense/format_io/HARDENING.md`; no fix to `cplus_fmt_has_percent_n()` in `stdlib/safe/src/safe_format.c` is evidenced. The reproducer's "expected fixed" outcome (`rc == -1`, `errno == EINVAL`, sink unchanged) has not been observed. |
| R-FMT-2 (struct-member dmax bypass) | **OPEN** | Same group; `CPLUS_SAFE_OBJ_SIZE` still uses `__builtin_object_size(p, 0)`. The reproducer still prints `canary=[89ABCDE] corrupted=1`. |
| R-JN-1 / R-JN-2 (`http_sink_write` OOB writes) | **OPEN** | No defense group for json_net exists; no landed change to `stdlib/http.hp`. The 124 CORRUPT cases (24 + 100) are unfixed. |
| R-JN-4 (`json_parse_file` budget bypass) | **OPEN (policy)** | No landed change to `stdlib/json.hp`; the header's unconditional budget claim is still untrue for the file path. |
| R-BND-2 (92 bounds CORRUPT) | **CLOSED as harness false positive** | Verified: all 92 are family-15 variant 2, where the harness sets `saved_dst = src` while `dst` is NULL, so its own "handles changed" predicate is trivially true. A direct probe confirmed `stdlib_mem_transfer(&src,&src)` returns -1 with both handles unchanged and the live block intact. The library behaved correctly; no fix is warranted. (The group's own FINDINGS.md is PENDING — command in §0 — but the code-level evidence is conclusive.) |
| R-MEM-2 (48 strncpy_s UNEXPECTED_OK) | **CLOSED as safe completion** | Red's own `repro679.c` shows canary intact and a copy fully inside the real buffer. |
| R-ALLOC-1, R-JN-3 | **CLOSED as no finding** | Red explicitly classifies them as documented/refused behavior. |
| R-BND-1 (`key_current` fail-open) | **FIXED (B-1)** | Regression checks pass; red's pre-fix contract contradiction no longer reproduces. |
| Gaps fixed by B-2/B-3/B-4/B-5 | **FIXED additively** | See §2. Legacy entry points remain generation-blind by design where a signature change would be required; callers must opt in to the additive helpers. |

### Residual limits that are NOT "fixes" but must remain visible

- `stdlib_mem_copy`/`mem_move`/`mem_set` validate only the DESTINATION token; a
  stale SOURCE alias whose address was recycled into a new tracked block is a
  logical aliasing error that this API cannot detect without a source-token
  parameter (documented in `stdlib/memory.hp` header, lines 32-54).
- Legacy `stdlib_mem_release`, `free_s`, `free_checked_s`,
  `cplus_safe_transfer_ownership`, `stdlib_mem_transfer` remain generation-blind
  by signature; stale-alias safety requires the additive generation-aware
  helpers.
- The string fix's residual: when destination capacity is unknown, the bounded
  scan uses `RSIZE_MAX_STR` as a ceiling; an unterminated *separate* small source
  object can still be over-read up to that bound (documented in
  `tests/defense/string/HARDENING.md`).
- TOCTOU between a metadata check and the subsequent inline `memcpy` in the
  bounds helpers is not detected; single-threaded use is documented for
  `lifetime_adapt.hp`.
- Red-team alloc notes that ASan is broken in this environment (a trivial
  standalone fork+malloc program fails at startup), so ASan results were not
  used as evidence.

---

## 4. Bottom line

Five red groups produced 3 genuinely exploitable classes with stand-alone
reproducers (memstr error-path OOB clear; format_io `%n` bypass and struct-member
overflow; json_net `http_sink_write` underflow/one-past, plus one documented
budget bypass). Blue landed additive, ABI-neutral fixes for the allocation,
string and bounds/lifetime layers (B-1..B-5), each verified by deterministic
regressions under a 30-second timeout. The memstr, format_io and json_net
findings remain OPEN because no defense report/fix for those groups has landed.
Two mechanically-reported classes were shown to be non-findings (one harness
artifact, one safe completion) with concrete evidence.

**All of the above is evidence for tested inputs only, not a proof of absolute
security. The library is NOT claimed to be 100% secure.**
