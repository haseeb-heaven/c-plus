# Adversarial container / stale-view regression family — evidence notes

Status: **report only**. No header, compiler, build script, expectation fixture,
or other repository file was modified. Only the three files listed below were
created. Every number in this document comes from a command that was actually
executed; no pass is inferred from a build alone and no failure was relabelled.

## 1. Files created

| File | Purpose | Checks |
|---|---|---:|
| `tests/stdlib/regressions/adv_containers.cp` | 7-container growth/collision/tombstone/reserve/failure-recovery stress | 70 |
| `tests/stdlib/regressions/adv_stale_views.cp` | `vector::slice` / `string_view` invalidation, metadata-only refusal | 70 |
| `tests/stdlib/regressions/ADV_CONTAINERS_NOTES.md` | this evidence record | — |

Both `.cp` files are self-checking: 70 `<tag> <1|0>` lines followed by a footer
`ADV_CONTAINERS 70 0` / `ADV_STALE_VIEWS 70 0`. Exit code is 1 when any check is
0. All randomness is a fixed-seed LCG, so stdout is byte-identical on every run
and in every mode (verified by sha256 below).

## 2. Environment / provenance

- Repo: `c-plus-stdlib`, branch `agent/modern-stdlib`.
- Driver: `./build/c+` → `C+ 0.1.0`, host Clang/LLVM backend.
- Host: macOS arm64. Backend pinned with `export CPLUS_CLANG=/usr/bin/clang`.
- PATH prefix on every invocation:
  `export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH`
- Every build and every run wrapped in `timeout 30`. **No timeout occurred.**
- Binaries/logs written to `build/adv-regressions/` (untracked build output).

## 3. Coverage

`adv_containers.cp`:

- **intmap** — 48 collision-heavy keys (low 8 hash bits zero), 3000-op LCG
  differential stress against `present[]`/`val[]` shadow, full delete + refill
  (tombstone reuse), value recheck, `grow(8)` no-op / `grow(1024)` real /
  `grow(SIZE_MAX)` recoverable refusal, `clear()`, allocation failure at all
  three rebuild allocations, `fail_after(0)`, retry success, double `drop()`.
- **intset** — same collision pool, 3000-op LCG differential stress against a
  presence shadow, tombstone reuse, grow/overflow, two rebuild allocation
  failures, retry, double drop.
- **strmap** — 32 FNV-colliding owned-string keys, 3000-op LCG differential
  stress against value shadow, NULL put/get/contains/remove recoverable,
  `clear()` + reuse, 4th-allocation (owned key copy) failure atomicity, retry,
  double drop.
- **deque** — 4000-op LCG push_back/push_front/pop_front/pop_back stream vs a
  shadow sequence, reserve no-op/grow/overflow with value recheck, ring-copy
  allocation failure at capacity and recovery, clear/reuse/drop.
- **list** — 3000-op LCG mixed-end stream vs shadow, bounds refusal, node
  allocation failure and recovery, clear/double-drop.
- **stack** — 4000-op LCG push/pop/top stream vs shadow, 8→16 growth boundary
  with injected failure and recovery, clear/reuse/drop.
- **queue** — 5000-op LCG stream vs shadow, deterministic wrap-around sequence
  (8 push / 5 pop / 5 push, verified by draining), ring-copy failure at
  capacity, value drain check, retry, clear/reuse/drop.
- **cross-family leak check** — telemetry enabled, all seven containers used
  including an injected failure, all dropped, `alloc_count == free_count`.

`adv_stale_views.cp`:

- **live slice** — size, `valid()`/`stale()`, `at_checked` values and bounds,
  `sub()` window/clamp, push_back-without-realloc does not invalidate, fixed
  size snapshot, empty view refusal.
- **slice invalidation** — `reserve()` reallocation, `erase`, `pop_back`,
  `clear`, `insert`, `shrink_to_fit`, `copy_from`, `drop`: each leaves
  `stale()==1`, `valid()==0`, `at_checked()==-1` while `size()` stays unchanged;
  repeated refusals; stale parent's `sub()` returns an empty non-stale slice
  that also refuses; a live child inherits staleness.
- **live string_view** — size, pointer identity, `equals`, `is_valid_utf8`,
  direct construction, metadata gate permits a live buffer, fixed-window
  in-place append.
- **string_view invalidation** — reallocating `append` and `reserve` and
  `drop()`: recorded pointer/size are unchanged snapshots; the checked
  allocator's generation no longer matches the old pointer; the new buffer has
  a different generation. All reads after invalidation go through
  `view_permit(ptr, size, gen)`, which consults **only** pointer, size and
  generation metadata and never dereferences the pointer. Repeated refusals
  (3x and 25x) are checked separately for both view kinds.
