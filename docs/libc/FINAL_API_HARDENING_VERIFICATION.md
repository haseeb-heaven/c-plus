# Final verification — missing-API closure and runtime hardening

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Verified HEAD:** `f2ab883` (archive `build/libc/libcplus_hardened.a`, built 2026-11-23)
**Toolchain:** `/usr/bin/clang` = Apple clang 16.0.0 (`clang-1600.0.26.6`),
`arm64-apple-darwin27.2.0`, SDK `MacOSX.sdk`. GNU `/usr/bin/timeout` used for the
30-second budget.

**Ownership of this file:** this document only. **No source file, header, build
script, CMake file, ABI-baseline file, `runtime/**` or `compiler/**` core file was
created, modified, staged or committed by this verification.** All binaries,
stdout/stderr captures and ABI-comparator outputs were written to a temp directory
outside the repository (`/private/var/folders/.../opencode/final_verify_6`). The
ABI comparator was deliberately run with `CPLUS_FP_OUT` pointed at that temp
directory so it could not write into the repository.

---

## 1. Method

All eight suites were independently re-compiled and re-executed from the
repository root under `timeout 30`. The hardened archive was **not** rebuilt; it
was verified first to contain all 10 new hardening symbols plus the 6 bare ISO
symbols (`nm -g --defined-only build/libc/libcplus_hardened.a`), so the
archive-only link shape of `MISSING_API_CLOSURE.md` §7 is the correct one.

Compile shape (per `docs/libc/MISSING_API_CLOSURE.md` §7, cross-checked against
each sibling doc):

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include <test>.c \
  build/libc/libcplus_hardened.a -lm -pthread -o <bin>
timeout 30 <bin>
```

Deviations from that common shape, taken from the owning docs:

- **`test_c23_arith.c`** additionally links `libc/log/log.c` (required by the
  logging ON/OFF parity section; `docs/libc/C23_ARITH.md` §4.1). The archive's
  `cplus_libc_log` member is not extracted in this link, so there is no duplicate
  definition. All 12 `fadd`…`fdiv` symbols come from the archive.
- **`test_c23_fenv_compat.c`** is header-only with respect to the new API and was
  compiled at `-O2` per `docs/libc/C23_FENV_COMPAT.md` §6
  (`-Ilibc/install-include`, archive linked but not required by the test).

Every compile emitted **zero diagnostics** and exited 0. No pass below is taken
from a document's prose; each row is the observed final stdout line and the
observed process exit status of a freshly built binary.

---

## 2. Exact results (test / final line / exit)

| Test (source) | Final stdout line | Exit |
|---|---|---:|
| `tests/libc/test_c23_nextup.c` | `c23_nextup: 84 0` | 0 |
| `tests/libc/test_c23_arith.c` | `c23_arith: 83 0` | 0 |
| `tests/libc/test_c23_hypot16.c` | `c23_hypot16: ok` | 0 |
| `tests/libc/test_c23_time_unicode.c` | `c23_time_unicode: 102 0` | 0 |
| `tests/libc/test_c23_fenv_compat.c` | `c23_fenv: 8 0` | 0 |
| `tests/libc/test_sync_checked.c` | `sync_checked: 156 0` | 0 |
| `tests/libc/test_alloc_hardened.c` | `alloc_hardened: 32 0` | 0 |
| `tests/libc/test_violation_log.c` | `violation_log: 41 0` | 0 |

**8/8 suites pass, exit 0, all under `timeout 30`.** Notes on the multi-line
outputs (nothing hidden behind "final line"):

- `test_c23_hypot16.c` printed exactly two stdout lines, in order:
  `c23_hypot16: 357 0` then `c23_hypot16: ok` (the latter is the final line).
- `test_c23_fenv_compat.c` printed a 22-line diagnostic report; the final line is
  `c23_fenv: 8 0` (this is the line `MISSING_API_CLOSURE.md` §7 selects with
  `| tail -1`).
- `test_c23_arith.c` printed exactly one stdout line (`c23_arith: 83 0`) and 12
  opt-in `[CPLUS-LIBC] …: enter` logger lines on stderr (expected per
  `C23_ARITH.md` §4.1). All other seven suites produced zero stderr lines.

These values are identical to the results recorded in `MISSING_API_CLOSURE.md` §7
and §2: `84 0`, `83 0`, `357 0`/`ok`, `102 0`, `8 0`, `156 0`, `32 0`, `41 0`.

---

## 3. Cross-check of `docs/libc/MISSING_API_CLOSURE.md`

Recomputed directly from the authoritative inventory rather than trusted from
prose:

```
python3 -c "
import json; d=json.load(open('docs/libc/iso_c_inventory.json'))
m=[a for h in d['headers'] for a in h['apis'] if a['classification']=='MISSING']
print(len(m))"
# 27
```

Ledger confirmed: **27 total = 26 `RESOLVED` + 1 `BLOCKED-BY-HOST` (`FE_TIESAWAY`),
0 `PENDING`/still-MISSING.** Rows 1–26 each map to a new source file and a test
that this verification re-ran (all eight executed; the 26 rows share those eight
suites). Breakdown holds: 6 bare ISO names (`nextup` family), 20 namespaced
`cplus_*`/`cplus_c23_*` entry points, 3 hardening groups / 10 new symbols.

### `FE_TIESAWAY` — honest `BLOCKED-BY-HOST`, re-probed here

```
printf '#include <fenv.h>\n#ifndef FE_TIESAWAY\n#error FE_TIESAWAY_ABSENT\n#endif\n' \
  | /usr/bin/clang -x c -std=c2x -fsyntax-only -
