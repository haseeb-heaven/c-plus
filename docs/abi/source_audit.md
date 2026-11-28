# Source-level ABI audit — recent stdlib / install-include header changes

**Task:** 4 (parallel audit run). **Strict file ownership:** this document is the
only file created or modified by this agent. No source edits, no `git add`, no
`git commit`, and no edits to `tests/abi/` were performed.

Verdict vocabulary: `NO_ABI_IMPACT` = no change to data layout, exported symbols,
signatures, calling convention, or global data; `POTENTIAL_ABI_IMPACT` = a new
surface whose stability matters or a host-dependent risk; `ABI_CHANGE` = an
existing consumer-visible layout/symbol/signature changed.

---

## 0. Baseline resolution — important scope finding

At task start `git rev-parse HEAD` was
`cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` (`cbcea57`), exactly the requested
baseline. It was re-checked at the end of the audit and had **not** changed: no
later commits appeared.

Consequences, stated plainly:

1. `git diff cbcea57..HEAD -- 'stdlib/*.hp' 'libc/install-include/**'` is
   **empty**. The pristine working tree also had **no tracked modifications**
   in scope (`git status --short` showed only untracked `scratch/bugs51..97.cp`
   and, later, other agents' untracked `tests/abi/c_to_cp/`, `cp_to_c/`,
   `legacy_c/` directories). A strictly literal reading of "since cbcea57"
   therefore yields **zero changed headers and zero ABI changes**.
2. `cbcea57` is a merge of `5bb3b3a` (code side) and `e04f6e1`
   (`origin/agent/modern-stdlib`, docs side). For every in-scope path the merge
   result is byte-identical to `5bb3b3a`:
   `git diff --stat 5bb3b3a cbcea57 -- 'stdlib/*.hp' 'libc/install-include/**'`
   is empty; the opposite side (`e04f6e1`) only contributes documentation.
3. To make the audit useful rather than vacuous, the body below audits the
   **recent header-change series that produced the current in-scope headers**,
   i.e. the five ancestor commits that actually touched them:

   | commit | subject | in-scope paths touched |
   | --- | --- | --- |
   | `1438bf2` | Fix file::read_line exact-fit classification | `stdlib/file.hp` |
   | `5fe387d` | Fix HIGH/MEDIUM safety findings | `libc/install-include/cplus_libc.h`, `stdlib/file.hp`, `stdlib/io.hp`, `stdlib/thread.hp` |
   | `1df837f` | ABI-preserving runtime safety, P0 Safe CRT hardening | `stdlib/error.hp`, `stdlib/memory.hp` (new) |
   | `9a061ae` | Wire new C23/atomic sources into builds | `libc/install-include/cplus_libc.h` |
   | `5bb3b3a` | Native C+ test conversion, strict-header adaptation | 5 new install-include headers, `cplus_libc.h`, and `stdlib/{collections,error,file,io,log,memory,string}.hp` |

   These commits are **ancestors of** `cbcea57`, not newer commits. This is the
   only deviation from the literal task wording and it is disclosed here.

---

## 1. Verdict summary

| In-scope file | Change class | Verdict |
| --- | --- | --- |
| `stdlib/memory.hp` (new, 254 lines) | new `static inline` free functions + comments only | **NO_ABI_IMPACT** |
| `stdlib/error.hp` | new inline functions; rewritten inline bodies (overflow check, division emulation, error-message copy) | **NO_ABI_IMPACT** |
| `stdlib/collections.hp` | rewritten inline method bodies (index wrapping, guarded loads) | **NO_ABI_IMPACT** |
| `stdlib/file.hp` | rewritten inline `file::read_line` body | **NO_ABI_IMPACT** |
| `stdlib/io.hp` | rewritten inline `read_line` / `stdlib_io_token` bodies | **NO_ABI_IMPACT** |
| `stdlib/log.hp` | rewritten inline `log_sink::write` body | **NO_ABI_IMPACT** |
| `stdlib/string.hp` | rewritten inline method bodies (alias-window widening, terminated writes) | **NO_ABI_IMPACT** |
| `stdlib/thread.hp` | added `#include <stdint.h>`; opaque-argument cast in inline body | **NO_ABI_IMPACT** |
| `libc/install-include/cplus_libc.h` | 3 new `#include`s; 6 new C function declarations (`nextup`/`nextdown`); earlier 1 new declaration (`cplus_swprintf_checked`) | **NO_ABI_IMPACT** (additive symbols) |
| `libc/install-include/cplus_stdatomic.h` (new, 240 lines) | new `typedef enum`, new 1-byte `typedef struct`, ~180 new C function declarations | **NO_ABI_IMPACT** (additive; new struct size is a new stability obligation) |
| `libc/install-include/cplus_math_c23_farith.h` (new) | 8 new C function declarations behind a capability macro | **NO_ABI_IMPACT** (additive, host-gated) |
| `libc/install-include/cplus_math_c23_hypot3_f16.h` (new) | 1 + 4 new C function declarations (`_Float16` gated) | **NO_ABI_IMPACT** (additive, host-gated) |
| `libc/install-include/cplus_math_c23_next.h` (new) | 6 new C function declarations; **not** included by `cplus_libc.h` | **NO_ABI_IMPACT** |
| `libc/install-include/cplus_c23_remainders.h` (new) | macros and comments only; zero declarations and zero symbols | **NO_ABI_IMPACT** |

