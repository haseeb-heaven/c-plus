# C+ standard library — threat model

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **Scope:** the modern C+ standard library surface (`stdlib/*.hp`, the
  `stdlib/safe` SafeCRT adapters, and the hardened libc allocation layer) and
  the test/audit evidence in this checkout.
- **Method:** source inspection plus the existing audits named in §9. No new
  exploit, proof of concept, or vulnerability was produced by this task.
- **File ownership:** this document only. No header, source, test, build file,
  or git index entry was created or modified.

## How to read this document — theoretical vs confirmed

> **Every threat, weakness, and attack path in this document is a *theoretical
> concern* derived from source inspection or an existing audit unless the row
> explicitly cites a reproduced finding with an evidence artifact. A theoretical
> concern is NOT a confirmed vulnerability.** Where an existing audit reproduced
> a defect, it is labeled **[REPRODUCED]** and cited. Where a property is
> asserted by design but not exercised by a recorded command, it is labeled
> **[DESIGN / UNTESTED]**. Passing tests are contract-conformance evidence, not
> a proof of safety. No Rust-equivalence, memory-safety, or live-game claim is
> made here.

Evidence labels used below:

| Label | Meaning |
|---|---|
| **[SOURCE]** | Property read directly from a checked-in header or implementation. |
| **[REPRODUCED]** | A defect was triggered by an existing test and subsequently fixed or documented; evidence artifact cited. |
| **[RECORDED]** | Count/result quoted from an existing report in this checkout; not re-measured here. |
| **[DESIGN / UNTESTED]** | Designed/documented but not exercised by a recorded fixture in this checkout. |
| **[BOUNDARY]** | An explicit unsafe boundary that a library alone cannot close; must be documented and avoided. |

---

## 1. System model

The library is a thin, ABI-preserving safety layer over libc/POSIX and selected
vendored C libraries (safeclib; yyjson for JSON). Public object representations
and pointer values are unchanged; safety state lives in private, out-of-band
side tables (`docs/stdlib/abi-preserving-runtime-safety.md` §1–2,
**[SOURCE]**). Three independent metadata stores exist — hardened-libc
allocations, SafeCRT allocations, and the modern `stdlib/error.hp` side table —
with no cross-table reconciliation and no single process-wide pointer registry
(§1.1, **[SOURCE]**).

Failures are reported through a stable result protocol
(`STDLIB_OK = 0`, `STDLIB_EOF = 1`, `STDLIB_FAIL = -1`) plus categorized error
records (`INVALID, RANGE, IO, MEMORY, BOUNDS, OWNERSHIP, LIFECYCLE, UTF8`)
and off-by-default telemetry / an installable error callback
(`stdlib/error.hp:39-53,96-100,650-667`, **[SOURCE]**). Some failures are
fatal by design through `CPLUS_EXIT_*` codes (bounds, resource, ownership,
lifecycle), and aborts are intended to fail closed.

---

## 2. Assets

