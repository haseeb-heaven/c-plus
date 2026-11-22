# Upstream study: SaferCPlusPlus (`duneroadrunner/SaferCPlusPlus`) — safe pointers, lifetime, ownership

Status: **study report only**. This document does not edit headers, `stdlib/*.hp`, `compiler/**`,
tests, or build files, and does not `git add`/commit. It records what the actual upstream source
says, which exact URLs were fetched, and how the ideas map onto this repository's C+ standard
library.

Method: raw source attempted from the URLs in the task brief, then real paths discovered through
the GitHub contents/tree APIs, then a shallow clone into
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/safercpp-upstream` for
line-accurate `grep`/`read` of the large headers. Clone HEAD:
`5edaf67acf746d9ee472a06a51094b31a3db57c0` (API tree SHA agrees). Default branch: `master`.

Evidence labels used below:

- **[SRC]** — directly observed in fetched or cloned upstream text; file and line/identifier given.
- **[INF]** — inference or design recommendation drawn from that source; not a claim that upstream
  or this repository does it.

Local audit citations use the path that actually exists on this branch:
`tests/stdlib/agent2_audit/module_audits/{vector,string,file}.hp.md`. The task brief named
`docs/stdlib/agent2_audit/module_audits/`; **that directory does not exist** here (checked with
`find docs/stdlib -path '*agent2_audit*'`). No path is invented below.

---

## 0. Fetch log (exact URLs and outcomes)

| # | Exact URL | Result | Notes |
|---|---|---|---|
| 1 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/LICENSE.txt` | **404** | Task-suggested. Real name is `LICENSE_1_0.txt`. |
| 2 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/include/msl/scope.h` | **404** | Task-suggested. No `include/msl/` directory on `master`. |
| 3 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/include/msl/secalloc.h` | **404** | Task-suggested. No `include/msl/` directory. |
| 4 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/include/msl/utility.h` | **404** | Task-suggested. No `include/msl/` directory. |
| 5 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/include/msl/saferpointerarray.h` | **404** | Task-suggested. No `include/msl/` directory. |
| 6 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/include/msepointer.h` | **404** | Task-suggested. Real file is `include/msepointerbasics.h`. |
| 7 | `https://api.github.com/repos/duneroadrunner/SaferCPlusPlus` | **SUCCESS** | `default_branch: master`, `license: {key: bsl-1.0, spdx_id: BSL-1.0}`, `pushed_at: 2026-10-12`, `updated_at: 2026-11-10`. |
| 8 | `https://api.github.com/repos/duneroadrunner/SaferCPlusPlus/contents/` | **SUCCESS** | Root has `LICENSE_1_0.txt`, `README.md`, `README2.md`, `examples/`, `include/`, `msetl.vcxproj`. |
| 9 | `https://api.github.com/repos/duneroadrunner/SaferCPlusPlus/contents/include?ref=master` | **SUCCESS** | 30 flat `include/mse*.h` files; **no `msl/`, no `msepointer.h`, no `secalloc.h`, no `saferpointerarray.h`, no `scope.h`**. Relevant: `msepointerbasics.h` (117,694 B), `msescope.h` (213,715 B), `msenorad.h` (84,092 B), `mseregistered.h` (55,173 B), `mserefcounting.h` (149,204 B), `msemsearray.h` (832,525 B), `msemsevector.h` (465,581 B), `msemstdarray.h`, `msemstdvector.h`, `mseivector.h`, `mseasyncshared.h` (207,440 B), `msescopeatomic.h`, `msestaticimmutable.h`, `mseslta.h`, `mseany.h`, `msethreadlocal.h`, `msesuppresschecks.h`. |
| 10 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/LICENSE_1_0.txt` | **SUCCESS** | Full BSL-1.0 text quoted in §1. |
| 11 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/include/msepointerbasics.h` | **SUCCESS** | 2,558 lines; webfetch displayed 1–1088 and saved the full text to a tool-output file; complete file re-read from clone. |
| 12 | `https://api.github.com/repos/duneroadrunner/SaferCPlusPlus/git/trees/master?recursive=1` | **SUCCESS** | Tree SHA `5edaf67…`; **no `tests/` directory**; test-like sources live under `examples/msetl_example/` (`msetl_example.cpp`, `msetl_example2.cpp`, `msetl_example3.cpp`, `msevector_test.h`, `msetl_example_defs.h`). |
| 13 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/examples/msetl_example/msetl_example_defs.h` | **SUCCESS** | Compile-time directive documentation; `#define MSE_SELF_TESTS`. |
| 14 | `https://raw.githubusercontent.com/duneroadrunner/SaferCPlusPlus/master/examples/msetl_example/msevector_test.h` | **SUCCESS** | 2,325-line upstream unit test; basis for §7. |
| 15 | local `git clone --depth 1` (URL from API `clone_url`) | **SUCCESS** | Used only for line-accurate reading of headers/test files; clone lives outside this repository and no local file other than this report was created. |

All five source URLs suggested by the brief (rows 1–6) are stale: upstream flattened `include/msl/*`
into `include/mse*.h` and renamed the license. Row 9/12 prove the replacements.

---

## 1. License

Every inspected upstream header carries this notice **[SRC]** (`msepointerbasics.h:1-3`,
`msescope.h:1-3`, `msenorad.h:1-3`, `msemsearray.h:1-3`, `msenorad.h:1-3`,
`examples/msetl_example/msevector_test.h:1-3`):

