# Deterministic allocation-failure injection suite — verification notes

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Compiler:** `build/c+` (checked-in binary; not rebuilt by this task)
**Timeout utility:** `/usr/bin/timeout` with `-k 5` escalation and a 30 s limit
**File ownership:** only `tests/stdlib/allocation_failure/` was created. No
header, compiler, runner, or other repository file was edited; no `git add` /
`git commit` was run. The one extra build flag used for UBSan is documented
below and is passed on the command line only.

## Files created

| File | Purpose |
| --- | --- |
| `af_hooks.cp` | hook semantics: fail-first, fail-Nth (one-shot), `fail_after`, `reset`, telemetry counters, vector reserve/push reallocation failures |
| `af_containers.cp` | every fallible allocation point of `vector` + all seven `collections.hp` containers: probe count, fail at each N, atomicity, retry, telemetry, repeated drop |
| `af_string.cp` | `string` ctor, `reserve`, `append`, `copy_from` failure atomicity and retry |
| `af_json_file.cp` | JSON and file-wrapper injection gaps, both backed by positive probes and reported as documented SKIPs |
| `run.sh` | bounded runner: strict/no-sanitizer **FIRST**, then UBSan; 30 s timeout on every build and run; byte-exact determinism check |
| `NOTES.md` | this file |

## Output protocol

Each check prints `<tag> <1|0>` (`1` = pass). Final line is
`ALLOC_FAIL <checks> <failures>`; exit code is 1 when `<failures> > 0`.
Documented skips additionally print `SKIP_REASON <tag> <reason>`; the skip's
`<tag> 1` line is still counted in `<checks>`. All loops are bounded, so
reaching the final line under `timeout 30` is the no-hang proof.

## Failure points covered

### `af_hooks.cp` — injection primitives (30 checks)

| Tag prefix | Failure point | Verified recovery |
| --- | --- | --- |
| `hk_first_*` | `stdlib_alloc_fail_at(1)` on the very next (first) allocation | `NULL`, `seq == 1`, `alloc_failures == 1`; reset retry succeeds; `release(NULL)` is a safe no-op |
| `hk_nth_*` | `stdlib_alloc_fail_at(3)`: allocations 1,2 succeed, 3 fails once, 4 succeeds | `seq == 4`, exactly one counted failure; three live blocks released, balanced |
| `hk_after_*` | `stdlib_alloc_fail_after(1)`: first succeeds, later ones fail until reset | two failures counted, cleanup while injection is still active allocates nothing, reset retry succeeds |
| `hk_realloc_reserve_*` | `vector::reserve(64)` reallocation from capacity 4 | `-1`, size 4 / capacity 4 and all bytes unchanged; retry gives capacity 64 |
| `hk_realloc_push_*` | `vector::push_back` reallocation at full capacity 64 → 128 | `-1`, size 64 / capacity 64 and last element `630` unchanged; retry gives size 65 / capacity 128 and element `999` |

### `af_containers.cp` — container growth (58 checks)

Counts are probed with `stdlib_alloc_seq()` after a successful run; the test
asserts each count at runtime. For every `N = 1..count` it injects
`stdlib_alloc_fail_at(N)` on a fresh identical container and checks: `rc == -1`;
size/capacity/`valid()` unchanged; every pre-existing element intact; new
element absent; `alloc_count == free_count` after `drop()`; `alloc_failures >= 1`;
and a reset retry succeeds. `drop()` is repeated at cleanup points.

| Tag prefix | Operation | Allocation points (`N`) |
| --- | --- | --- |
| `acvg` | `vector::push_back` growth (cap 4 → 8) | 1 |
| `acvs` | `vector::shrink_to_fit` (cap 64 → 3) | 1 |
| `acvc` | `vector::copy_from` from a 5-element source | 1 |
| `acim` | `intmap::put` rebuild (keys + values + state) | 1..3 |
| `acsm` | `strmap::put` rebuild (3 buffers + owned-key copy) | 1..4 |
| `acis` | `intset::insert` rebuild (keys + state) | 1..2 |
| `acdq` | `deque::push_back` on a full wrapped ring (ring-copy) | 1 |
| `aclsp` | `list::push_back` (one node) | 1 |
| `acst` | `stack::push` growth (one int buffer) | 1 |
| `acqu` | `queue::push` on a full wrapped ring (ring-copy) | 1 |