Cross-check outside the header diff (latent, host-dependent): see §4 — one
`POTENTIAL_ABI_IMPACT` finding regarding duplicate `cplus_fadd` definitions with
different return types in `libc/src/math/`.

**No `ABI_CHANGE` was found.**

---

## 2. Detailed findings

### 2.1 `stdlib/memory.hp` — new file — NO_ABI_IMPACT

- New include guard `CPLUS_STD_MEMORY_HP` at `stdlib/memory.hp:62-63`.
- All new definitions are `static inline` free functions:
  `stdlib_mem_capacity` (`:73`), `stdlib_mem_copy` (`:164`),
  `stdlib_mem_transfer` (`:230`), `stdlib_mem_release` (`:247`), plus the
  check/token/move/set/borrow helpers between them. The file ends at `:254`.
- The 1df837f commit message explicitly states no fat pointers, no pointer
  redefinition and no runtime change; the header comment at `:3-12` confirms
  pointers stay raw and capacities are read from the existing metadata table.
- ABI classification: **inline-only logic + new internal-linkage functions**.
  No struct/class data member, field order/type, exported symbol, signature,
  calling convention or global data was changed. The 5bb3b3a follow-up only
  expanded comments (`:32-40`, `:163`) — no code-layout change.
- Verdict: **NO_ABI_IMPACT**.

### 2.2 `stdlib/error.hp` — NO_ABI_IMPACT

- New additive inline function `stdlib_realloc(void *, size_t)` at
  `stdlib/error.hp:422` (added by 1df837f). Same signature style as existing
  `stdlib_alloc`/`stdlib_calloc`; no existing declaration altered.
- New additive inline helper `stdlib_safe_div(size_t, size_t)` at
  `stdlib/error.hp:462`.
- `stdlib_calloc` overflow test rewritten to `__builtin_mul_overflow` at
  `:400` (was a division-based check); the recorded `elem_size` and the
  `cplus_alloc_meta` layout are untouched.
- Consumers of the safe-division helper: `stdlib_alloc_length` at `:507` and
  `stdlib_alloc_query_length` at `:551` — body-only substitutions of `a / b`.
- `error_fail_cat` message copy rewritten from a `while` loop to
  `strlen` + `__builtin_memcpy` at `:688`; the 255-byte truncation and trailing
  NUL are preserved, so the `error` object's `text` field contents and size are
  unchanged.
- ABI classification: all changes are **inline-only logic**. No data members,
  field order/types, exported symbols, signatures, calling convention or globals.
- Verdict: **NO_ABI_IMPACT**. (Semantic note only: `stdlib_safe_div` is an exact
  shift-and-subtract division only for quotients < 2^16, as its comment states;
  that is a correctness boundary, not an ABI one.)

### 2.3 `stdlib/collections.hp` — NO_ABI_IMPACT

- `stdlib_ring_copy`: new `space == 0` guard at `:104` and guarded element load
  via `__builtin_memcpy(&v, items + pos, sizeof(v))` at `:112`.
- `deque` methods: conditional-subtraction index computation replaces `% space`
  at `:736`, `:747`, `:760`, `:771`, `:787`.
- `queue` methods: same substitution at `:1041` and `:1053`.
- The `deque`/`queue` class data members (`items`, `head`, `used`, `space`) are
  not present in any diff hunk; only method bodies changed. The transformation
  is arithmetically equivalent for the documented invariants (`head < space`,
  `used <= space`).
- ABI classification: **inline-only logic**, no layout/symbol changes.
- Verdict: **NO_ABI_IMPACT**.

### 2.4 `stdlib/file.hp` — NO_ABI_IMPACT

- `file::read_line` NUL stores replaced by `char nul` + `__builtin_memcpy` at
  `:84-85`, `:135-136`, `:149-150`; byte loads at `:109`, `:122`, `:144`.
- Exact-fit peek logic (from `1438bf2`, refined in `5bb3b3a`) lives wholly in
  this method body; the `file` class members and the member function signature
  are unchanged.
- ABI classification: **inline-only logic**.
- Verdict: **NO_ABI_IMPACT**.

### 2.5 `stdlib/io.hp` — NO_ABI_IMPACT

