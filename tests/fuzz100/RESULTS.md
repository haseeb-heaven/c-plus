# fuzz100 verification results

Verification of the `tests/fuzz100/run.sh` runner and the `f100_*` harnesses it
discovers. This file records what was **actually observed** by executing the
runner; nothing here is inferred from a build alone.

## Verification setup

| Field | Value |
|---|---|
| Command | `timeout 300 sh tests/fuzz100/run.sh` (run from repo root) |
| Environment | macOS, `/usr/bin/clang` (default C backend), `<root>/build/c+` (C+ driver) |
| Per-case bound | 30 s build + 30 s run, enforced in `run.py` via `subprocess.run(timeout=30)` |
| Wrapper check | Also verified from an unrelated cwd via absolute path (`--dry-run`, exit 0) — root resolution works from any directory. |
| Snapshot | Latest verification run: 72 discovered. Sibling agents were still adding harnesses during verification, so discovered counts grew between runs (12 -> 39 -> 47 -> 72); the output below is the latest run. |
| Machine-readable record | `build/fuzz100/summary.json` (and per-case `result.json` + logs) |

## Exact runner output (latest run)

```text
fuzz100: discovered 72, executed 66
config   case                               kind   json  status        stage       b/r    
------------------------------------------------------------------------------------------
F021     f100_alloc_f021_malloc.c           c      no    PASS          run         0/0    
F022     f100_alloc_f022_calloc.c           c      no    FAIL          run         0/1    
F023     f100_alloc_f023_realloc.c          c      no    PASS          run         0/0    
F024     f100_alloc_f024_free.c             c      no    PASS          run         0/0    
F025     f100_alloc_f025_aligned.c          c      no    PASS          run         0/0    
F026     f100_alloc_f026_sidetable.c        c      no    PASS          run         0/0    
F027     f100_alloc_f027_transfer.c         c      no    PASS          run         0/0    
F028     f100_alloc_f028_injection.c        c      no    PASS          run         0/0    
F029     f100_alloc_f029_canary.c           c      no    FAIL          run         0/1    
-        f100_alloc_harness.h               unsupported no    SKIPPED       discovery   -/-    
-        f100_alloc_life_run.sh             unsupported no    SKIPPED       discovery   -/-    
-        f100_fmt_checked.c                 c      no    PASS          run         0/0    
-        f100_fmt_common.h                  unsupported no    SKIPPED       discovery   -/-    
-        f100_fmt_contract.c                c      no    PASS          run         0/0    
-        f100_fmt_fprintf.c                 c      no    PASS          run         0/0    
-        f100_fmt_printf.c                  c      no    BUILD_FAILURE build       1/-    
-        f100_fmt_snprintf_float.c          c      no    PASS          run         0/0    
-        f100_fmt_snprintf_int.c            c      no    PASS          run         0/0    
-        f100_fmt_snprintf_trunc.c          c      no    PASS          run         0/0    
-        f100_fmt_sprintf.c                 c      no    PASS          run         0/0    
-        f100_fmt_sscanf.c                  c      no    PASS          run         0/0    
-        f100_fmt_vsnprintf.c               c      no    PASS          run         0/0    
-        f100_io_common.h                   unsupported no    SKIPPED       discovery   -/-    
-        f100_io_open_close.c               c      no    PASS          run         0/0    
-        f100_io_readline.c                 c      no    PASS          run         0/0    
-        f100_io_readwrite.c                c      no    PASS          run         0/0    
F030     f100_life_f030_cplus_malloc.c      c      no    PASS          run         0/0    
F031     f100_life_f031_cplus_calloc.c      c      no    FAIL          run         0/1    
F032     f100_life_f032_cplus_realloc.c     c      no    PASS          run         0/0    
F033     f100_life_f033_cplus_free.c        c      no    PASS          run         0/0    
F034     f100_life_f034_cplus_aligned.c     c      no    FAIL          run         0/2    
F035     f100_life_f035_cplus_meta.c        c      no    PASS          run         0/0    
F036     f100_life_f036_cplus_transfer.c    c      no    PASS          run         0/0    
F037     f100_life_f037_cplus_injection.c   c      no    PASS          run         0/0    
F038     f100_life_f038_repeated_drop.c     c      no    PASS          run         0/0    
F039     f100_life_f039_canary.c            c      no    PASS          run         0/0    
F040     f100_life_f040_custom_alloc_skip.c c      no    PASS          run         0/0    
-        f100_mem_memcmp_bounded.c          c      no    PASS          run         0/0    
-        f100_mem_memcmp_s.c                c      no    PASS          run         0/0    
-        f100_mem_memcpy_bounded.c          c      no    PASS          run         0/0    
-        f100_mem_memcpy_s_dmax.c           c      no    PASS          run         0/0    
-        f100_mem_memmove_bounded.c         c      no    PASS          run         0/0    
-        f100_mem_memmove_overlap.c         c      no    PASS          run         0/0    
-        f100_mem_memmove_s.c               c      no    PASS          run         0/0    
-        f100_mem_memset_bounded.c          c      no    PASS          run         0/0    
-        f100_mem_memset_s.c                c      no    PASS          run         0/0    
-        f100_mem_memzero_s.c               c      no    PASS          run         0/0    
-        f100_str_constraint_matrix.c       c      no    PASS          run         0/0    
-        f100_str_strcat_s.c                c      no    PASS          run         0/0    
-        f100_str_strcmp_s.c                c      no    FAIL          run         0/-6   
-        f100_str_strcpy_bounded.c          c      no    PASS          run         0/0    
-        f100_str_strlen_bounded.c          c      no    PASS          run         0/0    
-        f100_str_strncat_s.c               c      no    FAIL          run         0/-6   
-        f100_str_strncpy_s.c               c      no    PASS          run         0/0    
-        f100_str_strnlen_bounded.c         c      no    PASS          run         0/0    
-        f100_str_strnlen_s.c               c      no    PASS          run         0/0    
-        f100_cont_engine.hp                unsupported no    SKIPPED       discovery   -/-    
F061     f100_cont_f061_collide_int.cp      cplus  no    FAIL          run         0/1    
-        f100_cont_include_probe.cp         cplus  no    PASS          run         0/0    
-        f100_cont_include_probe.hp         unsupported no    SKIPPED       discovery   -/-    
-        f100_file_write_read.cp            cplus  no    FAIL          run         0/1    
-        f100_fs_cleanup.cp                 cplus  no    PASS          run         0/0    
-        f100_fs_pathgen.cp                 cplus  no    PASS          run         0/0    
-        f100_net_partial.cp                cplus  no    FAIL          run         0/1    
-        f100_net_peers.cp                  cplus  no    PASS          run         0/0    
-        f100_strview_accessors.cp          cplus  no    PASS          run         0/0    
-        f100_strview_append.cp             cplus  no    PASS          run         0/0    
-        f100_strview_reserve_copy.cp       cplus  no    PASS          run         0/0    
-        f100_strview_stale.cp              cplus  no    PASS          run         0/0    
-        f100_strview_utf8.cp               cplus  no    PASS          run         0/0    
-        f100_thread_mutex.cp               cplus  no    PASS          run         0/0    
-        f100_time_iso8601.cp               cplus  no    PASS          run         0/0    
------------------------------------------------------------------------------------------
fuzz100: pass=56, fail=9, build_failure=1, timeout=0, skipped=6, blocked=0
fuzz100: JSON summary: build/fuzz100/summary.json
fuzz100: logs: build/fuzz100/cases
```