> Copyright (c) 2015 Noah Lopez
> Use, modification, and distribution is subject to the Boost Software
> License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
> http://www.boost.org/LICENSE_1_0.txt)

`LICENSE_1_0.txt` begins **[SRC]**:

> Boost Software License - Version 1.0 - August 17th, 2003

and grants use/reproduction/distribution "all subject to the following" conditions. The GitHub API
license object is `{"key":"bsl-1.0","name":"Boost Software License 1.0","spdx_id":"BSL-1.0"}`.

**Exact identifier for attribution: Boost Software License 1.0 (BSL-1.0), Copyright (c) 2015 Noah
Lopez.** This repository's own `LICENSE` is "MIT License / Copyright (c) 2026 Haseeb Mir"; the two
are compatible, but **verbatim code reuse would require retaining the BSL notice**. Everything in
§9–§11 is a reimplementation of a mechanism or API shape, not copied text.

---

## 2. Safe-pointer lifetime tracking mechanism

### 2.1 Scope pointers: compile-time lifetime by construction

- Scope pointers are, by default, *raw pointers with static restrictions*, not runtime-checked
  objects **[SRC]** (`README.md:784-788`): "By default, (non-owning) scope pointers are just raw
  pointers and type aliases of raw pointers … rely entirely on a static analysis/enforcement tool
  like scpptool … you can define `MSE_SCOPEPOINTER_ENABLED` … [or]
  `MSE_SCOPEPOINTER_RUNTIME_CHECKS_ENABLED` … at a significant performance cost." The same
  source notes scope pointers are "just an alias for native pointers in non-debug builds" by
  default (`msepointerbasics.h:130-138`: `MSE_SCOPEPOINTER_DISABLED` is defined under `NDEBUG`
  unless `MSE_SCOPEPOINTER_RUNTIME_CHECKS_ENABLED` is set).
