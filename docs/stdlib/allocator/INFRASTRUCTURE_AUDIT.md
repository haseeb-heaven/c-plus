# Task 5 Phase 1 — Allocation / Deallocation Infrastructure Audit

Branch: `agent/modern-stdlib`. Static audit of every allocation/deallocation path
that can affect the standard-library modules, plus the runtime probes noted in
§4. Scope was read-only: no source file was modified.

## 0. Method, evidence legend, and scope discipline

* **Inspected in full**: `stdlib/error.hp`, `libc/src/stdlib/alloc.c`,
  `libc/src/stdlib/stdlib_c17.c`, `stdlib/memory.hp`, `stdlib/string.hp`,
  `stdlib/vector.hp`, `stdlib/collections.hp`, `stdlib/file.hp`, `stdlib/json.hp`,
  `stdlib/log.hp`, `stdlib/thread.hp`.
* **Legacy**: **REPRO** = runtime evidence exists (new probe here, or cited
  existing probe); **STATIC** = source-only observation, not executed;
  **DOC** = documented/intentional limit; **DRIFT** = documentation and code
  disagree. I do not claim a defect is reproduced unless a probe was run.
* All line numbers are from the files as they exist on this branch at audit time.
* Existing reproduced items are *cited*, not re-invented or re-counted. Where I
  quote prior test results (e.g. `12/12`, `294/294`) they come from the cited
  document, not from a new run.

### Allocation models present (three, not one)

`error.hp:12-23` states "one allocation model for every .hp container" and that
containers allocate **only** through `stdlib_alloc`/`stdlib_calloc` and release
**only** through `stdlib_release`. The code actually has three distinct models:

| Model | Producer | Backing | Side table | Fault injection / telemetry |
| --- | --- | --- | --- | --- |
| Checked stdlib | `stdlib_alloc/calloc/realloc/release` (`error.hp`) | `cplus_alloc/release/realloc` | `stdlib_state.meta_tab` (`error.hp:117-121`) | `stdlib_alloc_fail_at/after` + `stdlib_state.counters` |
| Hardened libc | `cplus_malloc/calloc/realloc/free*` (`alloc.c`) | host `malloc/calloc/realloc/free` | `g_tab` (`alloc.c:40-49`) | `cplus_alloc_fail_after` + `g_double_frees`/`g_invalid_frees` |
| Upstream host | `yyjson_*` in `json.hp`; `fopen/tmpfile` in `file.hp`/`log.hp`; `cplus_mutex_new`, `pthread_*` in `thread.hp` | host CRT / OS | none | none |

The checked-stdlib model is **layered on top of** the hardened-libc model: every
`stdlib_alloc` produces two metadata entries (one in `g_tab`, one in
`meta_tab`) with independently advancing generation counters. This duality is
the root of several findings in §5.

---

## 1. Checked allocator core — `stdlib/error.hp`

### 1.1 Function-by-function table