Total injected single-allocation failure points in this file: **16**.

### `af_string.cp` — string operations (26 checks)

| Tag prefix | Failure point | State proven unchanged |
| --- | --- | --- |
| `as_ctor_*` | ctor's single allocation via `string("abc")` | `valid()==0`, size 0, capacity 0, non-NULL `c_str()` equal to `""`, `at(0)=='\0'`; retry via fresh construction |
| `as_reserve_*` | `reserve(64)` reallocation | size 4, capacity 5, identical `c_str()` pointer and `"abcd"` bytes; retry gives capacity 64 |
| `as_append_*` | `append("EFGH")` growth reserve (4+4+1 > cap 5) | size 4, capacity 5, identical pointer and bytes; retry gives `size 8`, `capacity 10`, `"abcdEFGH"`; a no-growth append is proved allocation-free |
| `as_copy_*` | `copy_from` single allocation | size 8, capacity 9, identical pointer and `"original"` bytes; failed copy performed no allocation (`alloc_count==2`, `free_count==0`, `alloc_failures>=1`); retry gives `"s"`; full balance after drop |

### `af_json_file.cp` — documented gaps (8 checks, 2 SKIPs)

- `aj_json_baseline` — a small document parses through `json(text, &err)`.
- `aj_json_hook_unreachable` — with `stdlib_alloc_fail_after(0)` active (every
  checked allocation fails) the document still parses: proof that `json.hp`'s
  yyjson allocations bypass the checked allocator.
- `aj_json_no_checked_alloc` — telemetry shows no checked allocation and no
  counted failure for that parse.
- `aj_file_open` / `aj_file_io` / `aj_file_cleaned` — with
  `stdlib_alloc_fail_after(0)` active, `file_temp()` opens, writes and reads a
  byte, and double `drop()` leaves telemetry balanced.

## Verification 1 — strict / no sanitizer (run FIRST)

Exact commands (from the repo root; each build and run wrapped in
`timeout -k 5 30`):

```sh
mkdir -p tests/stdlib/allocation_failure/.run
for t in af_hooks af_containers af_string; do
  timeout -k 5 30 build/c+ build tests/stdlib/allocation_failure/$t.cp \
    --strict --sanitize=none \
    -o tests/stdlib/allocation_failure/.run/${t}_strict_none \
    > tests/stdlib/allocation_failure/.run/${t}_strict_none.build.log 2>&1
  timeout -k 5 30 tests/stdlib/allocation_failure/.run/${t}_strict_none \
    </dev/null > tests/stdlib/allocation_failure/.run/${t}_strict_none.out \
    2> tests/stdlib/allocation_failure/.run/${t}_strict_none.err
done
timeout -k 5 30 build/c+ build tests/stdlib/allocation_failure/af_json_file.cp \
  --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o tests/stdlib/allocation_failure/.run/af_json_file_strict_none \
  > tests/stdlib/allocation_failure/.run/af_json_file_strict_none.build.log 2>&1
timeout -k 5 30 tests/stdlib/allocation_failure/.run/af_json_file_strict_none \
  </dev/null > tests/stdlib/allocation_failure/.run/af_json_file_strict_none.out \
  2> tests/stdlib/allocation_failure/.run/af_json_file_strict_none.err
```

Results — all four builds rc=0, all four runs rc=0, all runtime stderr empty:

| Test | Final stdout line |
| --- | --- |
| `af_hooks` | `ALLOC_FAIL 30 0` |
| `af_containers` | `ALLOC_FAIL 58 0` |
| `af_string` | `ALLOC_FAIL 26 0` |
| `af_json_file` | `ALLOC_FAIL 8 0` |

**Strict/no-sanitizer: 4/4 passed, 122 checks, 0 failures, 0 timeouts.**

Note: the `af_json_file` build emits harmless clang warnings that `-lyyjson`
/ `-Lbuild/ext` are unused during the compile step (the library is linked in a
later step). They appear only in the build log; runtime stderr is empty.