| Asset | Representation / owner | Where it lives | Notes |
|---|---|---|---|
| **Buffers** | Caller-supplied `char *`/`void *` + capacity; `string`, `vector`, `collections` containers; guarded `stdlib_mem_*` helpers | `stdlib/memory.hp`, `string.hp`, `vector.hp`, `collections.hp` | Capacity recorded in side table for `stdlib_alloc` blocks; interior pointers only via explicit `(base, offset)` (`docs/stdlib/MEMORY_BOUNDS_CONTRACT.md` §6, **[SOURCE]**). |
| **File handles** | Owning/borrowed `FILE *` in `file`; stdio wrappers | `stdlib/file.hp`, `io.hp` | Ownership explicit; `close()` on borrowed stream detaches without `fclose`; failed close still clears handle (`DEEP_FILE_CONTRACT.md` §7, **[SOURCE]**). |
| **Sockets / descriptors** | `fd` in RAII `listener` / `connection` (`-1` = inert) | `stdlib/net.hp` | Borrowed handles for fd 0/1/2 are non-owning; destructor never closes them (`DEEP_NET_CONTRACT.md`, **[REPRODUCED/RECORDED]**). |
| **JSON documents** | `json` document, `json_builder`, borrowed `json_view` | `stdlib/json.hp` (yyjson backend) | Views borrow from the owner with no owner reference — dangling after `doc.drop()`; builder/document use yyjson's own allocator, not `stdlib_alloc` (`DEEP_JSON_CONTRACT.md` §2.4, §3, **[SOURCE]**). |
| **Allocations** | One-word pointer + out-of-band metadata (`ptr`, `byte_size`, `elem_size`, `generation`) | `libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`, `stdlib/error.hp` | Three independent open-addressed tables with tombstones; public mirror struct is 40 bytes / align 8 (`abi-preserving-runtime-safety.md` §1.1–1.6, **[SOURCE]**). |
| **Mutexes / guards** | Opaque `cplus_mutex` handle + `guard` owner token in `thread` | `stdlib/thread.hp` | `unlock()`/`drop()` clear `owner`; repeated calls are no-ops; zero-init and NULL guards safe (`DEEP_THREAD_CONTRACT.md` §1, **[REPRODUCED/RECORDED]**). |
| **Metadata / audit state** | Side tables, telemetry counters, error callback, recent-free caches | as above | Private to the runtime/header; no inline headers, no pointer widening. |

---

## 3. Trust boundaries