- The type-level machinery is in `msepointerbasics.h`: empty marker base classes
  `XScopeTagBase`, `ReferenceableByScopePointerTagBase` ("objects that might have a scope pointer
  targeting them must live to the end of the scope in which they are declared"),
  `ContainsNonOwningScopeReferenceTagBase` ("not, in the general case, safe to assign the value of
  a scope pointer to another object as that object may outlive the target") **[SRC]**
  (`msepointerbasics.h:1095-1130`). Template predicates `is_potentially_xscope` /
  `is_potentially_not_xscope` and `T_valid_if_not_an_xscope_type()` turn misuse into compile errors
  **[SRC]** (`msepointerbasics.h:1225-1270`).
- `msescope.h` supplies `TXScopeObj<T>` (transparent wrapper making a local a "scope object"),
  `TXScopeFixedPointer<T>`, `TXScopeNotNullPointer`, `TXScopeOwnerPointer`, and a *type-level
  lifetime tag*: `typedef size_t lifetime_info1_t; static const lifetime_info1_t no_lifetime_info1
  = 0;` used as an extra template parameter on all of those types **[SRC]**
  (`msescope.h:140-158, 198, 958-988`). When scope pointers are disabled these collapse to aliases
  such as `template<typename _Ty, lifetime_info1_t …> using TXScopeFixedPointer = _Ty*` **[SRC]**
  (`msescope.h:146-154`).
- Optional runtime checking is implemented by type erasure: with
  `MSE_SCOPEPOINTER_RUNTIME_CHECKS_ENABLED`, `TXScopeObjBase = mse::TNDNoradObj<_TROz>` and
  `TXScopeItemPointerBase = xsrtc::TAnyPointerBaseV1<_Ty>` **[SRC]**
  (`msescope.h:829-831`), where `TAnyPointerBaseV1` stores a type-erased safe pointer in a custom
  `any` (SOO storage + dynamic vtable) and only dereferences through the stored pointer's own
  safety check **[SRC]** (`msescope.h:660-735`).

### 2.2 "Lifetime tokens" are annotations for an external analyzer, not a runtime generation id

- `mseslta.h` defines `LT(...)`, `LTP(...)`, `LT_RETVAL(...)`, `LT_THIS(...)`, `LT_ENCOMPASSES(x,y)`,
  `LT_FIRST_CAN_BE_ASSIGNED_TO_SECOND(x,y)` — all expand to
  `__attribute__((annotate("mse::lifetime_…")))` on clang **[SRC]** (`mseslta.h:104-112`, with the
  generic `MSE_ATTR_STR` defined in `msepointerbasics.h:281-293`). Example **[SRC]**
  (`mseslta.h:83-93`): `MSE_INHERIT_LTA_ASSIGNMENT_OPERATOR_FROM` emits
  `mse::lifetime_notes{ label(mse_ilaof_42); this(mse_ilaof_42); return_value(mse_ilaof_42) }`.
- The file's own comment states the XSLTA (lifetime-annotated) types are "enforced by a static
  analyzer like scpptool, which doesn't rely on type system indicators like this one" **[SRC]**
  (`mseslta.h:116-122`). `README.md:5,19,275,788` likewise says full safety requires scpptool.
  **[INF]** Therefore "lifetime tokens" in this codebase are *source-level labels consumed by a
  compiler/analyzer*, materially different from this repository's runtime `generation` counter in
  `vector.hp` (`vector.hp:32,47-50,292-293`).

### 2.3 Runtime lifetime tracking without a compiler: registered pointers

- `TNDRegisteredObj` keeps an **intrusive singly linked list of outstanding pointers**
  (`mutable const CRegisteredNode * m_head_ptr`), with `register_pointer()` /
  `unregister_pointer()`; its destructor calls `unregister_and_set_outstanding_pointers_to_null()`,
  which walks the list, calls `rn_set_pointer_to_null()` on each node and unlinks it **[SRC]**
  (`mseregistered.h:540-552, 578-616`). `CRegisteredNode` implements
  `rn_set_pointer_to_null() → spb_set_to_null()` **[SRC]** (`mseregistered.h:276`, `363`).
  Effect **[SRC]** (`README.md:445`): when the target is destroyed, registered pointers are
  automatically set to `nullptr`, so dereferencing them throws rather than reading freed memory.
- Cost tradeoff **[SRC]** (`README.md:283`): `TRegisteredPointer<>` retargeting is *O(n)* in the
  number of pointers targeting the object, `TCRegisteredPointer<>` is *O(1)*.
- Heap allocations use a **thread-local allocation registry** in `msepointerbasics.h:2424-2522`:
  `CSAllocRegistry` "maintains a list of all objects allocated by a registered 'new' call and not
  (yet) subsequently deallocated with a corresponding registered delete", with a fast array of 8
  pointers plus an `std::unordered_set` and `thread_local static CSAllocRegistry
  tlSAllocRegistry_ref()`. `registered_new`/`registered_delete` register/unregister and throw
  `std::invalid_argument("invalid argument, no corresponding allocation found …")` on mismatch
  **[SRC]** (`mseregistered.h:629-643`).

### 2.4 Runtime lifetime tracking, cheaper: norad pointers

- `TGNoradPointer` holds a `TGNoradObj*` and increments/decrements a `mutable _TRefCounter
  m_counter = 0` on copy/assign/destruct **[SRC]** (`msenorad.h:237-264, 670-672`). Its own comment
  **[SRC]** (`msenorad.h:229-232`) is explicit: "where registered pointers are automatically set
  to `nullptr` when their target is destroyed, the destruction of an object while a 'norad' pointer
  is still targeting it results in program termination. This drastic consequence allows norad
  pointers' run-time safety mechanism to be very lightweight (compared to that of registered
  pointers)."
- Enforcement **[SRC]** (`msenorad.h:639-645`): `~TGNoradObj()` does
  `if (0 != m_counter) { … assert(false); std::terminate(); }`. Heap forms
  `ndnorad_new`/`ndnorad_delete` use the same thread-local `CSAllocRegistry` as registered pointers
  **[SRC]** (`msenorad.h:693-708`).
- **[INF]** This is the closest upstream analogue of a *runtime* "lifetime token": liveness is
  tied to a counter stored in the target object, checked at destruction (fail-fast), rather than a
  generation number stored in the owner and compared by the borrower (refusal).

---

## 3. Bounds-checked array / span-like types

- Error taxonomy is per family, all deriving from standard exceptions **[SRC]**:
  `nii_array_range_error`, `nii_array_null_dereference_error`, `msearray_range_error`,
  `msearray_null_dereference_error` (`msemsearray.h:147-160`);
  `gnii_vector_range_error`, `msevector_range_error` (`msemsevector.h:105-117`);
  `mstdarray_range_error` (`msemstdarray.h:76-79`); `mstdvector_range_error`
  (`msemstdvector.h:114`); `ivector_range_error` (`mseivector.h:35-38`);
  `primitives_range_error` (`mseprimitives.h:206`).
- `nii_array_base::operator[]` is **always checked**: it forwards to `at()`, which forwards to
  `std::array::at()` **[SRC]** (`msemsearray.h:3133-3141, 3186-3194`). Same pattern in
  `gnii_vector` (`msemsevector.h:1010-1015, 1141-1149`). `front()/back()` on empty throw explicit
  `*_range_error("front() on empty …")` **[SRC]** (`msemsearray.h:3139-3152`).
- The span analogue is `TRandomAccessSection` / `TRandomAccessConstSection` (plus
  `TXScopeRandomAccessSection`, `TXSLTARandomAccessSection` variants) **[SRC]**
  (`msemsearray.h:7542-7543, 8003-8006, 9184`). Storage is just **iterator + count**
  (`size_type m_count = 0; _TRAIterator m_start_iter;`) and every access is checked
  **[SRC]** (`msemsearray.h:8040-8058, 8653-8666`):

  ```cpp
  const_reference operator[](size_type _P) const {
      if (m_count <= _P) { MSE_THROW(msearray_range_error("out of bounds index - …")); }
      return m_start_iter[difference_type(mse::msear_as_a_size_t(_P))];
  }
  ```

  `at()` simply calls `operator[]`; `front()/back()` reject empty. `subsection(pos, n)` rejects
  `pos > size()` and clamps `n` with `std::min(n, size() - pos)` **[SRC]**
  (`msemsearray.h:8427-8429`); `remove_prefix(n)` / `remove_suffix(n)` reject `n > size()`
  **[SRC]** (`msemsearray.h:8187-8193, 8828-8834`).
- **[INF]** The section itself carries **no generation/owner liveness token** — safety comes from
  the iterator type being a scope/registered/norad pointer (or from an enclosing structure lock),
  not from a counter inside the section. That is the key structural contrast with this repository's
  `vector::slice` (`vector.hp:29-85`), which stores `generation_ref` + `generation_at`.
- `msear_pointer<T>` defaults to null and throws `msearray_null_dereference_error("attempt to
  dereference null pointer - mse::msear_pointer")` on dereference **[SRC]**
  (`msemsearray.h:165-186`); `mse::ivector` is a fixed-capacity array-like with its own
  `ivector_range_error` **[SRC]** (`mseivector.h:35-38`).

---

## 4. Ownership transfer rules

- **Scope ownership is deliberately non-transferable.** `TXScopeOwnerPointer<>` is described as
  "kind of like a `const std::unique_ptr<>` that cannot be null … must live to the end of the scope
  in which it is declared … Unlike `std::unique_ptr<>`s, you may not use `std::move()` with
  `TXScopeOwnerPointer<>`s" **[SRC]** (`README.md:806, 846`). This is a *restriction*, not a move
  protocol.
- **Ownership/authority is expressed by marker base classes** in `msepointerbasics.h:2148-2166`:
  `StrongPointerTagBase`, `NeverNullTagBase`, `StrongPointerNeverNullTagBase`,
  `StrongExclusivePointerTagBase`, `ExclusivePointerTagBase`, and the async markers
  `AsyncNotShareableTagBase` / `AsyncNotPassableTagBase` /
  `AsyncNotShareableAndNotPassableTagBase`. `TPointer` and `TPointerForLegacy` derive from
  `AsyncNotShareableAndNotPassableTagBase` **[SRC]** (`msepointerbasics.h:1512, 1597`).
- **Reference counting** aliases to the standard library when disabled
  (`template <class X> using TRefCountingPointer = std::shared_ptr<X>`, etc.,
  `mserefcounting.h:111-117`); the non-disabled forms add `TRefCountingNotNullPointer`,
  `TRefCountingFixedPointer`, `TRefCountingConstPointer`, and
  `refcounting_null_dereference_error` **[SRC]** (`mserefcounting.h:79-83, 134-153, 326`).
  There is no dedicated `weak_ptr`: "TRefCountingPointer<> does not have a specific associated
  weak pointer … registered pointers can be thought of as sort of independent, universal weak
  pointers" **[SRC]** (`README.md:612-614`).
- **Single-owner / non-copyable ownership** exists as `TSingleOwnerPointer<>` with
  `single_owner_null_dereference_error` **[SRC]** (`mserefcounting.h:1840`); to lend a scope pointer
  into such a non-copyable owner, upstream uses
  `make_xscope_borrowing_strong_pointer_store()` which takes a *scope pointer to the owner*, not
  the owner itself **[SRC]** (`README.md:921`).
- **Async ownership transfer** in `mseasyncshared.h`: `TTaggedUniquePtr<T> : std::unique_ptr<T>,
  StrongExclusivePointerTagBase` **[SRC]** (`mseasyncshared.h:1759-1761`);
  `TAsyncSharedV2ReadWriteAccessRequester<T>` wraps it (`make_asyncsharedv2readwrite<X>(args…)`,
  `mseasyncshared.h:1815-1818`); `TAsyncSharedV2WeakReadWriteAccessRequester` offers
  `try_strong_access_requester()` returning `mstd::optional<TAsyncSharedV2ReadWriteAccessRequester>`
  **[SRC]** (`mseasyncshared.h:1821-1831`).
- **Invalid ownership operations are detectable at runtime**: `registered_new`/`registered_delete`
  and `ndnorad_new`/`ndnorad_delete` both consult `CSAllocRegistry` and throw
  `std::invalid_argument("invalid argument, no corresponding allocation found …")` on a missing or
  wrong registry entry **[SRC]** (`mseregistered.h:629-643`, `msenorad.h:693-708`).
- **[INF]** Upstream's transfer story is "restrict at compile time (scpptool/tags), detect at
  runtime (intrusive pointer lists, refcounts, allocation registries)". It does **not** provide a
  borrow-checker-style "move invalidates all aliases" guarantee; `README.md:261` explicitly says
  it does not universally restrict the number of mutable references the way Rust does.

---

## 5. Thread-safety constructs

- `mseasyncshared.h` builds on `std::shared_timed_mutex` but supplies its own
  `recursive_shared_timed_mutex`, commented as allowing "a thread to hold 'read' (shared) locks and
  'write' locks at the same time" with additional `nonrecursive_lock()` functions
  **[SRC]** (`mseasyncshared.h:101-104`). `unlock_guard<Mutex>` temporarily releases with
  re-lock in the destructor **[SRC]** (`mseasyncshared.h:85-96`).
- The access model is lease + lock + pointer kind **[SRC]** (`mseasyncshared.h:716-729,
  741-803, 960-1043`): `TAsyncSharedXWPAccessLeaseObj<Lease>` holds the lease and the mutex;
  `TAsyncSharedV2ReadWritePointer` owns a `std::unique_lock<async_shared_timed_mutex_type>`,
  `TAsyncSharedV2ReadOnlyConstPointer` owns a `std::shared_lock<…>`, and
  `TAsyncSharedV2ExclusiveReadWritePointer` uses `unique_nonrecursive_lock`. `TXScope…` variants
  additionally derive from `XScopeTagBase`. `readlock_ptr()` / `writelock_ptr()` are the access
  points **[SRC]** (`mseasyncshared.h:2217, 2244`).
- `mseasyncshared.h:342` states registered pointers "cannot be shared among asynchronous threads
  (enforced by the type system and scpptool)"; cross-thread sharing goes through these async
  types. `AsyncNotShareableTagBase`/`AsyncNotPassableTagBase` are the markers.
- For atomics, `msestaticimmutable.h:87-93` defines
  `TCheckedThreadSafePointer<T> = TGNoradPointer<T, atomic_int_t>` with the comment "essentially
  just a simplified TNoradPointer<> with an atomic refcounter"; `msescopeatomic.h` uses
  `TCheckedThreadSafeObj<std::atomic<T>>` for `TXScopeAtomicObj` when scope-pointer runtime checks
  are enabled **[SRC]** (`msescopeatomic.h:85-95`).
- `msethreadlocal.h` provides thread-local pointer types (no `lifetime` occurrences in that header
  per local grep) and `CSAllocRegistry` itself is `thread_local` **[SRC]**
  (`msepointerbasics.h:2519-2522`).
- **[INF]** The reusable parts for C+ are the *lease-acquire/release pairing* and the distinction
  between shared-read and exclusive-write handles; the C++ mutex/atomic/type-marker machinery is not
  directly expressible in `.hp` (see §10).

---

## 6. How invalid access is reported

- Central macro **[SRC]** (`msepointerbasics.h:271-279`, repeated per header): `#ifdef
  MSE_CUSTOM_THROW_DEFINITION → #define MSE_THROW(x) MSE_CUSTOM_THROW_DEFINITION(x)` else
  `#define MSE_THROW(x) throw(x)`. With exceptions disabled, `MSE_CUSTOM_THROW_DEFINITION(x)` is
  defined as `exit(-11)` **[SRC]** (`msepointerbasics.h:99-101`). The sample config documents the
  override explicitly: "[The following will result in program termination instead of exceptions
  being thrown when an invalid memory access is attempted]" **[SRC]**
  (`examples/msetl_example/msetl_example_defs.h:44-47`).
