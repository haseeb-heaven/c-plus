# ADV1000 — deterministic adversarial scenarios against the checked alloc/free APIs

Subagent deliverable on branch `agent/modern-stdlib`. **File ownership was
restricted to `tests/adversarial_1000/alloc/`**; no compiler, runtime, header,
archive, or `.gitignore` outside this directory was modified. No `git add`/commit.

## Files

| File | Purpose |
|---|---|
| `adv1000_alloc.c` | The 1000-scenario deterministic C99 harness (source of truth). |
| `run.sh` | Rebuilds, verifies symbols with `nm`, runs under `timeout 120`, writes log + digest. |
| `.gitignore` | Ignores `results/` (the task requires the log to be gitignored). |
| `results/scenarios.log` | 1000 `S ...` lines + 1 `ADV1000 ...` summary line (generated, gitignored). |
| `results/scenarios.log.sha256` | SHA-256 digest of the log (generated, gitignored). |
| `results/build.stderr`, `results/stderr.log`, `results/adv1000_alloc` | Build stderr, run stderr, host binary (generated, gitignored). |

## Scope and symbol verification (`nm`)

Command (exact):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
/usr/bin/nm -gU build/safe/libcplus_safecrt.a | grep -E '_?(malloc_s|calloc_s|realloc_s|free_s|free_checked_s|aligned_alloc_s|cplus_safe_ptr_is_live|cplus_safe_ptr_size|cplus_safe_ptr_was_freed|cplus_safe_alloc_fail_after|cplus_safe_alloc_fail_reset)$' | sort
```

Result — all 11 required symbols are defined (`T`) in `libcplus_safecrt.a`
(macOS prefixes them with `_`; `cplus_safe_ptr_was_freed` is the `ptr_was_freed`
scope item):

```
0000000000000000 T _malloc_s
00000000000001e4 T _calloc_s
0000000000000324 T _aligned_alloc_s
0000000000000470 T _realloc_s
0000000000000a5c T _free_checked_s
0000000000000bf4 T _free_s
0000000000000de8 T _cplus_safe_ptr_is_live
0000000000000e90 T _cplus_safe_ptr_was_freed
0000000000000f60 T _cplus_safe_ptr_size
00000000000014c8 T _cplus_safe_alloc_fail_after
0000000000001504 T _cplus_safe_alloc_fail_reset
```

Archives used (pre-existing, **not** rebuilt or modified by this task):
`build/safe/libcplus_safecrt.a` (458520 B) and
`build/libc/libcplus_hardened.a` (276128 B). Host: arm64-apple-darwin27.2.0,
Apple clang 16.0.0.

## Exact build and run commands / results

Driver: `sh tests/adversarial_1000/alloc/run.sh`

Compile (exact, from driver; `-Wall -Wextra` produced **zero** diagnostics):

```sh
/usr/bin/clang -std=c99 -O1 -Wall -Wextra \
  -I/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/stdlib/safe \
  tests/adversarial_1000/alloc/adv1000_alloc.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread \
  -o tests/adversarial_1000/alloc/results/adv1000_alloc
# BUILD_STATUS=0, build.stderr empty
```

Run (exact, from driver):

```sh
timeout 120 tests/adversarial_1000/alloc/results/adv1000_alloc \
  > tests/adversarial_1000/alloc/results/scenarios.log \
  2> tests/adversarial_1000/alloc/results/stderr.log