## Verification 2 — UBSan (run AFTER strict/no-sanitizer)

Exact loop, with the documented command-line-only toolchain workaround:

```sh
for t in af_hooks af_containers af_string; do
  timeout -k 5 30 build/c+ build tests/stdlib/allocation_failure/$t.cp \
    --strict --sanitize=undefined -DCPLUS_EXIT_ARITHMETIC=110 \
    -o tests/stdlib/allocation_failure/.run/${t}_ubsan \
    > tests/stdlib/allocation_failure/.run/${t}_ubsan.build.log 2>&1
  timeout -k 5 30 tests/stdlib/allocation_failure/.run/${t}_ubsan \
    </dev/null > tests/stdlib/allocation_failure/.run/${t}_ubsan.out \
    2> tests/stdlib/allocation_failure/.run/${t}_ubsan.err
done
timeout -k 5 30 build/c+ build tests/stdlib/allocation_failure/af_json_file.cp \
  --strict --sanitize=undefined -DCPLUS_EXIT_ARITHMETIC=110 \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o tests/stdlib/allocation_failure/.run/af_json_file_ubsan \
  > tests/stdlib/allocation_failure/.run/af_json_file_ubsan.build.log 2>&1
timeout -k 5 30 tests/stdlib/allocation_failure/.run/af_json_file_ubsan \
  </dev/null > tests/stdlib/allocation_failure/.run/af_json_file_ubsan.out \
  2> tests/stdlib/allocation_failure/.run/af_json_file_ubsan.err
```

Results — all four builds rc=0, all four runs rc=0, all runtime stderr empty
(no UBSan diagnostic):

| Test | Final stdout line |
| --- | --- |
| `af_hooks` | `ALLOC_FAIL 30 0` |
| `af_containers` | `ALLOC_FAIL 58 0` |
| `af_string` | `ALLOC_FAIL 26 0` |
| `af_json_file` | `ALLOC_FAIL 8 0` |

**UBSan: 4/4 passed, 122 checks, 0 failures, 0 timeouts.**

### Toolchain workaround — evidence and scope

A plain `--strict --sanitize=undefined` build initially failed for **every**
test, including an unrelated existing one:

```sh
timeout -k 5 30 build/c+ build tests/stdlib/deep_collections_alloc.cp \
  --strict --sanitize=undefined -o /tmp/af_probe_ubsan
# rc=1
# /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23:
#   error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
# error: failed to compile instrumented C+ runtime
```

`--verbose` shows the backend compiling the external core runtime with
`-I .../c-plus-stdlib/runtime/include`; this repo's `runtime/include/cplus/cplus.h`
predates `CPLUS_EXIT_ARITHMETIC`, which is defined as `110` in the core header
(`c-plus-core/runtime/include/cplus/cplus.h:85`). This is a pre-existing
environment/version mismatch, not a defect in this suite. Supplying the missing
enum value with `-DCPLUS_EXIT_ARITHMETIC=110` on the build command line is
sufficient; **no file outside this directory was changed**. `run.sh` tries the
plain UBSan build first and applies this fallback only when that exact error is
seen, printing a `TOOLCHAIN-FALLBACK` line.

## Verification 3 — combined bounded runner

```sh
sh tests/stdlib/allocation_failure/run.sh; echo "RUNNER_RC=$?"
```