# <stdin>:3:2: error: FE_TIESAWAY_ABSENT
# 1 error generated.
# exit=1
```

`test_c23_fenv_compat.c` independently confirmed
`classification: UNSUPPORTED-BY-HOST`, a live compile probe that still fails with
`use of undeclared identifier 'FE_TIESAWAY'`, `cplus_fenv_has_tiesaway() == 0`,
and four distinct host modes that round-trip through `fesetround`/`fegetround`
(positive control). **No `FE_TIESAWAY` value is fabricated**; the header emits no
substitute. This matches `MISSING_API_CLOSURE.md` §3 and `C23_FENV_COMPAT.md`.

No "100% ISO C complete" claim is made; the 49 deliberate `UNSUPPORTED` entries are
unchanged. (The stale "28 MISSING" prose in `COVERAGE_100_AUDIT.md` is addressed in
`MISSING_API_CLOSURE.md` §1; `memset_explicit` is already `IMPLEMENTED_UPSTREAM`.)

---

## 4. Runtime hardening — additive wrappers only; core files untouched

**Explicit statement:** no `runtime/**` and no `compiler/**` core file was edited,
created, staged or committed. `git show --name-only --format= 2a2e13f | grep -E
'^(runtime|compiler)/'` returns nothing. The complete set of implementation files
in the integration commit is the following seven, all under `libc/src/`:

```
libc/src/math/c23_arith.c
libc/src/math/c23_hypot16.c
libc/src/math/c23_nextup.c
libc/src/runtime/violation_log.c     <- under libc/src, NOT the top-level runtime/**
libc/src/stdlib/alloc_hardened.c
libc/src/stdlib/c23_time_unicode.c
libc/src/thread/sync_checked.c
```

All hardening is **additive wrappers only**:

| Group | New additive symbols (10 total) | Existing ABI left untouched |
|---|---|---|
| Checked sync destroy/join | `cplus_mtx_destroy_checked`, `cplus_cnd_destroy_checked`, `cplus_thrd_join_checked` | `threads_c11.c` void forwarders, thread struct layouts |
| Allocation hardening | `cplus_alloc_atomic_realloc`, `cplus_alloc_checked_free`, `cplus_alloc_state_dump` | allocator metadata side table, pointer representation, `cplus_realloc`/`cplus_free` |
| Violation log | `cplus_violation_log_open`, `cplus_violation_log_event`, `cplus_violation_log_close`, `cplus_violation_log_count` | fatal/trap ABI (`cplus_fatal`, `cplus_check_bounds`, `cplus_check_nonnull`, runtime trap/failstop getters) |

Every solver reads existing state through existing public APIs and keeps its own
private side table in its own `.c`. No existing signature, struct layout or
exported symbol changed. `FE_TIESAWAY`'s query is header-only / `static inline`
and adds no exported symbol. Compiler-side safety (UAF/double-free, bounds, alias,
lifetime diagnostics) remains the documented Agent 1 handoff and was not touched
or claimed here.

**Working-tree note (read-only observation):** `git status --short` shows
pre-existing modifications and untracked files from other parallel agents
(`benchmarks/.../RESULTS.md`, several `tests/abi/.../results.log`,
`tests/expanded_examples_adversarial/...`, `scratch/bugs*.cp`, etc.). None of these
was created, modified or touched by this verification; this agent wrote only this
document inside the repository.

---

## 5. ABI is additive-only — re-verified here

The comparator was run from the repository root with its output redirected to the
temp directory, so no repository file was written:

```
CPLUS_FP_OUT="$TMP/abi_out" sh tools/abi_compare/run.sh compare \
  tests/abi/fingerprints/baseline.txt
# abi_fp: wrote .../fingerprint.txt (1235 records) and fingerprint.json
# abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
# abi_fp: diff is empty: .../diff.txt
# ABI_COMPARE_EXIT=0
```

This reproduces `MISSING_API_CLOSURE.md` §5 exactly (1235 records, empty diff,
exit 0). The baseline was refreshed by integration commit `2a2e13f` to include the
28 new records; the per-archive defined-symbol count grew `784 → 812`, and every
added record is a **new** symbol with **zero removed or retyped exports**. The
layout (`L`), compiler-fact (`C`) and function-signature (`F`) sections are
unchanged. The six bare ISO nextup/nextdown symbols and all ten hardening symbols
are confirmed present in the current archive by `nm`. Hence the ABI change is
additive-only.

Scope limitation (from `tools/abi_compare/README.md`): this is a MATCH against a
host-specific baseline (arm64-apple-darwin, Apple clang 16, LP64, two default
archives); it demonstrates no *unexpected* drift on this host, not Windows/x86 or
MSVC calling-convention correctness.

---

## 6. Bottom line

- All **8/8** executed suites pass with exit 0 under `timeout 30`; exact final
  lines are in §2. **No fabricated passes.**
- `MISSING_API_CLOSURE.md` is confirmed at **26/27 RESOLVED** with `FE_TIESAWAY`
  **BLOCKED-BY-HOST** and 0 pending; the host probe and the fenv test both prove
  the absence and the honest report.
- No `runtime/**`/`compiler/**` core file was edited; hardening is additive
  wrappers only (10 new symbols in 3 groups).
- ABI fingerprint MATCHES the committed baseline (exit 0, empty diff):
  **additive-only**, no removed or retyped exports.
- This verification made no repository change other than this document.