Exit code: `1` (expected — the runner exits 1 when any executed case is FAIL,
BUILD_FAILURE, TIMEOUT or BLOCKED).

## Counts (latest run)

| Metric | Count |
|---|---|
| Harnesses discovered | 72 |
| Executed (build attempted) | 66 |
| PASS | 56 |
| FAIL | 9 |
| BUILD_FAILURE | 1 |
| TIMEOUT | 0 |
| SKIPPED | 6 |
| BLOCKED | 0 |

The 6 `SKIPPED` cases have helper-extension suffixes and no `main`
(`f100_alloc_harness.h`, `f100_alloc_life_run.sh`, `f100_fmt_common.h`,
`f100_io_common.h`, `f100_cont_engine.hp`, `f100_cont_include_probe.hp`). They
are discovered but not built and are never counted as passes.

Earlier snapshots, for transparency about the still-growing corpus:

| Run | Discovered | Executed | PASS | FAIL | BUILD_FAILURE | TIMEOUT | SKIPPED |
|---|---|---|---|---|---|---|---|
| First (all C cases unbuildable) | 12 | 8 | 3 | 0 | 5 | 0 | 4 |
| After C-flag fix | 39 | 34 | 29 | 4 | 1 | 0 | 5 |
| Intermediate | 47 | 42 | 37 | 5 | 0 | 0 | 5 |
| Latest | 72 | 66 | 56 | 9 | 1 | 0 | 6 |

The intermediate `BUILD_FAILURE=5` were entirely the runner's missing
`-Ilibc/install-include` / hardened-archive flags (fixed below). The
intermediate `BUILD_FAILURE=1` was a harness-side compiler rejection in
`tests/stdlib/fuzz/f100_strview_accessors.cp` that the harness author
subsequently fixed. F-039 canary likewise went from FAIL to PASS after the
harness author fixed it. New harnesses added later introduced the additional
failures documented below. This churn is expected while sibling agents are
still writing; the runner itself is stable and correctly classifies every case.

## Fixes made (runner files only)

1. **`tests/fuzz100/run.sh` — repo-root path fix.** The wrapper originally
   computed `ROOT` with `../../..` from `tests/fuzz100/`, which resolved to
   `/Users/haseeb-mir/Documents/Code/Cpp` and made the very first run die with:
   ```
   Python: can't open file '/Users/haseeb-mir/Documents/Code/tests/fuzz100/run.py': [Errno 2] No such file or directory
   ```
   The runner agent corrected this to `../..` concurrently during verification;
   the final `run.sh` is correct and was re-verified from a clean invocation and
   from an unrelated cwd.

