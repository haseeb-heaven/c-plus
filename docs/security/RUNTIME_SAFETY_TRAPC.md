# Trap-C runtime-safety comparison — honest status

Evidence-only record for branch `agent/modern-stdlib`, collected 2026-11-23.
This document is one agent's contribution to the runtime-safety comparison. It
records exactly what can and cannot be said about **Trap-C** (Robin Rowe /
TRASEC / Heroic Robots) from the material available in this checkout.

## Headline status

| Question | Answer |
|---|---|
| Was Trap-C source integrated? | **NO — no source code was integrated.** |
| Can Trap-C source be integrated here? | **NO — none can be.** No public repository, no commit/tag, no LICENSE file, no SPDX identifier exists; the compiler/interpreter are unpublished. |
| License | **UNDETERMINED** — "FOSS intended, license not published." **Not MIT, not BSD, not Apache, not any SPDX identifier.** |
| Runtime-safety comparison result | **NOT TESTABLE** — no compiler, toolchain, or source is published. No pass is asserted or fabricated. |
| ABI verdict | **REJECTED** — one-way ABI, managed pointers with hidden RTTI, no-op `free`/`delete` would break the ordinary C pointer representation. |
| Is this an implementation or validation of Trap-C? | **NO — explicitly not.** This is documentation-based traceability only. |

---

## 1. Non-integration statement (read first)

**NO SOURCE CODE WAS INTEGRATED, AND NONE CAN BE.** As of this study date
(2026-11-23) there is **no public Trap-C repository**, hence no commit, no tag,
no `LICENSE`/`COPYING` file, no source file, no header and no test to inspect or
vendor. The two implementation artifacts named by the author — `itrapc`
(interpreter) and `trapc` (compiler) — are unreleased and closed; the vendor's
own About page describes Trap-C as *"in development and nearing release as free
open source software."* No Trap-C file, code, header, or test was copied into
this repository.

This document therefore makes **no reuse authorization**. Even if a future
published license permitted reuse, §5 shows that the documented design cannot
be ported without changing the C pointer contract and C signatures, which this
project's policy forbids.

**Explicit non-claim:** this document is **not an implementation of Trap-C and
not a validation of Trap-C**. It summarizes third-party documentation for the
purpose of an honest comparison; every runtime-safety property described in §3
is reported as the author's *documented design intent*, never as observed
behavior or a verified result.

---

## 2. Exact available pins and license

No repository exists to pin. The most precise anchors available are the dated
documents themselves:

| Artifact | Exact pin | URL |
|---|---|---|
| WG14 paper `n3423`, *TrapC: Memory Safe C Programming with No UB*, Pr. Robin Rowe | dated **2025-01-07** (WG14 Graz meeting, Feb 2025; 22 PDF text pages) | <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3423.pdf> |
| trapc.org "TrapC, a Year Later" | post dated **2026-01-26** (`itrapc` + `trapc`; "code complete this month"; "Aiming for Q1 2026") | <https://trapc.org/trapc-a-year-later> |
| trapc.org About | release/license-intent statements | <https://trapc.org/about/> |
| trapc.org "The Register on Vibing TrapC" | compiler-unfinished status | <https://trapc.org/the-register-on-vibing-trapc/> |
| trapc.org "C++ Style Member Functions" | "Pointers have hidden constructors and destructors" | <https://trapc.org/c-style-member-functions-in-trapc/> |
| The Register, 2026-01-26 | independent confirmation compiler not released ("almost works") | <https://www.theregister.com/2026/01/26/trapc_claude_c_memory_safe_robin_rowe/> |
| GitHub / GitLab repo search | **none** — no official repository; `github.com/heroicrobots` returns HTTP 404 | (see study §1 and §9) |

**License: UNDETERMINED.** The only published statements are *"free open source
software (FOSS)"* (n3423) and *"nearing release as free open source software"*
(trapc.org/about); neither names a license. There is no LICENSE file, COPYING
file, SPDX header, or copyright notice from any Trap-C source file anywhere.
Per the study, **do not write MIT, BSD, Apache, or any SPDX identifier for
Trap-C**. The literal string `$ echo "Open Source MIT" > LICENSE` appears in
n3423's *Cmaker Build Manager* tutorial as a user creating a LICENSE for their
own new project — it is **not** a statement of Trap-C's license and must never
be quoted as one.

These pins are stale as to release status: n3423 says "expected ... sometime in
2025" and the About page says "Q1 2026", yet as of 2026-11-23 the source is
still unpublished. trapc.org must be re-checked before any future reuse
discussion.

---

## 3. Documented runtime-safety ideas — RESEARCH-ONLY

Everything in this section is **RESEARCH-ONLY**: [DOCUMENTED] design
description quoted or closely paraphrased from author-published material, **not
source findings and not verified behavior**. The implementation of every
mechanism is **[NOT OBSERVED]** — no source exists. Source URLs are the exact
ones used by `docs/stdlib/upstream_study/trapc.md` (fetched 2026-11-23).