- **degenerate views** — `string_view(NULL,0)` and `string_view(NULL,3)` are
  inert and refused by the metadata gate; invalid UTF-8 and embedded NUL windows.

## 4. Verification — exact commands and results

Verification order followed the instruction: strict/no-sanitizer first, then
default and UBSan. All commands below were run from the repository root.

### 4.1 Strict, no sanitizer (run first)

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export CPLUS_CLANG=/usr/bin/clang

timeout 30 ./build/c+ build tests/stdlib/regressions/adv_containers.cp \
  --strict --sanitize=none -o build/adv-regressions/adv_containers.strict-none
# build stdout/stderr: (empty)   BUILD_RC=0
timeout 30 build/adv-regressions/adv_containers.strict-none
# RUN_RC=0   stderr bytes: 0   footer: ADV_CONTAINERS 70 0

timeout 30 ./build/c+ build tests/stdlib/regressions/adv_stale_views.cp \
  --strict --sanitize=none -o build/adv-regressions/adv_stale_views.strict-none
# build stdout/stderr: (empty)   BUILD_RC=0
timeout 30 build/adv-regressions/adv_stale_views.strict-none
# RUN_RC=0   stderr bytes: 0   footer: ADV_STALE_VIEWS 70 0
```

First strict build attempt failed with `[CPLUS-LIFETIME] variable 'got' is
uninitialized` at out-parameter reads; all out-parameter variables were
explicitly initialised (`got = 0, rc = 0`) and the final strict build is clean.

### 4.2 Default mode

```sh
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_containers.cp \
  -o build/adv-regressions/adv_containers.default
# BUILD_RC=0   build log: (empty)
timeout 30 build/adv-regressions/adv_containers.default
# RUN_RC=0   stderr bytes: 0   footer: ADV_CONTAINERS 70 0

timeout 30 ./build/c+ build tests/stdlib/regressions/adv_stale_views.cp \
  -o build/adv-regressions/adv_stale_views.default
# BUILD_RC=0   build log: (empty)
timeout 30 build/adv-regressions/adv_stale_views.default
# RUN_RC=0   stderr bytes: 0   footer: ADV_STALE_VIEWS 70 0
```

### 4.3 UBSan (`--strict --sanitize=undefined`)

```sh
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_containers.cp \
  --strict --sanitize=undefined -o build/adv-regressions/adv_containers.ubsan
# BUILD_RC=0   build log: (empty)
timeout 30 build/adv-regressions/adv_containers.ubsan
# RUN_RC=0   stderr bytes: 0   footer: ADV_CONTAINERS 70 0

timeout 30 ./build/c+ build tests/stdlib/regressions/adv_stale_views.cp \
  --strict --sanitize=undefined -o build/adv-regressions/adv_stale_views.ubsan
# BUILD_RC=0   build log: (empty)
timeout 30 build/adv-regressions/adv_stale_views.ubsan
# RUN_RC=0   stderr bytes: 0   footer: ADV_STALE_VIEWS 70 0
```

### 4.4 Default mode + UBSan (`--default --sanitize=undefined`)

```sh
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_containers.cp \
  --default --sanitize=undefined -o build/adv-regressions/adv_containers.default_ubsan
# BUILD_RC=0   build log: (empty)
timeout 30 build/adv-regressions/adv_containers.default_ubsan
# RUN_RC=0   stderr bytes: 0   footer: ADV_CONTAINERS 70 0

timeout 30 ./build/c+ build tests/stdlib/regressions/adv_stale_views.cp \
  --default --sanitize=undefined -o build/adv-regressions/adv_stale_views.default_ubsan
# BUILD_RC=0   build log: (empty)
timeout 30 build/adv-regressions/adv_stale_views.default_ubsan
# RUN_RC=0   stderr bytes: 0   footer: ADV_STALE_VIEWS 70 0
```

### 4.5 Per-mode summary

| Mode | adv_containers | adv_stale_views | Timeouts |
|---|---|---|---|
| strict-none (`--strict --sanitize=none`) | 70/70, exit 0 | 70/70, exit 0 | 0 |
| default (no flags) | 70/70, exit 0 | 70/70, exit 0 | 0 |
| ubsan (`--strict --sanitize=undefined`) | 70/70, exit 0 | 70/70, exit 0 | 0 |
| default+ubsan (`--default --sanitize=undefined`) | 70/70, exit 0 | 70/70, exit 0 | 0 |

All eight runs produced empty stderr and exit 0. Each file printed exactly 71
lines (70 unique check tags + footer); no check line ended in `0`.

### 4.6 Determinism (sha256 of stdout, all four modes and 3 repeat runs)

```
adv_containers   strict_none/default/ubsan/default_ubsan + 3 extra runs:
  007e70e79af7396cede8a357437b01d642bc31dcebef0b92f8106a946ad16cc3
