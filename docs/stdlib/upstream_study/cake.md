# Upstream study: Cake (`thradams/cake`) — C-compatible ownership, borrowing, defer

Study date: 2026-11-23. Branch: `agent/modern-stdlib`.
Upstream pinned state observed at `main` head: commit `2da2afd1759a7f3a62024ac781af65c8832bbe5f` ("codegen fixes, flow", 2026-11-12), `src/version.h` = `CAKE_VERSION "0.17.07"`. This is the *same* commit already pinned in `docs/UPSTREAM_AUDIT.md`.

> **LEGAL / USE RESTRICTION — read first.** Cake is **GPLv3 plus an AI Model Training Use addendum**, and this project had already **rejected it** before this study (`docs/UPSTREAM_AUDIT.md`: "GPLv3 + addendum forbidding AI training/reference use — **Rejected.** Legally incompatible with this MIT project and the addendum. No code inspected beyond README/license."). The addendum (quoted in §0) forbids using the source as "input, or reference" for AI models. This document therefore records **idea-level observations only for research/traceability**, quotes license identifiers and short API identifiers, and copies **no Cake source, header, or test file** into this repository. Do not treat anything below as an approved integration path; any reuse requires a license decision by the maintainers. I did inspect upstream source because the parent task explicitly requested it; that inspection is itself in tension with the addendum and with the prior rejection — flag this to the integrator.

## 0. License (exact identifier)

Fetched `LICENSE` successfully (`https://raw.githubusercontent.com/thradams/cake/main/LICENSE`, 36,339 bytes). The file begins:

> "Addendum: Restriction on AI Model Training Use
> ... The Software MAY NOT be used, directly or indirectly, in the training, fine-tuning, development, or improvement of any machine learning, artificial intelligence, or similar models ... This includes but is not limited to: Using the Software’s source code or any derivative works as training data, input, or reference; Incorporating the Software in datasets intended for AI model training; Analyzing, transforming, or processing the Software in any manner that results in contributions to AI model learning or optimization."

Then the standard license text:

> "GNU GENERAL PUBLIC LICENSE / Version 3, 29 June 2007
> Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>"

`README.md` §License confirms: "Cake uses the same license as GCC: [GPLv3](LICENSE)."
Identifier to record: **GPL-3.0-or-later + custom AI-training restriction addendum (non-OSI, non-standard)**. Not compatible with this MIT project without explicit legal review.

## 1. Exact URL inventory (success / failure)

All raw URLs use branch `main`; `master` is not the default branch.