- Thrown types and representative **exact messages seen in source** **[SRC]**:
  - `primitives_null_dereference_error("attempt to dereference null pointer - mse::TNoradPointer")`
    (`msenorad.h:273-278`);
  - `nii_array_range_error("front() on empty - … - nii_array_base")` (`msemsearray.h:3139`);
  - `msearray_range_error("out of bounds index - reference operator[](size_type _P) - TRandomAccessConstSectionBase")`
    (`msemsearray.h:8041`);
  - `std::invalid_argument("invalid argument, no corresponding allocation found - mse::ndnorad_delete() …")`
    (`msenorad.h:701-708`);
  - `asyncshared_runtime_error : std::runtime_error` and
    `asyncshared_use_of_invalid_pointer_error : std::logic_error` (`mseasyncshared.h:78-82`);
  - `norad_cannot_verify_cast_error`, `refcounting_null_dereference_error`,
    `single_owner_null_dereference_error`, `structure_lock_violation_error` (`msenorad.h:190`,
    `mserefcounting.h:79,1840`, `mseoptional.h:95`).
- **Fatal rather than recoverable** **[SRC]**: `~TGNoradObj()` with outstanding pointers does
  `assert(false); std::terminate();` and optionally calls
  `MSE_CUSTOM_FATAL_ERROR_MESSAGE_HANDLER("Fatal Error: mse::TGNoradObj<> destructed with
  outstanding references \n")` (`msenorad.h:639-645`). `structure_lock_violation_error` is thrown on
  "Attempting to modify …" / "access lock violation - Attempting to access …" (`mseany.h:802-814`).
