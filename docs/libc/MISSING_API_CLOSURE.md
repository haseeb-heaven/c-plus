# Aggregate audit — missing-API closure and runtime hardening

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Audited HEAD:** `e55c425` (integration commit `2a2e13f` — "Close 27 missing ISO C APIs and add runtime-safety wrappers")
**Ownership of this file:** this document only. No source, header, contract,
build, CMake, ABI-baseline, `runtime/**` or `compiler/**` file was created,
modified, staged or committed by this audit. All resolution claims below were
re-verified by **executing** the sibling tests from the repository root; none
rests on a sibling document's prose alone.

---

## 0. Headline counts

| Metric | Count |
|---|---:|
| APIs classified `MISSING` in authoritative `docs/libc/iso_c_inventory.json` | **27** |
| **RESOLVED** (changed source file + executed test, exact result captured) | **26** |
| **BLOCKED-BY-HOST** (exact probe evidence, no fabricated value) | **1** (`FE_TIESAWAY`) |
| **Still MISSING / PENDING** | **0** |
| APIs with the literal bare ISO spelling | 6 (`nextup`/`nextdown` family) |
| APIs resolved via namespaced `cplus_*` / `cplus_c23_*` entry points | 20 |
| Runtime-hardening additive entry points | 3 groups / **10 new symbols** |
| New .c source files added by the integration | 7 |
| Test suites independently re-executed by this audit | 8 (8/8 pass) |
| ABI fingerprint compare | **MATCH** (exit 0) — see §5 |

**Bottom line:** the 27 `MISSING` entries are closed as *capabilities*: 26 have
real implementations with passing tests, and the single genuinely
host-blocked entry (`FE_TIESAWAY`) has been converted from a silent gap into a
testable `UNSUPPORTED-BY-HOST` refusal. **No "100% ISO C complete" claim is
made** — the 49 deliberate `UNSUPPORTED` entries are unchanged.

---

## 1. Authoritative source of the 27

`docs/libc/iso_c_inventory.json` — 948 APIs, classification legend:
`MISSING` = "No implementation in this repo and no host/namespaced wrapper
entry point." Exact enumeration (extracted from the per-API `apis[]` records,
**not** from the inconsistent `by_header` metadata):

```sh
python3 -c "
import json
d=json.load(open('docs/libc/iso_c_inventory.json'))
m=[a for h in d['headers'] for a in h['apis'] if a['classification']=='MISSING']
print(len(m))
for a in m: print(a['header'], a['api'])
"
# 27
```

**Prose discrepancy, recorded honestly:** `docs/libc/COVERAGE_100_AUDIT.md`
(reference prose) says "28 APIs are MISSING" and lists `memset_explicit`
among them. That prose is **stale**: `memset_explicit` is already classified
`IMPLEMENTED_UPSTREAM` in the same JSON inventory (source
`libc/src/c23_upstream_small.c`, test `tests/libc/test_c23_upstream_small.c`).
The task's enumeration (23 C23 math + `timespec_getres` + `mbrtoc8` +
`c8rtomb` + `FE_TIESAWAY`) matches the JSON exactly at 27, so **27 is the
authoritative denominator** used here.

---

## 2. Per-API closure ledger (27/27)

Status rule: **RESOLVED** = a changed/new source file defines the capability
and a test was executed with a captured result. **BLOCKED-BY-HOST** = the host
compiler/SDK genuinely lacks the feature and the library reports that fact
without fabricating a value. **PENDING** = no source+test evidence (none
remain).

"Selling name" column distinguishes the literal ISO spelling from the
project-standard namespaced entry point. In this repository `PLATFORM_BACKED`
means a `cplus_`-prefixed delegating wrapper, so a namespaced entry point is a
valid resolution of a `MISSING` row under the inventory's own legend; still,
the distinction is recorded explicitly rather than glossed over.