### 3.1 Trap semantics (n3423, §"The 'trap' in TrapC")

- Caller-local, non-propagating failure handling: *"When an error is trapped in
  TrapC, the function returns immediately to the caller's 'trap' handler, if
  there is one... Faults must be trapped by the caller, do not get passed up the
  line like exceptions in C++."* No alternate return type and no alternate
  return stack (unlike C++ exceptions).
- Handler placement is syntax-constrained: the handler must immediately follow
  the trapped call; rethrow uses `trap.return`; there is no handler for
  `main()`.
- Fail-fast by default: with no handler, Trap-C *"will terminate the program
  printing an error message"* with `exit()` code equal to `errno`.
- A trapped function that must return a value returns *"a block of memory that
  is sizeof the return type (and zeroed)."*
- Handler introspection via `trap.errno`, `trap.msg`, `trap.return`; C
  compatibility via `#ifdef __trapc` mirroring to `if(!ok) { ... }`.
- POSIX/stdlib calls are *"trap-aware wrappers to the C functions."*

### 3.2 Extent / RTTI queries (n3423, §"TrapC Pointer Intelligence", etc.)

- `testof()` is a non-trapping validity query: *"returns false if the pointer
  failed test"*; `assert(testof(p))` *"confirms is TrapC pointer, not raw C
  pointer."*
- Mystery-pointer rule: *"attempting to manipulate a mystery pointer, that is,
  a pointer with no RTTI, will trap. Except for `testof()`, any attempt to use
  the pointer will also zero it."*
- Per-pointer metadata API `get_type_info(p)` returning `type_info`; helpers
  `nameof`, `typeof`, `sizeof`, `countof`, `offsetof`, `lastof`, `addressof`,
  `testof`. The paper explicitly labels its `struct type_info` conceptual:
  *"How the struct type_info is defined internally is a compiler implementation
  detail."*
- Extent use: `assert(p+9 == lastof(p))`; RTTI reports `size`/`count`; known
  extent enables p-string semantics (overwriting a z-string terminator is
  allowed because Trap-C *knows buffer size*).
- Nulling on overrun: *"When `p` steps off the end, TrapC nulls p"*;
  *"TrapC pointers always point to valid memory. Cannot overrun or contain
  garbage."*
- Offset-to-base recovery: `p-offsetof(p) == tokenp` in the `strsep` example;
  Trap-C *"frees tokenp, not p."*
- Automatic, lifetime-managed (not garbage-collected) memory: *"For C
  compatibility, it is ok to call free() in TrapC, but it is ignored"*;
  `free`/`delete` are declared no-ops (`void free(void* ptr) alias;`).

**Exact source URLs (from the study, all fetched 2026-11-23):**

- Primary design paper: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3423.pdf>
- Vendor site index: <https://trapc.org>
- About / license intent: <https://trapc.org/about/>
- "TrapC, a Year Later": <https://trapc.org/trapc-a-year-later>
- "The Register on Vibing TrapC": <https://trapc.org/the-register-on-vibing-trapc/>
- "C++ Style Member Functions": <https://trapc.org/c-style-member-functions-in-trapc/>
- Independent report: <https://www.theregister.com/2026/01/26/trapc_claude_c_memory_safe_robin_rowe/>

Third-party snippet note: a GitHub gist `lovemycodesnippets/trap_free.c`
(2025-03-11) reposts a 7-line example from the whitepaper's `use_after_free.c`.
It is a third-party repost, **not** Trap-C source, and was not used as evidence
for any implementation claim.

**[NOT OBSERVED]** — all implementation internals: metadata layout (hash table,
shadow memory, inline header, or side table), check ordering, trap
instruction/ABI, `testof()` complexity, generation/tag width, and thread safety
of metadata updates. No claim is made about any of them.

---

## 4. Why Trap-C cannot be tested here — NOT TESTABLE

**Runtime-safety comparison status: NOT TESTABLE.** This is a negative finding
backed by evidence, not a skipped test or a fabricated pass.

| Prerequisite for testing | Status | Evidence |
|---|---|---|
| Published source tree to build | **ABSENT** | Study §1/§9: no official GitHub/GitLab/Bitbucket repo; GitHub searches for `trapc`, `itrapc`, `"TrapC" in:name,description` return no Trap-C language repo; `github.com/heroicrobots` → HTTP 404; `trapc.org/?s=source` → no source download. |
| `trapc` compiler binary/toolchain | **ABSENT** | trapc.org/trapc-a-year-later (2026-01-26): *"I had wanted to make a software release by 1 January 2026, but too many bugs... Aiming for Q1 2026"*; The Register (2026-01-26): *"almost works."* No binary is distributed. |
| `itrapc` interpreter | **ABSENT** | Named in n3423 and trapc.org, but unreleased; no download exists. |
| Trap-C test suite to execute | **ABSENT** | No source → no tests. The third-party gist is a repost of a whitepaper snippet, not a test suite. |
| Commit/tag to pin and reproduce | **N/A** | No repository exists to pin; only dated documents are available. |
| Licensed permission to obtain/use source | **UNDETERMINED** | No LICENSE file, no SPDX identifier, no published license text (§2). |