2. **`tests/fuzz100/run.py` — C harness include/link flags (fixed by this
   verification agent).** The C build command lacked the canonical CRT include
   paths and link archives. All 5 C harnesses that include `cplus_libc.h` failed
   with `fatal error: 'cplus_libc.h' file not found`, and even with the header
   found they reference `cplus_*_bounded` symbols from `libcplus_hardened.a`.
   Fixed by adding `-I. -Ilibc -Ilibc/include -Ilibc/install-include` to the
   existing safe/safeclib includes and by linking `build/libc/libcplus_hardened.a`
   plus `build/safe/libcplus_safecrt.a` when present, and `-lm`. Verified by
   rebuilding all 5 previously failing harnesses (all exit 0 with correct
   digests) and by a clean full runner invocation.

3. **`tests/fuzz100/README.md` — documentation sync (fixed by this verification
   agent).** Updated the build-command table and the archive-linking paragraph
   so they describe the corrected C build command rather than the stale one.

No harness files were modified. No `git add`/`git commit` was performed. No
header files were edited.

## Documented harness failures (not hidden, not fixed here)

All non-PASS cases in the latest run are owned by other agents. `BUILD_FAILURE`
means the compiler rejected the input; the rest built (build rc 0) and then
exited non-zero. Verbatim captured output from each case's `result.json` /
logs:

### BUILD_FAILURE: `tests/crt/fuzz/f100_fmt_printf.c` (build rc 1)
```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/crt/fuzz/f100_fmt_printf.c:17:5: error: call to undeclared function 'f100_tmp_path'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
   17 |     f100_tmp_path(path, sizeof path, "printf");
      |     ^
1 error generated.
```
Harness-side issue: missing declaration of a helper (likely in
`f100_fmt_common.h`). Not a runner defect — the runner correctly reports it.

### `tests/crt/fuzz/f100_alloc_f022_calloc.c` — FAIL, run rc 1
```
FAIL F-022 overflow_refused
FAIL F-022 overflow_refused
FAIL F-022 overflow_refused
FAIL F-022 overflow_refused
FAIL F-022 overflow_refused
FAIL F-022 overflow_refused
FAIL F-022 overflow_refused
F100 F-022 CHECKS=5950 FAILS=7
```

### `tests/crt/fuzz/f100_alloc_f029_canary.c` — FAIL, run rc 1
```
F100 F-029 CANARY_INTEGRITY=1 GUARD_DETECTED=0 OK=0
FAIL F-029 canary_pages
F100 F-029 CHECKS=2403 FAILS=1
```

### `tests/crt/fuzz/f100_life_f031_cplus_calloc.c` — FAIL, run rc 1
```
FAIL F-031 overflow_refused
FAIL F-031 overflow_refused
FAIL F-031 overflow_refused
FAIL F-031 overflow_refused
FAIL F-031 overflow_refused
FAIL F-031 overflow_refused
F100 F-031 CHECKS=4766 FAILS=6
```

### `tests/crt/fuzz/f100_life_f034_cplus_aligned.c` — FAIL, run rc 2
```
F-034 PROBE p=0x77f4c08000 alignment_ok=1 live=0 get_meta=22 size=0 remaining=18446744073709551615
DEFECT F-034 cplus_aligned_alloc returns memory that is absent from the hardened allocation side table: live=0 get_meta=22 size=0 remaining=18446744073709551615; cplus_free() then refutes it as an invalid free and leaks the block (libc/src/stdlib/stdlib_c17.c calls aligned_alloc() directly instead of the tracked path)
F100 F-034 DEFECT_CONFIRMED=1
```
The pointer address in the `PROBE` line varies between runs; the defect report
itself is deterministic.

### `tests/crt/fuzz/f100_str_strcmp_s.c` — FAIL, run rc -6 (SIGABRT)
```
stderr: Assertion failed: (got == want), function main, file f100_str_strcmp_s.c, line 148.
```

### `tests/crt/fuzz/f100_str_strncat_s.c` — FAIL, run rc -6 (SIGABRT)
```
stdout: IGNORE CONSTRAINT HANDLER: (0) strncat_s: slen is 0
stderr: Assertion failed: (dst.data[i] == dpres[i]), function main, file f100_str_strncat_s.c, line 137.
```

### `tests/stdlib/fuzz/f100_cont_f061_collide_int.cp` — FAIL, run rc 1
```
F-061 INVARIANT intmap.size
```

### `tests/stdlib/fuzz/f100_file_write_read.cp` — FAIL, run rc 1
```
F-081 debug case 297 kind 3 pos 5 tell 8 total 5
F-081 INVARIANT position.shadow
```

### `tests/stdlib/fuzz/f100_net_partial.cp` — FAIL, run rc 1
```
INVARIANT f098_big_write
f098_digest 7c8cb6c9dc2a41b0
f098: FAILURES 1
```

Interpretation: F-022 and F-031 report `overflow_refused` check failures for
calloc overflow handling; F-029 reports a canary-page guard detection failure;
F-034 reports a confirmed defect in `cplus_aligned_alloc` side-table tracking;
the two `f100_str_*` cases abort on assertions; the three `.cp` cases report
invariant violations. These are genuine harness findings, not runner defects,
and remain visible as FAIL in the summary.