# RUN_STATUS=0, stderr.log empty (SafeCRT logging defaults to OFF)
```

Accounting / digest:

```sh
wc -l < results/scenarios.log                  # 1001
shasum -a 256 results/scenarios.log
# bb3016c7e5fef98446796dfeb946d247dd43a0d97619942cdacf460b3f51a02d
```

Final summary line in `results/scenarios.log`:

```
ADV1000 alloc 1000 644 1000 1000 0
```

Fields after `ADV1000 alloc 1000`:
`refused=644`, `canary_ok=1000`, `state_ok=1000`, `failures=0`.
Process exit code `0` (nonzero on any failure by construction).

Per-case line format: `S <id> <api> <case> <rc> <canary_ok> <state_ok>`.
`<rc>` is the **observed** result: for pointer-returning APIs it is `0` when a
non-NULL block came back, otherwise the `errno` set by the call (`22` EINVAL,
`84` EOVERFLOW on this macOS host, `12` ENOMEM); for `free_checked_s` / `free_s` it is the observed
return value / errno (`0` OK, `22` EINVAL refusal).

## Required breaking cases (pinned to ids 1–4, all safely refused)

```
S 1 malloc_s overflow_size_max 22 1 1
S 2 calloc_s count_size_overflow 84 1 1
S 3 realloc_s inject_fail_atomic 12 1 1
S 4 free_checked_s double_free 22 1 1
```

* `n = SIZE_MAX` → `malloc_s` returns NULL / EINVAL; also exercised for `realloc_s`
  (`overflow_atomic`) and `aligned_alloc_s` (`bad_size_max`).
* `count * size` overflow → `calloc_s` returns NULL / `errno = EOVERFLOW`
  (84 on this host; the separate `CPLUS_SAFE_EOVERFLOW` constant in
  `safe_error.h` is 75 — the API sets the system errno, which the harness
  checks).
* realloc failure with original still live → `fail_after(0)` makes `realloc_s`
  return NULL / ENOMEM; the original pointer stays live with unchanged tracked
  size and intact canary bytes (atomicity), verified before any cleanup.
* double free → second release refused (`free_checked_s` returns EINVAL / `free_s`
  propagates EINVAL), `cplus_safe_ptr_was_freed` true, `cplus_safe_ptr_is_live`
  false, guard canary intact (no heap corruption).

## Determinism and coverage checks

```sh
timeout 120 ./results/adv1000_alloc > results/scenarios.rerun.log
shasum -a 256 results/scenarios.log results/scenarios.rerun.log
cmp results/scenarios.log results/scenarios.rerun.log   # byte-identical
# both digests: bb3016c7e5fef98446796dfeb946d247dd43a0d97619942cdacf460b3f51a02d
```

* ids are contiguous `1..1000` (0 mismatches); all 1000 `(id, api, case)`
  triples are distinct; 0 lines with `canary_ok != 1` or `state_ok != 1`.
* Fixed LCG seed `0xC0FFEEAD1000`, reseeded per id with
  `BASE_SEED ^ (id * 0x9E3779B97F4A7C15)`; **no** time/stdin/randomness.
* Parameter ranges: sizes `0..65536`, counts `0..1024`, alignments `1..256`,
  injection point `k = 0..7` successes before refusal.
* Family coverage (counts include pinned ids): `inject_fail_atomic` 73,
  `count_size_overflow` 72, `overflow_atomic` 71, `malloc/aligned/calloc/
  alloc_fail_after` success families 71 each, plus invalid-free / interior /
  double-free / bad-alignment variants (full table in the log).
* 14 families assigned round-robin so every family is exercised; all payload
  parameters come from the fixed-seed PRNG.

## Contract details discovered while writing the harness (evidence, not guesses)

1. `calloc_s(count, size)` sets **both** arguments to 1 if **either** is zero,
   so e.g. `calloc_s(0, 15495)` tracks a 1-byte block with `elem_size == 1`
   (id 745 initially exposed a harness-side OOB read; fixed to match the
   documented contract — this was a harness bug, not a library defect).
2. `fail_after(k)` allows the next `k` allocation calls to succeed, then refuses
   every later one with ENOMEM until `fail_reset()`; overflow / invalid-pointer
   checks happen *before* the injection counter is consumed.
3. `realloc_s` checks size and ownership before injection, so an injected
   failure leaves the original block live and untouched.
4. A refused `free_checked_s` sets `errno = EINVAL`; `free_s` delegates to it.

## Honest limitations — evidence, not a proof of all memory safety

* This is **runtime evidence over 1000 deterministic scenarios**, not a proof
  of all memory-safety properties; it cannot establish absence of bugs outside
  the generated space (e.g. other sizes/counts/alignments/interleavings).
* The allocator's recent-free cache is bounded to 1024 slots: a very old double
  free whose address was not reused is diagnosed as an *invalid* free rather
  than a double free. Interior/stale-alias detection is generation-based and
  also has documented limits (see `stdlib/safe/safe_alloc.h`).
* This run is single-threaded and uses the default (non-ASan/UBSan) archives.
  Canary guards are ordinary live heap blocks checked byte-for-byte after each
  refusal; they detect corruption of those blocks, not every possible silent
  heap mutation. `calloc_s` zero-verification is bounded to a 4096-byte head
  window plus a 64-byte tail window per scenario for runtime.
* No compiler/runtime/header/archive was changed; findings describe the
  pre-existing archived implementation only.