- **[INF]** C+ already has the recoverable half (`stdlib_note(category, code, message)` + `0`/`-1`
  or sentinel, e.g. `vector.hp:57-67,165-172`; `error.hp:194-222`) and a fatal half
  (`cplus_check_live` → `CPLUS_EXIT_TYPE_CONFUSION` 105, `runtime/cplus_runtime.c:271-280`). The
  upstream *type names and messages* are not portable; the two-tier policy (recoverable refusal for
  bounds/staleness, fatal for UB-class lifetime errors) is.

---

## 7. Inspected upstream tests / cases

- The repository has **no `tests/` directory** **[SRC]** (recursive tree, row 12). `MSE_SELF_TESTS`
  is defined in `examples/msetl_example/msetl_example_defs.h:77` and gates in-header test code in
  e.g. `mseslta.h:15-17` and `msemstdarray.h:811`.
- **`examples/msetl_example/msevector_test.h` (fetched, row 14)** is the main inspected test
  source. It defines `class msevector_test` with `run_all()` invoking 40+ methods
  (`msevec_test_1…7`, `capacity`, `at`, `pointer`, `auto_ref`, `iterators`, `ebo`, `bvec1`,
  `msevec_test_m1…m7`, `_i1…i7`, `_s1…s7`, `_ss1…ss7`, `_mt1…mt7`) across `msevector`, `ivector`,
  `mstd::vector` and "structure-safe" (`ss_begin`/`ss_iterator_type`) and "cipointer"/`ipointer`
  augmented-iterator variants **[SRC]** (`msevector_test.h:30-99`). Relevant cases actually read:
  - `at()` **[SRC]** (`msevector_test.h:375-401`): after `v.push_back(10)`, `v.at(0)==10`, write
    through `v.at(0)`, then `MSE_TRY { v.at(1) = 20; EXAM_CHECK(false); } MSE_CATCH
    (std::out_of_range const&) { … }` — i.e. the checked accessor must throw, not corrupt.
  - `capacity()` (`:405-424`), `iterators()` (`:540-561`), `pointer()` (`:430-441`, vector of
    `TRegisteredPointer<int>`), `msevec_test_m6()` (`:~660-720`, `ipointer` with
    `set_to_beginning()/set_to_end_marker()/points_to_an_item()` and `erase(vit)`), and
    `msevec_test_i7()` (`ivector` with `std::reverse`/`std::sort`) were read directly.
  - The tests use `EXAM_CHECK` macros accumulating failures into `m_result1` (`msevector_test.h:40-47`)
    and `assert` for `EXAM_REQUIRE`, not a test framework.