| # | MISSING API (inventory row) | Header | Status | New source file | Executed test → exact result | Selling name |
|---:|---|---|---|---|---|---|
| 1 | `nextup` | math.h | RESOLVED | `libc/src/math/c23_nextup.c` | `tests/libc/test_c23_nextup.c` → `c23_nextup: 84 0` | bare ISO |
| 2 | `nextupf` | math.h | RESOLVED | same | same → `84 0` | bare ISO |
| 3 | `nextupl` | math.h | RESOLVED | same | same → `84 0` | bare ISO |
| 4 | `nextdown` | math.h | RESOLVED | same | same → `84 0` | bare ISO |
| 5 | `nextdownf` | math.h | RESOLVED | same | same → `84 0` | bare ISO |
| 6 | `nextdownl` | math.h | RESOLVED | same | same → `84 0` | bare ISO |
| 7 | `fadd` | math.h | RESOLVED | `libc/src/math/c23_arith.c` | `tests/libc/test_c23_arith.c` → `c23_arith: 83 0` | `cplus_fadd` |
| 8 | `faddf` | math.h | RESOLVED | same | same → `83 0` | `cplus_faddf` |
| 9 | `faddl` | math.h | RESOLVED | same | same → `83 0` | `cplus_faddl` |
| 10 | `fsub` | math.h | RESOLVED | same | same → `83 0` | `cplus_fsub` |
| 11 | `fsubf` | math.h | RESOLVED | same | same → `83 0` | `cplus_fsubf` |
| 12 | `fsubl` | math.h | RESOLVED | same | same → `83 0` | `cplus_fsubl` |
| 13 | `fmul` | math.h | RESOLVED | same | same → `83 0` | `cplus_fmul` |
| 14 | `fmulf` | math.h | RESOLVED | same | same → `83 0` | `cplus_fmulf` |
| 15 | `fmull` | math.h | RESOLVED | same | same → `83 0` | `cplus_fmull` |
| 16 | `fdiv` | math.h | RESOLVED | same | same → `83 0` | `cplus_fdiv` |
| 17 | `fdivf` | math.h | RESOLVED | same | same → `83 0` | `cplus_fdivf` |
| 18 | `fdivl` | math.h | RESOLVED | same | same → `83 0` | `cplus_fdivl` |
| 19 | `hypot (3-argument C23 overload)` | math.h | RESOLVED | `libc/src/math/c23_hypot16.c` | `tests/libc/test_c23_hypot16.c` → `c23_hypot16: 357 0` | `cplus_c23_hypot3` |
| 20 | `frexp16` | math.h | RESOLVED | same | same → `357 0` | `cplus_c23_frexp16` |
| 21 | `fma16` | math.h | RESOLVED | same | same → `357 0` | `cplus_c23_fma16` |
| 22 | `logb16` | math.h | RESOLVED | same | same → `357 0` | `cplus_c23_logb16` |
| 23 | `exp16` | math.h | RESOLVED | same | same → `357 0` | `cplus_c23_exp16` |
| 24 | `timespec_getres` | time.h | RESOLVED | `libc/src/stdlib/c23_time_unicode.c` | `tests/libc/test_c23_time_unicode.c` → `c23_time_unicode: 102 0` | `cplus_timespec_getres` |
| 25 | `mbrtoc8` | uchar.h | RESOLVED | same | same → `102 0` | `cplus_mbrtoc8` |
| 26 | `c8rtomb` | uchar.h | RESOLVED | same | same → `102 0` | `cplus_c8rtomb` |
| 27 | `FE_TIESAWAY` | fenv.h | **BLOCKED-BY-HOST** | `libc/install-include/cplus_c23_fenv_compat.h` (header-only query; **no value defined**) | `tests/libc/test_c23_fenv_compat.c` → `c23_fenv: 8 0` (proves absence + honest report) | query `cplus_fenv_has_tiesaway() == 0` |

### 2.1 Notes on the resolved rows

- **Rows 1–6** define the six **bare ISO symbols** (`nextup`/`nextupf`/`nextupl`
  and `nextdown` counterparts) via `nextafter` delegation. `nm` confirms exactly
  six new globals, zero removed/retyped (detail in `C23_NEXTUP.md` §4).
- **Rows 7–18** use `cplus_*` names. The 8 unsuffixed/`-l` names preserve the
  pre-existing `cplus_fadd`/`cplus_faddl`/… signatures; the 4 `/f` names were
  previously `extern`-declared but undefined and are now defined. The integration
  **replaced** `libc/src/math/math_c23_farith.c` with `c23_arith.c` in
  `libc/CMakeLists.txt` so each of the 12 symbols has exactly one definition
  (verified: `nm` count == 1 for all 12).
- **Rows 19–23:** the bare names `hypot(x,y,z)`, `frexp16`, … are absent from
  the host SDK, and the branch already shipped canonical `cplus_hypot3`,
  `cplus_frexp16`, … in `math_c23_hypot3_f16.c`. The new file therefore uses
  distinct `cplus_c23_*` names; additive coexistence with the old file is
  tested. `_Float16` is **available** on this host (see §3.2), so these are
  RESOLVED, not host-blocked. `frexp16`/`logb16` use exact f32 compositions
  because `__builtin_frexpf16` crashes the arm64 backend at `-O0`; this is a
  documented numerical-deviation note, not a blocker.