| URL | Result |
| --- | --- |
| `https://raw.githubusercontent.com/thradams/cake/master/include/cake.h` | **FAIL** — HTTP 404 (`{"message":"StatusCode: non 2xx status code (404 GET ...)"}`). Wrong branch; no root `include/`. |
| `https://raw.githubusercontent.com/thradams/cake/main/include/cake.h` | Not fetched: root contents API shows **no root `include/` directory**. Real headers live under `src/include/`. Task-assumed path does not exist. |
| `https://api.github.com/repos/thradams/cake/contents/` | OK (root listing: `src/`, `tests/`, `docs/`, `manual.md`, `flow.md`, `quickref.md`, `diagnostics.md`, `rules.md`, `LICENSE`, `README.md`, `ownership.pdf`, …) |
| `https://api.github.com/repos/thradams/cake/contents/src?ref=main` | OK (found `defer.c/.h`, `flow.c/.h`, `flow_branch.c/.h`, `cake_compat.h`, `lib.c` amalgamation, `include/`, `samples/`, `unit_test.c/.h`, …) |
| `https://api.github.com/repos/thradams/cake/contents/tests?ref=main` | OK (`constrains/`, `unit-tests/`, `run-tests/`, `output-test/`, …) |
| `https://api.github.com/repos/thradams/cake/contents/docs?ref=main` | OK (contains `ownership.pdf`, C standard drafts; PDF not fetched — binary) |
| `https://api.github.com/repos/thradams/cake/contents/src/include?ref=main` | OK (annotated stdlib headers: `stdlib.h`, `stdio.h`, `threads.h`, `stddefer.h`, `__cake_types.h`, …) |
| `https://api.github.com/repos/thradams/cake/contents/tests/constrains?ref=main` | OK (note upstream spelling "constrains") |
| `https://api.github.com/repos/thradams/cake/contents/tests/run-tests?ref=main` | OK |
| `https://api.github.com/repos/thradams/cake/commits/main` | OK (SHA/version above) |
| `https://raw.githubusercontent.com/thradams/cake/main/{README.md,LICENSE,quickref.md,flow.md,manual.md}` | OK |
| `https://raw.githubusercontent.com/thradams/cake/main/src/{defer.h,flow.h,flow_branch.h,cake_compat.h}` | OK |
| `https://raw.githubusercontent.com/thradams/cake/main/src/include/{stddefer.h,stdlib.h,stdio.h,threads.h}` | OK |
| `https://raw.githubusercontent.com/thradams/cake/main/tests/run-tests/defer_while.c` | OK |
| `https://raw.githubusercontent.com/thradams/cake/main/tests/unit-tests/flow3/{defer-runs-once-at-function-end.c,defer-runs-before-jump-join.c,borrowed-param-owner-not-consumed-at-exit.c,aliased-owner-lifetime-not-tracked.c,parameter-alias-owner.c,view-struct-strips-owner.c}` | OK (6/6) |
| `https://raw.githubusercontent.com/thradams/cake/main/tests/unit-tests/{flow_conditional_owner_borrow.c,break_out_of_defer_block.c,cwe401_realloc.c}` | OK (3/3) |
| `https://api.github.com/repos/thradams/cake/git/trees/a01367896bbffba2ff79a8d1faf22c6ac852ce12?recursive=1` | OK, 3,764 `.c`/`.h` paths, `truncated: false` (full `unit-tests/flow3/` ownership suite enumerated) |
| `diagnostics.md`, `rules.md`, `docs/ownership.pdf`, `src/defer.c`, `src/flow.c`, `src/lib.c` | Discovered but **not fetched** (large/binary). Anything about them below is marked as not observed. |

## 2. Ownership annotations in C

Cake is a **C23 front-end written in C** that emits C89; safety is a *checked annotation layer*, not a runtime. Source is self-annotated (`src/flow.h`, `src/defer.h`, `src/flow_branch.h` all use `_Owner`, `_Dtor`, `_View`, `_Nonnull`, `_Nullable`).

Annotation set observed in `src/cake_compat.h` and `quickref.md` (identifiers only):

| Annotation | Meaning (as documented in `quickref.md` / `flow.md`) |
| --- | --- |
| `_Owner` | The storage manages the referenced object's lifetime; ownership moves on assignment/argument. Any type may be an owner (pointer, `int` socket fd, …). |
| `_Nullable` / `_Opt` | May be null; absence of `_Opt` means non-null when nullable rules enabled. |
| `_Out` | Pointee uninitialized on entry, must be fully initialized on exit (all members, owner and scalar). |
| `_Dtor` | Pointee fully initialized on entry; callee must move out all owner members; implicitly treats every member as nullable (partially constructed objects allowed). |
| `_Clear` | Callee must leave every pointee member zero; on a return type means zero-initialized pointee (`calloc`). |
| `_Uninitialized` | On a return type: uninitialized pointee (`malloc`). |
| `_View` | On a *struct type*: strips `_Owner` from all members for that variable's scope; pass-by-value does not move. Quickref notes it is rarely used and a removal candidate. |
| `_Nonnull` | Non-nullable in the compiler's own headers. |