| Function (file:line) | Allocation call(s) | Deallocation call(s) | Ownership semantics | Capacity / size arithmetic | Cleanup on failure | Failure behavior |
| --- | --- | --- | --- | --- | --- | --- |
| `stdlib_alloc` `error.hp:365-392` | `cplus_alloc(bytes)` `:385` | — | Caller owns returned block; must release via `stdlib_release` | `bytes==0 -> 1` `:370-371`; cap check `bytes > STDLIB_ALLOC_LIMIT` `:372-376` (`STDLIB_ALLOC_LIMIT` def `:61-67`) | none needed | NULL + `stdlib_note(MEMORY,RESOURCE)` on cap/injection; NULL from `cplus_alloc` propagated. Records metadata only on success `:387`; counts alloc/bytes `:388-389` |
| `stdlib_calloc` `error.hp:395-414` | `stdlib_alloc(total)` `:408` (which calls `cplus_alloc`) | — | Caller owns zeroed block | `__builtin_mul_overflow(count,size,&total)` `:400`; zero operands -> `total=1` `:406` | n/a | NULL + `stdlib_note(MEMORY,"allocation size overflow")` `:402-404`. **Double-records metadata**: `stdlib_alloc` records with `elem=1` `:387`, then `:411` records again with real `elem` (see §4.3) |
| `stdlib_realloc` `error.hp:422-456` | `cplus_realloc(ptr,actual)` `:446`; falls back to `stdlib_alloc(bytes)` when `ptr==NULL` `:431-432` | indirect: `cplus_realloc` frees old storage when it moves | Caller keeps ownership of returned pointer; old identity retired | `actual = bytes==0?1:bytes` `:423`; cap check `:426-430` | On `cplus_realloc` NULL, nothing in `meta_tab` touched `:447-448` | Over-limit: NULL + `note(RANGE, BOUNDS)` `:427-429` (**category differs from `stdlib_alloc`'s MEMORY/RESOURCE**). Untracked `ptr`: NULL + `note(INVALID,UNSUPPORTED)` `:437-442`. **No fault-injection / no `seq` increment** (§4.1). Bumps growth counter `:454` |
| `stdlib_release` `error.hp:582-588` | — | `stdlib_meta_forget(ptr)` `:585`, `cplus_release(ptr)` `:587` | Relinquishes caller ownership; NULL is a no-op | n/a | `stdlib_meta_forget` is best-effort (no-op if untracked) | No generation check; no verification that `ptr` came from `stdlib_alloc` (§5.2) |
| `stdlib_meta_record` `error.hp:289-346` | raw `calloc(ncap,...)` `:307` | raw `free(meta_tab)` `:318` | Owns the side table itself, not caller blocks | Growth trigger `(used+1)*4 > cap*3` `:298-299`; double if `live*2 >= cap` `:303-306` with overflow guard; hash `stdlib_meta_hash` `:265-271` | On `calloc` failure, silently keeps old table and proceeds to insert `:308-323` (no `stdlib_note`) | Bumps `meta_gen` for every record, including a re-record of the same pointer `:329,:341` |
| `stdlib_meta_forget` `error.hp:348-362` | — | — (marks `STDLIB_META_TOMB`, `:355`) | Retires metadata identity | Decrements `meta_live` `:359`; **does not decrement `meta_used`** | n/a | Tombstone retains slot; linear-probe `stdlib_meta_find` stops at genuine NULL `:282-283` so tombstone entries stay findable |
| `stdlib_meta_find` `:273-287` / `stdlib_meta_hash` `:265-271` | — | — | Lookup only | `x ^= x >> 33` at `:267` (twice) — **UB on 32-bit `uintptr_t`** (§5.4) | n/a | NULL when not found |
| `stdlib_alloc_size/elem_size/length/generation` `:477-522`; `query_length` `:538-556` | — | — | Read-only metadata queries | `stdlib_safe_div` `:462-474` avoids variable division; exact for quotients < 2^16 as documented `:458-461` | n/a | 0/NULL if untracked; `query_length` returns -1 and zeroes `*out_count` `:542-545` |
| `stdlib_alloc_check_generation` `:524-536` | — | — | Validates a previously captured token | Exact generation compare `:532` | n/a | 0 for NULL ptr or token 0 `:528-529` |
| `stdlib_transfer_ownership` `:558-579` | — | — (re-points handle) | Moves block `*src -> *dst`, nulls `*src` `:575-576` | Bumps generation `:574` to invalidate pre-transfer tokens | n/a | -1 + `note(OWNERSHIP)` on invalid handles `:560-565` or untracked source `:568-572` |
| Fault injection `stdlib_alloc_fail_at/after/reset/seq` `:227-251` | — | — | Process-global test state in `stdlib_state` `:109-122` | Atomic seq/at/from `:228-245` | n/a | Applied **only** in `stdlib_alloc` `:377-384`; `stdlib_realloc` and the metadata `calloc` bypass it |
| Telemetry `stdlib_tm_add` `:127-131`, `note` `:194-222`, snapshot `:148-161` | — | — | Diagnostic counters, weak shared state `:125` | Atomic adds; opt-in `:133-140` | n/a | `stdlib_note` drops re-entrant reports via `__thread reporting` `:196-220` |

### 1.2 Invariants that are *not* enforced here

* `stdlib_release` does not require a matching generation and does not confirm
  the pointer is a live `stdlib_*` block before calling `cplus_release`.
* `stdlib_realloc` does not participate in fault injection or `stdlib_alloc_seq`,
  so coverage tools that enumerate allocation points using those hooks cannot
  see the realloc point (§4.1).
* The side table's own `calloc`/`free` are outside telemetry and injection.

---

## 2. Hardened libc core — `libc/src/stdlib/alloc.c` and `stdlib_c17.c`

### 2.1 `alloc.c` function-by-function table

| Function (file:line) | Allocation call(s) | Deallocation call(s) | Ownership semantics | Capacity / size arithmetic | Cleanup on failure | Failure behavior |
| --- | --- | --- | --- | --- | --- | --- |
| `meta_reserve_one` `:125-153` | raw `calloc(ncap,...)` `:136` | raw `free(g_tab)` `:148` | Owns `g_tab` | Doubling with guard `ncap > SIZE_MAX/4/sizeof(meta_slot)` -> -1 `:131-135` | on success rehashes all live slots `:139-147` | -1 on OOM; callers translate to ENOMEM |
| `meta_insert` `:156-187` | — | — | Registers a block in `g_tab`; `g_live_bytes` accounting `:181` | `elem_size==0 -> 1` `:160-161` | n/a | Recovers an existing slot (host-freed-and-reused address) `:162-168`; `recent_remove(p)` `:185` |
| `cplus_alloc_meta_register` `:193-203` | — | — | Registers a block produced by a *different* host API into `g_tab` | delegates to `meta_reserve_one` | caller must free with matching host deallocator if -1 (documented `:189-192`) | 0/-1 |
| `cplus_malloc` `:232-256` | `malloc(actual)` `:241` | `free(p)` on reserve failure `:249` | Caller owns block; free via `cplus_free`/`cplus_free_checked` | `size==0 -> 1` `:236`; **no cap** (host size only) | just-allocated block released on reserve failure `:247-251` | ENOMEM on injection `:237-240`, host OOM `:242-245`, or reserve failure |
| `cplus_calloc` `:258-292` | `calloc(1u,total)` `:277` | `free(p)` on reserve failure `:285` | same as malloc | overflow `size > SIZE_MAX/nmemb` `:262-265`; zero operands -> `1,1` `:266-272` | as malloc `:283-287` | ENOMEM on all three failure classes; overflow errno here is ENOMEM, not EOVERFLOW (`calloc_s` differs — F-08 in cited audit) |
| `cplus_realloc` `:294-354` | `realloc(ptr,actual)` `:325` | host realloc frees old on move; `meta_remove(s)` `:345` | Caller owns result; failure leaves old block untouched | `size==0 -> 1` `:302`; no RSIZE cap | on host realloc NULL, metadata untouched `:326-329` | EINVAL for unowned pointer `:307-314`; ENOMEM for injection/reserve failure `:318-322`; ABA generation guard after unlocked host call `:333-350`; **host call runs with lock released** (F-04, DOC) |
| `cplus_free_checked` `:356-387` | — | `free(ptr)` only when `meta_find` succeeds `:381-383` | Caller passes `void**`; handle nulled | n/a | n/a | `*inout=NULL` **unconditionally**, including on refusal `:380` (F-11). No generation compare (F-01 / §4.2). Double vs invalid free via `recent_contains` `:373-377` |
| `cplus_free` `:389-395` | — | delegates to `cplus_free_checked` `:394` | same | n/a | n/a | Discards the checked return — silent refusal except `errno`/counters (F-11) |
| `cplus_alloc_remaining_capacity` `:402-428` | — | — | Read-only diagnostic | exact-base match first `:407-414`, then interior `p>base && p<=base+size` `:415-424` | n/a | `(size_t)-1` on miss; `b + byte_size` wrap is theoretical/DOC (F-19) |
| `cplus_alloc_get_meta/is_live/size/elem_size/length/query_length/check_generation` `:429-538` | — | — | Read-only; exact-base keyed | `elem_size?elem_size:1` at `:445,:496,:515` | n/a | EINVAL/0 on miss; interior pointers intentionally report non-live (F-16, DOC) |
| `cplus_transfer_ownership` `:540-558` | — | — (re-points handles) | `*src -> *dst`, nulls `*src` `:554-555` | bumps `g_gen` `:553` | n/a | EINVAL on invalid handles/untracked source `:542-551`; **no generation check** (F-01) |
| Counters `:560-590`; injection `:592-602` | — | — | Global state under `g_meta_lock` | `g_inject<0` disables, `==0` fails, `>0` decrements `:215-222` | n/a | `fail_after(negative)` means "disable" here but "fail all" in the SafeCRT layer (F-09, not owned here) |

### 2.2 `stdlib_c17.c` function-by-function table

| Function (file:line) | Allocation call(s) | Deallocation call(s) | Ownership semantics | Capacity / size arithmetic | Cleanup on failure | Failure behavior |
| --- | --- | --- | --- | --- | --- | --- |
| `cplus_aligned_alloc` `:60-95` | host `aligned_alloc(alignment,size)` `:77` / `_aligned_malloc(size,alignment)` `:75` | `cplus_alloc_meta_register(p,size,1)` `:83`; on register failure `_aligned_free` `/` `free` `:86-90` | **Registered in `g_tab`**, so `cplus_free` can release it on POSIX — contradicts this file's own header `:13-23` (§4.2) | Rejects `alignment==0`, non-power-of-two, not multiple of `sizeof(void*)`, `size%alignment!=0` `:65-66`; **no `STDLIB_ALLOC_LIMIT`/RSIZE cap** (F-20) | matching host deallocator on register failure | EINVAL on constraint violation; ENOMEM on host OOM or register refusal |
| `cplus_quick_exit` `:97-100`, `cplus_at_quick_exit` `:102-105`, `cplus__Exit` `:107-110` | — | — | process lifecycle, no allocations | n/a | n/a | delegates to host |

---

## 3. Standard-library modules — function-by-function allocation tables

Ownership key: **O** = owns storage and releases in `drop()`; **B** = borrows,
never releases; **T** = transfers on success; **N** = no allocation.
All `.hp` owning classes use explicit `drop()`; none declares a destructor (§5.3).

### 3.1 `stdlib/memory.hp` (§68-252)

| Function (line) | Alloc / Free | Own | Capacity arithmetic | Failure behavior |
| --- | --- | --- | --- | --- |
| `stdlib_mem_capacity` `73-75` | query `stdlib_alloc_size` | N | — | 0 if untracked |
| `stdlib_mem_count` `81-84` | query `stdlib_alloc_query_length` | N | uses stdlib safe-div | -1 on NULL/untracked |
| `stdlib_mem_token` `91-93` | query generation | N | — | 0 untracked |
| `stdlib_mem_check_token` `98-107` | query | N | — | `LIFECYCLE/USE_AFTER_FREE` for stale non-zero token |
| `stdlib_mem_check` `111-119` | query | N | — | `INVALID/TYPE_CONFUSION` for NULL/untracked |
| `stdlib_mem_check_range` `126-153` | query | N | `off > cap` then `n > cap-off` — overflow-safe subtraction `:142-151` | BOUNDS or INVALID; fail-closed |
| `stdlib_mem_copy` `164-177` / `mem_move` `180-193` / `mem_set` `196-206` | `memcpy`/`memmove`/`memset` only | N | via `check_range` | All checks precede any write; `n==0` valid no-op; **only dst token checked** (DOC, memory.hp:40-45) |
| `stdlib_mem_borrow` `215-222` | none | B | range + token validation | no generation change |
| `stdlib_mem_transfer` `230-242` | `stdlib_transfer_ownership` `:236` | T | generation bumped by underlying helper | -1 + OWNERSHIP error, both handles unchanged on failure |
| `stdlib_mem_release` `247-252` | `stdlib_release(*ptr)` `:250`, then `*ptr=NULL` | O | — | **No token parameter** — generation-blind at this layer (§5.2) |

### 3.2 `stdlib/string.hp` (§79-408)

| Function (line) | Alloc | Free | Own | Capacity arithmetic | Cleanup on failure |
| --- | --- | --- | --- | --- | --- |
| `string` ctor `121-136` | `stdlib_alloc(n+1)` `:127` | — | O | `n < STDLIB_ALLOC_LIMIT` guard `:126`; no note when guard rejects | leaves invalid empty string; `valid()==0` |
| `reserve` `212-234` | `stdlib_alloc(want)` `:221` | `stdlib_release(old)` `:229` | O | `want>limit` `:216-220` | failure-atomic; growth telemetry `:232` |
| `append` `240-297` | via `string_reserve` `:287` | via reserve | O | `extra > LIMIT-1 || count > LIMIT-1-extra` `:254-259`; doubling `space*2` capped at `LIMIT/2` `:282-286`; self-append uses whole-span alias window `:274-278` | failure-atomic; `memmove` for aliases `:291` |
| `copy_from` `306-331` | `stdlib_alloc(n+1)` `:319` | `stdlib_release(old)` `:326` | O | `n >= LIMIT` silent return `:317-318` | failure-atomic |
| `drop` `400-407` | — | `stdlib_release(chars)` `:402` | O | — | idempotent; **no destructor** |
| `string_view` `80-114`, `to_view` `393-398` | — | — | B | — | dangling if owner freed (documented) |
| `substr` `339-355`, `field` `368-391`, `count_fields` `358-365` | — | — | N | `n+1 > out_size` clamp `:349,:379` | `n+1` cannot overflow because `n < ALLOC_LIMIT` |

### 3.3 `stdlib/vector.hp` (§27-306)

| Function (line) | Alloc | Free | Own | Capacity arithmetic | Failure behavior |
| --- | --- | --- | --- | --- | --- |
| `vector` ctor `94-99` | — | — | O | — | zero-initialized empty |
| `reserve` `111-133` | `stdlib_alloc(sizeof(int)*want)` `:121` | `stdlib_release` `:127` | O | `want > LIMIT/sizeof(int)` `:116-120` | -1 atomic; bumps vector `generation` `:130` + growth telemetry `:131` |
| `push_back` `135-151` | via `reserve` `:146` | via reserve | O | doubling guarded at `space > (LIMIT/sizeof(int))/2` `:139-145` | -1 atomic, notes RANGE at `:140-142` |
| `insert` `190-210` | via `reserve` `:201` | via reserve | O | same doubling guard `:196-200` | -1 atomic **without `stdlib_note`** at `:198` (§5.5) |
| `erase`/`pop_back`/`clear` `178-187,213-229` | — | — | O | — | bump generation; no frees |
| `copy_from` `233-259` | `stdlib_alloc(sizeof(int)*other->used)` `:246` | `stdlib_release(old)` `:253` | O | `other->used > LIMIT/sizeof(int)` silent -1 `:244-245` | -1 atomic; self-copy no-op `:241-242` |
| `shrink_to_fit` `263-286` | `stdlib_alloc(sizeof(int)*used)` `:276` | `stdlib_release` `:270,:281` | O | uses `used`, bounded by prior capacity | -1 atomic when alloc fails; zero-used path frees buffer `:268-275` |
| `view` `290-295` | — | — | B | slice holds pointer to owner's `generation` | stale after shape change; dangling after owner death (DOC, header `:16-19`) |
| `drop` `297-305` | — | `stdlib_release(items)` `:299` | O | — | idempotent; **no destructor** |

### 3.4 `stdlib/collections.hp` (§37-1083)

Shared helpers: `stdlib_map_next_pow2` `57-65` (`cap > SIZE_MAX>>2` guard);
`stdlib_map_needs_room` `68-72` (70% load); `stdlib_map_rebuild_capacity`
`77-86` (double only when `count+1 >= capacity/2`, overflow guard);
`stdlib_ring_copy` `90-116` (`newspace > LIMIT/sizeof(int)` `:94-98`, alloc `:99`,
never touches source).

| Class / function (line) | Alloc | Free | Own | Capacity arithmetic & rollback | Failure behavior |
| --- | --- | --- | --- | --- | --- |
| `intmap::stdlib_rebuild` `165-210` | 3× `stdlib_alloc` `:172,:175,:180` | rollback `release` `:177,:182-183`; replace `:197-202` | O | `cap<8 || cap > LIMIT/sizeof(int)` `:170-171`; all three buffers allocated before mutation | -1 atomic; growth telemetry `:208` |
| `intmap::grow` `214-221`, `put` `223-255`, `remove` `273-280`, `clear` `282-287`, `drop` `289-302` | via rebuild; no alloc in put | via rebuild/drop | O | probe bounded by capacity `:153-159`; tombstone reuse does not bump `used_slots` `:240-243` | -1 leaves map logically unchanged; drop releases all three arrays |
| `strmap::stdlib_copy_key` `333-346` | `stdlib_alloc(len+1)` `:341` | caller (`remove`/`clear`/`drop`) | O (per-key) | `len >= LIMIT` silent NULL `:339-340` | NULL on failure |
| `strmap::stdlib_rebuild` `367-414` | 3× `stdlib_alloc` `:374,:377,:382` | rollback `:379,:383-386`; replace `:401-406` | O | `cap > LIMIT/sizeof(char*)` `:372-373`; keys **moved, not copied** | -1 atomic; key ownership survives rebuild |
| `strmap::put` `425-468` | rebuild + `copy_key` `:442,:447` | on rebuild/remove/clear/drop | O | rebuild happens **before** the last fallible key copy `:440-449` | on `copy_key` failure logical contents unchanged but capacity/generation changed ("failure-atomic" is logical only) |
| `strmap::remove` `486-495`, `clear` `497-508`, `drop` `510-525` | — | `stdlib_release(key)` `:490,:501`; arrays `:513-518` | O | — | idempotent; `clear` nulls keys, `drop` calls `clear` first |
| `intset::stdlib_rebuild` `569-603` | 2× `stdlib_alloc` `:575,:578` | rollback `:580`; replace `:593-596` | O | `cap > LIMIT/sizeof(int)` `:573-574` | -1 atomic |
| `intset::insert/remove/clear/drop` `616-675` | via rebuild | via rebuild/drop | O | same probe/tombstone scheme | -1 atomic; drop releases both arrays |
| `deque::reserve` `702-717` | `stdlib_ring_copy` `:706` | `stdlib_release(old)` `:711` | O | `stdlib_ring_copy` checks cap | -1 atomic; head reset to 0 `:713` |
| `deque::stdlib_make_room` `719-731` | via reserve | via reserve | O | doubling guard `:726-729` (silent -1 at `:727`) | -1 |
| `deque` push/pop/at/clear/drop `733-806` | via make_room | drop `:801` | O | modular index `idx -= space` (one subtraction, proven by bounds `:738,:749,:762,:773,:789`) | clear keeps buffer |
| `list::push_back/push_front` `834-864` | `stdlib_alloc(sizeof(list_node))` `:835,:851` | via pop/clear | O (per-node) | single node, no arithmetic | -1 leaves links untouched (alloc before link change) |
| `list::pop_front/pop_back/clear/drop` `866-930` | — | `stdlib_release(node)` `:878,:894,:920` | O | — | idempotent; `drop()` aliases `clear()` `:928-930` |
| `stack::push` `950-973` | `stdlib_alloc(sizeof(int)*want)` `:960` | `stdlib_release(old)` `:966` | O | doubling guard `:955-959` (silent -1 at `:957`) | -1 atomic; growth telemetry `:969` |
| `stack::pop/top/clear/drop` `975-999` | — | drop `:995` | O | — | clear keeps buffer |
| `queue::push` `1021-1047` | `stdlib_ring_copy` `:1030` | `stdlib_release(old)` `:1035` | O | doubling guard `:1025-1029` (silent -1 at `:1027`); `want` pre-checked by ring_copy | -1 atomic; growth telemetry `:1039` |
| `queue::pop/front/clear/drop` `1049-1080` | — | drop `:1075` | O | — | clear keeps buffer |

### 3.5 `stdlib/file.hp`, `json.hp`, `log.hp`, `thread.hp`

| Module / function (line) | Alloc | Free | Own | Failure / cleanup |
| --- | --- | --- | --- | --- |
| `file` ctor `20-23` | `fopen` `:21` | `close` `:215`, `drop` `:227` | O if `owns`, B otherwise | `owns` set only on successful open `:22` |
| `file::read/read_line/write/write_bytes/seek/flush` `33-203` | none | none | N | stdio only; `read_line` drains overlong lines `:95-99,:130-131`; no stdlib allocations |
| `file::close` `206-223` | — | `fclose` when owned `:215` | O | double-close refused (LIFECYCLE) `:209-213`; handle nulled `:216-217` |
| `file_borrow` `234-239`, `file_temp` `242-253` | `tmpfile()` `:245` | via `drop` | B / O | `tmpfile` failure sets err and `owns=0` `:246-249` |
| `json` ctor `145-159` | `yyjson_read_opts` `:153` | `drop` -> `yyjson_doc_free` `:174` | O | parse failure -> invalid doc + error; no leak |
| `json_parse_file` `181-195` | `yyjson_read_file` `:190` | caller `drop` | O | error on NULL path / read failure |
| `json_view` `21-138`; `write` `133-137` | `yyjson_val_write` `:136` | caller must use `json_free` `:202-204` | B / O(buffer) | third allocator (host CRT via yyjson); no fault injection or telemetry (§5.7) |
| `json_builder` ctor `210-218` | `yyjson_mut_doc_new` `:211`, `yyjson_mut_obj` `:214` | `drop` -> `yyjson_mut_doc_free` `:298` | O | partial construction leaks nothing: `drop` frees doc when `root_value==NULL` |
| `json_builder::add_*` `224-288`, `write` `290-294` | yyjson mut ops, `yyjson_mut_write` `:293` | via `drop` / `json_free` | O | maps yyjson failure to ERROR/MEMORY; non-finite double refused `:246-250` |
| `log_sink` ctor `69-73`, `write` `98-177`, `drop` `192-197` | stack buffers only (`char text[512]` `:102`); `fopen` in `log_open_file` `:210` | `fclose` when owned `:194` | O/B | truncation checked at `:126-130,:143-147,:153-157,:160-164`; `flockfile`/`funlockfile` paired `:167-175` |
| `mutex` ctor `32-34`, `drop` `54-59` | `cplus_mutex_new` `:33` | `cplus_mutex_free` `:56` | O (OS handle) | NULL-safe no-ops; **no destructor** |
| `guard` `63-88` | none | `mutex_unlock` in `unlock`/`drop` `:78,:85` | B | clearing owner `:79` prevents double unlock |
| `condvar` ctor `94-100` | `stdlib_alloc(sizeof(pthread_cond_t))` `:95` | `stdlib_release` on `pthread_cond_init` failure `:97` | O | **good cleanup example**: init failure releases the block |
| `condvar::drop` `126-132` | — | `pthread_cond_destroy` `:128` + `stdlib_release` `:129` | O | idempotent via NULL check |
| `thread::start/join/drop` `148-196` | `pthread_create` `:164` | `pthread_join` in `join`/`drop` `:182,:193` | O (joinable) | `joinable` reset on both success and failure `:183,:194` |

---

## 4. Reproduced defects (runtime evidence)

### 4.1 REPRO-1 (new) — `stdlib_realloc` bypasses stdlib fault injection and `stdlib_alloc_seq`

**Probe** (`probe_realloc_inject.cp`, compiled with
`build/c+ build ... --sanitize=none`, run on this branch):

```
initial_alloc=0x1022c98d0 size=32
seq_before=0
realloc_under_fail_after=0x1022c98f0 new_size=64 seq_after=0
control_stdlib_alloc=0x0
```

After `stdlib_alloc_fail_after(0)` ("every later allocation fails"), a
`stdlib_realloc(p, 64)` **succeeds**, changes size to 64, and leaves `seq` at 0,
while a control `stdlib_alloc(16)` correctly returns NULL. Cause:
`stdlib_realloc` calls `cplus_realloc` directly (`error.hp:446`) and never
executes the injection/seq block that exists only in `stdlib_alloc`
(`error.hp:377-384`). The existing test comment acknowledges the split:
`tests/libc/test_stdlib_realloc.cp:3` says injection for `cplus_realloc` is
covered by the *libc-layer* fault matrix, not by `stdlib_alloc_fail_at/after`.
Impact: any stdlib-level fault-injection sweep that uses `stdlib_alloc_seq` to
enumerate allocation points will silently omit every realloc call site.

### 4.2 REPRO-2 (new) — `cplus_aligned_alloc` is registered and freeable via `cplus_free`, contradicting its own contract

**Probe** (`probe_aligned.c`, linked against `build/libc/libcplus_hardened.a`):

```
aligned=0x7bd1004000 live=1 size=128
after cplus_free live=0
```

The current `stdlib_c17.c:50-54,83` calls `cplus_alloc_meta_register`, so the
block is live in `g_tab` and `cplus_free` releases it successfully on this
POSIX host. This directly contradicts the same file's header
`stdlib_c17.c:13-23`, which says the memory is "intentionally NOT routed
through cplus_malloc", is "untracked", and "MUST be released with the host
free() -- never with cplus_free() or free_checked()". It also makes the older
`docs/libc/AUDIT_alloc.md` F-03 "untracked" wording stale for POSIX.
Cross-platform hazard (static, not reproduced here): the Windows branch uses
`_aligned_malloc` (`:75`), but `cplus_free` ultimately calls host `free()`
(`alloc.c:382`), which is invalid for `_aligned_malloc` blocks per this file's
own warning at `:20-21`.

### 4.3 REPRO-3 (new, benign) — `stdlib_calloc` burns two generations per allocation

**Probe** (`probe_dual_meta.cp`):

```
stdlib_alloc  gen=1 size=16
stdlib_calloc gen=3 size=32 elem=8 len=4
```

`stdlib_calloc` calls `stdlib_alloc` (records metadata at `error.hp:387`) and
then calls `stdlib_meta_record` again at `:411` to set the real `elem_size`.
The global `meta_gen` therefore advances twice for one block. No memory safety
impact, but it confirms the redundant dual-record path and makes stdlib
generations numerically diverge from `cplus` generations (see §5.1).

### 4.4 REPRO-4 and REPRO-5 (prior evidence, cited — not re-run)

These are pre-existing reproduced items; I cite the existing evidence rather
than claim a new run:

* **Stale-alias free/transfer (F-01)** — `docs/stdlib/memory_ownership/MEMORY_SAFETY_AUDIT.md:178-213`.
  Probe output there: `B=... gen=2 same_addr=1` then "free via stale alias:
  rc=0 B still live=0", i.e. the *new* owner was freed. Root cause:
  `cplus_free_checked` (`alloc.c:369-383`), `cplus_realloc` (`:306`), and
  `cplus_transfer_ownership` (`:547`) match on address without generation.
* **Recent-cache saturation misclassification (F-06)** — same doc `:215-239`.
  Probe output: `saturated double-free: rc=22 double_delta=0 invalid_delta=1`;
  `recent_add` (`alloc.c:85-101`) evicts the victim entry when the 1024-slot
  table is full. No host-level corruption — the double free is still refused;
  this is a diagnostic-counter defect.

---

## 5. Static observations (not reproduced in this audit)

### 5.1 Dual metadata tables can diverge — ownership ambiguity

`stdlib_alloc` registers each block in **both** `g_tab` (`alloc.c:253` via
`cplus_alloc`) and `stdlib_state.meta_tab` (`error.hp:387`). The tables keep
independent `byte_size`, `elem_size`, and generation counters. `stdlib_calloc`'s
double record (§4.3) is a concrete divergence. Consequences:
* a generation token from one table is meaningless to the other
  (`stdlib_alloc_check_generation` vs `cplus_alloc_check_generation`);
* a block registered by `cplus_alloc_meta_register` (`stdlib_c17.c:83`) exists
  in `g_tab` but not in `meta_tab`, so `stdlib_realloc` refuses it as
  "not a live stdlib block" (`error.hp:436-442`) even though `cplus_realloc`
  would accept it;
* `stdlib_release` (`error.hp:585`) forgets only the stdlib table and relies on
  `cplus_release` to update `g_tab`. Any path that updates one table directly
  leaves the other stale.

### 5.2 `stdlib_release` and `stdlib_mem_release` are generation-blind

`stdlib_mem_release` (`memory.hp:247-252`) has no token parameter and calls
`stdlib_release` unconditionally. `stdlib_release` itself performs no
generation or liveness check (`error.hp:582-588`). This is the stdlib-layer
mirror of F-01 (§4.4): a stale alias whose address was recycled can release a
new live block. `stdlib_mem_check`/`stdlib_mem_check_token` (`memory.hp:98-119`)
*can* prevent this, but only when the caller separately supplies a captured
token — `stdlib_mem_release` cannot be guarded that way.

### 5.3 No destructors; "RAII" is manual `drop()` and owning classes have no copy control

No `.hp` class in `stdlib/` declares a destructor (`grep '~' stdlib/*.hp` is
empty). Examples of cleanup that is therefore not automatic:
`string::drop` (`string.hp:400-407`), `vector::drop` (`vector.hp:297-305`),
all seven `collections.hp` drops, `json::drop` (`json.hp:172-177`),
`file::drop` (`file.hp:225-230`), `mutex::drop` (`thread.hp:54-59`).
Headers nevertheless say "RAII" (`string.hp:4`, `json.hp:3`, `thread.hp:1`) or
"the destructor closes" (`file.hp:3-4`). Ownership ambiguity: an early return
before `drop()` leaks, and any value copy of an owning class would shallow-copy
the pointer and double-release. Copy/move semantics were not verified with the
C+ compiler, so this remains STATIC, not a reproduced double-free.

### 5.4 32-bit undefined behaviour in both hash functions

`error.hp:267` and `alloc.c:53,55,57` compute `x >> 33` where `x` is
`uintptr_t`. On a 32-bit target (a supported build per `stdlib_c17.c:71-78`'s
`_WIN32` branch) the shift count exceeds the type width — undefined behaviour.
Not reproduced on this 64-bit host. Both tables use the same defective hash.

### 5.5 Inconsistent failure reporting / telemetry on capacity rejection

These paths return -1 (or NULL) **without** `stdlib_note`, so the failure is
absent from `STDLIB_TM_API_FAIL` and from the error callback, unlike equivalent
guards elsewhere:
`vector::insert:198`, `vector::copy_from:245`, `stack::push:957`,
`queue::push:1027`, `string::copy_from:317-318`, `string` ctor `:126`,
`strmap::stdlib_copy_key:339-340`, `strmap::stdlib_rebuild:372-373`,
`intmap::stdlib_rebuild:170-171`, `intset::stdlib_rebuild:573-574`,
`deque::stdlib_make_room:727`.
Also `stdlib_realloc` reports an over-limit request as `RANGE/BOUNDS`
(`error.hp:427-429`) while `stdlib_alloc` reports the same condition as
`MEMORY/RESOURCE` (`:373-375`).

### 5.6 `stdlib_meta_record` has no OOM reporting

If the side-table `calloc` at `error.hp:307` fails, `stdlib_meta_record`
silently skips growth and continues inserting into the old table
(`:308-323`). There is no `stdlib_note`, and side-table allocations are not
counted by telemetry. The corresponding libc path *does* fail cleanly
(`alloc.c:247-251` releases the just-allocated block and returns ENOMEM).

### 5.7 JSON uses a third allocator outside the checked model

`json.hp` allocates with `yyjson_*` (`:136,:153,:190,:211,:214,:282,:293`) and
releases serialized buffers with raw `free()` in `json_free` (`:202-204`).
These blocks are invisible to both metadata tables, to fault injection, and to
telemetry. Passing a `write()` result to `stdlib_release` would be reported as
an invalid free by the runtime. The header documents `json_free()` as the
matching deallocator (`:7-8`), so this is an allocation-model inconsistency,
not a leak.

### 5.8 Additional inherited libc limits (documented, not re-proved here)

* F-04: host `realloc` runs with `g_meta_lock` released (`alloc.c:325-352`); the
  ABA generation guard protects bookkeeping only.
* F-11: `cplus_free` discards the checked return (`:389-395`), and
  `cplus_free_checked` nulls `*inout` even on refusal (`:380`).
* F-15: zero-size requests uniformly normalize to a tracked 1-byte block
  (`error.hp:370-371,406,423`; `alloc.c:236,266-268,302`); `realloc(p,0)` does
  not free.
* F-20: `cplus_aligned_alloc` has no size cap (`stdlib_c17.c:65-77`).

### 5.9 Positive cleanup paths (explicitly checked, no defect)

* `condvar` ctor releases its `stdlib_alloc` block when `pthread_cond_init`
  fails (`thread.hp:95-99`); `drop` destroys then releases (`:126-132`).
* Every multi-buffer rebuild in `collections.hp` allocates all buffers first
  and rolls back partial allocations: `intmap:172-185`, `strmap:374-387`,
  `intset:575-582`.
* `cplus_malloc`/`cplus_calloc` release the just-allocated block when metadata
  reservation fails (`alloc.c:247-251,283-287`).
* `stdlib_realloc` is failure-atomic with respect to `meta_tab` on host
  realloc failure (`error.hp:447-448`), and `cplus_realloc` preserves the old
  block and generation on injected/reserve failure (`alloc.c:318-322`).

---

## 6. Summary of top findings

1. **REPRO-1** — `stdlib_realloc` ignores `stdlib_alloc_fail_at/after` and
   `stdlib_alloc_seq`; stdlib-level fault-injection coverage has a blind spot
   (`error.hp:422-456` vs `:365-392`).
2. **REPRO-2 / DRIFT** — `cplus_aligned_alloc` is now registered in the C+
   side table and freeable via `cplus_free`, contradicting `stdlib_c17.c:13-23`
   and creating a Windows `_aligned_free` hazard (`:75,:83`).
3. **REPRO-4 (cited)** — generation-blind checked free/realloc/transfer via a
   stale alias frees/transfers the new owner (`alloc.c:369-383,:306,:547`).
4. **STATIC** — two independent metadata tables with divergent generations and
   membership (`error.hp:117-121` vs `alloc.c:40-49`); cross-layer tokens and
   pointers are not interchangeable.
5. **STATIC** — no destructors and no copy control on any owning `.hp` class;
   cleanup is manual `drop()` despite "RAII" claims in headers.
6. **REPRO-5 (cited)** — recent-free cache saturation misclassifies a genuine
   double free as an invalid free (`alloc.c:85-101`); no host-level corruption.
7. **STATIC** — fault-injection bypass extends to the metadata `calloc`
   (`error.hp:307`) and telemetry omits it; capacity-rejection paths across
   containers report failures inconsistently (§5.5).

No counts of containers, bugs, or allocation sites were synthesized; the only
numbers quoted are those printed by the probes in §4 or cited from the
referenced documents.