- **Rows 24–26:** bare names are undeclared in the SDK (`uchar.h` is absent
  entirely); the implementations are from-standard originals using host
  `clock_getres`/`mbrtowc`/`wcrtomb` backends. `timespec_getres` uses a
  documented 1 µs constant from a measured 200000-call probe; no fabricated
  semantics for a host that doesn't provide the API.
- **Row 4 of the task's example (`_Float16 unavailable`) does not apply to
  this host** — the measured probe gives `__FLT16_MANT_DIG__ == 11` and `1.0f16`
  compiles under `-std=c99 -Wall -Wextra -pedantic`.

---

## 3. BLOCKED-BY-HOST evidence — `FE_TIESAWAY` (row 27)

### 3.1 Compiler header lacks the macro (exact probe, re-run by this audit)

```sh
printf '#include <fenv.h>\n#ifndef FE_TIESAWAY\n#error FE_TIESAWAY_ABSENT\n#endif\n' \
  | /usr/bin/clang -x c -std=c2x -fsyntax-only -
# <stdin>:3:2: error: FE_TIESAWAY_ABSENT
# 1 error generated.
# exit=1
```

The sibling's equivalent probe (`printf '#include <fenv.h>\nint main(void){return FE_TIESAWAY;}\n' | /usr/bin/clang -x c -std=c11 -fsyntax-only -`) yields
`error: use of undeclared identifier 'FE_TIESAWAY'` with `EXIT=1`.
Both compilers available here agree:

| Compiler | Result of the `FE_TIESAWAY` preprocessor/identifier probe |
|---|---|
| `/usr/bin/clang` = Apple clang 16.0.0 (`clang-1600.0.26.6`), SDK `MacOSX.sdk` | absent (`exit=1`) |
| `/opt/homebrew/opt/llvm/bin/clang` = Homebrew clang 23.1.6 | absent; its sysroot is additionally broken here (`no such sysroot directory … MacOSX27.sdk`, `'fenv.h' file not found`) |

SDK-wide and vendored searches also return nothing:

```sh
grep -rln FE_TIESAWAY /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include
# (no files)
grep -rn FE_TIESAWAY third_party/
# (no matches)
grep -n 'define FE_' .../MacOSX.sdk/usr/include/fenv.h
# FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO only — no FE_TIESAWAY
```

### 3.2 Contrast: the `_Float16` probe that is **not** blocked

```sh
/usr/bin/clang -std=c99 -dM -E -x c /dev/null | grep __FLT16_MANT_DIG__
# #define __FLT16_MANT_DIG__ 11
printf 'int main(void){_Float16 x=1.0f16;(void)x;return 0;}\n' > /tmp/f16.c
/usr/bin/clang -std=c2x -fsyntax-only /tmp/f16.c   # exit=0
```

So the four f16 APIs (rows 20–23) are implemented, whereas `FE_TIESAWAY`
(row 27) cannot be. The distinction is evidence-based, not assumed.

### 3.3 Honest disposition (not "RESOLVED")

`cplus_c23_fenv_compat.h` deliberately emits **no** `FE_TIESAWAY` substitute:
it publishes `CPLUS_C23_FENV_TIESAWAY_STATUS == "UNSUPPORTED-BY-HOST"` and
`cplus_fenv_has_tiesaway() == 0`, with a forward-compatible branch that aliases
the *real* host macro should a future SDK provide one. The task's classification
options do not include "honestly unsupported", so this row is recorded as
**BLOCKED-BY-HOST** (the closest correct bucket) rather than being counted as a
fabricated resolution. `docs/libc/COVERAGE_100_AUDIT.md` still lists it under
MISSING; `docs/libc/C23_FENV_COMPAT.md` is the authoritative reclassification
and notes that the audit was out of scope for that agent.

---

## 4. Runtime-hardening additive APIs (3 groups / 10 new symbols)

All three are **additive wrappers**, wired into `libc/src/**` (not `runtime/**`
or `compiler/**`); no existing signature, struct layout or exported symbol was
changed. Each gap statement is backed by the sibling's survey and each result
by a test this audit re-executed.

