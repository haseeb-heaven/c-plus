# Checked memory APIs — hardening report (BLUE-TEAM agent 6)

Scope: `stdlib/safe/safe_memory.h`, `stdlib/safe/src/safe_memory.c`,
`tests/defense/memory/`. No other file was edited (no build scripts, compiler,
runtime, third_party, or red-team files).

Build used for every result below:

```sh
CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh
sh tests/defense/memory/run.sh        # timeout 30 on the binary
```

Reference: `stdlib/safe/src/safe_memory.c` line numbers are from the
**pre-hardening snapshot** captured at the start of this session (sha256
`d6d2c37c…`), unless marked "current". The hardening was committed upstream as
`1b656ff`; line numbers marked "current" refer to that committed version.

---

## Gap 1 — CONFIRMED, FIXED: source over-read in `memcpy_s_auto` / `memmove_s_auto`

**Reference (HEAD):** `safe_memory.c:138-165` (`cplus_memcpy_s_auto`) and
`:167-194` (`cplus_memmove_s_auto`).

Both wrappers proved the **destination** capacity (`:154`, `:183`) but passed
the source straight to `cplus_memcpy_s(dest, dmax, src, n)` / `cplus_memmove_s`
with `smax = n`. Meanwhile `cplus_memcmp_s_auto` (`:240-241`) checks *both*
operands. A tracked heap source with `n` larger than its remaining capacity was
therefore accepted and read past the allocation. Analogous to the member
`memcmp` path, which is already bounded.

**Fix (current):** additive file-static helper
`cplus_safe_check_src_capacity()` (`:255`) and one call in each wrapper
(`:291`, `:321`). It refuses only when `cplus_safe_remaining_capacity(src)`
proves `n > capacity`: `errno = ERANGE`, no write, `cplus_runtime_violation`.
If capacity is unknown it keeps the pre-existing policy untouched. No
signature, macro, exported symbol or struct layout changed; valid source
lengths are unaffected. This also makes the wrapper family internally
consistent with `cplus_memcmp_s_auto`.

**Evidence:** `tests/defense/memory/test_memory_hardening.c` T1/T2/T3.
Before: 9 failures, e.g. `r=0x… errno=0` and 8 `'A'` bytes copied from a
source with 8 proven bytes when 32 were requested. After: `r == NULL`,
`errno == ERANGE`, 64-byte destination canary intact.

---

## Gap 2 — CONFIRMED, FIXED (within provable-capacity scope): safeclib's
## refusal path clears `dmax` bytes past the real destination

**Reference (HEAD):** `safe_memory.c:35-81` — all five direct `_s` adapters
forward the caller's `dmax` verbatim to
`cplus_sc_memcpy_s/memmove_s/memset_s/memcmp_s/memzero_s` with BOS_UNKNOWN.

Upstream `third_party/safeclib/src/mem/safe_mem_constraint.h:handle_mem_error()`
does `memset(dest, 0, dmax)` before returning the refusal code; `memcpy_s.c`'s
overlap branch does `mem_prim_set(dp, dmax, 0)`. With `dmax > real capacity` the
refusal itself corrupts memory. Parent confirmed: `repro_corrupt 512 memcpy`
→ `rc=400`, first corrupted byte offset 0, 512 bytes destroyed; `5740` → signal
134/139. Red-team baseline: `REDTEAM memstr 6000 5742 50 160 48`.

**Fix (current):** additive file-static helper
`cplus_safe_refuse_unproven_dmax()` (`:57`) plus a call at the top of each of
the five adapters (`:110`, `:124`, `:138`, `:154`, `:170`). It takes the minimum
of tracked allocation capacity (`cplus_safe_remaining_capacity`, which handles
interior pointers) and `__builtin_object_size(dest, 1)` (`CPLUS_SAFE_OBJ_SIZE`).
If `dmax` exceeds that proven capacity it returns `ESNOSPC` (406), sets
`errno = ERANGE`, records telemetry, emits a `cplus_runtime_violation`, and
**returns before safeclib is entered**, so no zeroing happens. Unknown capacity
keeps the old behaviour (documented below). Valid inputs (`dmax <= proven`)
still delegate to safeclib and keep the exact upstream return code. In
`cplus_memcmp_s` the out-parameter `*diff` is set to `-1` on this early refusal.

**Evidence:** T10 in `test_memory_hardening.c`. Before: `cplus_memcpy_s(buf[64],
512, NULL, 3)` returned 400 and zeroed all 64 bytes plus 448 bytes of adjacent
stack. After: returns 406, `errno == ERANGE`, all 64 canary bytes intact; same
for memmove and for an overlap violation; a valid `dmax = 64` copy still works.

**ABI:** `sh tools/abi_compare/run.sh compare
tests/abi/fingerprints/baseline.txt` →
`ABI fingerprint MATCHES baseline (1238 records); diff is empty`. Symbols,
signatures, offsets, struct layouts unchanged; only file-static helpers added.

---

## Gap 3 — CONFIRMED, FIXED: errno domain left ambiguous on direct `_s` refusals