- `stdlib_io_token`: byte/NUL stores via memcpy locals at `:43`, `:51`.
- `read_line`: same pattern at `:79`, `:112`, `:127`, `:136`, `:141`.
- No signature, member, or global changes.
- ABI classification: **inline-only logic**.
- Verdict: **NO_ABI_IMPACT**.

### 2.6 `stdlib/log.hp` — NO_ABI_IMPACT

- `log_sink::write`: `keys[i]` / `values[i]` reads replaced by memcpy into
  locals `const char *key` and `long long value` at `:133-140`. The `snprintf`
  format string and the `text` buffer size are unchanged.
- ABI classification: **inline-only logic**.
- Verdict: **NO_ABI_IMPACT**.

### 2.7 `stdlib/string.hp` — NO_ABI_IMPACT

- `string::compare`: new local `char first` at `:183`, read via
  `__builtin_memcpy(&first, other, 1)` at `:190`; same return values.
- `string::append` alias-window widened from `chars + count` to
  `chars + space` at `:275` — a logic fix for interior pointers after `clear()`;
  it reads the existing `space` member and does not add or reorder members.
- `substr` / split write the terminator via a `char term` local at `:341`,
  `:353`, `:372`, `:383` (one byte + NUL semantics preserved).
- ABI classification: **inline-only logic**.
- Verdict: **NO_ABI_IMPACT**.

### 2.8 `stdlib/thread.hp` — NO_ABI_IMPACT

- `5fe387d` added `#include <stdint.h>` at `:25` and changed the opaque
  `pthread_create` argument to `(void *)(uintptr_t)arg` at `:165` (comment at
  `:162`). Bit-preserving cast of an opaque pointer; `pthread_t handle` member
  and `thread::start` signature unchanged.
- ABI classification: **inline-only logic**.
- Verdict: **NO_ABI_IMPACT**.

### 2.9 `libc/install-include/cplus_libc.h` — NO_ABI_IMPACT

- `9a061ae` added three includes of new headers at `:21`, `:25`, `:26`
  (`cplus_stdatomic.h`, `cplus_math_c23_farith.h`,
  `cplus_math_c23_hypot3_f16.h`).
- `9a061ae` added six **new** C declarations at `:165-166`:
  `cplus_nextup/f/l`, `cplus_nextdown/f/l`.
- `5fe387d` had earlier added one **new** declaration,
  `cplus_swprintf_checked`, at `:278`.
- The public metadata struct `cplus_alloc_meta` at `:38-44` has fields
  `byte_size` (:39), `elem_size` (:40), `elem_count` (:41), `generation` (:42),
  `is_live` (:43) — **none of the audited diff hunks touches this definition**.
- ABI classification: **new exported symbol declarations only**; no existing
  symbol removed, renamed, or re-typed; no calling-convention or global-data
  change. Adding declarations to an installed header does not change the ABI of
  existing consumers (though it increases the public namespace — see §4 for a
  symbol-collision cross-check).
- Verdict: **NO_ABI_IMPACT**.

### 2.10 New install-include C23/atomic headers — NO_ABI_IMPACT

- `cplus_stdatomic.h`: `typedef enum cplus_memory_order` at `:30-37`;
  **new** `typedef struct cplus_atomic_flag { unsigned char __cplus_value; }`
  at `:40` (1 byte, matching C11 `atomic_flag`), init macro at `:41`; ~180 new
  C function declarations at `:50-235` (per-type atomic ops, flag ops at
  `:228-231`, fences at `:234-235`). Header comment at `:3-15` states no host
  `_Atomic` type is exposed and no existing header layout is changed. All
  operation signatures take `int order`, not the enum type, so the enum's
  implementation-defined size does not cross the ABI boundary. All functions
  are `extern "C"` (`:25-27`, `:237-239`), i.e. C linkage/calling convention by
  construction. **New** struct size (1 byte) becomes a forward stability
  obligation, not a break.
- `cplus_math_c23_farith.h`: new declarations at `:81-91` guarded by
  `CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND` (`:46-54`). The header asserts at
  `:25-29` that the stale `double`-returning declarations live in
  `libc/src/math/*.c`, not in any previously installed header — confirmed by
  `git log -S'cplus_fadd' -- 'libc/install-include/**'`, which returns only
  `5bb3b3a` (the introduction of this file).
- `cplus_math_c23_hypot3_f16.h`: new declarations at `:30` and `:35-46` (the
  latter gated on `__FLT16_MANT_DIG__`).
- `cplus_math_c23_next.h`: new declarations at `:26-32`. This header is **not**
  currently included by `cplus_libc.h` (the declarations were duplicated there
  at `cplus_libc.h:165-166` instead), so it does not yet alter any TU that
  includes only `cplus_libc.h`.
