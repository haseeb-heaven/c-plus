# Allocation Wrapper Safety Audit (`AUDIT_alloc.md`)

Branch: `agent/modern-stdlib`. READ-ONLY audit — no source or test changes were made.

## 0. Scope, method, and how to read this document

Sources inspected (line numbers below refer to these exact files):

| File | Layer |
| --- | --- |
| `libc/src/stdlib/alloc.c` | Hardened libc allocation + metadata table |
| `libc/src/stdlib/stdlib_c17.c` | C17 aligned allocation / process entry points |
| `stdlib/safe/src/safe_alloc.c` | SafeCRT checked allocation + metadata table |
| `libc/install-include/cplus_libc.h` | Public hardened-libc declarations |
| `stdlib/safe/safe_alloc.h` | Public SafeCRT declarations + stated limits |
| `libc/src/string/bounded.c` | Capacity-checked ordinary C memory/string ops |

Test evidence used: `tests/libc/test_alloc_attack.c`, `tests/libc/test_alloc_metadata.c`,
`tests/adversarial/alloc_attack_NOTES.md`, `tests/crt/test_safe_alloc.c`,
`tests/crt/unit/{test_malloc_s.c,test_calloc_s.c,test_realloc_s.c,test_alloc_metadata_s.c}`,
`tests/crt/asan/test_alloc_asan.c`, `tests/crt/fuzz/fuzz_allocator.c`,
`tests/compile_fail/{alloc_size_oob.c,alloc_return_oob.c}`,
`docs/patches/agent1-len-managed-allocators.md`, `docs/libc/CONTRACT_STATUS.md`.

Every "check" claimed below is present in the cited source line. Nothing is inferred
from tests alone where the source does not implement the behaviour.

### 0.1 Enforcement taxonomy (explicitly separated)

| Bucket | What it actually covers here | What it does NOT cover |
| --- | --- | --- |
| **Runtime-enforced** | Side-table membership (`meta_find`/`tab_find`), generation compare, overflow guards, NULL/out-param rejection, zero-size → 1-byte normalization, injection counters, bounded recent-free classification, host `free` only after table says live, capacity checks in `bounded.c`, `errno` on refusal. | Pointer provenance, lifetime of raw aliases, interior-object identity, C abstract-machine UB, host allocator mixing. |
| **Compiler-enforced (Agent 1)** | Only `__builtin_object_size(p,1)` via `CPLUS_LIBC_OBJ_SIZE` in `libc/install-include/cplus_libc_safe.h:33` and `__attribute__((format(...)))` on printf/scanf wrappers in `cplus_libc.h:120-132`. The C+ compiler's own `cplus_alloc` extent tracking is exercised by `tests/compile_fail/alloc_size_oob.c` and `alloc_return_oob.c`. | **No `alloc_size` attribute exists on any exported wrapper** (`cplus_libc.h:24-28`, `safe_alloc.h:51-61`). `malloc_s`/`cplus_malloc`/`cplus_calloc` etc. are invisible to static extent analysis. `docs/patches/agent1-len-managed-allocators.md:22,39-50` lists this as an open hand-off (`.len` not lowered for these allocators). |
| **Caller responsibility** | Matching deallocator for each API, re-deriving aliases after `realloc`, calling `cplus_alloc_check_generation`/`cplus_safe_ptr_check_generation` before using a possibly-stale alias, not freeing library memory with host `free` (and vice versa), checking return values, NUL-termination of strings in `bounded.c`, choosing `cplus_libc_set_unknown_capacity_mode(1)` when unknown capacity is unacceptable. | Cannot be shifted to these wrappers without changing their C signatures/allocator model. |

### 0.2 Backends at a glance

- `cplus_malloc`, `cplus_calloc`, `cplus_realloc`, `cplus_free`, `cplus_free_checked` → **host**
  `malloc`/`calloc(1,total)`/`realloc`/`free` plus a separate internal open-addressed
  `meta_slot` table (`alloc.c:225,261,309,366`; table at `alloc.c:24-29,104-153`).
