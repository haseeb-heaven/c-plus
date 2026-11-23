# Modern C+ standard library

Owner: Agent 2 (branch `agent/modern-stdlib`, **PR #17 open**; nothing pushed or
merged). Companion to [`STDLIB.md`](STDLIB.md), which is the API reference.
Agent 2 has stopped adding new stdlib surface; the current priority is
compiler-enforced safety (P0/P1/P2), which is Agent 1 work — see
[`STATUS.md`](STATUS.md) and `docs/libc/AGENT1_PRIORITY_HANDOFF.md`.

> **Current, verified state (2026-11-23, Phase 9):** see
> [`stdlib/AGENT2_FINAL_REPORT.md`](stdlib/AGENT2_FINAL_REPORT.md) for the
> consolidated counts, upstream-reuse credit, defects fixed, ABI/benchmark
> results and blockers. §11 below is the short summary. Earlier snapshots in
> §§8–10 are historical and are not the current numbers. API *reachability* is
> not runtime *coverage*, and no 100% stdlib-safety claim is made.

Scope: `stdlib/`, the vendored upstream C libraries in `third_party/`, their
tests and examples in `tests/stdlib/`, and the interfaces this layer needs from
the compiler/runtime owned by Agent 1 (`agent/core-safety`).

Design order, as instructed: **safety, memory safety and RAII first; features
second; speed measured, not assumed.**  The language stays small: C plus the C+
RAII constructs.  No exceptions, no `longjmp`, no mandatory GC, no new syntax.

### One library, three areas

The deliverable spans the modern C+ `.hp` library (`stdlib/*.hp`: core containers/strings/sync/files/FS/error/telemetry plus JSON/HTTP/async/GUI/audio/image/video adapters), the optional SafeCRT slice (`stdlib/safe/`, `libcplus_safecrt.a`), and the hardened `libc/` slice (`libcplus_hardened.a`). Compiler-owned static analysis (lifetime, provenance, unknown-capacity raw pointers) remains Agent 1's domain.

## 1. Surface: C++-flavoured, C underneath

Types are nouns, construction names the type, operations are methods:

```c
string s = string("a,b,c");       /* constructor, not NewString()          */
vector v = vector();              /* one obvious construction path         */
v.push_back(1);                   /* STL-style method names                */
file f = file("x.txt", "r");      /* RAII: the destructor closes           */
json doc = json(text, &err);      /* parse; doc.valid() reports failure    */
```

Rules the surface follows:

| rule | why |
| --- | --- |
| `this->field` / `this->method()` only, never a `self` keyword | keeps the source valid C++-shaped C; `self` is not part of the language |
| constructors take an `error *` when they can fail | no exceptions; failure is a value |
| alternate construction is a snake_case free function (`json_parse_file`, `file_borrow`, `image_from_memory`) | C+ has no overloading or static methods |
| headers define methods inline | C+ classes lower to struct + functions |
| internal cross-object calls use the lowered names (`mutex_lock(owner)`, `error_reset(err)`) | inside a class body C+ only rewrites `this->method()`; see §7 |

## 2. Ownership model

Every handle declares which of four states it is in:

| state | expressed by | destructor |
| --- | --- | --- |
| **owned** | factory/ctor result (`file(...)`, `audio_engine(...)`, `json(...)`) | releases |
| **borrowed** | `file_borrow`, `listener_borrow`, `connection_borrow`, `string_view`, `json_view`, `guard`, `native()` | releases nothing |
| **invalid** | failed ctor / `valid() == 0` / `is_open() == 0` | releases nothing |
| **moved-from** | after `drop` or `cplus_move` | releases nothing |

Safety rules enforced in the library:

- **A destructor must be inert on a zeroed handle.**  The compiler
  zero-initialises a resource local with no initialiser (`= {0}`), so
  `listener l;` must not close descriptor 0.  Handles carry an `owns` flag and
  validators check it before closing.
- **Descriptors 0/1/2 are never closed by a zero-initialised handle**: `owns`
  is 0, so the guard is ownership, not an fd range.
- **A constructor that fails leaves a valid object in the invalid state**, never
  half-initialised.  Constructors contain no `return`; they use nested branches
  or a `do { ... } while (0)` with `break`.
- **Constructors never use `->` on other objects**, because a C+ constructor
  body rewrites `->` to `.` (that is how `this->field` becomes the result
  struct member).  They take native pointers instead
  (`renderer(win.native(), ...)`, `timer(loop.native(), ...)`).
