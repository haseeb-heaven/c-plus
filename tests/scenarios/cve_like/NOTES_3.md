# Cases 6 / 7 / 8 — safe variants and honest evidence

Scope: only `case_06_safe.c`, `case_07_safe.c`, `case_08_safe.c` and this file
were created. No other repository file was modified, and nothing was staged or
committed.

## Bottom line (read this first)

These are **ownership / lifetime bugs**, not missing bounds checks. No library
can make a raw dangling pointer safe: C pointers carry no lifetime or alias
metadata. In every safe variant the actual fix is **caller-side ownership
discipline**. The C+ libraries contribute only (a) ordinary-C allocation /
free / memset entry points that are overflow- and failure-checked and tracked
in a live-allocation table, and (b) explicit provenance / checked-free APIs
used here as diagnosis and defence in depth. **We do not claim the library
fixes raw pointer aliasing or a raw write through a stale pointer.**

## Baseline evidence (original files, AddressSanitizer)

Compile (per task): `clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`
plus `-g`. Inputs: case 6 `0 1` (evict slot 0, then read the surviving alias in
slot 1), case 7 `3 3 0` (two `op 3` releases), case 8 `3` (middle node).

| Case | Input | Exit | First ASan diagnostic line | Summary |
|---|---|---:|---|---|
| `case_06.c` | `0 1` | 134 | `==4856==ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000958 at pc ...` (`WRITE of size 8`, `case_06.c:11`, freed at `case_06.c:9`) | `SUMMARY: AddressSanitizer: heap-use-after-free case_06.c:11 in main` |
| `case_07.c` | `3 3 0` | 134 | `==5114==ERROR: AddressSanitizer: attempting double-free on 0x6060000001a0 in thread T0:` (`case_07.c:8`) | `SUMMARY: AddressSanitizer: double-free case_07.c:8 in main` |
| `case_08.c` | `3` | 134 | `==5177==ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000990 at pc ...` (`WRITE of size 16` via `__asan_memset`, `case_08.c:9`, freed at `case_08.c:9`) | `SUMMARY: AddressSanitizer: heap-use-after-free case_08.c:9 in main` |

Toolchain caveat (warning, not a program result): `/usr/bin/clang` is Apple
clang 16 whose ASan runtime aborts at startup on this host
(`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189
"((!asan_init_is_running)) != (0)"`, exit 134) even for a two-line `hello.c`.
The real baseline reports above were therefore produced with Homebrew clang
23.1.6 using the **same** `-fsanitize=address -g -isysroot
/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk` flags. No `-DPLAIN_LIBC`
or library archive is involved in the baseline runs.

## Safe results (both verification layers)

Build command (exact, from the task) with `/usr/bin/clang -O1 -g -std=c99`,
include paths `-I. -Istdlib/safe -Ilibc -Ilibc/install-include
-Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include`:

* Layer A (SafeCRT): link `build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread`.
* Layer B (`-DPLAIN_LIBC`): link `build/libc/libcplus_hardened.a -lm -pthread` only.

| Case | Input | Layer A result | Layer B result | Outcome line (identical in both) |
|---|---|---|---|---|
| 06 | `0 1` | exit 0 | exit 0 | `outcome: SAFE \| single-owner eviction cleared every alias; stale read target absent (slot cleared); checked tracked allocation active` |
| 07 | `3 3 0` | exit 0 | exit 0 | `outcome: SAFE \| token slot cleared on first release; repeat op 3 no-op; checked re-release of stale token REFUSED by library; checked tracked allocation active` |
| 08 | `3` | exit 0 | exit 0 | `outcome: SAFE \| removed=1 nodes; saved next before free + unlinked + scrubbed while live; checked tracked allocation active` |

Extra checks: additional inputs (`1 1`, `0 0`, `3 3 3 1 3 2 0`, head tag `1`,
tail tag `5`) also exit 0 in both layers. The shared gate
`run_cve_like.sh` feeds a single `1` to every safe variant, so `case_06_safe.c`
tolerates a single integer (`rd` defaults to slot 1) while keeping the
baseline's two-integer protocol intact when both values are supplied. Compiling each safe file with
`-Wall -Wextra -pedantic` produced **no warnings** in either layer; compiling
the safe variants with Homebrew clang ASan (archives linked, `detect_leaks=0`)
gave exit 0 and **zero** `ERROR: AddressSanitizer` lines for all three.