- `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `free_checked_s` → **host**
  `malloc`/`calloc(count,size)`/`realloc`/`free` plus a separate internal
  `slot` table (`safe_alloc.c:173,207,286,342`; table at `safe_alloc.c:13-18,46-86`).
- `aligned_alloc_s` → **host `posix_memalign`**, but the result **is tracked** in the
  SafeCRT table (`safe_alloc.c:232,238`).
- `cplus_aligned_alloc` → **host `aligned_alloc` / `_aligned_malloc`**, deliberately
  **NOT tracked** (`stdlib_c17.c:13-23,53-71`).
- All query/metadata/generation/transfer/injection APIs → **internal table only**;
  they never touch the host allocator.

---

## 1. Per-function audit — hardened libc (`libc/src/stdlib/alloc.c`, `stdlib_c17.c`)

Legend for severity: **H** = HIGH, **M** = MEDIUM, **L** = LOW, **DL** = DOCUMENTED LIMIT.
Finding IDs (`F-01` … `F-20`) are detailed in §5 and used in the top-5 in §7.

| # | Function (signature) | Backend | Checks actually present in source | Unsafe cases NOT handled | Sev | Evidence | Recommendation (no allocator rewrite) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `void *cplus_malloc(size_t size)` — `alloc.c:216` | host `malloc(actual)`, `actual = size==0?1:size` | Injection (`should_fail`, :221); NULL result → `errno=ENOMEM` (:226-229); `meta_reserve_one` failure → `free(p)`, `ENOMEM` (:231-236) | No `RSIZE_MAX_MEM` cap (`SIZE_MAX` is passed to host; host refuses). Zero size is a silent 1-byte block, not C23-defined. Host `free(p)` bypasses the table → false "live" + accounting leak (F-02). | H (F-02), L (F-15) | `test_alloc_attack.c:73-75,182-200,624-641`; `CONTRACT_STATUS.md:56-59` | Document the 1-byte rule in `cplus_libc.h`; add explicit `cplus_alloc_forget(ptr)` for the host-free case; do not change allocation path. |
| 2 | `void *cplus_calloc(size_t nmemb, size_t size)` — `alloc.c:242` | host `calloc(1,total)` | Overflow `nmemb!=0 && size>SIZE_MAX/nmemb` → `errno=ENOMEM` (:246-249); zero operands → `total=1,elem=1` (:250-256); injection (:257); OOM and reserve failure (:262-271) | Overflow reports `ENOMEM`, while `calloc_s` reports `EOVERFLOW` for the identical condition (F-08). Same host-free bypass / stale accounting as #1 (F-02). Zero-size product is never overflow-checked because the `nmemb==0||size==0` branch wins. | M (F-08), H (F-02) | `alloc.c:246-256`; `safe_alloc.c:193-198`; `test_alloc_attack.c:78-92` | Align `errno` to `EOVERFLOW` (or document the deliberate difference in `cplus_libc.h`); same `forget` fix for F-02. |
| 3 | `void *cplus_realloc(void *ptr, size_t size)` — `alloc.c:278` | host `realloc`; `ptr==NULL` → `cplus_malloc` | Unowned pointer → `EINVAL`, no host free, counters bumped (:290-298); injection + `meta_reserve_one` **before** host call, failure preserves owner (:302-306); ABA generation guard re-checks `old_gen` after lock is reacquired (:317-334); zero size → 1 byte (:286) | Host `realloc` runs with `g_meta_lock` **released** (:308-314): concurrent stale-pointer use is UB; guard only protects bookkeeping (F-04). In the ABA `else` branch `meta_insert(p,…)` can overwrite a slot another thread created at `p` (:333). `realloc(p,0)` does **not** free (F-15). | DL/H (F-04), M (F-15) | `alloc.c:308-337`; `test_alloc_attack.c:94-109,124-151,361-387`; `asan_sc_06/07/08`; `CONTRACT_STATUS.md:65-68` | Keep ABA guard; document lock-free host-call window; document `realloc(p,0)==realloc(p,1)`; add a stress test that hammers the ABA `else` branch. |
| 4 | `void cplus_free(void *ptr)` — `alloc.c:373` | host `free` only via `cplus_free_checked` | NULL no-op (:376-377); delegates to checked API (:378) | Return value of `cplus_free_checked` is discarded: double/invalid free is a silent no-op except `errno` + counters (F-11). Host pointer refused → leak. Stale alias whose address was reissued → **frees the new live block** (F-01). | H (F-01), M (F-11) | `alloc.c:373-379`; `test_alloc_attack.c:221-231`; `CONTRACT_STATUS.md:85-89` | Add `cplus_free_checked_gen(void **, unsigned long long)` and recommend it for alias-bearing code; make `cplus_free` log refusals. |
| 5 | `int cplus_free_checked(void **inout)` — `alloc.c:340` | host `free` after table lookup | `inout==NULL` → `EINVAL` (:344-347); `*inout==NULL` → 0 (:349-350); live slot → `meta_remove` + host `free` (:353-356,365-368); recent → `double_free_count`; else → `invalid_free_count` (:357-361); `*inout=NULL` **always** (:364) | `meta_find` does **not** compare generation → stale alias at a reissued address frees the new owner (F-01). Recent cache is keyed by address only: a fresh host `malloc` reusing a just-freed cplus address is refused and counted as a double free (Finding A/A2, F-05). `*inout` is nulled even on refusal, losing the caller's only handle to a still-live host block (F-11). When the 1024-entry table is full, `recent_add` overwrites the bucket's first slot (:98-100) → misclassification (F-06). | H (F-01), M (F-05/F-06/F-11) | `alloc.c:340-371,85-101`; `test_alloc_attack.c:556-613`; `alloc_attack_NOTES.md:62-98`; `CONTRACT_STATUS.md:48-55` | Add generation-aware variant; downgrade address-only "double free" claims to `invalid_free`/`unknown` unless corroborated; port a tagged recent cache. |
| 6 | `int cplus_alloc_get_meta(const void*, cplus_alloc_meta*)` — `alloc.c:413` | table only | Zeroes `*out` first (:415-421); NULL ptr/out → `EINVAL` (:422-425); exact-base lookup; `elem_count = byte_size/elem_size` (:428-435); not live → `EINVAL` (:437-440) | Interior pointers intentionally report non-live (F-16). After a host-side `free`, stale metadata remains in the table (F-02). | DL (F-16/F-02) | `alloc.c:413-442`; `test_alloc_attack.c:263-276,287-296` | Document exact-base semantics in `cplus_libc.h`; no code change required. |
| 7 | `size_t cplus_alloc_remaining_capacity(const void*)` — `alloc.c:386` | table only | NULL → `(size_t)-1` (:389); exact base match first (:391-398); interior `p>base && p<=base+size` → remaining bytes (:399-408); no match → `-1` (:410-411) | `b + byte_size` can wrap `uintptr_t` for a block at the top of the address space (theoretical, F-19). Arbitrary addresses that happen to fall inside a live range are reported as valid capacity — provenance is not established (F-16). O(`g_cap`) linear scan per call. | L/DL (F-19/F-16) | `alloc.c:386-412`; `bounded.c:24-29`; `cplus_libc_safe.h:13-17` | Keep; document that it is a diagnostic capacity hint, not a provenance proof. |
| 8 | `int cplus_alloc_is_live(const void*)` — `alloc.c:444` | table only | Mutex-protected exact-base `meta_find` (:445-449); NULL → 0 | False "live" after host-side `free` (F-02); false dead for interior pointers (F-16). | DL/H (F-02) | `alloc.c:444-450`; `test_alloc_attack.c:624-641` | Document; add `forget` API to clear stale entries. |
| 9 | `size_t cplus_alloc_size(const void*)` — `alloc.c:452` | table only | Mutex-protected lookup; 0 when not tracked (:453-461) | Same stale/ interior limitations as #8. | DL | `alloc.c:452-461`; `test_alloc_attack.c:262-269` | Document. |
| 10 | `size_t cplus_alloc_elem_size(const void*)` — `alloc.c:463` | table only | Mutex-protected lookup; 0 when not tracked (:464-472) | Same as #8; `malloc` path always records 1, so this is meaningful mainly for `cplus_calloc`. | DL | `alloc.c:463-472`; `test_alloc_attack.c:24-33` | Document. |
| 11 | `size_t cplus_alloc_length(const void*)` — `alloc.c:474` | table only | `byte_size / (elem_size?elem_size:1)` under lock (:475-485) | Same as #8. Division uses stored elem_size, never caller-supplied. | DL | `alloc.c:474-485`; `test_alloc_metadata.c:25,88` | Document. |
| 12 | `int cplus_alloc_query_length(const void*, size_t, size_t*)` — `alloc.c:487` | table only | Zeroes `*out_count` (:490-491); NULL ptr/out → `EINVAL` (:492-495); `elem_size==0` means "use recorded" (:499); not found → `EINVAL`, count 0 (:504-507) | Same as #8; the caller-supplied `elem_size` is not validated against the allocation's actual element type (caller responsibility). | DL | `alloc.c:487-509`; `test_alloc_attack.c:348-358`; `alloc_attack_NOTES.md:121-123` | Document the `elem_size==0` rule (already done in NOTES). |
| 13 | `int cplus_alloc_check_generation(const void*, unsigned long long)` — `alloc.c:511` | table only | NULL ptr or generation 0 → 0 (:514-515); exact-base compare under lock (:516-521) | 64-bit `g_gen` wrap to 0 is theoretical (:159); generation is global, not per-address, so correlation with a specific alias is caller responsibility. | L (F-18) | `alloc.c:511-522,159`; `test_alloc_attack.c:311-327` | Keep; note wrap in docs. |
| 14 | `int cplus_transfer_ownership(void **dst, void **src)` — `alloc.c:524` | table only | Rejects NULL params, `*src==NULL`, `*dst!=NULL`, `dst==src` (:526-529); `meta_find(*src)` required (:531-536); bumps generation, nulls `*src` (:537-539) | Membership is the only ownership test — a stale alias at a reissued address passes and transfers the **new** block (F-01). No cross-thread ownership token. | H (F-01) | `alloc.c:524-542`; `test_alloc_attack.c:311-346`; `test_alloc_metadata.c:40-47` | Allow an optional expected-generation parameter; document single-threaded transfer discipline. |
| 15 | `size_t cplus_alloc_live_count(void)` / `cplus_alloc_live_bytes(void)` — `alloc.c:544/552` | table only | Mutex-protected reads of `g_live` / `g_live_bytes` (:545-549,554-558) | Permanent inflation after host-side `free` (Finding C2, F-02). Values are `size_t`; a bookkeeping bug would wrap silently (none observed). | M (F-02), L (F-17) | `alloc.c:544-558`; `test_alloc_attack.c:426-440,633-641`; `alloc_attack_NOTES.md:100-113` | Keep; pair with `forget` API so callers can correct accounting. |
| 16 | `unsigned long cplus_alloc_double_free_count(void)` / `cplus_alloc_invalid_free_count(void)` — `alloc.c:560/568` | table only | Mutex-protected reads (:561-565,569-573) | Counters are process-global and heuristic (F-05/F-06): a fresh host pointer can increment `double_free_count`, and a real double free can increment `invalid_free_count` when the recent table is full. | M (F-05/F-06) | `alloc.c:560-574,85-101`; `test_alloc_attack.c:556-613` | Rename/document as "heuristic classification counters"; add a `recent_full` counter. |
| 17 | `void cplus_alloc_fail_after(long count)` / `cplus_alloc_fail_reset(void)` — `alloc.c:576/582` | injection state | `count<0` → `g_inject=-1` (disable) (:578); reset → `-1` (:584); `inject_fail_locked` implements `<0 off / ==0 fail / >0 decrement` (:199-206) | Negative-argument semantics **differ** from `cplus_safe_alloc_fail_after` (F-09). State is global, not thread-local, and there is no getter (F-10). | M (F-09/F-10) | `alloc.c:199-214,576-586`; `safe_alloc.c:532-536` | Document negative == disable here; add `cplus_alloc_injection_active()` getter; align or clearly differentiate the two APIs. |
| 18 | `#define cplus_alloc_len(ptr)` — `cplus_libc.h:66-67` | macro over `cplus_alloc_size` | NULL-safe; divides by `sizeof(*(ptr))` | Silent 0 for a freed/stale pointer (no generation check); not lowered by the compiler's `.len` property (F-07). | M (F-07) | `cplus_libc.h:66-67`; `docs/patches/agent1-len-managed-allocators.md:22,32-37` | Add `alloc_size` attributes + Agent 1 lowering; document macro as diagnostic. |
| 19 | `void *cplus_aligned_alloc(size_t alignment, size_t size)` — `stdlib_c17.c:53` | **host `aligned_alloc`** (POSIX/glibc) or **`_aligned_malloc`** (Windows) — **untracked** | Rejects `alignment==0`, non-power-of-two, not a multiple of `sizeof(void*)`, and `size%alignment!=0` with `EINVAL` (:57-62); short-circuit avoids modulo by zero (:55-56); never rounds size up (:29) | Memory is **not** in any C+ table → all metadata/liveness queries return 0 and `cplus_free` refuses it (leak). On Windows it **must** be freed with `_aligned_free`, not `free`. Under `CPLUS_MALLOC_TRACKING`, `free` is macro-redefined to `cplus_free` (`cplus_libc.h:69-78`) → mixing hazard (F-03). `size==0` is host-defined; no `RSIZE_MAX_MEM` cap (F-20). | H (F-03), L (F-20) | `stdlib_c17.c:13-23,53-71`; `asan_sc_11` (`test_alloc_asan.c:320-348`); `cplus_libc.h:69-78` | Add namespaced `cplus_aligned_free` that calls `_aligned_free` on Windows / `free` elsewhere; document that this API is untracked. |

