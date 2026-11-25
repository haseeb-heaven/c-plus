# Upstream study: TrapC (Robin Rowe / TRASEC / Heroic Robots) — source unpublished

Study date: 2026-11-23. Branch: `agent/modern-stdlib`. One of 7 parallel upstream
studies; companion index is `docs/stdlib/upstream-reuse.md`.

> **STATUS / LEGAL — read first.** **No TrapC repository, commit/tag, LICENSE
> file, or source file was found or inspected.** As of this study date the
> compiler/interpreter are unreleased and closed: the vendor's own About page
> says TrapC is *"in development and nearing release as free open source
> software. Current estimate is Q1 2026, but it depends on how fast debugging
> goes."* No SPDX identifier or license text has been published, so **the
> license is UNDETERMINED, not "MIT"**. Nothing below is a source-code finding
> and nothing may be treated as an approved reuse path; it is documentation-
> based traceability only. No TrapC file, code, header, or test was copied into
> this repository.

Labels used below:

- **[DOCUMENTED]** — quoted or closely paraphrased from a fetched,
  author-published primary document (WG14 paper n3423 or trapc.org). This is
  **not** the same as source evidence.
- **[INFERENCE]** — my engineering interpretation of the documentation.
- **[NOT OBSERVED]** — would require source that does not exist publicly; no
  claim is made about it.

## 0. Findings at a glance

| Question | Answer |
| --- | --- |
| Exact repository URL | **None found.** No official GitHub/GitLab/Bitbucket repo; see §1 and §9 for the exhaustive search. |
| Commit / tag | **N/A** — no repository exists to pin. |
| LICENSE identifier quoted from a license file | **UNOBTAINABLE.** No license file published. Only the phrases *"free open source software (FOSS)"* (n3423) and *"nearing release as free open source software"* (trapc.org/about) exist; neither names a license. |
| Exact source files inspected (runtime pointer checks, allocation tracking, failure handling) | **NONE.** There is no public `trap_runtime.c`, no `free.c`, no `type_info` header, no trap handler source to inspect. All §§3–5 content is **[DOCUMENTED]** design description, not source. |
| Can any TrapC code be reused without changing pointer representation or C signatures? | **No.** See §6: TrapC is a one-way-ABI, managed-pointer language whose allocation/free functions are deliberately no-op/wrapper APIs. Reuse would require Managed-pointer semantics, hidden RTTI, and non-C `trap`/wrapper signatures. |
| Ownership of the mechanisms | Compiler + runtime + language design = **Agent 1** territory. Header-side idea transfers only = §7. |
| Top reusable *algorithms/ideas* (idea-level, not code) | (1) per-pointer metadata queryable as `base/size/count/offset/validity`; (2) offset-to-base recovery so an *advanced* pointer still frees/owns the original allocation; (3) caller-local non-propagating failure handler with a zeroed value on trapped return. |

---

## 1. Provenance — what was actually identified

**[DOCUMENTED] Identity.** TrapC is a C-derived, safety-oriented language
designed by **Prof. Robin Rowe** (paper contact `Robin.Rowe@trasec.com`; site
contact `robin.rowe@heroicrobots.com`). Both the paper and the site name the
implementing organisations: startup **TRASEC** and non-profit **Fountain Abode**
for the compiler; the site states *"TrapC is a trademark of Heroic Robots."*

**[DOCUMENTED] The only two implementation artifacts named are not public:**