| Group | New additive API(s) | Gap evidence | Test → exact result |
|---|---|---|---|
| Checked sync destroy | `cplus_mtx_destroy_checked`, `cplus_cnd_destroy_checked`, `cplus_thrd_join_checked` (3 symbols) | `libc/src/thread/threads_c11.c:304,348` export **void** forwarders `cplus_mtx_destroy`/`cplus_cnd_destroy` that discard the pthread destroy return code; a second destroy is UB with no refusal, and `cplus_thrd_join` has no double-join guard. The Rust-examples survey marked both double-destroy cases `BLOCKED`. | `tests/libc/test_sync_checked.c` → **`sync_checked: 156 0`** (also 5× repeat, UBSan, negative control 5 failures). Refuses repeats with `EINVAL`, tables 64 identities, generation token + rollback. |
| Allocation hardening | `cplus_alloc_atomic_realloc`, `cplus_alloc_checked_free`, `cplus_alloc_state_dump` (3 symbols) | Existing `cplus_realloc`/`cplus_free` give no failure-atomic `*ptr` publication, no double-free refusal at the API boundary, and no bounded diagnostic view. `nm` survey confirms none of the 3 new names existed in the baseline archive (see `ALLOC_HARDENED.md` §4–5; fault-injection context in `docs/libc/ALLOC_FAULT_MATRIX.md` and `docs/libc/CHECKED_RUNTIME_SAFETY.md`). | `tests/libc/test_alloc_hardened.c` → **`alloc_hardened: 32 0`**. Injected failure leaves `*ptr`, block bytes (0xA5 canary), `byte_size` and `generation` unchanged; double-free refused with `EINVAL`; 12-line bounded dump. |
| Violation log | `cplus_violation_log_open`, `cplus_violation_log_event`, `cplus_violation_log_close`, `cplus_violation_log_count` (4 symbols) | Existing safety ABI (`cplus_fatal`, `cplus_check_bounds`, `cplus_check_nonnull` in `runtime/cplus_runtime.c`; `cplus_runtime_get_violation_trap`/`_get_failstop` in `stdlib/safe/src/safe_runtime.c`) always terminates via `_Exit(code)` — there was **no** opt-in, bounded, thread-safe channel to *record* a violation description before termination. `nm` survey of `build/libcplus_runtime.a` and `build/safe/libcplus_safecrt.a` (see `VIOLATION_LOG.md` §2). | `tests/libc/test_violation_log.c` → **`violation_log: 41 0`** (10× repeat). Fixed 256-byte line, 96-char `kind` truncation, 1,048,576-event cap, 2×100-thread concurrency with no interleaving, caller path (nothing hard-coded). |

`FE_TIESAWAY`'s query (`cplus_fenv_has_tiesaway`, row 27) is **header-only /
static inline** — it adds no exported symbol and is not part of the 10.

---

## 5. ABI preservation

**Claim: ABI fingerprint MATCHES the committed baseline — verified, not
asserted.** Exact command executed from the repository root at the audited HEAD:

```sh
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt
```

Observed output (verbatim, exit 0):

```
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1235 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
ABI_COMPARE_EXIT=0
```

**Why this is a legitimate MATCH and not a stale-baseline artifact:** the
integration commit `2a2e13f` refreshed `tests/abi/fingerprints/baseline.txt`
and `baseline.json` to include the 28 new symbol records (e.g. `::_nextup`,
`::_cplus_c23_hypot3`, `::_cplus_mtx_destroy_checked`,
`::_cplus_violation_log_open`), and the rebuilt archive
`build/libc/libcplus_hardened.a` now defines all of them. Per-archive defined
count grew `784 → 812`; every added record is a **new** symbol, with zero
removed or retyped exports. The layout (`L`), compiler-fact (`C`) and
function-signature (`F`) sections are unchanged.

**Refresh command** (only needed if the baseline is ever regenerated; recorded
here so the MATCH is reproducible, not relied upon blindly):

```sh
sh tools/abi_compare/run.sh baseline
# writes tests/abi/fingerprints/baseline.txt and baseline.json
```

Scope limitation (from `tools/abi_compare/README.md`): this detects *change*
against a host-specific baseline (arm64-apple-darwin, `/usr/bin/clang`
Apple clang 16, LP64, two default archives); it does not prove Windows/x86 or
MSVC calling-convention correctness.

---

## 6. Agent 1 coordination boundary

**Verified from commit `2a2e13f`:** no `runtime/**` and no `compiler/**` core
file was added, modified, staged or committed by the 10 parallel agents. The
complete set of changed implementation files under `libc/src/` was:

```
libc/src/math/c23_arith.c
libc/src/math/c23_hypot16.c
libc/src/math/c23_nextup.c
libc/src/runtime/violation_log.c      <- NEW file under libc/src, not runtime/**
libc/src/stdlib/alloc_hardened.c
libc/src/stdlib/c23_time_unicode.c
libc/src/thread/sync_checked.c
```