Non-allocation C17 entry points `cplus_quick_exit`, `cplus_at_quick_exit`, `cplus__Exit`
(`stdlib_c17.c:73-86`) are direct host forwards with logging only; they have no
allocation-safety surface and are out of scope beyond recording that fact.

---

## 2. Per-function audit — SafeCRT (`stdlib/safe/src/safe_alloc.c`)

| # | Function (signature) | Backend | Checks actually present in source | Unsafe cases NOT handled | Sev | Evidence | Recommendation (no allocator rewrite) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 20 | `void *malloc_s(size_t size)` — `safe_alloc.c:158` | host `malloc(req)`, `req = size==0?1:size` | `size>RSIZE_MAX_MEM` → `EINVAL` (:161-166); injection (:167); host OOM → `ENOMEM` (:173-179); `finish_alloc` bookkeeping OOM → `free(p)`, `ENOMEM` (:133-148) | Host `free` of a `malloc_s` pointer bypasses table (F-02); zero → 1 byte (F-15); no `alloc_size` attribute (F-07). | H (F-02), M (F-07) | `safe_alloc.h:40-41`; `test_malloc_s.c:7-15`; `test_safe_alloc.c:9-19` | Same `forget`/doc fixes as #1; add compiler attribute. |
| 21 | `void *calloc_s(size_t count, size_t size)` — `safe_alloc.c:183` | host `calloc(req_count, req_size)` | `req_count*req_size` overflow and `>RSIZE_MAX_MEM` → `errno=EOVERFLOW` (:193-198); zero operands normalized to 1×1 (:189-192); injection/OOM (:201-213); records `elem_size` via `finish_alloc` (:214) | Same host bypass / stale alias (F-01/F-02). Difference from `cplus_calloc` `errno` documented in F-08. | H (F-01/F-02) | `safe_alloc.c:183-215`; `test_calloc_s.c:7-14`; `test_safe_alloc.c:31-33` | Align with libc-layer `errno` or document. |
| 22 | `void *realloc_s(void *ptr, size_t size)` — `safe_alloc.c:241` | host `realloc` | `size>RSIZE_MAX_MEM` → `EINVAL`, owner preserved (:247-252); `ptr==NULL` → `malloc_s` (:253-255); unowned ptr → `EINVAL`, no free (:259-266); injection before host call (:271-277); `tab_reserve_one` before host call (:279-284); ABA generation guard (:295-312); zero → 1 byte (:243) | Host `realloc` runs with `g_lock` released (:285-293) — concurrent stale-pointer use is UB (F-04). ABA `else` branch `tab_insert(p,…)` may overwrite another thread's slot (:310). `realloc_s(p,0)` does not free (F-15). | DL/H (F-04), M (F-15) | `safe_alloc.c:241-317`; `test_realloc_s.c:7-21`; `test_alloc_metadata_s.c:72-92`; `asan_sc_08` | Keep guard; document lock window and zero-size rule; add ABA stress test. |
| 23 | `void free_s(void *ptr)` — `safe_alloc.c:352` | host `free` via `free_checked_s` | NULL no-op (:354-357); delegates (:358); double-counts stats for non-OK (:359) | Discards refusal detail (only counters/log). Stale alias at reissued address frees new block (F-01). Direct-mapped recent cache false positive (F-05/F-06). | H (F-01), M (F-05/F-06) | `safe_alloc.c:319-360`; `test_safe_alloc.c:15-17`; `test_alloc_metadata_s.c:94-109` | Add generation-aware checked variant; port tagged recent cache. |
| 24 | `int free_checked_s(void *ptr)` — `safe_alloc.c:319` | host `free` after table lookup | NULL → `CPLUS_SAFE_OK` (:322-325); live slot → `tab_remove` + `free` (:328-342); direct-mapped recent hit → `double_free` stat, else `invalid_free` stat (:332-338); both return `CPLUS_SAFE_EINVAL` and set `errno=EINVAL` (:333-338,345-347) | **No generation check** → frees a new block through a stale alias (F-01). **Direct-mapped 1024-slot `g_recent`** (:30,42,111,117) → real double free can be counted as invalid free on collision (Finding B still present in this layer, F-06); host pointer reusing a freed address is misclassified (F-05). Unlike `cplus_free_checked`, it takes the pointer **by value**, so it cannot null the caller's alias (no leak-inducing side effect, but no help preventing a second attempt). | H (F-01), M (F-05/F-06) | `safe_alloc.c:30-42,319-350`; `CONTRACT_STATUS.md:48-55`; `alloc_attack_NOTES.md:87-98` (same root cause at libc layer) | Port the open-addressed `recent_find`/`recent_add`/`recent_remove` pattern from `alloc.c:69-101` into `safe_alloc.c`; add generation-aware variant. |
| 25 | `void *aligned_alloc_s(size_t alignment, size_t size)` — `safe_alloc.c:217` | host `posix_memalign` → **tracked** via `finish_alloc` | Rejects `alignment<sizeof(void*)`, non-power-of-two, `size>RSIZE_MAX_MEM` with `EINVAL` (:220-226); injection (:227-231); `posix_memalign` failure → `ENOMEM` (:232-237); zero → 1 byte (:219) | Does **not** enforce C11 `size % alignment == 0` (unlike `cplus_aligned_alloc`, F-14); safe because `posix_memalign` accepts it, but a contract divergence. **Opposite deallocator from `cplus_aligned_alloc`**: this one must be freed with `free_s`, the other with host `free`/`_aligned_free` (F-03). | M (F-14), H (F-03) | `safe_alloc.c:217-239`; `safe_alloc.h:49`; `test_alloc_metadata_s.c:62-70`; `asan_sc_11:320-348` | Add the `size%alignment` check for C11 conformance; loudly document the two-allocator distinction. |
| 26 | `int cplus_safe_alloc_get_meta(...)` — `safe_alloc.c:394` | table only | Zeroes out struct (:396-402); NULL ptr/out → `EINVAL` (:403); exact-base lookup, `elem_count = size/es` (:404-414) | Same interior/stale limitations as #6. | DL | `safe_alloc.c:394-416`; `test_alloc_metadata_s.c:31-37` | Document. |
| 27 | `int cplus_safe_ptr_is_live(const void*)` — `safe_alloc.c:418` | table only | Mutex-protected `tab_find` (:419-423) | False live after host free (F-02); false dead for interior pointers (F-16). | DL/H | `safe_alloc.c:418-424`; `test_alloc_metadata_s.c:26,40` | Document; `forget` API. |
| 28 | `int cplus_safe_ptr_was_freed(const void*)` — `safe_alloc.c:426` | table + direct-mapped recent cache | NULL → 0 (:428); `(tab_find==NULL) && g_recent[idx]==ptr` (:429-432) | Same bounded-cache false positives/negatives as F-05/F-06: after 1024 other frees, an actually-freed pointer reports 0; a fresh host pointer at a recycled address reports 1. | M (F-05/F-06) | `safe_alloc.c:426-433`; `safe_alloc.h:33-37` | Port tagged open-addressed cache; document best-effort. |
| 29 | `size_t cplus_safe_ptr_size` / `_elem_size` / `_count` — `safe_alloc.c:435/445/455` | table only | Mutex-protected lookups; 0 when not tracked (:436-466); count uses recorded elem_size | Same as #27. | DL | `safe_alloc.c:435-466`; `test_alloc_metadata_s.c:27-29,54-59` | Document. |
| 30 | `size_t cplus_safe_remaining_capacity(const void*)` — `safe_alloc.c:367` | table only | NULL → `-1` (:370); exact base first (:372-379); interior range check (:380-390); no match → `-1` (:392) | Same theoretical `uintptr` wrap and provenance gap as #7 (F-19/F-16). O(`g_cap`) scan. | L/DL | `safe_alloc.c:367-393`; `safe_alloc.h:76-80` | Document as diagnostic hint. |
| 31 | `int cplus_safe_query_length(const void*, size_t, size_t*)` — `safe_alloc.c:468` | table only | Zeroes `*out_count` (:471); NULL ptr/out → `EINVAL` (:472); `elem_size==0` uses recorded (:476); not found → `EINVAL` (:481) | Same as #12. | DL | `safe_alloc.c:468-482`; `test_alloc_metadata_s.c:30,42` | Document. |
| 32 | `int cplus_safe_ptr_check_generation(const void*, unsigned long long)` — `safe_alloc.c:484` | table only | NULL/generation 0 → 0 (:487); exact-base compare (:488-492) | Same wrap/caller-correlation limits as #13 (F-18). **This is the only way for callers to detect F-01**; it is opt-in. | L (F-18) | `safe_alloc.c:484-493`; `test_alloc_metadata_s.c:34-35,50` | Document that callers must use it before freeing aliases. |
| 33 | `size_t cplus_safe_alloc_live_count` / `_live_bytes` — `safe_alloc.c:495/503` | table only | Mutex-protected reads (:496-509) | Same permanent inflation after host free as #15 (F-02). | M (F-02) | `safe_alloc.c:495-509`; `test_alloc_metadata_s.c:111-112` | Pair with `forget` API. |
| 34 | `int cplus_safe_transfer_ownership(void**, void**)` — `safe_alloc.c:511` | table only | Same validation as #14 (:513-516); `tab_find` required (:518-523); generation bumped, src nulled (:524-527); stats recorded (:514,521,528) | Same stale-alias weakness as #14 (F-01). | H (F-01) | `safe_alloc.c:511-530`; `test_alloc_metadata_s.c:44-51` | Optional expected-generation parameter. |
| 35 | `void cplus_safe_alloc_fail_after(long)` / `_fail_reset` / `_seq` — `safe_alloc.c:532/538/545` | injection state | `fail_after(count)` sets `g_inject = count<0 ? 0 : count` (:534) — **negative means "fail all"**, unlike libc layer's "disable"; `reset` → `-1` and `g_seq=0` (:539-542); `inject_fail_locked` increments `g_seq` on **every** call (:126) | Inconsistent negative semantics across layers (F-09); global state (F-10). `g_seq` counts injection decisions, not successful allocations — a caller expecting the latter will misread it (documented in header only as "seq"). | M (F-09/F-10) | `safe_alloc.c:124-131,532-551`; `safe_alloc.h:96-101`; `test_alloc_metadata_s.c:72-86` | Make negative mean "disable" in both layers, or document the divergence prominently; rename/document `seq` semantics. |