**Reference (HEAD):** `safe_memory.c:35-81` logs `constraint/error` but never
sets `errno` on safeclib constraint failures. Safeclib codes are positive
(400/401/403/404/406); callers testing `errno` (a common C habit) saw stale or
zero values and could mistake a refusal for success — relevant to the
`memcmp_s` `-1` collision (a genuine "less than" also returns `-1`).

**Fix (current):** file-static `cplus_safe_publish_constraint_errno()` (`:83`)
maps safeclib codes onto standard errno on **error paths only**:
400/401/404/410 → `EINVAL`; 403/406 → `ERANGE`; platform `EOVERFLOW` → itself;
default → `EINVAL`. Return codes are still the safeclib codes verbatim, so the
existing ABI/contract is intact. Success paths do not touch `errno`.

**Evidence:** T8 plus T1/T2/T10 assert `errno != 0` on refusal and `errno == 0`
on a genuine `diff == -1` comparison.

---

## Gap 4 — CONFIRMED, FIXED: data race on the process-wide capacity-mode flag

**Reference (HEAD):** `safe_memory.c:86-92`: `static int
g_unknown_capacity_strict` read and written without atomics.

**Fix (current):** `__atomic_store_n`/`__atomic_load_n` with release/acquire
(`:192`, `:196`). No signature or semantic change; single-threaded behaviour
identical.

---

## Honest limitation — NOT fixed from this owned file set (escalated to parent)

The requested end-state ("`repro_corrupt 512 memcpy` shows canary INTACT and
red-team crashes/corruptions drop to 0") is **not achievable** by editing only
the two files I own. Evidence:

1. `tests/redteam/memstr/repro_corrupt.c` declares and calls
   `cplus_sc_memcpy_s(...)` **directly**. `nm -gA build/safe/libcplus_safecrt.a`
   shows that symbol is defined in `sc_cplus_sc_wrappers.o`, i.e. compiled from
   the parent-owned third-party file
   `third_party/safeclib/src/cplus_sc_wrappers.c`. My archive object
   `w_safe_memory.o` only *references* it. After my fix:
   `repro_corrupt 512 memcpy` → `rc=400`, 512 corrupted bytes (unchanged);
   `repro_corrupt 512 memmove` → `rc=400`, 512 corrupted bytes (unchanged);
   `repro_corrupt 5740 memmove` → signal 134 (unchanged).

2. The red-team harness calls both families (`fam=0` raw `cplus_sc_*`,
   `fam=1` `cplus_*_s`) with a **stack** buffer `buf[320]` and a lying `dmax`.
   Rerun after rebuild: `REDTEAM memstr 6000 5742 50 160 48` — byte-identical
   to baseline (92 findings in fam=0, 118 in fam=1). Inside a non-inlined
   callee `__builtin_object_size(dest, 1)` is `(size_t)-1`, and a stack buffer
   has no `malloc_s` tracking, so the library cannot prove capacity. Fixing
   this requires the owner of `third_party/safeclib/src/cplus_sc_wrappers.c`
   (or the build script / harness) to pass a real BOS to the `_chk` functions
   or to bound the error-path clear. I did not edit those files.

3. `tests/scenarios/runtime_03_memcpy.c` scenario 3
   (`sc_03_src_one_short_gap`, around line 94) **explicitly codifies Gap 1 as
   expected behaviour** ("the auto wrapper does NOT check source capacity …
   the copy is accepted"). After the fix it reports `1 of 25 scenarios failed`.
   That test is outside my ownership; the owning agent must update it to expect
   `errno == ERANGE` + destination unchanged. I did not edit it.

**Suggested parent-owned follow-up (not applied here):** change
`third_party/safeclib/src/cplus_sc_wrappers.c` to call
`cplus_sc__memcpy_s_chk(dest, dmax, src, smax, actual_bos, BOS_UNKNOWN)` with a
caller-supplied BOS (requires a signature/ABI change) or compile safeclib with
a clamped `handle_mem_error`; and update `repro_corrupt.c` / harness to use the
public `cplus_*_s` API with a provable destination if the verdict is meant to
measure this layer.

---

## Verification summary (all under `timeout 30` unless noted)

| Check | Before | After |
|---|---|---|
| `sh tests/defense/memory/run.sh` | `FAIL (10 failures)` | `PASS (0 failures)` |
| T1/T2/T3 (source over-read) | accepted, canary overwritten | `NULL`/`ERANGE`, canary intact |
| T10 (dmax > proven cap, error path) | 400 + 64 canary bytes zeroed | 406 + `ERANGE`, 0 bytes written |
| 85 `tests/crt/unit/test_*.c` (patched archive) | 85 passed | 85 passed |
| `tests/methods/hundred_memory.c` | 100/100 | 100/100 |
| `runtime_02_memset`, `runtime_04_memmove_memcmp`, `safec_layer_memory` fuzz | pass | pass |
| `tools/abi_compare/run.sh compare baseline.txt` | MATCH | MATCH (1238 records, empty diff) |
| `repro_corrupt 512 memcpy` (direct raw API) | 512 corrupted | **512 corrupted (unfixed — see limitation)** |
| `tests/redteam/memstr/run.sh` | `6000 5742 50 160 48` | **`6000 5742 50 160 48` (unchanged — see limitation)** |

`safety boundary`: this remains runtime constraint checking, not pointer
provenance/lifetime proof. No Rust-grade memory-safety claim is made.