Consequences for the comparison:

- **No Trap-C program was compiled, run, or measured in this checkout.** There
  is no executable and no source; no test harness can be constructed.
- **No runtime-safety property of Trap-C was verified.** The trap, extent,
  nulling, and no-op-`free` behaviors in §3 remain author-published *claims*
  only; this document does not mark any of them PASS, FAIL, or "equivalent to
  C+."
- **No performance, memory, or safety-number comparison against Trap-C is
  asserted.** Any such number would be fabricated. The cross-language
  `benchmarks/safety_compare/` artifacts in this repository compare C+ with
  Rust, not with Trap-C, and are not evidence here.
- The only honest classification for every Trap-C row of a runtime-safety
  comparison is **NOT TESTABLE (no published source/toolchain)**. This matches
  `docs/stdlib/upstream-reuse.md` §10.4, where Trap-C is classified
  **RESEARCH-ONLY / no source** with no changed source file and no executed
  integration test.

---

## 5. ABI verdict — REJECTED

**Verdict: Trap-C's mechanisms are rejected for this project; they cannot be
reused in C+ without changing the pointer representation or the C signatures.**
This follows the documented design (n3423, §"What is TrapC in a Nutshell?" and
§"C ABI Compatibility") and is decisive independently of the license question.

Documented facts:

- *"TrapC is one-way ABI compatible with C, such that TrapC functions may call
  any C function as-is."* The reverse direction is not supported.
- *"Passing a raw C pointer safely to a TrapC function requires extra steps,
  because TrapC pointers have hidden RTTI."*
- *"Passing raw C pointers to TrapC functions is not ok. There are no 'mystery
  pointers' in TrapC."* A Trap-C pointer may be passed to C because that
  transition *"ignores hidden TrapC pointer RTTI"*; the reverse would leave
  Trap-C *"missing necessary pointer RTTI."*
- Trap-C supplies *"API-compatible versions of C POSIX and C++ STL standard
  libraries, to not return raw pointers."*
- Calling into C is described as *"like using 'unsafe' in Rust, or maybe even
  less secure than that."*

Why this breaks C+:

1. **Pointer representation / contract.** C+ keeps ordinary thin pointers
   (`T *` is one address word). Trap-C pointers are *"diligently
   compiler-managed"* and carry hidden per-pointer RTTI plus managed lifetime.
   A C+ `T *` passed to Trap-C-compiled code is exactly the forbidden
   "mystery pointer." (Even whether the physical word width is unchanged is
   **not stated** in the paper — do not assume it.)
2. **C signatures.** `free`/`delete` become no-ops, `malloc` is re-expressed
   via `new` with hidden RTTI, POSIX calls become trap-aware wrappers, and
   trapped calls return a zeroed buffer. These are signature/semantics changes,
   not a drop-in library.
3. **Language surface.** `trap`, `trap.return`, `alias`, castplates, member
   functions, `nullptr`, `new`, RTTI functions, and removal of `goto`/`union`
   are compiler/language features that cannot be added by a header.

All three are compiler/runtime/language territory (Agent 1), and the project's
stated policy (`docs/UPSTREAM_AUDIT.md`, `docs/FOUNDATION.md`,
`docs/stdlib/abi-preserving-runtime-safety.md`) preserves the native C ABI and
the existing toolchain. This is the same reason Fil-C's fat/capability pointers
and SoftBoundCETS's shadow/trie mechanisms are rejected for port. In addition,
there is no published Trap-C source to reuse even if the policy changed.

---

## 6. What this document does NOT claim

- It is **not an implementation or validation of Trap-C**. No Trap-C mechanism
  was built, compiled, executed, measured, or verified.
- It makes **no license determination** beyond UNDETERMINED; it assigns no SPDX
  identifier to Trap-C.
- It claims **no runtime-safety PASS** for Trap-C, and no C+-vs-Trap-C
  equivalence, parity, or superiority result.
- It attributes **no runtime internals** (metadata layout, check ordering, free
  cache, locks, tag width) to Trap-C; those are [NOT OBSERVED].
- It is documentation-based traceability only, consistent with
  `docs/stdlib/upstream-reuse.md` §10.4 (RESEARCH-ONLY / REJECTED).

Fuller per-source detail: `docs/stdlib/upstream_study/trapc.md` and
`docs/stdlib/upstream-reuse.md` §§1–2, §4, §9, §10.4. No source file, header,
test, configuration, or build file was created or modified by this task other
than this document; no git add/commit was performed.