---

## 3. Per-function audit — capacity-checked ordinary C ops (`libc/src/string/bounded.c`)

These are not allocators but are part of the allocation-safety surface: they consume the
capacity reported by `cplus_alloc_remaining_capacity` and `__builtin_object_size`.

| Function | Checks actually present | Unsafe cases NOT handled | Sev | Evidence | Recommendation |
| --- | --- | --- | --- | --- | --- |
| `void cplus_libc_set_unknown_capacity_mode(int)` — `:20` | Coerces to 0/1 into `g_unknown_strict` (:21) | Process-global setting; off by default (`:18`) → unknown capacity is **unchecked** (F-12) | M | `bounded.c:18-22,36-44`; `cplus_libc_safe.h:15-17` | Document that callers must opt into strict mode. |
| `void *cplus_memset_bounded(void*,int,size_t,size_t)` — `:54` | NULL dest → `EINVAL` (:56); `check_capacity` refuses overflow (`ERANGE`) or unknown-in-strict-mode (`EINVAL`) (:33-52); then raw `cplus_memset` (:58) | Pointer lifetime/provenance is caller responsibility; `known` is compiler-derived and can be `(size_t)-1` | DL | `bounded.c:33-59`; `cplus_libc_safe.h:55-56` | Document. |
| `void *cplus_memcpy_bounded(void*,const void*,size_t,size_t)` — `:61` | NULL dest/src → `EINVAL` (:63); **destination** capacity checked (:64) | **Source capacity is never checked** (the single `known` argument is the destination's `CPLUS_LIBC_OBJ_SIZE(d)` in `cplus_libc_safe.h:58`). Reading past `s` is possible when `n > sizeof(*s)` (F-12). `memcmp_bounded` checks both operands, so the surface is inconsistent. | M | `bounded.c:61-66,75-82`; `cplus_libc_safe.h:57-62` | Add a `known_src` parameter or a `cplus_memcpy_bounded2(d,s,n,known_d,known_s)`; keep old symbol as wrapper. |
| `void *cplus_memmove_bounded(...)` — `:68` | Same as memcpy (:69-72) | Same unchecked source (F-12) | M | `bounded.c:68-73` | Same fix as memcpy. |
| `int cplus_memcmp_bounded(...,known_a,known_b)` — `:75` | NULL → `EINVAL` (:78); checks **both** capacities (:79-80) | Only the two explicit `known_*` values; no tracked-capacity fallthrough for interior pointers unless `cplus_alloc_remaining_capacity` recognizes them (it does — `bounded.c:24-29`) | DL | `bounded.c:75-82`; `cplus_libc_safe.h:61-62` | Keep. |
| `char *cplus_strcpy_bounded(char*,const char*,size_t)` — `:84` | NULL → `EINVAL` (:86); `need = cplus_strlen(s)+1` then dest capacity check (:87-89) | `cplus_strlen(s)` is called **before** any capacity check: unterminated `s` remains caller UB (explicitly documented in `:87`) (F-13) | DL | `bounded.c:84-90` | Document already present; optionally add `strnlen_s`-based bounded variant. |
| `char *cplus_strncpy_bounded(...)` — `:92` | NULL → `EINVAL`; capacity check on `n` (:94-96) | Capacity is checked against `n`, which may be larger than the source length — standard `strncpy` semantics, not a safety gap per se | DL | `bounded.c:92-97` | Keep. |
| `char *cplus_strcat_bounded(char*,const char*,size_t)` — `:99` | NULL → `EINVAL`; `need = strlen(d)+strlen(s)+1` then dest check (:101-104) | `strlen(d)` and `strlen(s)` run before validation; unterminated buffers are caller UB (F-13) | DL | `bounded.c:99-105` | Document. |
| `char *cplus_strncat_bounded(...)` — `:107` | NULL → `EINVAL`; `need = strlen(d)+min(n,strlen(s))+1` then check (:109-112) | Same unterminated-buffer UB (F-13) | DL | `bounded.c:107-113` | Document. |

---

## 4. Cross-cutting audits requested

### 4.1 Allocation-size overflow in `calloc` product
- `cplus_calloc`: `size > SIZE_MAX/nmemb` guarded when `nmemb != 0` → `ENOMEM` (`alloc.c:246-249`).
  Correct, but `errno` differs from the `_s` layer. Tests: `test_alloc_attack.c:78-92`,
  `test_alloc_metadata.c:92-93`.
- `calloc_s`: `req_count > SIZE_MAX/req_size || req_count*req_size > RSIZE_MAX_MEM`
  → `EOVERFLOW` (`safe_alloc.c:193-198`). Tests: `test_calloc_s.c:12-14`,
  `test_safe_alloc.c:31-33`.
- `cplus_calloc(0, SIZE_MAX)` and `calloc_s(0, SIZE_MAX)`: zero branch wins before the
  multiply check → a 1-byte block is returned. No overflow occurs, but callers expecting
  a rejection for a nonsensical request will not get one (DOCUMENTED LIMIT, F-15).

### 4.2 `realloc` ABA handling
- Both `cplus_realloc` (`alloc.c:316-335`) and `realloc_s` (`safe_alloc.c:293-312`)
  re-find the old address after the host call and require `slot.generation == old_gen && old_gen != 0`
  before touching it. This prevents stripping a newer block's metadata.
- Residual limits: (a) the host `realloc` runs with the lock released, so using a stale
  pointer concurrently is UB that the guard does not fix (F-04); (b) the `else` branch
  calls `meta_insert`/`tab_insert` for the new address, whose "existing slot" path
  (`alloc.c:162-168`, `safe_alloc.c:93-99`) can overwrite another thread's freshly
  inserted metadata and bump its generation — a bookkeeping race, not a host-heap
  corruption; (c) no true compare-and-swap / versioned pointer is provided to callers.
- Evidence: `CONTRACT_STATUS.md:65-68`, `SCENARIO_RESULTS.md:56`, `asan_sc_06/07/08`.

### 4.3 `live_count` / `live_bytes` accounting
- Maintained under one mutex; updated symmetrically in insert/remove and in the
  in-place realloc path (`alloc.c:156-197,323-331`; `safe_alloc.c:89-122,300-311`).
- Tests confirm exact return to baseline after alloc/realloc/free cycles
  (`test_alloc_metadata.c:107-108`, `test_alloc_metadata_s.c:111-112`,
  `asan_sc_12`, `test_alloc_attack.c:426-440`). No underflow observed.
- Known permanent inflation: host-side `free()` of a tracked pointer leaves the slot
  live → both counters stay high forever until that address is reissued through C+
  (Finding C2, F-02).

### 4.4 Zero-size semantics
- Uniform: `malloc`/`calloc` operands of 0 are normalized to a tracked **1-byte**
  allocation; `realloc(p,0)` is `realloc(p,1)` and does **not** free.
- `cplus_malloc`: `alloc.c:220`; `cplus_calloc`: `alloc.c:250-256`;
  `cplus_realloc`: `alloc.c:286`; `malloc_s`: `safe_alloc.c:160`;
  `calloc_s`: `safe_alloc.c:189-192`; `aligned_alloc_s`: `safe_alloc.c:219`;
  `realloc_s`: `safe_alloc.c:243`; `cplus_aligned_alloc`: size 0 is host-defined
  (`stdlib_c17.c:57-70`).
- Evidence: `test_alloc_attack.c:181-205`, `test_malloc_s.c:13-15`,
  `asan_sc_10`, `alloc_attack_NOTES.md:37-38,121-123`.
- Severity: DOCUMENTED LIMIT (F-15). No UB; divergence from C23 `realloc(p,0)`.

### 4.5 Failure-injection state
- `alloc.c`: `g_inject=-1` off; `fail_after(n>=0)` sets `n` (`n==0` → fail all from the
  next attempt); `fail_after(n<0)` **disables**; `fail_reset()` → `-1`
  (`alloc.c:48,199-214,576-586`).
- `safe_alloc.c`: same counter machine but `fail_after(n<0)` → `g_inject=0` → **fail
  all** (`safe_alloc.c:31,124-131,532-536`); `fail_reset()` also zeroes `g_seq` (:541).
- `g_seq` increments on every `inject_fail_locked()` call, including calls that do not
  fail (`safe_alloc.c:126`), so `cplus_safe_alloc_seq()` counts injection decisions,
  not allocations. Test asserts only the simple `+1` case (`test_alloc_metadata_s.c:75-79`).
- Injection is checked **before** the host allocation in all paths, so failure never
  mutates metadata or the original block (`alloc.c:221-224,257-260,302-306`;
  `safe_alloc.c:167-172,201-206,271-277`). Tests: `test_alloc_attack.c:115-176`,
  `test_alloc_metadata_s.c:72-86`, `asan_sc_08`, `fuzz_allocator.c`.
- Issues: cross-layer negative-argument divergence (F-09), process-global state (F-10).

---

## 5. Full finding register

| ID | Finding | Severity | Primary source evidence |
| --- | --- | --- | --- |
| F-01 | Checked free/transfer are generation-blind: a stale alias whose address was reissued frees/transfers the **new** live block | **HIGH** | `alloc.c:340-371,524-542`; `safe_alloc.c:319-350,511-530`; `safe_alloc.h:33-37`; `CONTRACT_STATUS.md:85-89` |
| F-02 | Host `free()` of a tracked pointer leaves false-live metadata and permanently inflates `live_count`/`live_bytes` (Findings C/C2) | **HIGH** | `alloc.c:189-197,544-558`; `test_alloc_attack.c:624-641`; `alloc_attack_NOTES.md:100-113` |
| F-03 | Two aligned APIs with opposite deallocators; `cplus_aligned_alloc` untracked, Windows needs `_aligned_free`, macro `free` → `cplus_free` refusal | **HIGH** | `stdlib_c17.c:13-23,53-71`; `safe_alloc.c:217-239`; `cplus_libc.h:69-78` |
| F-04 | Host `realloc` runs with metadata lock released; ABA guard protects only bookkeeping, not concurrent stale-pointer UB | **DOCUMENTED LIMIT (high impact)** | `alloc.c:308-337`; `safe_alloc.c:285-313`; `CONTRACT_STATUS.md:65-68` |
| F-05 | Recent-free cache false positive: fresh host `malloc` at a just-freed cplus address is refused and counted as a double free (Findings A/A2) | **MEDIUM** | `alloc.c:85-101,357-361`; `test_alloc_attack.c:556-581,361-387`; `alloc_attack_NOTES.md:62-85` |
| F-06 | Recent-cache classification is heuristic: libc table overwrites first slot when full; SafeCRT still uses a direct-mapped slot → real double free counted as invalid free | **MEDIUM** | `alloc.c:98-100`; `safe_alloc.c:30-42,111-117,332-338`; `CONTRACT_STATUS.md:48-55` |
| F-07 | No `alloc_size` attributes on exported wrappers; Agent 1 `.len`/extent lowering incomplete | **MEDIUM** | `cplus_libc.h:24-28`; `safe_alloc.h:51-61`; `docs/patches/agent1-len-managed-allocators.md:22,39-50`; `tests/compile_fail/alloc_size_oob.c:5` (uses `cplus_alloc`, not these wrappers) |
| F-08 | `cplus_calloc` overflow → `ENOMEM`, `calloc_s` overflow → `EOVERFLOW` | **MEDIUM** | `alloc.c:246-249`; `safe_alloc.c:193-198` |
| F-09 | `fail_after(negative)`: disables in libc layer, fails-all in SafeCRT layer | **MEDIUM** | `alloc.c:578`; `safe_alloc.c:534` |
| F-10 | Injection state is process-global with no libc-layer getter; `g_seq` counts decisions, not allocations | **MEDIUM** | `alloc.c:48,576-586`; `safe_alloc.c:31,124-131,545-551` |
| F-11 | `cplus_free` discards refusal; `cplus_free_checked` nulls `*inout` even on refusal, dropping the caller's handle to a still-live host block | **MEDIUM** | `alloc.c:364,373-379`; `test_alloc_attack.c:226-231` |
| F-12 | `bounded` memcpy/memmove check only destination capacity; source capacity unchecked; unknown capacity is unchecked by default | **MEDIUM** | `bounded.c:24-52,61-73`; `cplus_libc_safe.h:33,55-62` |
| F-13 | `bounded` string helpers call `strlen` on source/dest before any capacity validation; unterminated input is caller UB (documented) | **DOCUMENTED LIMIT** | `bounded.c:87,102,110` (comment at `:87`) |
| F-14 | `aligned_alloc_s` does not enforce C11 `size % alignment == 0`, unlike `cplus_aligned_alloc` | **MEDIUM** | `safe_alloc.c:217-226`; `stdlib_c17.c:57-62`; `safe_alloc.h:49` |
| F-15 | Zero size uniformly becomes a 1-byte tracked block; `realloc(p,0)` does not free | **DOCUMENTED LIMIT** | `alloc.c:220,250-256,286`; `safe_alloc.c:160,189-192,219,243`; `test_alloc_attack.c:181-205` |
| F-16 | Interior pointers: `remaining_capacity` supports them but all liveness/size/meta APIs report non-live — inconsistent surface, no provenance | **DOCUMENTED LIMIT** | `alloc.c:386-412` vs `444-485`; `test_alloc_attack.c:278-285` |
| F-17 | `g_live`/`g_live_bytes` are `size_t`; a future bookkeeping error would wrap silently (no wrap observed) | **LOW** | `alloc.c:189-197,544-558`; `test_alloc_attack.c:426-440` |
| F-18 | 64-bit generation counter can wrap to 0 (check treats 0 as invalid); correlation with a specific alias is caller responsibility | **LOW** | `alloc.c:159,511-522`; `safe_alloc.c:484-493` |
| F-19 | `remaining_capacity` uses `base + byte_size` (theoretical `uintptr_t` wrap) and an O(`g_cap`) linear scan | **LOW** | `alloc.c:399-409`; `safe_alloc.c:380-390` |
| F-20 | `cplus_aligned_alloc` has no `RSIZE_MAX_MEM` cap and zero size is host-defined | **LOW** | `stdlib_c17.c:53-71` |

---

## 6. Summary counts and severity distribution

- Exported allocation-related entry points recorded: **53** (21 functions + 1 macro in the
  hardened-libc/C17 tables, 21 functions + 1 macro in the SafeCRT table, 9 bounded ops).
- Distinct findings: **20**.

| Severity | Count | Finding IDs |
| --- | --- | --- |
| HIGH | **3** | F-01, F-02, F-03 |
| MEDIUM | **9** | F-05, F-06, F-07, F-08, F-09, F-10, F-11, F-12, F-14 |
| DOCUMENTED LIMIT | **4** | F-04, F-13, F-15, F-16 |
| LOW | **4** | F-17, F-18, F-19, F-20 |

By layer (a root cause can touch more than one layer):
- Hardened libc (`alloc.c` + `stdlib_c17.c`): F-01, F-02, F-03, F-04, F-05, F-06,
  F-07, F-08, F-09, F-10, F-11, F-15, F-16, F-17, F-18, F-19, F-20.
- SafeCRT (`safe_alloc.c`): F-01, F-02, F-03, F-04, F-06, F-07, F-08, F-09, F-10,
  F-14, F-15, F-18, F-20.
- Bounded ops (`bounded.c`): F-12, F-13 (plus F-07-adjacent compiler gap).
- Compiler-enforcement gap (F-07) spans both public headers.

Counts are by distinct root cause; a single root cause (e.g., F-02) appears in multiple
function rows above.

---

## 7. Prioritized top 5 issues with concrete fix proposals

All proposals preserve the existing host-allocator design; none rewrite the allocator.

### 1. F-01 — generation-blind checked free/transfer can free a new owner (HIGH)
- **Where:** `libc/src/stdlib/alloc.c`, `cplus_free_checked` (`:340`) and
  `cplus_transfer_ownership` (`:524`); `stdlib/safe/src/safe_alloc.c`,
  `free_checked_s` (`:319`) and `cplus_safe_transfer_ownership` (`:511`).
- **Fix:** add optional generation-aware entry points, e.g.
  `int cplus_free_checked_gen(void **inout, unsigned long long expected_gen)` and
  `int free_checked_s_gen(void *ptr, unsigned long long expected_gen)`, which require
  `check_generation(ptr, expected_gen)` to succeed before calling `meta_remove`/`tab_remove`
  and host `free`. Keep existing symbols as wrappers passing `expected_gen == 0`
  (meaning "no check") so ABI is unchanged. Document that alias-bearing code must use
  the `_gen` form (`safe_alloc.h:33-37` already hints at this).
- **Test:** new scenarios in `tests/libc/test_alloc_attack.c` and
  `tests/crt/unit/test_alloc_metadata_s.c`: allocate A, save alias, free A, allocate B
  until the address is reused, capture B's generation, then call the `_gen` free with
  A's stale generation and assert it is refused (B still live); call with B's generation
  and assert success. Extend `tests/crt/asan/test_alloc_asan.c` scenario 5 with the
  stale-alias variant.

### 2. F-02 — host `free()` of a tracked pointer leaves false live state + permanent accounting leak (HIGH)
- **Where:** `libc/src/stdlib/alloc.c`, `meta_remove`/`meta_insert` (`:189-197,156-187`)
  and `cplus_alloc_live_count/bytes` (`:544-558`); mirrors in `safe_alloc.c` (`:114-122,89-112,495-509`).
- **Fix (no interposition):** add an explicit invalidation API
  `int cplus_alloc_forget(const void *ptr)` / `int cplus_safe_alloc_forget(const void *ptr)`
  that removes the slot (and recent entry) without calling host `free`, returning
  `EINVAL` for an untracked pointer. Add a `host_free_bypass_count` diagnostic so drift
  is visible. Document in `cplus_libc.h` and `safe_alloc.h` that host `free` must be
  preceded by `_forget`. `meta_insert`'s existing "existing slot" recovery
  (`alloc.c:162-168`) stays as a fallback.
- **Test:** change `tests/libc/test_alloc_attack.c:624-641` (Finding C/C2) to call
  `cplus_alloc_forget(q)` before `free(q)` and assert `is_live(q)==0` and counters return
  to baseline; keep the un-forgotten variant as a documented regression.

### 3. F-03 — two aligned allocators with opposite deallocation contracts (HIGH)
- **Where:** `libc/src/stdlib/stdlib_c17.c:53-71` (untracked `cplus_aligned_alloc`) vs
  `stdlib/safe/src/safe_alloc.c:217-239` (tracked `aligned_alloc_s`); macro hazard at
  `libc/install-include/cplus_libc.h:69-78`.
- **Fix:** add the namespaced deallocator
  `void cplus_aligned_free(void *ptr)` implemented in `stdlib_c17.c`:
  `#ifdef _WIN32` → `_aligned_free(ptr)`; otherwise → `free(ptr)`. Export it in
  `cplus_libc.h`. In `cplus_libc_safe.h`, add a comment warning that `free()` on a
  `cplus_aligned_alloc` pointer is invalid under `CPLUS_MALLOC_TRACKING`; do not
  interpose further. State `aligned_alloc_s` ⇔ `free_s` in `safe_alloc.h:49-50`.
- **Test:** extend `tests/crt/asan/test_alloc_asan.c` scenario 11 to release `a` with
  `cplus_aligned_free(a)` and `b` with `free_s(b)`; add a Windows-only compile check that
  `cplus_aligned_free` is used (cannot be verified on this host — record as such).

### 4. F-07 — no compiler-enforced extents on the exported wrappers (MEDIUM; Agent 1 hand-off)
- **Where:** `libc/install-include/cplus_libc.h:24-28` and
  `stdlib/safe/safe_alloc.h:51-61`; hand-off doc
  `docs/patches/agent1-len-managed-allocators.md:22,39-50`.
- **Fix:** guard with `#if defined(__GNUC__) || defined(__clang__)` and annotate:
  `void *cplus_malloc(size_t) __attribute__((alloc_size(1)));`,
  `void *cplus_calloc(size_t,size_t) __attribute__((alloc_size(1,2)));`,
  `void *cplus_realloc(void*,size_t) __attribute__((alloc_size(2)));`, and the same for
  `malloc_s`, `calloc_s`, `realloc_s`, `aligned_alloc_s` (position 2). Then extend
  `compiler/Sema/CPlusRewriter.cpp::collectDynamicPointers()` `kAllocators` per the hand-off
  doc so `.len` lowers for these names. This is the **compiler-enforced (Agent 1)** bucket;
  it does not change runtime behaviour.
- **Test:** add compile-fail cases mirroring `tests/compile_fail/alloc_size_oob.c` but
  using `cplus_malloc(sizeof(int)*size)` and `malloc_s(sizeof(int)*size)`, with matching
  `.err` expectation files; add a `.len` success case from the hand-off doc.

### 5. F-05 / F-06 — recent-free cache false positives and heuristic classification (MEDIUM)
- **Where:** `libc/src/stdlib/alloc.c:85-101,357-361` (address-only recent table, first-slot
  overwrite when full) and `stdlib/safe/src/safe_alloc.c:30-42,111-117,332-338`
  (direct-mapped cache).
- **Fix:** (a) port the open-addressed `recent_find`/`recent_add`/`recent_remove` pattern
  from `alloc.c:69-101` into `safe_alloc.c`, replacing `g_recent[RECENT_SLOTS]` with a
  `{ptr, generation}` slot array; (b) in both layers, stop reporting "double free" from
  an address match alone — if there is no live slot, classify as `invalid_free`/`unknown`
  unless the caller supplies a corroborating generation via the F-01 `_gen` API;
  (c) add a `recent_table_full` counter so saturation is observable. No host-allocator
  behaviour changes.
- **Test:** keep `tests/libc/test_alloc_attack.c:556-613` Findings A/A2/B as regression;
  add the equivalent scenarios to `tests/crt/test_safe_alloc.c` (host `malloc` reusing a
  freed `malloc_s` address; two hashed-colliding freed pointers), asserting the chosen
  classification and that the host pointer is never freed by C+.

---

## 8. Verification statement

This is a source-and-test-backed static audit performed on branch `agent/modern-stdlib`.
No build, injection, or live-game run was performed for this document. Findings A/A2/B/C/C2
are reproduced from the checked-in test and its notes; note that the libc-layer open-addressing
change described at `alloc.c:65-68` mitigates collision Finding B there, but the SafeCRT layer
still uses a direct-mapped cache (`safe_alloc.c:30,42,117`) and the libc table can still
overwrite its first slot when full (`alloc.c:98-100`). Classification is best-effort in both.
No claim in this document should be read as a proof of pointer lifetime, provenance, or
absence of host-level undefined behaviour.