adv_stale_views  strict_none/default/ubsan/default_ubsan + 3 extra runs:
  0f004969b755737e71c97336b663aab0236ec4036b71dea56efa648f87b2562d
```

## 5. Exact stdout (strict-none; byte-identical in every mode)

<details>
<summary><code>adv_containers.strict-none.stdout</code> (71 lines)</summary>

```text
ac_im_keys 1
ac_im_diff 1
ac_im_tomb_empty 1
ac_im_tomb_reuse 1
ac_im_tomb_values 1
ac_im_grow_noop 1
ac_im_grow_real 1
ac_im_grow_overflow 1
ac_im_clear 1
ac_im_oom1 1
ac_im_oom2 1
ac_im_oom3 1
ac_im_oom_retry 1
ac_im_fail_after 1
ac_im_drop 1
ac_is_keys 1
ac_is_diff 1
ac_is_tomb_empty 1
ac_is_tomb_reuse 1
ac_is_grow 1
ac_is_oom1 1
ac_is_oom2 1
ac_is_oom_retry 1
ac_is_drop 1
ac_sm_keys 1
ac_sm_diff 1
ac_sm_null_put 1
ac_sm_null_query 1
ac_sm_values_after_null 1
ac_sm_clear 1
ac_sm_reuse 1
ac_sm_oom_keycopy 1
ac_sm_oom_retry 1
ac_sm_drop 1
ac_dq_empty 1
ac_dq_diff 1
ac_dq_reserve_noop 1
ac_dq_reserve_grow 1
ac_dq_reserve_values 1
ac_dq_reserve_overflow 1
ac_dq_full 1
ac_dq_oom 1
ac_dq_oom_retry 1
ac_dq_clear 1
ac_dq_push_after_clear 1
ac_dq_drop 1
ac_ls_empty 1
ac_ls_diff 1
ac_ls_oob 1
ac_ls_oom 1
ac_ls_oom_retry 1
ac_ls_clear 1
ac_ls_drop 1
ac_st_empty 1
ac_st_diff 1
ac_st_oom 1
ac_st_oom_retry 1
ac_st_clear 1
ac_st_reuse 1
ac_st_drop 1
ac_q_empty 1
ac_q_diff 1
ac_q_wrap 1
ac_q_oom 1
ac_q_oom_values 1
ac_q_oom_retry 1
ac_q_clear 1
ac_q_reuse 1
ac_q_drop 1
ac_leak_balance 1
ADV_CONTAINERS 70 0
```

</details>

<details>
<summary><code>adv_stale_views.strict-none.stdout</code> (71 lines)</summary>

```text
av_sl_size 1
av_sl_live 1
av_sl_read 1
av_sl_bounds 1
av_sl_sub_live 1
av_sl_sub_read 1
av_sl_sub_clamp 1
av_sl_push_noinval 1
av_sl_fixed_window 1
av_sl_empty 1
av_sl_re_setup 1
av_sl_re_grow_rc 1
av_sl_re_stale 1
av_sl_re_snapshot 1
av_sl_re_refuse1 1
av_sl_re_refuse2 1
av_sl_re_refuse3 1
av_sl_re_refuse4 1
av_sl_re_nullout 1
av_sl_re_fresh 1
av_sl_re_after_drop 1
av_sl_erase 1
av_sl_pop 1
av_sl_clear 1
av_sl_insert 1
av_sl_shrink 1
av_sl_copy 1
av_sl_inherit_live 1
av_sl_inherit_stale 1
av_sl_stale_sub 1
av_sl_stale_sub_repeat 1
av_vw_size 1
av_vw_identity 1
av_vw_content 1
av_vw_utf8 1
av_vw_direct 1
av_vw_permit_live 1
av_vw_reserve_rc 1
av_vw_append_rc 1
av_vw_fixed 1
av_vw_window_not_grown 1
av_vw_re_setup 1
av_vw_re_append_rc 1
av_vw_re_moved 1
av_vw_re_snapshot 1
av_vw_re_gen_stale 1
av_vw_re_new_gen 1
av_vw_re_refuse1 1
av_vw_re_refuse2 1
av_vw_re_refuse3 1
av_vw_re_fresh 1
av_vw_rd_setup 1
av_vw_rd_reserve_rc 1
av_vw_rd_reserve_refuse 1
av_vw_rd_owner_empty 1
av_vw_rd_fields 1
av_vw_rd_gen_stale 1
av_vw_rd_refuse1 1
av_vw_rd_refuse2 1
av_vw_rd_size_zero_refuse 1
av_vw_rd_gen_zero_refuse 1
av_vw_rd_null_refuse 1
av_vw_zero 1
av_vw_zero_refuse 1
av_vw_null_len 1
av_vw_null_len_refuse 1
av_vw_bad_utf8 1
av_vw_embedded_nul 1
av_rep_slice_25 1
av_rep_view_25 1
ADV_STALE_VIEWS 70 0
```

</details>

## 6. Findings

### 6.1 Library defect

**None found.** Every check above held in all four modes: the seven containers
preserved their shadow-model values across collision-heavy growth, tombstone
reuse, reserve boundaries and injected allocation failures; both view kinds
refused every post-invalidation access exactly as documented. No header edit
was needed or made.

### 6.2 Defects in the first drafts of the tests (all fixed before the recorded runs)

These were test-logic bugs, not library defects; each is listed because a naive
read of the first failing output could be mistaken for a container failure.

1. `ac_sm_values_after_null` failed because the check compared all 32 shadow
   keys, including keys absent after the LCG stress (those correctly return
   -1 from `get`). Fixed by gating on `present[i]`.
2. `ac_q_wrap` failed because `sh_reset()` cleared the shadow without clearing
   the queue, so LCG leftovers were compared against a fresh model. Fixed with
   `qu.clear()`; a minimal standalone repro of the queue wrap sequence
   (push 0..7, pop 5, push 100..104, drain) produced the correct sequence
   `5 6 7 100 101 102 103 104`, confirming no library defect.
3. `ac_q_oom` failed because `front_checked()` was called in a loop as if it
   advanced the head. Fixed by draining with `pop()` and checking the front
   separately.
4. `ac_q_wrap` also failed a second time because the verification loop used
   the shrinking `g_seq_n` as its bound, popping only half the elements.
   Fixed by caching `total = g_seq_n` before the loop.
5. `av_vw_window_not_grown` failed because the draft called `equals()` on a
   `string_view` after the owner had reallocated — i.e. it committed the exact
   stale-pointer dereference this family is meant to avoid. Fixed by reserving
   capacity *before* taking the view so the append is in-place. The library was
   correct to reject the stale window.

### 6.3 Toolchain limitations (not library defects), with minimal repro

**A. Class methods dispatch by variable name across the whole translation
unit.** Reusing one variable name for instances of different classes makes the
compiler select the wrong overload. Minimal repro compiled with
`--strict --sanitize=none`:

```c
#include "cplus/cplus.h"
#include "stdlib/collections.hp"
static void fa(void) {
    intmap m = intmap();
    m.put(1, 2);              /* works only if fb() below is absent */
}
static void fb(void) {
    strmap m = strmap();      /* same variable name, different class */
    m.put("a", 1);
}
int main(void) { fa(); fb(); return 0; }
```

Observed diagnostic:

```
scope_probe.cp:6:20: error: [CPLUS-FFI] incompatible integer to pointer conversion passing 'int' to parameter of type 'const char *'
      m.put(1, 2);