```
ok       af_containers (strict-none): ALLOC_FAIL 58 0 (0 documented skip reason(s))
TOOLCHAIN-FALLBACK af_containers (ubsan): -DCPLUS_EXIT_ARITHMETIC=110 (external core/runtime header mismatch)
ok       af_containers (ubsan): ALLOC_FAIL 58 0 (0 documented skip reason(s))
deterministic af_containers: strict-none == ubsan stdout
ok       af_hooks (strict-none): ALLOC_FAIL 30 0 (0 documented skip reason(s))
TOOLCHAIN-FALLBACK af_hooks (ubsan): -DCPLUS_EXIT_ARITHMETIC=110 (external core/runtime header mismatch)
ok       af_hooks (ubsan): ALLOC_FAIL 30 0 (0 documented skip reason(s))
deterministic af_hooks: strict-none == ubsan stdout
ok       af_json_file (strict-none): ALLOC_FAIL 8 0 (2 documented skip reason(s))
TOOLCHAIN-FALLBACK af_json_file (ubsan): -DCPLUS_EXIT_ARITHMETIC=110 (external core/runtime header mismatch)
ok       af_json_file (ubsan): ALLOC_FAIL 8 0 (2 documented skip reason(s))
deterministic af_json_file: strict-none == ubsan stdout
ok       af_string (strict-none): ALLOC_FAIL 26 0 (0 documented skip reason(s))
TOOLCHAIN-FALLBACK af_string (ubsan): -DCPLUS_EXIT_ARITHMETIC=110 (external core/runtime header mismatch)
ok       af_string (ubsan): ALLOC_FAIL 26 0 (0 documented skip reason(s))
deterministic af_string: strict-none == ubsan stdout

MODE             PASSED   FAILED  SKIPPED     CHECKS SKIP-REASONS
strict-none           4        0        0        122          2
ubsan                 4        0        0        122          2
allocation failure: 8 passed, 0 failed, 0 timed-out, 244 checks, 4 documented skip reasons (4 file(s) x 2 modes)
documented SKIPs: af_json_file af_json_file
RUNNER_RC=0
```

SHA-256 of stdout (identical in both modes for each test — determinism evidence):

| Test | SHA-256 (both modes) |
| --- | --- |
| `af_hooks` | `cf7658fa055343dc21efabe1b4720526387e84d54ba5355998ffc559cee70a67` |
| `af_containers` | `720be93b1d5fc0565261245eb67c0abeb8549db847bcee73fec131832f11937e` |
| `af_string` | `a6a1e7859285182d5f892a59ba61438551420f39bf8b1424a33cb1046df75c71` |
| `af_json_file` | `0aa49f6c611c6fcdb96b2a69efe6f0245550bde98605bd21eacebeaca7ec09e3` |

Binaries and logs are written to `tests/stdlib/allocation_failure/.run/` and
are regenerated on every run; they are not source artifacts.

## Documented SKIPs (with reasons)

1. `json_alloc_failure_hook` — `json.hp` passes a NULL `yyjson_alc` to
   `yyjson_read_opts()` and `yyjson_mut_doc_new()`. yyjson's allocator is
   per-call with no global hook and `json.hp` exposes no allocator setter, so
   `stdlib_alloc_fail_at/after` cannot reach its allocations; no honest OOM test
   can be fabricated (`DEEP_JSON_CONTRACT.md` §3). Positive probe:
   `aj_json_hook_unreachable` and `aj_json_no_checked_alloc`.
2. `file_alloc_failure_hook` — `file.hp`'s constructor and `file_temp()` use
   `fopen()`/`tmpfile()` directly and perform no `stdlib_alloc`/`stdlib_calloc`,
   so there is no allocation path for the hooks to reach
   (`DEEP_FILE_CONTRACT.md` decision 9). Positive probe: `aj_file_open`,
   `aj_file_io`, `aj_file_cleaned`.

## Defects and corrections

**No library defect was reproduced.** Every reachable failure point returned
`-1` with unchanged state, balanced telemetry after cleanup, and a successful
reset retry; no fatal, hang, or sanitizer diagnostic was observed.

Corrections made *in the test fixtures* while authoring (no header/API change):

1. `af_hooks`: read `stdlib_alloc_seq()` **before** `stdlib_alloc_fail_reset()`
   — reset clears `seq`, so reading it afterwards incorrectly returned 0.
2. `af_hooks`: the second "reallocation" probe initially pushed at size 4 of
   capacity 64, which needs no growth. It now fills to capacity 64 first, so
   the injected failure lands on a genuine 64 → 128 reallocation.
3. `af_string`: the `copy_from` accounting check initially asserted full
   `alloc_count == free_count` while the two source/destination buffers were
   still live. It now asserts the exact failure-point accounting
   (`alloc_count == 2`, `free_count == 0`, `alloc_failures >= 1`); full balance
   is asserted after `drop()`.
4. `run.sh`: UBSan builds need the command-line-only fallback for the external
   core/runtime header mismatch described above. This is an environment fix,
   not a library or test-expectation change.