- **`msetl_example_defs.h` (fetched, row 13)** documents the configuration matrix actually used by
  those tests, including `MSE_SAFER_SUBSTITUTES_DISABLED`, `MSE_SCOPEPOINTER_DISABLED`,
  `MSE_NORADPOINTER_DISABLED`, `MSE_DISABLE_RAW_POINTER_SCOPE_RESTRICTIONS`,
  `MSE_CHAR_STAR_EXEMPTED`, `MSE_SCOPEPOINTER_RUNTIME_CHECKS_ENABLED`,
  `MSE_CUSTOM_THROW_DEFINITION`, `MSE_MSEVECTOR_USE_MSE_PRIMITIVES`,
  `MSE_ENABLE_REENTRANCY_CHECKS_BY_DEFAULT` **[SRC]** (`msetl_example_defs.h:9-75`).
- **[INF]** The pattern worth transplanting is not the test framework but the *matrix*: run the
  same container cases against safe and "disabled" (native) configurations, and specifically pin
  the checked-accessor throw/refusal path. This repository's analogous fixtures are
  `tests/stdlib/deep_error_vector.cp`, `deep_vector_task4.cp`, `deep_string_view.cp`,
  `deep_string_task4.cp`, `deep_file_task2.cp` (cited in the module audits).

---

## 8. Mapping to this repository's C+ stdlib