- `cplus_c23_remainders.h`: capability macros and blocker strings only
  (`:35-42`, `:46-61`); `:26-28` explicitly documents "macros only, no types,
  no declarations, no symbols".
- ABI classification throughout: **additive new types/declarations**; no
  existing type layout or symbol touched.
- Verdict: **NO_ABI_IMPACT** for every file in this group.

---

## 3. Verification performed (read-only)

- `tests/abi/fingerprints/`: at task start it did **not** exist. During the
  audit the directory was created concurrently by another agent (timestamp
  09:27 local), but it contains **0 files** at the time of writing. No
  fingerprint comparison was therefore possible; no fingerprints were created
  or edited by this agent.
- I did **not** run `tests/abi/run_abi_compat.sh` or
  `tests/abi/run_abi_layouts.sh`, because both write generated artifacts under
  `build/abi/`, which is outside my strict file ownership. A different agent
  did regenerate them concurrently; I only read the results:
  - `build/abi/compat/results.log` ends with `RESULT: abi_compat: 69 passed, 0 failed`.
  - `build/abi/compat/layouts.out` reports `abi_compat_layouts: 80/80 layouts match`
    with **0** `MISMATCH` lines.
  - `build/abi/host.out` / `build/abi/cplus.out` (older, 09:05) are byte-identical
    (`diff -q` clean) and report `abi_layouts: 70/70 layouts match`.
  These are independent confirmation that the captured `sizeof`/`offsetof`
  constants still hold; they are **not** evidence I generated myself.
- Direct read-only check of the most relevant captured layout:
  `tests/abi/abi_compat_layouts.c` expects `sizeof(cplus_alloc_meta) == 40`,
  align 8, field offsets `byte_size=0`, `elem_size=8`, `elem_count=16`,
  `generation=24`, `is_live=32`. The current definition at
  `libc/install-include/cplus_libc.h:38-44` still yields exactly that layout
  (4 × 8-byte fields + 4-byte `int` + tail padding), and no audited hunk touched
  it. The `cplus_safe_alloc_meta` mirror is also unchanged by these commits.
- **Symbol-coverage gap (honest limitation):** the three expected-symbol lists
  `tests/abi/abi_compat_expected_libc_symbols.txt`,
  `..._compat_symbols.txt`, `..._safecrt_symbols.txt` contain **none** of the
  newly declared names (`cplus_nextup*`, `cplus_nextdown*`, `cplus_atomic_*`,
  `cplus_fadd`/`fsub`/`fmul`/`fdiv`/`hypot3*`, `cplus_swprintf_checked`).
  The "no exported-symbol impact" verdict is therefore based on source-level
  inspection of the header diffs (all additions, no removals/retyping), **not**
  on the ABI suite proving those new symbols are present in the archive. No new
  symbol was observed being removed or re-typed.
- No `sizeof`/`offsetof` recompilation was performed by this agent for the same
  file-ownership reason; the existing captured-constant outputs above are the
  closest feasible verification.

---

## 4. Cross-check outside the header diff (latent, host-dependent)

This finding is **not** a change in any audited header, but it is directly
referenced by one of them and is worth recording:

- `libc/install-include/cplus_math_c23_farith.h:81` declares
  `float cplus_fadd(double, double);` while
  `libc/src/math/math_c23.c:186` defines
  `double cplus_fadd(double x, double y)` under
  `#if CPLUS_C23_HAS_BUILTIN(__builtin_fadd)`, and
  `libc/src/math/math_c23_farith.c:72` defines the authoritative `float`
  version. Both translation units are build inputs
  (`libc/CMakeLists.txt:71-73`, `tests/libc/build_libc.sh`).
  On this host the builtin guard is false (the `float` definition is the one
  compiled, consistent with the capability macro evaluated to 1 at
  `cplus_math_c23_farith.h:46-54` and with the clean link of the ABI suite), so
  there is no current symbol conflict. On a host where `__builtin_fadd` exists,
  two C definitions of the same unmangled symbol with different return types
  would collide — a latent **POTENTIAL_ABI_IMPACT** in source wiring, outside
  this task's file scope. It does **not** change the verdict for the header
  addition itself, which is purely additive.

---

## 5. Final verdicts

- **Literal diff since `cbcea57`: empty** — no commits after the baseline, no
  tracked in-scope working-tree changes; merge side `e04f6e1` is docs-only on
  these paths.
- **Recent ancestor header series (5 commits): 14 in-scope files audited —
  14 × `NO_ABI_IMPACT`, 0 × `POTENTIAL_ABI_IMPACT`, 0 × `ABI_CHANGE`**
  (the one `POTENTIAL_ABI_IMPACT` in §4 is outside the header diff and is
  host-dependent/latent).
- No struct/class data member, field order/type, existing exported symbol,
  function signature, calling convention, or global data was changed by any
  audited header diff. All changes are additive declarations or inline-only
  logic.