- **No second allocator.**  `string`/`vector` use `cplus_alloc`; images use the
  same libc allocator stb frees with; JSON frees exactly what yyjson allocated.

## 3. Recoverable errors ("exceptions" without exceptions)

```c
error err = error();
int n = 0;
if (read_int(&n, &err) != STDLIB_OK)
    print("input failed:", err.what(), err.code());
```

- Status: `STDLIB_OK` (0), `STDLIB_EOF` (1), `STDLIB_FAIL` (-1).
- `error` carries `failed` + a `cplus_exit_code` + a bounded message.  Reusing
  the runtime's code space keeps one namespace.
- End of input is **not** an error.
- `error_reset(err)` / `error_fail(err, code, msg)` are free helpers used
  internally (method sugar is not applied inside class bodies); user code can
  use `err.clear()`, `err.set(...)`, `err.ok()`, `err.what()`.
- Fatal safety violations (bounds, double free, null deref) remain
  `cplus_fatal`'s job and are never converted into return codes.

## 4. Telemetry

Every function and method logs a trace entry through `cplus_log`:

```sh
c+ build app.cp --log trace       # or --log-level debug, --log info ...
```

With logging off (`cplus_log(...)` expands to `((void)0)`) the calls cost
nothing at run time.  `tests/stdlib/run_stdlib_tests.sh` exercises the log path
via the runtime's logging fixture.

## 5. Multi-translation-unit linkage

**Fixed.** A `.hp` header may be included from any number of translation units.

The library does its part: every function defined in a `.hp` is `static inline`,
so a definition never collides at link time.  Class members are lowered by the
compiler, so the remaining half is a small compiler change: members lowered from
a header are emitted `static inline` (each TU gets a private copy).  The driver
already knows which file is a header (it is lowering an `#include`d `.hp`), so
it passes `headerMode` into the rewriter; the rewriter only adds the prefix when
lowering a header, and ordinary `.cp`/`.c` sources are unchanged.

Evidence (both positive tests, run by the suite):

| test | what it proves |
| --- | --- |
| `tests/stdlib/multi_tu/` | three TUs; two include `stdlib/io.hp` + `stdlib/error.hp`; links and runs |
| `tests/stdlib/known_gaps/class_linkage/` | two TUs both include the class-bearing `stdlib/string.hp`; links and runs |

Note on ownership: this change touches `compiler/` (`CPlusRewriter`,
`Driver`), which is Agent 1's area.  It is minimal (one flag, prefixing emitted
member signatures), defaults to off, and is covered by the tests above plus the
whole repo suite, but it should be reviewed and integrated by Agent 1 rather
than assumed.

## 6. Vendored and linked upstreams

Only the approved subset is copied into `third_party/`; no upstream checkout is
committed wholesale.  Shallow checkouts live in `../c-plus-library-upstreams/`
and are not part of the build.  For every library the implementation is
compiled **once** as ordinary C and linked; nothing is pasted into a `.hp`.

| library | repo | revision | license | how it is used |
| --- | --- | --- | --- | --- |
| yyjson 0.13.0 | `ibireme/yyjson` | `be9bd74bb3c96e00006c2075c330a6b8b90123dc` | MIT | vendored `yyjson.c/.h` + `LICENSE`, built once |
| stb_image / stb_image_write | `nothings/stb` | `6b5799bae8a1fe3792691ef4b72ae26434c3e799` | public domain / MIT | vendored `stb_image.h`, `stb_image_write.h`, `LICENSE`, `stb_impl.c` |
| miniaudio | `mackron/miniaudio` | `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6` | public domain / MIT-0 | vendored `miniaudio.h` + `LICENSE`, `miniaudio_impl.c` |
| Nuklear | `Immediate-Mode-UI/Nuklear` | `bab18497df988c171fd0d82baad798e1e0e34259` | MIT or public domain | vendored `nuklear.h` + `nuklear_sdl3_renderer.h` + `LICENSE`, `nuklear_impl.c` |
| SDL3 3.4.18 | `libsdl-org/SDL` | `313dd44068a19a42e008fe8fdd3b9cf5596531eb` | zlib | linked via `pkg-config sdl3`, never copied |
| libcurl 8.7.1 | `curl/curl` | `cb870c11c9a71b62780aeb41d9add1a1b34045ea` | curl (MIT-like) | linked via `pkg-config libcurl`, never copied |
| libuv 1.51.0 | `libuv/libuv` | `f08538cca14f73e30c89625e473fea3f5f55e300` | MIT | linked via `pkg-config libuv`, never copied |
| FFmpeg 9.0.2 | `FFmpeg/FFmpeg` | system build | LGPL by default, GPL if `--enable-gpl` | headers + `pkg-config libav*`, never copied |