All local claims below are quoted from the existing audit files at
`tests/stdlib/agent2_audit/module_audits/` (the task brief's `docs/...` path does not exist).

### 8.1 `stdlib/vector.hp` — `slice` generation tracking → lifetime-token idea

- **Current mechanism [SRC local]:** `slice` stores `int *items`, `size_t count`,
  `const size_t *generation_ref`, `size_t generation_at` (`vector.hp:29-39`); `view()` records
  `&this->generation` and the current value (`vector.hp:290-295`); every shape-changing operation
  bumps `generation` (`vector.hp:130,185,208,222,228,257,273,284,303`). `stale()` compares the
  live counter to the snapshot, `valid()` requires non-null/non-empty/fresh, `at_checked()` returns
  `-1` without reading stale memory, `sub()` clamps and propagates the generation reference
  (`vector.hp:47-85`).
- **Exact current limitations [SRC local audit]** (`module_audits/vector.hp.md` §4/§7/§8):
  - "the `generation_ref` points into the owning `vector`, so staleness is only meaningful while
    that object is alive; a slice outliving its owner is explicitly declared undetectable
    (`vector.hp:17-19`)" — listed as **highest residual risk, "by design not testable as a
    recoverable failure"** (§7 item 1).
  - "**Concurrent mutation / thread safety.** No locking; concurrent `push_back`/`view()` is not
    documented as safe" (§7 item 4).
  - "`generation` counter wraparound … a full wrap would make a stale slice compare equal again"
    (§7 item 3).
  - "`slice::sub` on a stale slice returns an empty, **non-stale** slice (`generation_ref==NULL`)"
    is only **[OBSERVED]** (§3 table), i.e. not a documented promise.
  - "Pointer-to-member-state borrowing: `slice` stores `const size_t *generation_ref` into the
    owning object … the reason a slice cannot outlive its owner" (§8).
- **[INF]** Upstream contribution: keep generation-based refusal (already stronger than upstream
  sections, which have no generation at all), but add a *second, orthogonal* liveness signal for
  the owner itself — the upstream registered-pointer pattern (§2.3) or norad counter (§2.4) — so a
  borrowed view can refuse after owner destruction, not just after owner mutation. This is a header/
  runtime design idea; see §9–§10 for what is actually feasible here.

### 8.2 `stdlib/string.hp` — `string_view` → bounds-checked section idea

- **Current mechanism [SRC local]:** `string_view` stores only `const char *chars; size_t count;`
  (`string.hp:80-87`), with `data()`/`size()` snapshots and `equals()`/`is_valid_utf8()` over the
  window (`string.hp:89-113`); `to_view()` returns it (`string.hp:378-383`).
- **Exact current limitations [SRC local audit]** (`module_audits/string.hp.md` §4/§7):
  - "`string_view` owns nothing — it stores only `(const char *chars, size_t count)` … Unlike
    `vector::slice`, it has **no generation tracking and no library-enforced refusal** — detecting
    staleness is the caller's job (`DEEP_VECTOR_TASK4_CONTRACT.md:83`)."
  - "Alias pointer past `count` after `clear()` → overlapping `memcpy` … the standard-level
    overlapping-`memcpy` UB is **not diagnosed** because ASan's `memcpy-overlap` check is
    unavailable … *Highest residual risk in this module*." (§7 item 1)
  - "Concurrent mutation / thread safety. No locking; `string` is not documented as thread-safe."
    (§7 item 7)
  - Embedded NUL cannot be represented through `equals`/`compare`/`find` arguments (§7 item 6).
- **[INF]** Upstream contribution: borrow a *checked section* shape rather than a naked
  pointer+count pair: a small view descriptor that validates every indexed access against its
  recorded extent, with an explicit `substring`/`remove_prefix`/`remove_suffix` that clamps
  (`msemsearray.h:8040-8058, 8427-8429, 8187-8193`). Note that this fixes **bounds**, not
  **liveness**: upstream sections rely on safe iterator types, which C+ does not have (§10).

### 8.3 `stdlib/file.hp` — `file_borrow` → ownership/borrow rules

- **Current mechanism [SRC local]:** `file` carries `FILE *stream` and `int owns`; `file_borrow()`
  sets `owns = 0` (`file.hp:211-216`), `drop()` closes only when `owns` (`file.hp:202-207`), and
  owning copies are rejected by the compiler while `cplus_move` is accepted.
- **Exact current limitations [SRC local audit]** (`module_audits/file.hp.md` §4/§7):
  - "**Known lifecycle limitations [DOCUMENTED]:** `close()` clears the handle even when `fclose`
    fails, so a failed close cannot be retried" (`DEEP_FILE_CONTRACT.md:142-143`).
  - "`file_borrow(NULL)`. The factory accepts any `FILE*` without validation (`file.hp:211-216`);
    no test passes NULL. *Risk:* a borrowed NULL handle yields a closed-but-invalid file" (§7 item 2).
  - "`seek` on a NULL handle … and `flush` on a NULL handle … Guards are documented but no named
    fixture calls them after `close()`" (§7 item 5).
  - Owned-vs-borrowed/copy rejection is "verified by *separate one-shot compiler checks*, not by an
    in-process deep fixture" (§4).
  - "Generation-blind stale alias across families. Checked free/transfer across containers, file
    handles and sockets not exercised" (`tests/stdlib/agent2_audit/known_gaps.md:198`).
- **[INF]** Upstream contribution: the intrusive pointer list of §2.3 is a *runtime* analogue of
  "borrow must not outlive owner". For `file_borrow`, a lighter, local equivalent is to validate at
  the factory (`handle != NULL`) and record a small owner-liveness token; but see §10 — full
  borrow enforcement is Agent-1 territory (`known_gaps.md:99`: "escaped-borrow" rejections are an
  A1-compiler gap; `MODERN_STDLIB.md:154`: "flow analysis of object lifetime … documented gap").

---

## 9. Patterns feasible without Agent 1 compiler work

These rely only on what `.hp` headers and the existing runtime already provide; none require new
compiler analysis, new syntax, or edits to `compiler/**`.

1. **Checked accessor + refusal API (direct transplant of §3 and §6).** Keep `at_checked`-style
   methods that validate `index` and an internal validity flag, return `0`/`-1` or a sentinel,
   call `stdlib_note`, and **never write `*out` on refusal**. Already present in
   `vector.hp:57-67,165-172`; the upstream evidence supports making this the *only* accessor shape
   for borrowed views (e.g. a future `string_view::at_checked`) rather than relying on the caller.
   No compiler work: plain `if` + return, which is exactly the documented `.hp` style
   (`docs/MODERN_STDLIB.md:34-38`).
2. **Checked subrange with clamping and explicit emptiness (§3).** A `sub(start, length)` / span
   helper that rejects `start > size`, clamps `length` with `min`, and returns an explicitly
   empty/invalid descriptor — mirroring `TRandomAccessConstSectionBase::subsection()`. This is
   arithmetic + a struct in a header; `vector::slice::sub()` already does part of it
   (`vector.hp:71-85`), and the same shape can be added to `string_view` without touching the
   compiler. Pair it with the existing `0`/`-1` and `stdlib_note` error model, not with C++
   exceptions.
3. **Typed, two-tier invalid-access reporting and config-driven policy (§6).** Recoverable
   violations go through `stdlib_note(category, code, message)`; UB-class lifetime probes may use
   the existing fatal `cplus_check_live(…)` (`runtime/cplus_runtime.c:271-280`). The upstream
   `MSE_THROW`/`MSE_CUSTOM_THROW_DEFINITION` and `MSE_SELF_TESTS` configuration style (§7) maps to
   compile-time `#define` switches already used in this repository (e.g. `STDLIB_ALLOC_LIMIT`
   override, `CPLUS_SAFE`). No new compiler hooks.
4. **Runtime liveness registry in the existing C runtime (§2.3, partial).** A token table keyed by
   owner address, registered on owner construction and unregistered in `drop()`, with a `valid()`
   probe callable from `.hp`, is a *runtime* change (Agent 2's domain), not a compiler change.
   Caveat **[SRC local]**: `cplus_check_live` is fatal and only understands C+ allocator blocks
   (`runtime/cplus_runtime.c:271-280`), so it cannot serve as a recoverable `slice::valid()` query
   for stack/embedded owners; a new recoverable query would be needed.
5. **Test-matrix discipline (§7).** Pin the refusal/throw path, the empty subrange path, and the
   "disabled/native" comparison in fixtures. This repository already has the deep-test harness;
   adding cases needs no Agent 1 work.

---

## 10. What is impractical here

1. **Static scope-pointer / lifetime-annotation enforcement (§2.1–§2.2).** Upstream's zero-cost
   guarantee depends on clang attributes (`MSE_ATTR_STR → __attribute__((annotate(...)))`),
   template marker bases, deleted/ref-qualified operators, and the external **scpptool** analyzer
   (`README.md:5,788`; `mseslta.h:104-122`). C+ has no templates, attributes, operator overloading,
   or a lifetime analyzer: "Compiler-owned static analysis (lifetime, provenance, unknown-capacity
   raw pointers) remains Agent 1's domain" (`docs/MODERN_STDLIB.md:16`). The audit is explicit that
   "Alias analysis / exclusivity for class owners, slice→source linking", "escaped-borrow"
   rejections, and "Stdlib class bounds metadata (constant OOB via container methods, stale
   slices)" are A1-compiler gaps (`tests/stdlib/agent2_audit/known_gaps.md:98-100,103`).