Enabling: `#pragma safety enable` = `#pragma nullable enable` + `#pragma ownership enable`; also `#pragma flow enable`, or `-fanalyzer`. Annotations are parsed even when checks are disabled, and are **erased in generated C89** — "checked, not compiled; the generated code is the same with or without them" (`flow.md` §"Contracts in the age of AI", §"Code Transition Strategy`).

Annotated stdlib is a real mechanism: `src/include/stdlib.h` declares `malloc` as `[nodiscard] _Uninitialized void* _Owner _Nullable malloc(...)`, `calloc` as `_Clear ... _Owner _Nullable`, `free(void* _Owner _Nullable)`, `realloc(void* _Nullable, ...)`; `src/include/stdio.h` declares `FILE* _Owner fopen(...)`, `int fclose(FILE* _Owner)`, `_Owner popen/fdopen/pclose`. Cake's header directory must come first on the include path; its headers then `#include_next <...>` the real system header. A user library is annotated by declaring the annotated prototype *before* including its plain header (C compatible redeclaration).

## 3. Borrow / observable semantics

- **View vs owner.** A plain (non-`_Owner`) pointer is a borrow by default. Stated rule: "The lifetime of the referenced object must exceed the lifetime of the view reference." `quickref.md` §7 shows a local address stored in an outer pointer being flagged after scope exit. `_View` is the explicit struct-level borrow.
- **Two resources per owner-to-struct pointer.** `free(void*)` releases only storage; `struct X*` owning a `_Owner` member must release the member first, then the storage. Converting `T* _Owner` to `void* _Owner` requires the pointee object to be empty first — this is how Cake distinguishes "move object" from "move raw storage" (`flow.md` §"Deleting Owner Pointers").
- **Observable call effects (strongest finding for our purposes).** `src/flow.h` defines `enum flow_pointee_effect_kind`: `FLOW_EFFECT_NONE`, `FLOW_EFFECT_CLEAR` (`_Clear`: all members zero + reachable owner pointees ended), `FLOW_EFFECT_LIFETIME_ENDED` (plain `_Owner` consumes whole object), `FLOW_EFFECT_ANY` (`_Out` or plain mutable pointer: unknown), `FLOW_EFFECT_DTOR` (members uninitialized + reachable owner pointees ended), `FLOW_EFFECT_REACHABLE_ANY` (const is shallow: pointee kept, pointer members become ANY). Each effect carries the pointee, the call token (origin of the state), nullability after the call, and pointer-target alternatives. This is a compact, explicit model of a call's *observable effect on reachable owned state*.
- **Worst-case rule.** A call taking a non-const `struct X*` may have released `x.text`; any pointer captured before the call is considered possibly lifetime-ended until re-read. Making the parameter `const` suppresses this, with *deep const* semantics — nothing reachable changes, which C `const` alone does not provide (`quickref.md` §11). `flow.h` also tracks `restrict` arguments and "same-object side" alternatives for alias reasoning.
- **Ownership conversion matrix** (`quickref.md` §8): owner←owner moves; non-owner←owner is fine (owner keeps ownership); owner←non-owner warns. Same for arguments.
- **Borrowed-parameter restoration rule.** A plain pointer parameter that frees an `_Owner` member must restore it on *every* exit; only `_Dtor` may consume. Directly evidenced by `tests/unit-tests/flow3/borrowed-param-owner-not-consumed-at-exit.c` (early `return` after `free(p->name)` warns; restoring to `0` before return, unconditional reassignment, and `_Dtor` do not).
- Alias-sensitive cases observed: `parameter-alias-owner.c` (owner freed via one parameter seen through an alias), `aliased-owner-lifetime-not-tracked.c` (ended fact found through an ancestor map even after an alias assignment), `view-struct-strips-owner.c`, `flow_conditional_owner_borrow.c` (owner arm of `?:` is a borrow when passed to a non-owner parameter; note upstream comment about issue 491 for the `p : 0` form).

## 4. Defer and cleanup patterns

Two distinct, complementary defer mechanisms were observed:

1. **Language `_Defer`** (`stddefer.h`: `#if CAKE_HEADERS ... #define defer _Defer` with `__STDC_VERSION_STDDEFER_H__ 202602L`; reference in `manual.md` §10.5: WG14 paper N3734). Rules from `manual.md` §10.5 and §5.4.9:
   - Runs on scope exit in **reverse order of appearance**; unreached defers do not run.
   - Copied as a block before **every** exit: `return`, `break`, `continue`, `goto`, falling off the end; a `return expr` first stores the expression in a temporary so the defer runs before the value is returned.
   - Constraints: cannot be jumped over with `goto`; a defer body may not itself `break`/`continue`/`return`/`goto`. `tests/unit-tests/break_out_of_defer_block.c` pins the diagnostic (`//lint 1200 break cannot jump out of a defer block`).
   - `try`/`catch`/`throw` lower to `if (1)` / `else` + label / `goto`.
   - Compiler-side implementation: `src/defer.h` builds a "defer list" on the AST during a visit, with `defer_scope` tails and a `defer_visit_ctx` (`_View struct ast ast`, `tail_block` owner, label search mode, function-literal body). `src/defer.c` (44 KB) not fetched, so lowering details beyond `manual.md` §5.4.9 are **not observed**.
   - `defer` inside a loop body runs once per iteration (end of body, `continue`, `break`, return) — evidenced by `tests/run-tests/defer_while.c` (5 runtime scenarios).
2. **Flow-level deferred effects.** `src/flow.h` `flow_ctx` has `deferred_effects[64]`, `collect_deferred_effects`, `argument_serial`, and explicit comment: "pointee write-effects deferred until after all arguments are evaluated (C evaluates arguments first, then the callee runs)." This prevents a call argument's mutation from being applied to sibling arguments too early.
3. **Cleanup contract family.** `_Dtor` (move out owner members), `_Clear` (zero everything), `_Out _Clear` (uninitialized input, zeroed output), and the idempotent `delete` built on destroy + free (`flow.md` §9: `x_delete` = `x_destroy(p)` then `free(p)`). `_Dtor` implies `_Opt` because construction may fail halfway — destructor must accept partially built objects.
4. **Flow analysis sees the defer's effect**: `defer free(p)` at function end satisfies the owner-not-moved check (`tests/unit-tests/flow3/defer-runs-once-at-function-end.c`, with `compile_assert(n == 1)`); defers run before jump joins so state at the join includes their effects (`defer-runs-before-jump-join.c` covers break/continue/throw/goto).

## 5. How lifetime violations are detected

**Compile time only, in the front-end**; no runtime checks and no runtime representation of ownership state.

- Per function, not interprocedural: calls are checked against declarations; callee body is separately checked against its own contract. Annotations are the contract.
- State map per path (`flow.md` §4, `flow_branch.h`): map from each tracked object (variable, member, pointee) to a set of alternatives = relation (`==0`, `!=0`, `<10`, unknown) plus optional *imaginary* part (`uninitialized`, `moved`, `lifetime-ended`). Stored-value states: `null`, `not-null`, `zero`, `not-zero`. Imaginary states "exist in flow analysis but not at runtime … the bits are just bits."
- Branches fork and narrow; joins merge alternatives; `break`/`return`/`throw`/`goto` join into target maps. Loops are visited **twice** (first pass to reach iteration state, second with warnings); backward `goto` is *not* treated as a loop — a documented limitation (`flow.md` §5).
- Checks are queries over all alternatives: "can `p` be null?", "is `p` still owning at scope exit?" Reported once with origin tracking and up to 3 explanation samples (`flow.h`: `flow_reported_finding`, `FLOW_MAX_REPORTED_FINDINGS 256`, `FLOW_FINDING_MAX_SAMPLES 3`).
- Diagnostics observed in examples: 9 ignoring `_Owner` result, 26 overwrite without release, 29 still owns resource, 30 possibly uninitialized, 31 lifetime ended, 32 moved, 33 possible null deref, 35 may be null, 69 `_Clear` member not zero, 71 `_Out` member not initialized, 72 moved but never reassigned, 100 warning in `parameter-alias-owner.c`, 1200 defer jump. `flow.h` also exposes `W_FLOW_PARAM_MAY_ALIAS`.
- Developer intrinsics: `static_debug(x)` prints state; `compile_assert(expr)` proves a fact from current knowledge, generates **no code**, and does not narrow later state (unlike `assert`/`_Assert`). Regression tests use `//lint N` to assert that warning N occurs on that line — "the safest way to silence a warning … if the expected warning does not happen, that is also a warning."
- Known unsound spots (`flow.md` §5 + §18): pointer equality between two distinct concrete pointees folds to false; `realloc` cannot be expressed (ownership moves only on success — examples use `p = realloc(p, ...)` plus `//lint 26`); `id(p)` returning its argument loses the alias so use-after-free is missed.

## 6. Allocator tracking

**No user-level allocation tracking observed.** Cake's allocator story is *contracts on allocator functions* (`_Owner`/`_Clear`/`_Uninitialized` on `malloc`/`calloc`/`free` in `src/include/stdlib.h`), not a heap registry, side table, or generation counter. The only allocation-tracking-named structures in observed source are compiler-internal arenas: `flow_allocated_object_arena` and `flow_branch_arena` in `src/flow_branch.h` (with `_Owner` pointers and `_Clear` reset functions), which allocate *analysis objects and branch maps*, not user allocations. `src/lib.c` (3.9 MB amalgamation) and `src/flow.c` were not fetched, so a runtime tracker there is **not ruled out, but not observed**. By contrast, this repository already has real runtime tracking (`cplus` metadata table, `g_recent` free cache, `stdlib_transfer_ownership`, generation-checked `slice::stale`) — that is an area where C+ is *ahead*, not behind.

## 7. Inspected upstream tests and cases

- `tests/unit-tests/flow3/` — large ownership/lifetime suite enumerated via recursive tree (`owner-resource-*`, `owner-type-*`, `flow3/safety-*`, `nullable-*`, `parameter-alias-*`, `restrict-*`, `defer-*`, `dtor-*`, `clear-*`, `view-*`, loop/throw/catch cases). Files inspected in detail are listed in §1.
- `tests/unit-tests/_Out/test_*.c` — ~215 numbered cases (`test_100.c` … `test_21500.c`).
- `tests/unit-tests/out/flow_*.c` — older golden-output suite (`flow_use_affter_free`, `flow_lifetime_ended`, `flow_linked_list_destroy`, `flow_non_pointer_owner`, `flow_view_member`, `flow_try_catch_leak`, `flow_malloc`, `flow_calloc`, …).
- `tests/unit-tests/failing/parameter-alias-move-through-other.c`, `tests/unit-tests/flow3/aliased-owner-lifetime-not-tracked.c`, `cwe401_realloc.c` (Juliet CWE401 leak through `realloc`) — negative/regression cases.
- `tests/run-tests/defer_while.c`, `tests/unit-tests/break_out_of_defer_block.c` — runtime/constraint cases for defer.
- `tests/constrains/` — C23 constraint suite (not ownership-specific; upstream typo preserved).

## 8. Mapping to this repository's C+ stdlib

Note on the requested citation path: `docs/stdlib/agent2_audit/module_audits/` **does not exist** in this checkout. The actual path is `tests/stdlib/agent2_audit/module_audits/`; citations below use that real path plus the deep contracts under `docs/stdlib/`.

### `stdlib/file.hp` — borrow vs own
Current model: `file` owns `FILE*` only when `fopen`/`tmpfile` succeeded; `file_borrow` wraps without ownership; zero-initialized `file` closes nothing; `close()` on a borrowed stream detaches; `cplus_move` zeroes the source; copy/assignment of an owning `file` is rejected by `c+ check` (`tests/stdlib/agent2_audit/module_audits/file.hp.md` §(4); `docs/stdlib/DEEP_FILE_CONTRACT.md:58-63,125-126`).
Cake analogue: `_Owner FILE*` from `fopen`, `fclose(FILE* _Owner)`, and the rule that an owner must be moved before scope exit — stronger than C+'s "owns=0 ⇒ inert drop", because Cake reports a *missing* release even when the handle is zero-initialized as a fallback. Cake's `void* _Owner` storage-vs-object distinction also mirrors `close()`-on-borrow detaching.
**Gaps to cite:** `file_borrow(NULL)` accepts any `FILE*` without validation and has no test (`file.hp.md` §7 item 2); `close()` clears the handle even when `fclose` fails, so a failed close cannot be retried (`DEEP_FILE_CONTRACT.md:136-143`); `file.hp.md` §7 notes copy/assign rejection is verified by one-shot compiler checks, not an in-process deep fixture. Cake's origin-tracked "lifetime ended" fact would be the model for a diagnostic that survives an alias of `native()`.

### `stdlib/thread.hp` — guards
Current model: `guard` borrows a `mutex` and owns only the critical section, clearing `owner` in `unlock()` so unlock + drop cannot double-unlock; `thread` is move-only and `drop()` joins (`tests/stdlib/agent2_audit/module_audits/thread.hp.md` §(4), F2 fix).
Cake analogue: the view-borrow rule ("referenced object must outlive the view") plus deep-const and same-object alternatives is the exact class of check a `guard` needs — e.g. rejecting a `guard` whose mutex is destroyed before the guard, and reasoning about aliased mutex handles.
**Gaps to cite:** "thread has no rejection of any kind: all four unsafe guard/mutex shapes compile cleanly and are runtime-enforced by the header" (`docs/libc/COMPILE_TIME_GAPS.md` §5). The C+ stack-borrow heuristic produced `[CPLUS-BORROW] stack borrow passed to thread: 'arg'` and required a `(void *)(uintptr_t)arg` round-trip workaround (`thread.hp.md` F2, `DEEP_THREAD_CONTRACT.md:117-126`). Uncovered branches: `thread::start` NULL fn / already running, `pthread_create`/`pthread_join` failure, and RAII drop of a joinable thread (`thread.hp.md` §7 items 2-4).

### `stdlib/error.hp` — ownership transfer
Current model: `stdlib_transfer_ownership(void **dst, void **src)` rejects NULL handles, non-empty destination, `dst == src`, and a source that is not a live block (`stdlib/error.hp:494-509`; verified in `tests/stdlib/agent2_audit/module_audits/error.hp.md` §4 and `DEEP_CROSS_FAMILY_CONTRACT.md:282-306`); `error` copy/move transfers state and zeroes the source; `cplus_move` is the move primitive.
Cake analogue: owner assignment moves ownership and marks the source `moved`; using a moved owner warns (32). The `moved` state is imaginary — no runtime nulling required — which is precisely why Cake emits no runtime check, unlike `unique_ptr`. `stdlib_transfer_ownership` is the runtime half of that contract.
**Gaps to cite:** method-call sugar on a released `error *` lowers to a free-function call and is **not** checked; stale state after `error_reset` compiles clean (`error.hp.md` compile-time GAP table; `tests/stdlib/compile_time/error.json` cases `error_invalid_lifecycle`, `error_null_pointer`, `error_use_after_reset.cp` → ACCEPT). Untagged use-after-move diagnostics for `collections`/`vector` are lowering diagnostics, not `[CPLUS-` enforcement (`docs/libc/COMPILE_TIME_GAPS.md` §6).

## 9. Feasible adaptations vs compiler-dependent features

**Feasible idea-level (no Cake code, no new compiler):**
1. Keep making ownership/nullability part of the *signature* and verify both caller and callee. This repository already does this via `c+ check`, `[CPLUS-OWNER]`, `[CPLUS-LIFETIME]`, and one-shot compiler checks; Cake adds the discipline that the *implementation* is checked against the declared contract, not merely assumed.
2. Empty-macro compatibility header pattern (`src/cake_compat.h`, guarded by `#ifndef __CAKE__`): identical source compiles with an ordinary compiler before any annotation work. Directly analogous to a staged rollout for future C+ header annotations; no licence issue with the *idea*.
3. Names and semantics of the output-contract family (`_Out`/`_Dtor`/`_Clear`/`_Uninitialized`) as documentation for `error *` out-parameters and allocator contracts; `error_reset` is effectively `_Clear`-ish, `error_fail` is `_Out`-ish.
4. Defer correctness rules (reverse order, copy to every exit including `return expr` via a temporary, no jumps out of a defer body, unreached defers don't run) as a conformance checklist for the existing `RaiiLowering.cpp` / `defer` tests.

**Requires a compiler change (Agent 1 territory):**
- `_Owner`, `_Opt`, `_Dtor`, `_Clear`, `_View`, `_Uninitialized` enforcement, including the imaginary `moved`/`lifetime-ended`/`uninitialized` states and origin tracking.
- View-lifetime and deep-const analysis; same-object/alias alternatives; `restrict` argument reasoning.
- `//lint N` self-verifying suppression and `compile_assert`/`static_debug` intrinsics.
- `_Defer` keyword, its constraint diagnostics, and AST copy-to-all-exits lowering; `try`/`catch`/`throw` sugar.
- Annotated system headers with `#include_next` and pragma-controlled incremental enablement.

This repository's current position (per `docs/libc/COMPILE_TIME_GAPS.md`, `tests/stdlib/compile_time/*.json`) is partial: tagged `[CPLUS-OWNER]`/`[CPLUS-LIFETIME]`/`[CPLUS-BORROW]` checks exist, but method-call sugar on released pointers, reset-state staleness, and unsafe thread guard shapes remain gaps. Cake is a proof that those gaps are *checkable statically*, not a drop-in fix.

## 10. What cannot be used

- **The code, wholesale or piecemeal.** GPLv3 + AI-training addendum; prior explicit rejection in `docs/UPSTREAM_AUDIT.md`. No files from Cake were copied here.
- **Cake itself as a tool.** It is a C23→C89 compiler front-end, not a library; it cannot be inserted into a Clang-based `c+` pipeline. Its backend/codegen fixes are for its own C89 emitter.
- **`stddefer.h` / `_Defer` syntax and lowering.** Requires Cake's parser/AST/codegen; C+ must use its own lowering. Also its `try`/`catch`/`throw` extension would collide with the project's recoverable-error/exceptions4c model.
- **Pragmas, intrinsics, and suppression syntax** (`#pragma safety/nullable/ownership/flow`, `static_debug`, `compile_assert`, `//lint`) — Cake-only surface.
- **Annotated libc headers via `#include_next`** — tied to Cake's include-path ordering and its own annotations; C+ uses Clang and its own headers.
- **Runtime allocator tracking** — none observed in Cake (§6); reuse the existing `cplus` metadata/generation infrastructure instead.
- Any claim about `src/defer.c`, `src/flow.c`, `src/lib.c`, `diagnostics.md`, `rules.md`, or `docs/ownership.pdf` contents: **not directly observed**, therefore not asserted here.

## Top 3 reusable patterns (idea-level only)

1. **Contract-in-signature + per-object state map with origin tracking.** Calls are checked against declarations (no interprocedural analysis); each object holds a set of alternatives including `uninitialized`/`moved`/`lifetime-ended`; every warning is a query over all alternatives and reports its origin. Maps directly onto the C+ `[CPLUS-OWNER]`/`[CPLUS-LIFETIME]` gap list.
2. **Two-level defer.** (a) Language `_Defer` copied to every scope exit in reverse order with a temporary for `return expr`; (b) *deferred call effects* applied only after all arguments are evaluated, with an explicit effect enum distinguishing CLEAR / LIFETIME_ENDED / ANY / DTOR / REACHABLE_ANY. The second is a precise model for modelling what `error *`, `stdlib_release`, and `cplus_move` do to reachable state at a call site.
3. **Verified output-parameter contract family.** `_Out` (all members initialized), `_Dtor` (all owner members moved out, implicitly nullable for partial construction), `_Clear` (all members zero), `_Uninitialized` (fresh allocation) — with the *implementation* checked against the contract and a combined `_Out _Clear` for init. Apply as a documentation/checklist discipline to `error *` and allocator wrappers already present in `stdlib/error.hp`.

## Failures / caveats

1. `master`-branch URL 404; correct branch is `main`.
2. Task-assumed `include/cake.h` / `include/cake/*.h` paths do not exist; actual headers are `src/include/*.h`.
3. Requested local citation path `docs/stdlib/agent2_audit/module_audits/` absent; used `tests/stdlib/agent2_audit/module_audits/`.
4. `docs/ownership.pdf` and large sources (`src/flow.c`, `src/defer.c`, `src/lib.c`, `diagnostics.md`, `rules.md`) not fetched — no claims made about them.
5. **Legal:** GPLv3 + AI-training addendum, already rejected by this project; this study is traceability only and must not become an integration proposal.