Reference design reading: **`thradams/cake`** (`2da2afd`), a from-scratch C23
front end with ownership/nullability annotations.  What we took from it:

| cake idea | C+ equivalent here |
| --- | --- |
| `_Owner` pointer annotation | owning *types* (`string`, `file`, `json`, ...) with a `drop` destructor |
| `_View` annotation | borrowed types (`string_view`, `json_view`) and `borrow` factories |
| `_Opt` (nullable) annotation | `valid()` / `is_open()` / `is_valid()` result states |
| `_Out` parameters | `error *err` and `(buf, size)` out-parameters |
| assignment moves ownership, "use after move" error | `cplus_move` + the RAII pass's use-after-move rejection |
| warning on ignoring an `_Owner` result (leak) | **request to Agent 1** (§7.3) |
| flow analysis of object lifetime | the borrow checker subset (documented gap) |
| fast front end, C89 backend | keep `.hp` small; compile upstream C once (§9) |

## 7. Requests to Agent 1 (compiler/runtime), with reproductions

1. **`AstSafetyCheck` false positive on out-parameters.**  A pointer set to
   `NULL` and then written through by an external function
   (`char *end = NULL; strtol(..., &end, ...);`) is still treated as provably
   null, so `*end` is reported.  *Contract:* drop a variable from the
   provably-null set when its address escapes.