2. **Full runtime lifetime tracking for arbitrary borrowers (§2.3–§2.4).** An intrusive list of all
   outstanding pointers requires every safe pointer type to be a class with hooks in its
   copy/move/destruct operations and the target to be a wrapper object (`TNDRegisteredObj`). C+
   `.hp` has resource classes with `drop`, but no pointer wrappers with implicit lifetime hooks,
   and adding them is a compiler/language change. Even upstream admits registered pointers' checks
   do not extend to raw references or implicit `this` pointers derived from a dereference
   (`README.md:334-340`).
3. **Transplanting exceptions / `std::terminate` policy (§6).** C++ `throw`, custom exception
   hierarchies, `std::terminate` on destruction-with-outstanding-references, and
   `MSE_CUSTOM_THROW_DEFINITION` are not expressible in `.hp`; the local policy is recoverable
   `stdlib_note` + sentinel with fatal probes reserved for UB-class errors, as the GSL sibling study
   also concluded (`docs/stdlib/upstream_study/microsoft_gsl.md` §10).
4. **Thread-safety construct transplant (§5).** `std::shared_timed_mutex`, `std::unique_lock`,
   `std::atomic` refcounts, and async-shareable/passable type markers have no `.hp` equivalent.
   Local `thread.hp` is a thin wrapper; the deep-thread contract documents surface threading
   only. A lease/lock *API shape* can be imitated with existing thread primitives, but not the
   type-system guarantees.
5. **Copying upstream source verbatim.** BSL-1.0 attribution must be retained (§1); more
   importantly, the source is template/SFINAE/clang-attribute heavy and does not compile as C+.
   Recommendations in §9 are reimplementations of *mechanisms*, not code ports.
6. **Claiming memory safety from these patterns.** The local audits repeatedly state that passing
   fixtures are contract-conformance evidence, not memory-safety proof, and that ASan is
   unavailable on this host (`module_audits/string.hp.md` § safety-claim boundary and §7 item 1;
   `module_audits/vector.hp.md` §7 item 5). This study is source reading only — nothing was built or
   executed here.

---

## 11. Top 3 reusable patterns

1. **Checked view descriptor with refusal, not a naked pointer+count.** Upstream
   `TRandomAccessSection` stores iterator+count and validates every `operator[]`, `front/back`,
   `subsection`, `remove_prefix/suffix` with explicit `msearray_range_error` paths
   (`msemsearray.h:8040-8058, 8187-8193, 8427-8429`). C+ analogue: extend the `at_checked`/`sub`
   refusal API already used by `vector::slice` to `string_view`, preserving `*out` on failure and
   emitting `BOUNDS` notes — feasible today (§9 items 1–2), and it closes the string audit's
   "no library-enforced refusal" gap without a borrow checker.
2. **Two independent liveness signals: mutation generation + owner-liveness token.** C+
   `vector::slice` has only the generation half, which is why "slice outliving its owner" is
   undocumented and undetectable (`vector.hp.md` §7 item 1). Upstream separates compile-time scope
   lifetime from runtime owner lifetime via intrusive pointer lists (registered pointers,
   `mseregistered.h:540-616`) or target-side refcounts with fail-fast destruction (norad pointers,
   `msenorad.h:639-645`). C+ can add a **runtime** liveness token in the existing allocator runtime
   (§9 item 4); the compile-time half needs Agent 1 (§10 items 1–2).
3. **Configuration matrix + typed error taxonomy tested across safe/native modes.** Upstream
   ships `MSE_SELF_TESTS`, `MSE_SCOPEPOINTER_DISABLED`, `MSE_CUSTOM_THROW_DEFINITION`, etc.
   (`msetl_example_defs.h:9-77`) and runs the same container suite against `msevector`, `ivector`,
   `mstd::vector` and structure-safe iterator variants (`msevector_test.h`), with the checked
   throw path explicitly asserted (`at()` test). C+ already has the deep-test harness and
   `CPLUS_EXIT_*` categories; pinning the refusal/throw and empty-subrange cases in both safe and
   strict modes is a high-value, low-cost transplant.

---

## 12. Failures and study limitations

- **All five task-suggested source URLs were 404** (fetch log rows 1–6): `LICENSE.txt`,
  `include/msl/{scope,secalloc,utility,saferpointerarray}.h`, `include/msepointer.h`. Correct paths
  came from the contents/tree APIs; the closest real counterparts are
  `include/msescope.h`, no standalone secalloc, `include/msemsearray.h` / `msemsevector.h`,
  `include/msepointerbasics.h`. No `include/msl/` directory exists on `master`.
- The brief's audit path `docs/stdlib/agent2_audit/module_audits/` does not exist; the real path is
  `tests/stdlib/agent2_audit/module_audits/` and was used for every local citation.
- Large headers (`msepointerbasics.h`, `msevector_test.h`) were truncated in webfetch output; the
  full text was read from the shallow clone. No claim is made about unread regions of the other
  large headers (`msemsearray.h` is 832 KB; `msemstdstring.h`/`mseoptional.h` were only grepped).
- No upstream code was compiled, and no C+ build/test was run for this study. All "current
  limitation" statements about this repository are quotations of committed audit documents, not
  re-verified behavior.
- No wide diff, no pinned release tag beyond the fetched `master` commit `5edaf67…`; line numbers
  are tied to that tree SHA.
- No files other than this report were created, modified, staged, or committed in the working tree.