`git show --name-only --format= 2a2e13f | grep -E '^(runtime|compiler)/'`
returned nothing. Hardening is **additive wrappers only**: every solver reads
existing state through existing public APIs, keeps a private side table in its
own `.c`, and leaves the existing fatal/trap ABI, allocator metadata side
table, pointer representation, thread struct layouts and `threads_c11.c`
void forwarders untouched. Compiler-side safety (UAF/double-free, bounds,
alias, lifetime diagnostics) remains the documented Agent 1 handoff
(`docs/libc/AGENT1_PRIORITY_HANDOFF.md`, `docs/libc/AGENT1_COMPILER_GAPS.md`);
none of it was touched or claimed here.

---

## 7. Exact reproduction commands

Run from the repository root. Use `/usr/bin/clang`: the default `clang` on
`PATH` is Homebrew clang 23.1.6 with a broken SDK sysroot in this environment.

```sh
# --- authoritative count ----------------------------------------------------
python3 -c "
import json; d=json.load(open('docs/libc/iso_c_inventory.json'))
m=[a for h in d['headers'] for a in h['apis'] if a['classification']=='MISSING']
print(len(m))
"

# --- re-execute all eight suites (archive now contains all new sources) -----
CC=/usr/bin/clang
INC='-Ilibc -Ilibc/include -Ilibc/install-include'
LIB=build/libc/libcplus_hardened.a
TMP=$(mktemp -d)

$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_c23_nextup.c     $LIB -lm -pthread -o $TMP/n   && timeout 30 $TMP/n   # c23_nextup: 84 0
$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_c23_arith.c      $LIB -lm -pthread -o $TMP/a   && timeout 30 $TMP/a   # c23_arith: 83 0
$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_c23_hypot16.c    $LIB -lm -pthread -o $TMP/h   && timeout 30 $TMP/h   # c23_hypot16: 357 0 / c23_hypot16: ok
$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_c23_time_unicode.c $LIB -lm -pthread -o $TMP/t && timeout 30 $TMP/t # c23_time_unicode: 102 0
$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_sync_checked.c   $LIB -lm -pthread -o $TMP/s   && timeout 30 $TMP/s   # sync_checked: 156 0
$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_alloc_hardened.c $LIB -lm -pthread -o $TMP/g   && timeout 30 $TMP/g   # alloc_hardened: 32 0
$CC -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_violation_log.c  $LIB -lm -pthread -o $TMP/v   && timeout 30 $TMP/v   # violation_log: 41 0
$CC -O2 -std=c99 -Wall -Wextra -pedantic $INC tests/libc/test_c23_fenv_compat.c -o $TMP/f && timeout 30 $TMP/f | tail -1 # c23_fenv: 8 0

# --- host probes ------------------------------------------------------------
printf '#include <fenv.h>\n#ifndef FE_TIESAWAY\n#error FE_TIESAWAY_ABSENT\n#endif\n' \
  | /usr/bin/clang -x c -std=c2x -fsyntax-only -          # exit=1 -> FE_TIESAWAY_ABSENT
/usr/bin/clang -std=c99 -dM -E -x c /dev/null | grep __FLT16_MANT_DIG__     # 11 -> _Float16 present

# --- ABI --------------------------------------------------------------------
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt     # exit=0, MATCH

# --- coordination boundary --------------------------------------------------
git show --name-only --format= 2a2e13f | grep -E '^(runtime|compiler)/'      # no output
```

---

## 8. Honest conclusion and limits

- All **27** `MISSING` rows have a disposition: **26 RESOLVED**, **1
  BLOCKED-BY-HOST**, **0 PENDING**. Each RESOLVED row is backed by a new source
  file and a test this audit ran; the single blocked row has exact compiler/
  SDK probe output and a no-fabricated-value policy.
- Resolution is **capability-level**: 20 of the 26 use the project-standard
  namespaced entry points rather than the literal bare ISO spelling. The bare
  spellings remain intentionally unexported for those families to avoid a
  future host `<math.h>` collision; this is recorded per row in §2.
- Numerical fidelity is **platform-backed, not a formal proof**: correct
  rounding for `fadd`…`fdiv` is inherited from host operators
  (`FLT_EVAL_METHOD == 0`, `FLT_RADIX == 2`); `hypot3` may differ a few ulp
  from a hypothetically correctly-rounded 3-arg hypot; `exp16` lowers through
  host `expf`. These deviations are documented in the sibling docs and are not
  hidden here.
- `memset_explicit` is **not** part of these 27 (already `IMPLEMENTED_UPSTREAM`);
  the prose "28" figure in `COVERAGE_100_AUDIT.md` is stale. The 49 deliberate
  `UNSUPPORTED` entries and the library's non-100% completeness statement
  remain in force.
- ABI MATCH is against a baseline refreshed by the integration commit; it
  proves no *unexpected* drift on this host, not cross-platform correctness.