2. **Discarded owning results (from cake's `_Owner` rule).**  `json(text, &err);`
   as a statement leaks the parsed document; a warning in the class lowering
   would catch it.  *Contract:* warn when a value of a class with `drop` is
   discarded.
3. **Constructor result is registered for cleanup.**  A lowered constructor
   emits a dead `T_drop(&_cplus_self);` after `return _cplus_self;`.  Harmless
   today (unreachable), fragile if the emission order changes; the ctor result
   should not be registered as a resource local.
4. **Optional: runtime recoverable-error ABI.**  If the runtime grows a
   non-fatal error type, `error` should delegate instead of carrying its own
   buffer.

## 8. Tests and coverage

```sh
tests/stdlib/run_stdlib_tests.sh            # tests + examples + coverage
tests/stdlib/run_stdlib_tests.sh json       # one pattern
python3 tests/stdlib/coverage.py            # per-library API coverage
```

The runner builds the vendored C once, generates the FFmpeg fixture, compiles
each test/example with the real C+ compiler, runs it (dummy SDL/audio drivers)
and diffs stdout against recorded expectations.

## What "coverage" means here (and what it does not)

`tests/stdlib/coverage.py` measures **API surface reachability**: every public
function/method is checked for a call site in a test or example.  It is *not*
execution coverage — a function can be named in a test that never runs that
path.  `tests/stdlib/coverage_exec.sh` is the execution measurement: it
preprocesses a test into one unit, compiles it with `-fprofile-instr-generate
-fcoverage-mapping`, runs it and reports executed lines with `llvm-cov`.
On this host the per-header attribution collapses to the lowered unit (the
`#line` mapping is lost through the driver's preprocess step, and Homebrew's
LLVM tools are unusable here — see LIMITATIONS.md), so the script reports the
lowered-unit number and CI/Linux is where per-header execution coverage is
verified.  Both are reported separately; neither is claimed to be the other.

Coverage is measured from the headers' public API against every test and
example (destructors count as covered when their type is used, because RAII
calls them).  Gate: **>= 80% per library** — current worst is 88.9%.

| library | coverage |
| --- | --- |
| async, audio, error, file, graphics, image, input, io, net, thread, vector, window | 100% |
| gui | 88.9% |
| json | 93.3% |
| string | 95.0% |
| video | 92.0% |

The table above is the older `coverage.py` test-and-example surface measure.
The authoritative deep-test snapshot
(`docs/stdlib/REGRESSION_INVENTORY.md`) is a **static call-site inventory** with
denominators: 21 `stdlib/*.hp` headers, 381 public callable entries, 255 of
381 with a static deep-test call-site reference (234 literal calls + 21
compiler-inserted `drop()` credits) and 126 of 381 without. It is not execution
coverage, branch coverage or a safety proof; function-level runtime attribution
for `.hp` headers is unavailable on this host. No new coverage percentage is
claimed here.

Suite state on this branch:

```
tests/stdlib/run_stdlib_tests.sh   -> 29 passed, 0 failed   (incl. multi-TU; coverage gate green)
tests/run_tests.sh                 -> 109 passed, 1 failed  (pre-existing classic_bug34_longjmp)
tests/stress/cplus_breaker_v3      -> 25 passed, 0 failed
tests/stress/cplus_stress_10       -> 10 passed, 0 failed
tests/cp_breaker.sh                -> 24 passed, 0 failed
```

> **Current status note (2026-11-23).** The figures above are an earlier
> point-in-time snapshot. For current status, §10 records the authoritative
> final results from `tests/stdlib/agent2/FINAL_AUDIT.md` and
> `docs/stdlib/TASK6_FINAL_REPORT.md`: 50 of 50 Agent 2 runner tests PASS,
> 28 deep tests x 3 modes = 84 of 84 invocations PASS, 23 of 23 evidence suites
> PASS, and compiler tests recorded at 150 of 150. The older 109/1 compiler
> snapshot and the `classic_bug34_longjmp` discussion below are historical and
> concern Agent 1's compiler area, not the stdlib contents of this document.

`classic_bug34_longjmp` is a **baseline** failure, verified unchanged here:
`git diff origin/develop...HEAD -- compiler/` is empty (the compiler is
byte-identical to `develop`), and the case file itself is untouched.  The
current behaviour is a warning (`'setjmp' bypasses C+ deterministic cleanup`)
rather than the rejection the `compile_fail` entry expects.  The responsible
workstream is **#12** ("guarantee deterministic cleanup on all exits and reject
unsafe unwinding"), with #1/#5 as parents; it is not a library defect and no
stdlib code is involved.  `classic_bug34_longjmp` is a pre-existing `develop` failure (the compiler warns
instead of rejecting `setjmp`/`longjmp`); it does not involve the library, and
`compiler/` and `runtime/` are untouched on this branch.

## 9. Measurements

Owning containers through the managed allocator (safe mode, `-O0` + UBSan,
arm64 macOS, three runs):

| workload | run 1 | run 2 | run 3 |
| --- | --- | --- | --- |
| `vector` 200k `push_back` + 200k `at` | 14.4 ms | 9.3 ms | 7.4 ms |
| `string` 20k construct + append | 7.1 ms | 5.5 ms | 4.2 ms |

The vector settles at capacity 262 144 for 200 000 elements (geometric growth,
one live buffer) and the checksum is identical across runs.

Compile time: a program that includes the full stdlib vocabulary
(`foundation.cp`, 7 headers) compiles in **0.28 s** against **0.075 s** for a
trivial plain-C file with the same compiler.  The difference is header lowering
plus the sanitizer instrumentation safe mode mandates.

Fair runtime comparison against the hand-written C equivalent
(`tests/stdlib/bench_compare.sh`, identical work, identical flags, best of 3):

| build | workload | plain C | C+ containers | ratio |
| --- | --- | --- | --- | --- |
| safe (`-O0` + sanitizers) | `vector` 200k push + read | 3.1 ms | 7.3 ms | 2.3x |
| safe | `string` 20k construct + append | 1.0 ms | 4.2 ms | 4.0x |
| release (`-O2`) | `vector` 200k push + read | 0.21 ms | 1.33 ms | 6.3x |
| release | `string` 20k construct + append | 0.37 ms | 2.10 ms | 5.6x |

The checksum is identical in every row, so no work is skipped.  The cost is
the managed allocator's per-allocation bookkeeping (double-free detection) plus
the sanitizer checks that release mode keeps on, by design.  **C+ is not faster
than C here, and this document does not claim it is.**  Closing the gap means
changing the allocator or the sanitizer policy, which lives in Agent 1's
runtime - not in the library.

The build recipe does help where it can: vendored libraries are compiled once
(`third_party/*_impl.c`), so no test or application re-lowers yyjson,
miniaudio, stb, Nuklear or SDL3.

## 10. Final hardening status (2026-11-23)

Branch `agent/modern-stdlib`, **PR #17 open**, nothing pushed or merged. Agent 2
has stopped adding new stdlib surface. The user's current priority order is
compiler-enforced safety — P0 (UAF/double-free, CFG ownership, pointer bounds,
alias analysis, cross-function lifetime), P1 (cross-TU analysis, definite
initialization, strict-safe mode, thread-safety), P2 (fuzzing/differential
testing, Linux/macOS/Windows verification, performance vs Clang and Fil-C) —
which is Agent 1 work. See `docs/STATUS.md` and
`docs/libc/AGENT1_PRIORITY_HANDOFF.md` for the full mapping and reproducers.

Recorded results (counts with denominators):

| Area | Count with denominator | Recorded result |
| --- | --- | --- |
| Agent 2 aggregate runner | 50 of 50 tests (28 deep + 11 `agent2/*.cp` + 11 curated existing) | 50 PASS, 0 FAIL/BUILD_FAILURE/TIMEOUT/SKIPPED/BLOCKED |
| Deep stdlib runner | 28 tests x 3 modes = 84 of 84 invocations | 84 passed, 0 failed, 0 skipped |
| Aggregate evidence | 23 of 23 suites | 23 passed, 0 failed |
| Compile-time suite | 52 cases / 8 manifests: 6 of 9 REJECT, 19 of 19 ACCEPT, 24 of 24 RUNTIME | 27 of 52 capability/tagging gaps not counted as passes |
| Sanitizer matrix | 72 builds attempted (9 tests x 8 configurations); 27 of 72 executed | 23 of 27 PASS, 4 of 27 FAIL under `--sanitize=integer`; 45 of 72 UNSUPPORTED |

Inventory and gaps: 21 stdlib headers / 381 callable entries / 255 of 381
statically referenced / 126 of 381 not; 127 audit findings (17 of 127 FIXED,
46 of 127 OPEN, 7 of 127 GAP-COMPILER, 4 of 127 BLOCKED, 53 of 127
DOCUMENTED-CONTRACT); SafeCRT 40 of 437 integrated, 10 of 437 adapter-only,
387 of 437 not integrated; ISO C 27 of 948 MISSING and 49 of 948 UNSUPPORTED;
sanitizers ASan/Leak/Thread/Memory/CFI UNSUPPORTED on the selected backend
(0 of 9 tests ran under ASan).

**UNVERIFIED**: remote Linux CI, SonarCloud, Windows x86 ABI/DLL behaviour and
live-game (injected-DLL) results. Overall verdict remains **PARTIALLY COMPLETE**
— all runnable library tests pass and every fix has a regression, but compiler
gaps, 46 open findings, untested platform modules, unsupported sanitizers and
unverified remote gates remain. Source of truth:
`tests/stdlib/agent2/FINAL_AUDIT.md`, `docs/stdlib/TASK6_FINAL_REPORT.md`,
`docs/stdlib/REGRESSION_INVENTORY.md`, `docs/stdlib/upstream-reuse.md`,
`docs/libc/AUDIT_FINDINGS_TRACKER.md`,
`tests/stdlib/agent2/results/summary.txt` and
`tests/stdlib/agent2/SANITIZER_MATRIX.md`.  The stdlib itself is header-only apart from
the compiled-once upstream units.

## 11. Phase 9 verified synthesis (2026-11-23)

Authoritative consolidated report: [`stdlib/AGENT2_FINAL_REPORT.md`](stdlib/AGENT2_FINAL_REPORT.md).
Branch `agent/modern-stdlib`, HEAD `ab8cb51`; PR #17 open, nothing pushed or
merged. The figures below are recorded results from the cited artifacts, not a
new execution by the synthesis agent.

**Headline counts (with denominators):**

| Area | Result |
| --- | --- |
| Public APIs | **381** callable entries across **21** `stdlib/*.hp` headers; **255 of 381** have a static deep-test call-site (234 literal + 21 implicit `drop()` credits; strict code-only 250 of 381), **126 of 381** without. This is a **static name-reachability inventory, not execution coverage or a safety proof.** |
| ISO C / SafeCRT | 27 of 948 ISO C APIs MISSING, 49 of 948 UNSUPPORTED; SafeCRT 40 of 437 integrated, 10 of 437 adapter-only, 387 of 437 not integrated. |
| Audit findings | 127 total: 17 FIXED, 46 OPEN, 7 GAP-COMPILER, 4 BLOCKED, 53 documented-contract. |
| Runtime tests (primary `--strict --sanitize=none`) | Agent 2 aggregate runner **50 of 50 PASS**, 0 FAIL/BUILD_FAILURE/TIMEOUT/SKIPPED/BLOCKED. Deep runner **28 of 28 per mode**, **84 of 84 invocations** across three modes. Aggregate evidence **23 of 23 suites**. |
| Broader phase-3 audit | **99 discovered/executed: 88 PASS, 1 FAIL, 9 BUILD_FAIL, 1 TIMEOUT, 0 BLOCKED** (runner exit 1) — see `tests/audit_phase3/work/results.tsv`; these 11 non-passes are recorded, not folded into the 50/50 figure. |
| Compile-time / sanitizers | 8 manifests / 52 cases: 6 of 9 REJECT, 19 of 19 ACCEPT, 24 of 24 RUNTIME; 27 capability gaps not counted as passes. Sanitizer matrix: 72 builds attempted, 27 executed, 23 PASS, 4 FAIL (`-fsanitize=integer` in external runtime hashing), 45 UNSUPPORTED; **0 of 9 tests ran under ASan**. |
| Upstream reuse (source-level only) | **9 of 9** integrations have a changed source file + an executed regression (I1 safeclib, I2 mimalloc, I3 Fil-C queries, I4 GSL, I5 yyjson budgets, I6 SaferCPlusPlus, I7 SoftBoundCETS predicates, I8/I9 MS STL). Agent C's landed audit (`docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md`) counts **8 projects with real code** (MS STL vector+string as one; plus GSL, SaferCPlusPlus, Fil-C predicates, SoftBoundCETS predicates, safeclib/Secure-CRT incl. 10 vendor groups, mimalloc, LLVM `memset_explicit`) and **18 of 19 named tests PASS** — the one failure `test_vendor_mem` is a documented security-patch/test-expectation mismatch, not a regression. Doc-only/rejected sources (Cake, jemalloc, TrapC, Fil-C pointer representation, SoftBoundCETS machinery) get **no** reuse credit. |
| ABI | Fingerprint **MATCH** (1162 records, diff 0 bytes, SHA-256 `352b10e8…61027c`); compatibility **69 passed, 0 failed**; layouts **80/80** and **70/70**; C↔C+ interop and precompiled archives verified. **Windows x86 ABI UNVERIFIED.** |
| Benchmarks (real medians) | Driver `ok=15, blocked=5, fail=0`, exit 0. Modern workload (7 interleaved runs): C+ safe 0.0140 s, C+ legacy 0.0061 s, Rust 0.0060 s, Goose 0.0044 s; safe/legacy ratio 2.31×. The 5 blocked rows are **externally blocked builds**, not timings. |

**PENDING sibling artifacts (not landed at synthesis time — verify with the
exact command, never fabricate):**

| Artifact | Status | Exact command |
| --- | --- | --- |
| `docs/stdlib/API_INVENTORY_AGENT2.md` | PENDING | `test -f docs/stdlib/API_INVENTORY_AGENT2.md && echo LANDED || echo PENDING` |
| `docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md` | **LANDED** (Agent C; 8 projects with real code, 18/19 tests pass) | `test -f docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md && echo LANDED || echo PENDING` |
| `tests/defense/memory2/HARDENING.md` | PENDING | `test -f tests/defense/memory2/HARDENING.md && echo LANDED || echo PENDING` |
| `tests/defense/containers2/HARDENING.md` | PENDING | `test -f tests/defense/containers2/HARDENING.md && echo LANDED || echo PENDING` |
| `tests/defense/io_json2/HARDENING.md` | PENDING | `test -f tests/defense/io_json2/HARDENING.md && echo LANDED || echo PENDING` |
| `tests/defense/thread2/HARDENING.md` | PENDING | `test -f tests/defense/thread2/HARDENING.md && echo LANDED || echo PENDING` |
| `tests/defense/fs_net2/HARDENING.md` | PENDING | `test -f tests/defense/fs_net2/HARDENING.md && echo LANDED || echo PENDING` |
| `benchmarks/phase8/RESULTS.md` | PENDING | `test -f benchmarks/phase8/RESULTS.md && echo LANDED || echo PENDING` |

**Blockers / limits:** external `CPLUS_EXIT_ARITHMETIC` runtime/header skew
(Agent 1; see `stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`), ASan and
Leak/Thread/Memory/CFI unsupported on this host, macOS arm64 only (Linux CI,
SonarCloud, Windows x86 and live-game unverified), 46 OPEN findings and 27
compiler gaps. Overall verdict: **PARTIALLY COMPLETE**. This document does
**not** claim 100% stdlib safety and does **not** treat API reachability as
runtime coverage.