```

Both regression files therefore give every container/view instance a globally
unique name (`im`, `im_a`, `im_b`, `iset`, `sm`, `dq`, `lst`, `stk`, `qu`,
`v_live`, `sl_live`, …).

**B. Member calls directly on a returned temporary are not lowered.**
`sl_live.sub(6, 99).size()` fails with `no member named 'size' in 'struct slice'`
because the lowering emits `slice_sub(&sl_live, 6, 99).size()`. Workaround used:
assign the temporary to a named `slice` first.

**C. Strict mode requires explicit initialisation of out-parameter variables.**
`int got; map.get(k, &got);` is reported as `[CPLUS-LIFETIME] variable 'got' is
uninitialized`; `int got = 0;` satisfies the checker.

**D. Sanitizer support.** Only `none`, `undefined` (and the default unsanitized
build) were exercised, as required. Other sanitizers were not attempted here;
separate agent evidence records driver-probe rejections for address/leak/thread/
memory/cfi on this host.

## 7. Known limitations of this family

- `string_view` has no generation counter of its own in `stdlib/string.hp`.
  Detection is therefore implemented test-side through the checked allocator's
  generation token plus pointer/size snapshot, exactly as the task specifies;
  it always refuses and never dereferences the stale pointer.
- `vector::slice::stale()` reads the owner's generation field, so the owner
  object must still be alive. Every test keeps its vector in scope, which is
  within the documented contract; a slice outliving the vector object itself is
  not detectable by the library.
- The tests are not auto-discovered by `run_deep_tests.sh` (which globs
  `deep_*.cp`). They are standalone and are invoked with the commands in §4.