- `itrapc` — a TrapC *interpreter*.
- `trapc` — a separate *compiler*. (n3423 also describes a "TRASEC trapc
  cybersecurity compiler with AI code reasoning".)

Both are described in `trapc.org/trapc-a-year-later` (2026-01-26): *"A TrapC
interpreter called itrapc and a separate compiler called trapc. I had wanted to
make a software release by 1 January 2026, but too many bugs. I only reached
code complete this month and am now on the painstaking and sleepless process of
debugging... Aiming for Q1 2026."* The Register (2026-01-26) quotes Rowe:
*"We're almost there... It almost works."*

**[OBSERVED] No repository.** Searches performed (all outcomes in §9):

- GitHub repo search `trapc`, `itrapc`, and `"TrapC" in:name,description` —
  hundreds of results, **none** belonging to TrapC the language. The only
  plausible-looking name, `jbangert/trapcc` ("Computing with traps", BSD-3,
  last push 2013), is unrelated. `tandoan/ITrapClient` is an unrelated Java
  repo.
- GitHub user `rower` (the trapc.org author handle) owns one empty 2013 repo
  `rower/rower`, unrelated. GitHub org `heroicrobots` returns **HTTP 404**.
- trapc.org site search for `source` returns no download, no repo, no source
  tree. The site has only 7 posts (2026-01-26 → 2026-02-02) plus an About page.
- The WG14 paper points only to `https://trapc.org/`, and to GitLab projects
  `gitlab.com/robinrowe/cmaker` and `gitlab.com/robinrowe/cinepaint` — these
  are *other* Rowe projects (a CMake generator and CinePaint), **not** TrapC.

**[DOCUMENTED] Primary documents that were obtained:**

| Document | Provenance | Role in this study |
| --- | --- | --- |
| `n3423` — *TrapC: Memory Safe C Programming with No UB*, Pr. Robin Rowe, 2025-01-07, 22 PDF pages | `https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3423.pdf` (WG14, Graz meeting, Feb 2025) | Authoritative design description; all §§3–6 quotes come from here. Extracted text is ~35.5 KB. |
| trapc.org About / "TrapC, a Year Later" / "The Register on Vibing TrapC" / "C++ Style Member Functions" | `https://trapc.org/...` | Release status, license intent, implementation-status statements. |
| The Register, 2026-01-26 | `https://www.theregister.com/2026/01/26/trapc_claude_c_memory_safe_robin_rowe/` | Independent confirmation that the compiler is not released. |

**[DOCUMENTED] Third-party snippet, explicitly not upstream source.** A GitHub
gist `lovemycodesnippets/trap_free.c` (2025-03-11, "From the TNS post on TrapC")
contains a 7-line example copied from the whitepaper's `use_after_free.c`. It
is a third-party repost, not TrapC source, and was not used as evidence for any
implementation claim.

---

## 2. License (exact identifier cannot be quoted)

No `LICENSE` file, `COPYING` file, SPDX header, or copyright notice from a
TrapC source file was available. Exact statements found:

**[DOCUMENTED]** n3423 §"The TRASEC trapc Compiler": *"The TRASEC trapc
cybersecurity compiler with AI code reasoning is expected to release as free
open source software (FOSS) sometime in 2025."*

**[DOCUMENTED]** `trapc.org/about`: *"TrapC is a Safe-by-Design extension of the
C programming language designed by Robin Rowe that is in development and
nearing release as free open source software."*

**[DOCUMENTED]** `trapc.org/about`: *"TrapC is a trademark of Heroic Robots."*

**Identifier to record: UNDETERMINED — "FOSS intended, license not published".
** Do not write "MIT", "BSD", or any SPDX ID into `upstream-reuse.md` for
TrapC; there is no evidence for one.

**Trap for other agents:** n3423 line 758 contains the literal text
`$ echo "Open Source MIT" > LICENSE`. It appears in the §"Cmaker Build Manager"
walkthrough as an *example of a user creating a LICENSE for their own new
project* (`$ cmaker project HelloWorld` follows immediately). **It is not a
statement of TrapC's license and must not be quoted as one.** The paper's
Cmaker example is also unrelated to the TrapC compiler.

This repository is MIT (`LICENSE`, "MIT License", Copyright (c) 2026 Haseeb
Mir). Because no upstream license text exists, any future vendoring decision
must be deferred until an actual published license is reviewed; no reuse is
authorized by this study.

---

## 3. Runtime pointer checks (documented behaviour, no source observed)

All items in this section are **[DOCUMENTED]** from n3423. The *implementation*
of every check is **[NOT OBSERVED]** — no source exists. Exact phrases and
section names are given so the parent session can re-verify.

| Check / behaviour | Exact documented wording or example | n3423 location |
| --- | --- | --- |
| Managed-pointer validity query | *"`testof()` ... returns false if the pointer failed test"*; example `assert(testof(p));//confirms is TrapC pointer, not raw C pointer` | §"TrapC Pointer Intelligence" |
| Unknown ("mystery") pointer use traps and zeroes | *"In TrapC, attempting to manipulate a mystery pointer, that is, a pointer with no RTTI, will trap. Except for `testof()`, any attempt to use the pointer will also zero it."* | §"TrapC Pointer Intelligence" |
| Null dereference traps | `*p = 42; // TrapC will trap if p is null` | §"Lifetime Safety" |
| Buffer overrun terminates with diagnostic | *"The action TrapC takes on overrun, or any other unanticipated error condition, such as divide-by-zero, is termination with a useful error message. That is, unless there is a programmer-supplied 'trap' error handler."* `gets(buffer);// TrapC will terminate on overrun!` | §"Memory Safety, No Buffer Overruns" |
| Pointer stepping past end is nulled, not dangling | *"When `p` steps off the end, TrapC nulls p"*; `assert(p == 0);// true in TrapC, not true in C/C++` | §"TrapC Nulls Buffer Overrun Pointers" |
| General invariant | *"TrapC pointers always point to valid memory. Cannot overrun or contain garbage."* | §"TrapC Nulls Buffer Overrun Pointers" |
| Known extent enables p-string semantics | Overwriting a z-string terminator is allowed: *"Unterminated string ok, TrapC knows buffer size"*; `strlen` uses the p-string size. | §"TrapC Z-strings Are also P-strings" |
| Last-element / extent query | `assert(p+9 == lastof(p));// last member of any array`; RTTI reports `sizeof = 80, countof = 10`. | §"TrapC Pointer Intelligence" |
| Base recovery from an advanced pointer | `assert(p-offsetof(p) == tokenp)` in the `strsep` example; TrapC *"frees tokenp, not p"* while C `defer` would free `p` and crash. | §"Memory Management in TrapC vs. ANSI C" |
| Compile-time type checking | `"{}"` printf specifier chooses the right conversion from the argument type; mismatched `%d` rejected at compile time when detectable. | §"Typesafe printf" |

**[NOT OBSERVED]** Everything an implementation study would need: the trap
instruction/ABI, check ordering, the metadata layout, whether metadata is a
hash table, shadow memory, inline header or side table, whether `testof()` is
O(1), generation/tag width, and thread-safety of metadata updates. The paper
explicitly says of the conceptual `struct type_info` and of local-lifetime
fixups: *"How the compiler handles ... is an implementation detail. Do not
assume TrapC uses reference counting like in C++."*

---

## 4. Allocation tracking (documented model, no source observed)

**[DOCUMENTED]** n3423 §"What is TrapC in a Nutshell?"

> "TrapC memory management is automatic, cannot memory leak, with pointers
> lifetime-managed not garbage collected"
> "TrapC pointers may look the same as in C/C++, but all pointers are
> diligently compiler-managed and memory safe"
> "TrapC pointers have Run-Time Type Information (RTTI), with typeof(),
> nameof() and other details accessible"

**[DOCUMENTED] Lifetime end is compiler-determined, not programmer-determined.**

> "For C compatibility, it is ok to call free() in TrapC, but it is ignored."
> `free(p);//TrapC ignores free, p not freed yet`
> `*p = 10;//UB in C, no problem in TrapC`
> `return 0;// Leaving Scope, TrapC now frees p automatically`

and:

> "It isn't possible to use-after-free in TrapC. Because the TrapC compiler
> determines when it frees memory, the free() call shown above does nothing.
> That free() and 'delete' are a no-op is for C/C++ compatibility..."

The `alias` section reinforces this with an explicit no-op declaration:
`void free(void* ptr) alias;// no-op, do nothing`.

**[DOCUMENTED] Per-pointer metadata API.** `get_type_info(p)` returns a
`type_info` object; helpers are `nameof`, `typeof`, `sizeof`, `countof`,
`offsetof`, `lastof`, `addressof`, `testof`. The paper shows a *conceptual*
struct with fields `name`, `type`, `size`, `count`, `offset`, `address`,
`test`, while stating *"How the struct type_info is defined internally is a
compiler implementation detail, conceptually like this..."*

**[DOCUMENTED] Offset-to-base association.** The `strsep` example requires the
runtime to know the allocation base behind an interior/advanced pointer
(`p-offsetof(p) == tokenp`, then free `tokenp`). This is the strongest
*algorithmic* hint in the documentation.

**[DOCUMENTED] Returned locals are made safe.** Returning a pointer to a local
(heap or stack) is not an error; *"TrapC will safely return p, not rug-pull the
memory of i ... there is never garbage in a TrapC pointer"*. Mechanism
unspecified.

**[DOCUMENTED] Handle/RAII discipline is partly programmer-owned.** `fclose()`
is redundant because *"in TrapC fclose is RAII"*; the library's `FILE`
destructor calls `fclose()` a second time but skips it because the handle was
invalidated. *"Programmers are expected to make handles RAII-safe and
leak-safe, typically by setting the handle to a known-bad state such as 0 or
-1. RAII protection of programmer-defined handles is not automatic in TrapC,
maybe in the future."*

**[NOT OBSERVED]** No allocation registry, free cache, generation counter,
tombstone table, lock, telemetry counter, or failure-injection hook is
described anywhere. Do not attribute any such structure to TrapC.

---

## 5. Failure handling (the `trap` model, documented)

**[DOCUMENTED]** n3423 §"The 'trap' in TrapC":

> "The TrapC error-handling mechanism looks a bit like 'catch' from C++
> exceptions, yet is quite different... Unlike C++ exceptions, TrapC has no
> alternate return type, nor an alternate return stack."
> "When an error is trapped in TrapC, the function returns immediately to the
> caller's 'trap' handler, if there is one. A normal return skips over the trap
> handler. TrapC has no alternate return stack like C++ exceptions. Faults must
> be trapped by the caller, do not get passed up the line like exceptions in
> C++."

**[DOCUMENTED] Handler placement is syntax-constrained.**

> "It is possible to rethrow in TrapC by using `trap.return`... It is a
> compile-time error to have any other code between the semi-colon after a
> function call and its trap handler. The trap handler, if any, must
> immediately follow the function return being trapped."
> "There is no trap handler for `main()`."

**[DOCUMENTED] Failure policy is fail-fast by default.**

> "If `DoSomething()` fails ... TrapC will terminate the program printing an
> error message. The exit() code will be the errno of the error."
> "Lacking trap handler, TrapC will terminate here on error, not return 0"

**[DOCUMENTED] Trapped value returns are zeroed.**

> "What happens when a function gets trapped that must return a value? What
> does it return? It will return a block of memory that is sizeof the return
> type (and zeroed)."

**[DOCUMENTED] Handler introspection:** `trap.errno`, `trap.msg`,
`trap.return`. **[DOCUMENTED]** C compatibility uses preprocessor mirroring:
`#ifdef __trapc` chooses `trap { ... }`, otherwise `if(!ok) { ... }`.

**[DOCUMENTED]** POSIX/stdlib are *trap-aware wrappers*: *"The scanf and other
POSIX library calls in TrapC are trap-aware wrappers to the C functions"*;
`fopen(...)` comment: *"TrapC may terminate or call handler"*.

**[INFERENCE]** This is a *terminating* policy by default (contrast §7: C+
`stdlib/error.hp` explicitly never terminates), plus a caller-local recovery
form that does not unwind. There is no TrapC equivalent of a status-returning
`error *` object in the documented design.

---

## 6. ABI compatibility assessment — decisive for reuse

**[DOCUMENTED]** n3423 §"What is TrapC in a Nutshell?":

> "TrapC is one-way ABI compatible with C, such that TrapC functions may call
> any C function as-is"
> "Passing a raw C pointer safely to a TrapC function requires extra steps,
> because TrapC pointers have hidden RTTI"
> "TrapC has API-compatible versions of C POSIX and C++ STL standard libraries,
> to not return raw pointers"

**[DOCUMENTED]** n3423 §"C ABI Compatibility":

> "Passing pointers from TrapC into a C function is fine... Dropping into C,
> TrapC cannot track it. Calling into C is like using 'unsafe' in Rust, or maybe
> even less secure than that."
> "Passing raw C pointers to TrapC functions is not ok. There are no 'mystery
> pointers' in TrapC. A TrapC pointer may be passed as-is to a C API, because
> that transition ignores hidden TrapC pointer RTTI. Attempting the reverse
> would have TrapC missing necessary pointer RTTI."

The prescribed six-step C→TrapC workaround is: create a TrapC wrapper with the
same name, create a suitable (RTTI-bearing) pointer inside it, call the
`extern "C"` function, `trap.return` on `false`/`errno`, return the TrapC
pointer on success, and hide the raw C function from TrapC application code.

**[INFERENCE] Verdict for C+.** C+ keeps ordinary thin pointers (`T*` is one
address word), the existing C toolchain/linker/ABI, and unchanged C signatures
(`docs/ROADMAP.md`, `docs/STATUS.md`, `docs/STRUCTURE.md`,
`docs/stdlib/abi-preserving-runtime-safety.md`). TrapC as documented fails
that contract in three independent ways:

1. **Pointer representation/semantics.** The paper says pointers *"may look
   the same"* / *"look just like C/C++ pointers"* but carry *"hidden RTTI"* and
   are *"diligently compiler-managed"*. Even if the physical word width is
   unchanged (**not stated anywhere — do not assume it**), the semantic
   representation includes per-pointer metadata and managed lifetime that a
   plain `T*` does not carry. A C+ `T*` handed to a TrapC-compiled function is
   exactly the forbidden "mystery pointer".
2. **C signatures.** `free`/`delete` become no-ops; `malloc` is re-expressed
   via `new` with hidden RTTI (`void *malloc(size_t size) alias void* new
   void[size/sizeof(void)]`); POSIX calls become trap-aware wrappers; trapped
   calls return a zeroed buffer. These are signature/semantics changes, not a
   drop-in library.
3. **Language surface.** `trap`, `alias`, castplates, member functions,
   `nullptr`, `new`, removal of `goto`/`union`, and RTTI functions are compiler
   features; they cannot be added by a `.hp` header.

**Therefore: no TrapC runtime code can be reused in C+ without changing the
pointer representation/contract or the C signatures.** All of the above is
Agent 1 (compiler/runtime/language) territory and, per `docs/UPSTREAM_AUDIT.md`
(Fil-C row) and `docs/FOUNDATION.md`, is rejected by this project's stated
policy of preserving the native C ABI and reusing the existing toolchain. There
is also nothing concrete to reuse even if the policy changed, because the
source has not been published.

---

## 7. Applicability to C+ stdlib side-table metadata (`stdlib/error.hp`)

Citations verified in this checkout (line numbers from `stdlib/error.hp`):

- `stdlib_meta_slot { ptr, byte_size, elem_size, generation }` — lines 102–107;
  shared `stdlib_state_t` with `meta_tab/meta_cap/meta_used/meta_live/meta_gen`
  and an atomic `meta_lock` — 109–125; `STDLIB_META_TOMB` — 253–254.
- `stdlib_meta_find` — 273–287: **exact-address lookup** (`slot->ptr == p` at
  line 280), open addressing, hash at 265–271. No interior-pointer lookup.
- `stdlib_meta_record` — 289–346 (grow/rehash, bump `generation`);
  `stdlib_meta_forget` — 348–362 (tombstone + `generation = 0`).
- `stdlib_alloc` — 365–392; `stdlib_calloc` — 395–412 (overflow-checked);
  `stdlib_release` — 517–524.
- Queries: `stdlib_alloc_size` 415–424, `stdlib_alloc_elem_size` 426–435,
  `stdlib_alloc_length` 437–448, `stdlib_alloc_generation` 450–459,
  `stdlib_alloc_check_generation` 461–473, `stdlib_alloc_query_length`
  475–492, `stdlib_transfer_ownership` 494–515.
- Diagnostics: `STDLIB_CATEGORY_*` 45–53, `stdlib_note` 194–210, and the
  non-terminating policy *"Nothing in this header terminates the process;
  fatal safety violations stay the runtime's job (`cplus_fatal`)"* — lines 5–6.

| TrapC concept **[DOCUMENTED]** | Existing C+ analogue (verified) | Idea-level transfer / gap |
| --- | --- | --- |
| Hidden RTTI `size`, `count`, `offset`, `type` per pointer | `stdlib_meta_slot` + `stdlib_alloc_size` / `elem_size` / `length` / `query_length` | **Already convergent in shape.** Continue documenting these as named queries with explicit "unknown" results (0 / -1). |
| `testof()` = non-trapping validity query | `stdlib_meta_find` (273), `stdlib_alloc_check_generation` (461) | Name a single documented "is this a live, recorded block?" predicate. **Gap:** TrapC additionally *zeros* the pointer on any non-`testof` use of an invalid pointer; C+ cannot do that to arbitrary thin-pointer copies. |
| `offsetof(p)` recovers the allocation base from an advanced/interior pointer | `stdlib_meta_find` matches **exact addresses only** (line 280) | **Real contract gap worth documenting:** a side table keyed only by base pointer cannot answer "which allocation owns this interior pointer?" Do **not** silently claim C+ has TrapC's behavior; either document the precondition (query with base pointer) or add a range query as a separate, tested feature. |
| Compiler frees at scope exit; `free()` is a no-op; cannot leak | `stdlib_release` 517–524 + runtime double-free/invalid-free detection; `cplus_fatal` | **Do not copy the no-op semantics.** C+ callers must still release; changing `free` to a no-op would break the C ABI and every libc interop path. |
| `trap` = caller-local, non-propagating, zeroed return on failure | `error` value + `stdlib_note` + `CPLUS_EXIT_*`, non-fatal by design | Idea only: keep failure reporting *recoverable* (`-1`/NULL + category) as C+ already does. The terminating default (`exit(errno)`) is incompatible with `error.hp:5-6`. |
| Trap-aware POSIX wrappers, stdlib workalikes that never return raw pointers | `stdlib_alloc`/`stdlib_release` wrappers over `cplus_alloc`/`cplus_release` | C+ already wraps the allocator at the header boundary without changing C signatures. Keep that pattern; do not introduce trap-aware stdlib signatures. |

**What belongs to Agent 1 (not this repo):** hidden per-pointer RTTI, automatic
scope-exit freeing, nulling-on-overrun, `testof`, `trap`/`trap.return`,
`alias`, `new`/`nullptr`, RTTI API, and any ABI-level metadata. These need
compiler lowering and runtime support; header-only `.hp` code cannot implement
them and must not pretend to.

**What is realistically actionable for Agent 2 (documentation/contract only):**
write down the side-table query contract (who records, who forgets, what a
missing entry returns, when generation changes) and explicitly state the
base-pointer-only lookup limitation next to `stdlib_meta_find`. No header edit
is made by this study.

---

## 8. What must NOT be copied / claimed

1. **Any TrapC source** — none exists publicly; any "source" found later must
   first get a real license review (§2).
2. **The license label MIT/BSD/Apache** — no license was published. The
   `"Open Source MIT"` string in n3423 is a Cmaker tutorial example (§2).
3. **Managed-pointer representation or hidden RTTI** — changes the pointer
   contract and breaks the one-way "mystery pointer" boundary (§6).
4. **`free()`/`delete` as no-ops, automatic scope-exit freeing, or nulling on
   overrun** — these change C signatures/semantics and conflict with
   `error.hp:14-23` ("release ONLY through `stdlib_release`", "never hand a
   stdlib_alloc block to raw free()").
5. **`trap`, `trap.return`, `alias`, castplates, `new`, `nullptr`, RTTI API,
   removal of `goto`/`union`** — compiler/language features (Agent 1).
6. **Uncatchable termination with `exit(errno)` as the default failure policy** —
   `error.hp:5-6` requires non-terminating recoverable diagnostics.
7. **Claims of "TrapC-level safety"** in C+ — C+ has thin pointers and an
   exact-address metadata table, not managed pointers; the safety properties
   are not transferable without the compiler.
8. **Any claim about TrapC runtime internals** (metadata layout, check order,
   free cache, locks, generation width) — **[NOT OBSERVED]**, no source.

---

## 9. Exact evidence log

All fetches performed 2026-11-23. Web tools: `websearch` and `webfetch`;
local PDF text extraction via `python3` + `pypdf`. Temporary evidence files:
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/n3423.pdf`
(331,046 bytes, 28-page PDF container; `file` output "PDF document, version 1.7,
28 pages") and `.../n3423.txt` (35,522 characters, 22 text pages after
extraction). These are outside the repository.

### Successful fetches

| # | URL | Result |
| --- | --- | --- |
| 1 | `https://trapc.org` | SUCCESS (HTML). Site index; 7 posts; "TrapC is a trademark of Heroic Robots". |
| 2 | `https://trapc.org/about/` | SUCCESS (HTML, 2nd attempt; 1st timed out). Key release/license quote in §0/§2. |
| 3 | `https://trapc.org/trapc-a-year-later` | SUCCESS. `itrapc` + `trapc`; "code complete this month"; "Aiming for Q1 2026". |
| 4 | `https://trapc.org/the-register-on-vibing-trapc/` | SUCCESS. Confirms compiler unfinished, written with Claude, debugged with DeepSeek. |
| 5 | `https://trapc.org/c-style-member-functions-in-trapc/` | SUCCESS. Compile-time member functions; "Pointers have hidden constructors and destructors". |
| 6 | `https://trapc.org/?s=source` | SUCCESS (no results for source code). |
| 7 | `https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3423.pdf` | SUCCESS via `curl -sSL --max-time 45 -A "Mozilla/5.0" ... -o n3423.pdf`; `webfetch` alone FAILED ("Unsupported fetched file content type: application/pdf"). Text extracted with `pypdf.PdfReader(...).pages[i].extract_text()`. |
| 8 | `https://www.theregister.com/2026/01/26/trapc_claude_c_memory_safe_robin_rowe/` | SUCCESS. "almost works", Q1 2026 aim. |
| 9 | `https://api.github.com/search/repositories?q=trapc` | SUCCESS, 193 results, none TrapC-the-language. |
| 10 | `https://api.github.com/search/repositories?q=itrapc` | SUCCESS, 1 unrelated result. |
| 11 | `https://api.github.com/search/repositories?q=%22TrapC%22+in%3Aname%2Cdescription&sort=updated` | SUCCESS, unrelated repos only. |
| 12 | `https://api.github.com/users/rower/repos?per_page=100` | SUCCESS, one unrelated 2013 repo `rower/rower`. |
| 13 | `https://gist.github.com/lovemycodesnippets/b348ed6d5790da9eaf1936bf7fa4ec8d` | SUCCESS, third-party repost of whitepaper snippet; not used as source evidence. |

### Failed / negative fetches (recorded precisely)

| URL / check | Outcome |
| --- | --- |
| `https://trapc.org/about/` (first attempt) | FAILED — "Request timed out"; retried successfully (#2). |
| `https://open-std.org/.../n3423.pdf` via `webfetch` | FAILED — "Unsupported fetched file content type: application/pdf"; recovered via `curl` + `pypdf` (#7). |
| `https://api.github.com/orgs/heroicrobots` | FAILED — HTTP 404 (no such org). |
| `https://www.reddit.com/r/C_Programming/comments/1lwd4bz/...` | No tool output (fetch returned nothing); not used as evidence. |
| No `LICENSE` / `LICENSE.md` / `COPYING` URL | **Not attempted: no repository exists to fetch one from.** |
| No source file URLs (runtime pointer checks, allocation tracking, failure handling) | **Not attempted: no published source tree or file paths exist.** |

### Local files inspected (verified to exist)

`stdlib/error.hp` (lines 1–30, 95–129, 252–347, 340–524, 602–603),
`docs/UPSTREAM_AUDIT.md`, `docs/stdlib/upstream-reuse.md`,
`docs/stdlib/upstream_study/{cake,fil_c}.md`, `LICENSE`, repo root listing,
and `AGENTS.md` (via session context). No file outside
`docs/stdlib/upstream_study/trapc.md` was created or modified; no git
add/commit was performed.

---

## Failures / caveats (summary for the parent session)

1. **Repository could not be identified or reached because none exists.**
   Recorded as a negative finding, not as a fetch error to be papered over.
2. **License identifier is UNDETERMINED.** Only "free open source software"
   intent is published; do not assign an SPDX ID. Watch the false positive
   `$ echo "Open Source MIT" > LICENSE` in n3423 (Cmaker example).
3. **No source files were inspected.** Sections 3–5 are the author's design
   description, not code; metadata layout, check implementation, and trap
   machinery are explicitly **[NOT OBSERVED]**.
4. **Commit/tag:** N/A. The most precise pins available are the whitepaper
   `n3423`, dated 2025-01-07, and trapc.org post dates 2026-01-26 … 2026-02-02.
5. **Release status is stale in the paper** ("expected ... sometime in 2025")
   and in the About page ("Q1 2026"); as of 2026-11-23 the source is still
   unpublished. Re-check trapc.org before any future reuse discussion.
6. **ABI verdict is negative:** no code reuse without changing pointer
   semantics or C signatures, and no code is available anyway.