| Boundary | Untrusted input crossing it | Current handling | Residual concern |
|---|---|---|---|
| **stdin / stream input** | Arbitrary bytes, line lengths, integer text, malformed records | `io.hp::read_line` / `file::read_line` bound with `fgets(size≤INT_MAX)`, peek to distinguish exact fit from truncation, drain overlong lines, recoverable `BOUNDS`; `scan` bounded for arrays (fixed 256 bytes for `char *`) (`DEEP_FILE_CONTRACT.md`, `LIMITATIONS.md`, **[REPRODUCED]**) | `read()` has no out-count (short read indistinguishable from full); embedded NUL ends `read_line`; `scan` on `char *` has no true capacity (§4.3). |
| **Files / paths** | Attacker-controlled path strings, mode strings, file contents, temp names | `file.hp` constructor passes `path`/`mode` to `fopen` after NULL check; `fs.hp` helpers; named temp helpers | TOCTOU (`access`→`open`), symlink swap, predictable temp names, umask/permission races, `mkdir` template reuse are classified **[BOUNDARY]** (`ADVERSARIAL_MATRIX.md` bugs 185–190); `fs_mkdtemp` consumes its `XXXXXX` template on failure (`TASK6_FINAL_REPORT.md` §8, **[SOURCE]**). |
| **Network** | Remote bytes, peer close, short writes, EINTR, connection floods | `net.hp` uses loopback-only evidence; partial-write loop, EINTR retry, broken-pipe → `STDLIB_FAIL`/`IO`, no-progress detection, bounds checks on fd (`DEEP_NET_CONTRACT.md`, **[REPRODUCED/RECORDED]**) | `SIGPIPE` is caller-managed; no off-host/remote-peer evidence; no built-in rate/size limits or timeouts on the public API (test-only `SO_RCVTIMEO`/watchdogs). |
| **Environment / argv** | `getenv`/`putenv` strings, argument vectors, toolchain env (`CPLUS_INCLUDE`) | `getenv`/`putenv` storage contracts classified **[BOUNDARY]** (`ADVERSARIAL_MATRIX.md` bugs 101–102); fuzz runners parse `argv` with bounds | `envp`/`argv` contents are caller/OS-owned and not capacity-checked by the library; include-path resolution via `CPLUS_INCLUDE` is implicated in the external runtime blocker (§7.4). |
| **Callbacks / error sinks** | Installed `stdlib_set_error_callback`, qsort-style comparators, signal handlers | Error callback has a per-thread re-entrancy guard; nested `error_fail` is suppressed, no latch; telemetry OFF by default (`DEEP_ERROR_TASK5_CONTRACT.md`, `abi-preserving-runtime-safety.md` §3.4, **[RECORDED]**) | Cross-thread callback behavior and contention on `meta_lock` untested; the callback can be re-entered on a different thread; logger is `write()`-based and async-signal-safe for its own path only (`ADVERSARIAL_FINDINGS.md` #10). |

---

## 4. Adversaries and capabilities

| Adversary | Capability assumptions | In scope? |
|---|---|---|
| **Untrusted data source** | Supplies bytes to stdin, files, or a loopback socket; chooses declared lengths up to `SIZE_MAX`; constructs deeply nested or malformed JSON. | Yes — primary focus. |
| **Buggy / legacy C caller** | Calls checked adapters incorrectly, mixes allocators, passes interior or raw pointers, ignores return codes, uses raw format strings / varargs. | Yes, as a documented boundary: checked APIs cannot prove caller ownership, lifetime, or provenance (`SAFECLIB_AUDIT.md` "Not-safe-by-name"). |
| **Programmer error in safe C+** | Bad index, double release, escaping borrow, copied owning resource, stale view, same-named owning local. | Yes — partly compile-time, partly runtime-diagnostic. |
| **Concurrent thread** | Races allocator bookkeeping, reuses a freed address (ABA), re-enters the error callback, releases a block while another thread accesses it. | Yes for diagnostic guards; full race verification is out of scope. |
| **Hostile peer on the network** | Closes connection mid-write, sends short/partial data, interrupts reads with signals. | Yes, loopback only in evidence. |
| **Filesystem attacker** | Swaps symlinks, races `access`/`open`, predicts temp names. | Boundary only — documented, not prevented by the library. |
| **Hostile OS / compromised compiler / deliberate cast/assembly circumvention** | Full control of the toolchain or raw memory. | **Out of scope**, consistent with `docs/THREAT_MODEL.md`. |

---

## 5. Threat categories

For each category: representative attack, current mitigation, and residual risk.
Threats are theoretical unless marked with a reproduced/recorded evidence label.

### 5.1 Nullable / invalid input

- **Threat:** `NULL` buffers, `NULL` out-params, zero/invalid sizes, invalid fd or
  port, NULL handles, NULL error record drive a checked wrapper into a
  dereference or a misleading success.
- **Mitigation [SOURCE/REPRODUCED]:** uniform `EINVAL`/`UNSUPPORTED` guards
  (e.g. `frexp(x, NULL)`, `modf`, `remquo`, signal/fenv NULL params,
  `strnlen_s`); `STDLIB_FAIL` with `CATEGORY_INVALID` and an untouched out
  sentinel; zero-size allocation defined as a tracked 1-byte block; invalid
  ports and borrowed fd 0/1/2 are inert; `error_fail(NULL)` is NULL-safe
  (`ADVERSARIAL_FINDINGS.md` #7, `ALLOCATION_CONTRACT.md` §5–7,
  `DEEP_JSON_CONTRACT.md` §2.2, `DEEP_NET_CONTRACT.md`).
- **Residual [DESIGN / UNTESTED]:** ISO-optional NULL parameters are preserved
  by design; a caller that ignores a returned error still proceeds with invalid
  data. `json_parse_file(NULL)` deliberately pairs `CATEGORY_INVALID` with
  `CPLUS_EXIT_IO` (documented contract, not a defect).

### 5.2 Overflow

- **Threat:** `dim * elem` or `count * size` overflow before allocation; huge
  `n` passed to memory/string wrappers; size cast to `int` for `fgets`; signed
  overflow in counters and hashers.
- **Mitigation [SOURCE/REPRODUCED]:** `calloc_s` checks `count > SIZE_MAX/size`
  **and** `count*size > RSIZE_MAX_MEM`; hardened libc checks `SIZE_MAX` product;
  out-of-range sizes refused, never clamped; bounded helpers use
  `__builtin_object_size(..., 1)`; `read_line` rejects `size > INT_MAX`;
  UBSan `undefined` aborts on signed overflow in instrumented code
  (`ALLOCATION_CONTRACT.md` §8, `ADVERSARIAL_FINDINGS.md` #5, #8,
  `DEEP_FILE_CONTRACT.md`).
- **Residual [RECORDED / DESIGN]:** `cplus_calloc` does not apply the
  `RSIZE_MAX_MEM` limit (family difference, `ALLOCATION_CONTRACT.md`);
  `-fsanitize=integer` produced **4 genuine reports** in the runtime splitmix64
  multiplier — recorded as FAIL, not "unsupported"
  (`TASK6_FINAL_REPORT.md` §6, `SANITIZER_MATRIX.md`); generation-counter wrap
  is unguarded and untested (`abi-preserving-runtime-safety.md` §1.3).

### 5.3 Truncation

- **Threat:** silent data loss or lack of NUL termination — overlong lines,
  `snprintf` return treated as bytes written, `strnlen_s` reading past a guard
  page, one-byte buffers that never consume input.
- **Mitigation [REPRODUCED]:** exact-fit peek + drain in `file::read_line` and
  `io::read_line` (7 line-handling defects reproduced and fixed); bounded
  `strnlen_s` loop patched; bounded format adapters return would-be length and
  NUL-terminate; `read_line` rejects invalid/one-byte cases with `BOUNDS`
  (`DEEP_FILE_CONTRACT.md`, `ADVERSARIAL_FINDINGS.md` #5, `SAFECLIB_AUDIT.md`).
- **Residual [DESIGN / BOUNDARY]:** missing NUL terminators are generally not
  detected; `read()` has no out-count; JSON string length is not exposed through
  `json.hp`; wide `%n` Tier-1 remains caller responsibility
  (`LIMITATIONS.md`, `DEEP_JSON_CONTRACT.md` §3, `TASK6_FINAL_REPORT.md` §8).

### 5.4 Path tricks

- **Threat:** TOCTOU between check and open, symlink swaps between check and
  use, predictable temp names, reusable/overwritten `XXXXXX` templates, umask
  and permission races, control characters or traversal in caller-supplied
  paths.
- **Mitigation [BOUNDARY]:** classified as explicit OS/API boundaries in the
  204-pattern matrix (bugs 185–190); `mkdir`/temp helpers exist but are not a
  general TOCTOU defense (`ADVERSARIAL_MATRIX.md`).
- **Residual [SOURCE]:** `fs_mkdtemp` consumes its `XXXXXX` template on failure
  (inherited from libc; retry requires reseeding). No library-level
  `openat`/`O_EXCL` discipline is enforced. Path validation is the caller's
  responsibility (`TASK6_FINAL_REPORT.md` §8, **[SOURCE]**).

### 5.5 Resource exhaustion

- **Threat:** oversized allocations, attacker-driven recursion/depth, fd
  exhaustion (`EMFILE`), long lines, table growth without bound, hung calls
  that tie up a thread.
- **Mitigation [RECORDED/REPRODUCED]:** JSON bounded budgets (depth 96 in-test,
  8192-byte line cap, 900-element array, `alarm(20)` backstop); deterministic
  allocation fault injection with latching counters; `EMFILE` fail-closed inert
  handles (forked `RLIMIT_NOFILE=3`); table growth capped by
  `ncap <= SIZE_MAX/4/sizeof(slot)`; recent-free cache bounded at 1024;
  harness timeouts and SIGKILL escalation (`DEEP_JSON_CONTRACT.md` §3,
  `DEEP_NET_CONTRACT.md`, `ALLOCATION_CONTRACT.md` §9,
  `TASK6_FINAL_REPORT.md` §1).
- **Residual [DESIGN / UNTESTED]:** no global memory/fd quota in the library;
  the stdio close-table had to be made fail-closed `EMFILE` after a probe chain
  defect (`ADVERSARIAL_FINDINGS.md` #1); a full recent-free table degrades
  double-free diagnosis; no production watchdog is shipped by the library —
  watchdogs exist only in tests (§6.5).

### 5.6 Lifecycle errors

- **Threat:** double free / double close / double unlock; use-after-free;
  stale aliases after address reuse; views outliving owners; copied owning
  resources; destructors skipped by non-local exits.
- **Mitigation [SOURCE/REPRODUCED]:** allocator side tables + bounded
  recent-free caches; generation tokens; transfer nulls the source and bumps
  the generation; `free_checked` nulls the caller handle on every path;
  ownership copies rejected at compile time; RAII `drop()` idempotent; guard
  `unlock()` clears `owner`; ERRORCHECK-mutex double-unlock detector
  (`ALLOCATION_CONTRACT.md` §5, §10, `DEEP_THREAD_CONTRACT.md` §2,
  `DEEP_JSON_CONTRACT.md` §2.4, `docs/THREAT_MODEL.md`).
- **Residual [BOUNDARY/RECORDED]:** raw use-after-free is **not** detected
  (ASan broken on this host: `sanitizer_malloc_mac.inc:189`); a stale pointer
  whose address was reissued can be misattributed; recent-free cache is 1024
  entries and best-effort; `json_view` dangles after owner drop;
  `setjmp`/`longjmp`, `pthread_cancel`, signals, `_Exit` skip side-table cleanup
  (§3.4 of `abi-preserving-runtime-safety.md`, **[DESIGN / UNTESTED]**);
  a compiler RAII name-dedup defect can omit a destructor for a same-named
  owning local in a sibling scope (F3, `DEEP_THREAD_CONTRACT.md` §4,
  **[REPRODUCED]**).

### 5.7 Races / concurrency

- **Threat:** allocator ABA race during `realloc` (lock released around the host
  call), concurrent release during guarded access, data races on shared state,
  callback re-entrancy across threads, TOCTOU on files/sockets.
- **Mitigation [SOURCE/REPRODUCED]:** ABA guard captures old generation under
  lock and only adjusts a slot whose generation still matches;
  reserve-before-realloc prevents post-success table failure; a metadata mutex
  protects table queries; per-thread error-callback re-entrancy guard;
  deterministic concurrency fault injection (`test_corpus_concurrency` 10,203
  ops; 16 metadata-loss failures → 0 after the ABA fix)
  (`ALLOCATION_CONTRACT.md` §ABA guard, `ADVERSARIAL_FINDINGS.md` #3,
  `MEMORY_BOUNDS_CONTRACT.md` §6, `DEEP_ERROR_TASK5_CONTRACT.md`).
- **Residual [DESIGN / UNTESTED]:** the ABA guard protects metadata only, not
  raw stale-pointer dereferences; checked generation is opt-in; capacity and
  generation are read under the lock per query but a concurrent release after a
  successful check is outside the wrapper's control; no static or dynamic
  data-race verification; cross-thread callback behavior and `meta_lock`
  contention unexercised (`docs/LIMITATIONS.md` "Concurrency",
  `docs/THREAT_MODEL.md`).

### 5.8 Allocator errors

- **Threat:** OOM / bookkeeping OOM, host `realloc` moving or failing,
  cross-allocator free, interior/stack/static pointers passed as owned,
  interior-pointer capacity confusion, realloc failure overwriting the
  original handle.
- **Mitigation [SOURCE/RECORDED]:** reserve table room before host `realloc`;
  failure detected before mutation; original preserved on every failure path;
  successful realloc bumps generation and removes old address; unowned pointers
  return `EINVAL` without calling host `free`; zero-size defined; deterministic
  fault injection (294/294 checks in plain and UBSan builds)
  (`ALLOCATION_CONTRACT.md` §2–§9, `TASK6_FINAL_REPORT.md` §7).
- **Residual [BOUNDARY/RECORDED]:** host `free()` of a tracked pointer leaves a
  false-live slot and inflated counters (F-02); `cplus_aligned_alloc` is
  deliberately untracked; `cplus_alloc_remaining_capacity` is a range hint, not
  provenance; interior pointers need explicit `(base, offset)` and are otherwise
  refused; no cross-table reconciliation; generation check is opt-in
  (`abi-preserving-runtime-safety.md` §3.1–3.3, `MEMORY_BOUNDS_CONTRACT.md` §6).

---

## 6. Mitigations currently present

### 6.1 Side-table metadata

Three private, open-addressed tables record `ptr`, `byte_size`, `elem_size`, and
`generation` for tracked allocations; tombstones mark deletion; the public
mirror struct is 40 bytes / align 8 and ABI layout is asserted 70/70 × 2
(`abi-preserving-runtime-safety.md` §1.1–1.6, §2.2, **[SOURCE/RECORDED]**).
Metadata is out-of-band: pointers stay one word and public layouts are unchanged.
Queries (`is_live`, `size`, `generation`, `remaining_capacity`) drive the checked
wrappers. Untracked pointers return `0`/`-1` and are never silently trusted.

### 6.2 Bounded checks

- `RSIZE_MAX_MEM` and `SIZE_MAX` division checks on allocation sizes;
  `calloc` overflow guarded (`ALLOCATION_CONTRACT.md` §8, **[SOURCE]**).
- `__builtin_object_size(p, 1)` (exact enclosing object) after a reproduced
  8-byte canary overwrite with mode `0` (`ADVERSARIAL_FINDINGS.md` #8,
  **[REPRODUCED]**).
- `fgets` width ≤ `INT_MAX` with exact-fit peek and drain
  (`DEEP_FILE_CONTRACT.md`, **[REPRODUCED]**).
- JSON depth/size budgets, bounded fd table, fail-closed `EMFILE`
  (`DEEP_JSON_CONTRACT.md`, `ADVERSARIAL_FINDINGS.md` #1, **[REPRODUCED]**).
- UBSan `undefined,bounds,null,alignment` with
  `-fno-sanitize-recover=all` in safe mode; runtime negative tests abort on
  first violation (`docs/THREAT_MODEL.md`, **[SOURCE]**).

### 6.3 Fault injection

Deterministic, seeded, canary-based — not random or production:
allocation fail-after/reset/sequence latches; link-time `read`/`write` overrides
(zero-progress, short-write, one-shot `EINTR`, hard `EIO`);
`RLIMIT_NOFILE=3` for `EMFILE`; ERRORCHECK mutexes and a self-deadlocking
positive control; fixed-seed `f100_*` corpora (100 configurations × 3 modes)
(`DEEP_NET_CONTRACT.md`, `DEEP_THREAD_CONTRACT.md`, `ADVERSARIAL_FINDINGS.md`,
`adversarial-testing.md` §6, **[RECORDED]**). Injection is a coverage tool;
successful normal allocation alone is never reported as safety evidence.

### 6.4 Generation tokens

Per-allocation counters (`g_gen`, `stdlib_state.meta_gen`) increment on record
and transfer; tombstones reset generation to 0; public `check_generation`
APIs exist. Transfer nulls the source only on success; a successful `realloc`
bumps the generation. Generation checks are **opt-in** and wrap is unguarded
(`abi-preserving-runtime-safety.md` §1.3, `ALLOCATION_CONTRACT.md` §3,
**[SOURCE]**).

### 6.5 Watchdogs and timeouts

- `alarm(2)` SIGALRM deadlock detector for `guard` re-lock, with a
  self-deadlocking positive control (`DEEP_THREAD_CONTRACT.md` §2).
- 45 s SIGALRM net watchdog writing a fatal line and `_exit(90)`; 3 s socket
  timeouts and pipe handshakes (`DEEP_NET_CONTRACT.md`).
- `alarm(20)` JSON recursion backstop; 5 s parent watchdog with `SIGKILL`
  fallback for time tests (`DEEP_JSON_CONTRACT.md`, `DEEP_FS_TIME_CONTRACT.md`).
- Harness build/run timeouts (60/90 s build, 30 s run, `kill-after=5`) classify
  `124`/`≥128` as TIMEOUT/SKIP, never PASS
  (`TASK6_FINAL_REPORT.md` §1, `DEEP_TESTS_HARNESS.md`, **[RECORDED]**).

These are **test-harness** controls; the shipped library does not install
watchdogs for callers.

### 6.6 Ancillary controls

RAII scope-exit `drop()` and compile-time copy/escape rejection; recoverable
error protocol + off-by-default callback/telemetry with per-thread re-entrancy
guard; `write()`-based async-signal-safe logger; ABI-compat symbol/layout
audits (69 passed, 0 failed; 70/70 layouts × 2)
(`docs/THREAT_MODEL.md`, `DEEP_ERROR_TASK5_CONTRACT.md`,
`ADVERSARIAL_FINDINGS.md` #10, `abi-preserving-runtime-safety.md` §6.1,
**[RECORDED]**).

---

## 7. Residual risks

### 7.1 Raw pointers / unknown capacity

Arbitrary `malloc`/stack/static pointers have no side-table entry; guarded
helpers refuse them, but ordinary dereferences and pointer arithmetic through a
raw pointer are unchecked. Missing NUL terminators, flexible-array-member
copies, arbitrary pointer arithmetic, and interior pointers expressed as
`base + offset` without an explicit offset are not protected
(`MEMORY_BOUNDS_CONTRACT.md` §6, `LIMITATIONS.md`, **[SOURCE/BOUNDARY]**).

### 7.2 Uninstrumented C libraries

Metadata covers only allocations made through the named wrappers. A precompiled
C library writing through a raw pointer never invokes a checked helper, so no
check runs; a raw host `free()` of a tracked pointer leaves false-live state.
yyjson (JSON) uses its own allocator, so `stdlib_alloc_fail_after` cannot inject
builder OOM. No interposition or store instrumentation exists
(`abi-preserving-runtime-safety.md` §3.1, `DEEP_JSON_CONTRACT.md` §3,
**[SOURCE]**).

### 7.3 Compiler gaps

27 compile-time capability/tagging gaps across 8 manifests; untagged rejections
are PARTIAL, not enforcement; `[CPLUS-BOUNDS]`/`[CPLUS-LIFETIME]`/`[CPLUS-OWNER]`
static diagnostics with `--sanitize=none` are not implemented; the RAII
name-dedup defect (F3) can omit a destructor; `goto`/`longjmp`/`setjmp` bypass
cleanup. 46 of 127 wrapper-safety findings remain OPEN, 7 are compiler gaps, 4
BLOCKED (`TASK6_FINAL_REPORT.md` §4, §10, `DEEP_THREAD_CONTRACT.md` §4,
`docs/LIMITATIONS.md`, `AGENT1_COMPILER_GAPS.md`, **[RECORDED]**).

### 7.4 External runtime blocker

The compiler's sanitizer build path compiles the **external**
`c-plus-core/runtime/cplus_runtime.c` against this repo's local
`runtime/include/cplus/cplus.h`. The external source uses
`CPLUS_EXIT_ARITHMETIC` (value 110 in the external header); the local header
ends at `CPLUS_EXIT_IO = 109`. Every instrumented runtime compilation therefore
fails before a test binary is produced: `tests/run_tests.sh` recorded
**99 passed / 51 failed** (all 51 the same build diagnostic) and the deep runner
recorded **28 passed / 58 build failures / 1 unrelated skip**. This is a
cross-repository interface mismatch owned by Agent 1, not a library defect;
minimal fixes (define the constant locally, or align include paths via
`CPLUS_INCLUDE`) are specified in `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`,
**[REPRODUCED]**. Until resolved, the sanitizer-backed dynamic verification
path cannot be relied on as green.

### 7.5 Verification-platform gaps

ASan/Leak/Thread/Memory/CFI are rejected by the backend probe on this host
(Apple clang 16 / arm64); UBSan passing is **not** a memory-safety proof.
Windows x86 ABI, calling conventions, and live-game/injected-DLL behavior are
untested; remote Linux CI and SonarCloud post-change gates are **UNVERIFIED**.
SafeCRT integration is a checked slice (40 of 437 APIs integrated; 387 not);
ISO C inventory has 27 MISSING and 49 UNSUPPORTED APIs; ABI evidence is 2 of
714 declared matrix rows
(`TASK6_FINAL_REPORT.md` §6, §10, `LIMITATIONS.md` "Agent 2 stdlib hardening
status", `docs/AUDIT.md`, **[RECORDED]**).

### 7.6 Other documented, non-fixed limits

`SO_REUSEADDR` semantics, caller-managed `SIGPIPE`, no object-key iteration or
string-length accessor in JSON, wide `%n` Tier-1, an interior-pointer alias
window in `string::append` past `count` after `clear()`
(`container_safety/UPSTREAM_REUSE.md:97`, **[SOURCE]**), and generation wrap.
Each is documented as a contract/limit rather than "fixed to pass"
(`DEEP_NET_CONTRACT.md` §5, `DEEP_JSON_CONTRACT.md` §4,
`TASK6_FINAL_REPORT.md` §8).

---

## 8. Top risks (ranked)

1. **External runtime blocker disables sanitizer verification (§7.4).** The
   `CPLUS_EXIT_ARITHMETIC` header mismatch breaks instrumented-runtime builds
   (51 + 58 recorded build failures); the primary dynamic defense cannot be
   treated as green until Agent 1 aligns the interface. **[REPRODUCED]**
2. **Raw pointers and uninstrumented C libraries (§7.1–7.2).** Largest unguarded
   surface; no ASan on this host; checked wrappers cannot observe raw stores or
   prove caller ownership/lifetime/provenance. **[BOUNDARY]**
3. **Generation opt-in, wrap, and address reuse (§5.6–5.8).** A stale alias can
   act on a new block at a recycled address (F-01/F-04/F-05/F-06); generation
   checks are opt-in and wrap unguarded. **[DESIGN / UNTESTED]**
4. **Compiler gaps and RAII name dedup (§7.3).** 27 compile-time gaps, 46 OPEN
   findings, and a reproduced destructor-omission defect mean compile-time
   enforcement is partial. **[REPRODUCED/RECORDED]**
5. **Resource exhaustion and path-trick boundaries (§5.4–5.5).** No global
   quotas; TOCTOU/symlink/temp-name races are documented, not prevented.
   **[BOUNDARY]**
6. **Platform/verification coverage (§7.5).** No Windows x86, remote CI, or
   full-sanitizer evidence; passing local fixtures are not a safety proof.
   **[RECORDED]**

---

## 9. Evidence base (sources inspected)

Primary documents read for this model:

- `docs/THREAT_MODEL.md`, `docs/LIMITATIONS.md`, `docs/AUDIT.md`;
- `stdlib/error.hp`, `stdlib/net.hp`, `stdlib/file.hp`, `stdlib/io.hp`,
  `stdlib/memory.hp`, `stdlib/thread.hp`, `stdlib/json.hp` (source headers);
- `docs/stdlib/abi-preserving-runtime-safety.md` (metadata architecture,
  mitigations, limits), `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md`,
  `docs/stdlib/memory-container-safety.md`;
- `docs/stdlib/DEEP_FILE_CONTRACT.md`, `DEEP_NET_CONTRACT.md`,
  `DEEP_JSON_CONTRACT.md`, `DEEP_THREAD_CONTRACT.md`,
  `DEEP_ERROR_TASK5_CONTRACT.md`, `DEEP_TESTS_HARNESS.md`,
  `adversarial-testing.md`, `TASK6_FINAL_REPORT.md`,
  `AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`;
- `docs/libc/ADVERSARIAL_FINDINGS.md`, `ADVERSARIAL_MATRIX.md`,
  `ALLOCATION_CONTRACT.md`, `AUDIT_FINDINGS_TRACKER.md`,
  `AGENT1_COMPILER_GAPS.md`;
- `docs/SAFECLIB_AUDIT.md`,
  `docs/stdlib/container_safety/UPSTREAM_REUSE.md`,
  `docs/stdlib/REGRESSION_INVENTORY.md`.

**Closing statement:** this model catalogs *theoretical* concerns and
reproduced findings from existing audits. None of the theoretical concerns is a
confirmed vulnerability, and this document reports no new vulnerability. All
counts and defects are quoted from the cited artifacts; none was invented here.