## Per-case analysis

| Case | Baseline result | Safe result | Caller-side changes (the actual fix) | Library mechanism (not a fix for raw aliasing) | Classification |
|---|---|---|---|---|---|
| 06 — aliased cache slot | ASan heap-use-after-free, `WRITE of size 8` at `case_06.c:11` through slot 1 after slot 0 was freed; exit 134 | exit 0, no ASan error, stale read skipped | Single-owner rule: `cache_clear_aliases()` NULLs **every** slot aliasing the victim before release; `heavy_blob` member cleared before the owner is freed; local `p` scoped out so the slots are the only owners; allocation results checked for NULL | `malloc/calloc/free` are ordinary names mapped by `safe_c.h` / `cplus_libc_safe.h` to tracked, overflow-checked entry points; `cplus_safe_ptr_is_live` / `cplus_alloc_is_live` used only as defence-in-depth provenance query | **NOT-FIXABLE-BY-LIBC** for the aliasing/UAF write: the library cannot revoke a raw alias. PARTIAL for tracked-free refusal of any repeat/invalid release. |
| 07 — repeated session token release | ASan double-free at `case_07.c:8`; exit 134 | exit 0; repeat `op 3` is a no-op; checked re-release of the saved stale token reports `REFUSED` | Owning slot is set to NULL **before** the release (`session_drop_token`), so a second `op 3` sees NULL; optional `op 2` touches the token only while owned; the saved stale copy is diagnostic-only and never dereferenced | `free_checked_s` / `cplus_free_checked` refuse a double or invalid free without calling the host allocator; `malloc` is tracked through the ordinary-C mapping | **PARTIAL**: the checked API detects and refuses repeat release when used; a raw `free(p)` twice remains UB. The lexical fix (clearing the slot) is caller-side. |
| 08 — write to freed list node | ASan heap-use-after-free, `WRITE of size 16` from `memset` at `case_08.c:9`; exit 134 | exit 0; node removed and list fully released | Save `next` before freeing, unlink from owner (`prev->next`/`head`), scrub with `memset` **while the node is live**, advance via the saved pointer, no post-free access; partial-list cleanup uses the same saved-next discipline | Ordinary `malloc/free/memset` mapped to tracked allocation, checked free and capacity-checked memset; `cplus_safe_ptr_is_live` / `cplus_alloc_is_live` query is diagnosis only | **NOT-FIXABLE-BY-LIBC** for the raw post-free write; capacity tracking cannot validate a freed pointer. PARTIAL for checked free / live-capacity memset. |

## Warnings and limits

* Do not read these safe variants as "the library repaired a dangling pointer."
  In all three cases the UB disappears because the **caller stops retaining or
  using a stale reference**. Library checks only diagnose misuse (case 7's
  refusal, case 6/8's liveness queries) and give checked allocation semantics.
* Case 7's `stale_diagnostic` is intentionally retained to demonstrate the
  library's refusal path. It is never dereferenced and is passed only to the
  checked free API; the checked API sets its local slot to NULL and leaves the
  host allocator untouched. A raw `free` on that value would still be UB.
* Provenance queries track only blocks from the selected checked allocator.
  Mixing layers (e.g. allocating via `malloc_s` and querying via
  `cplus_alloc_is_live`) would give meaningless results; each safe file uses
  the query matching its own header. Address reuse after free can defeat a
  liveness query; generation/ownership APIs are the correct tool for that, and
  they still require caller discipline.
* The `calloc` "spray" in case 06 is kept only to preserve the baseline's shape;
  it is explicitly **not** the fix and must not be cited as heap-grooming
  mitigation.
* Both archives were rebuilt at 13:48 during this work; all six verification
  builds above were re-run against the freshly built archives. The libc build
  directory showed a concurrent rebuild (temporary `.o.tmp` file), so no build
  artifact source was edited or deleted by this agent.
* Baseline ASan toolchain defect (Apple clang 16 runtime CHECK failure) is a
  host issue; it is not evidence about any of the three programs.
