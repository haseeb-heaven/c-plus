# Modern C+ standard library

Owner: Agent 2 (branch `agent/modern-stdlib`).  Companion to
[`STDLIB.md`](STDLIB.md), which is the API reference.

Scope: `stdlib/`, the vendored upstream C libraries in `third_party/`, their
tests and examples in `tests/stdlib/`, and the interfaces this layer needs from
the compiler/runtime owned by Agent 1 (`agent/core-safety`).

Design order, as instructed: **safety, memory safety and RAII first; features
second; speed measured, not assumed.**  The language stays small: C plus the C+
RAII constructs.  No exceptions, no `longjmp`, no mandatory GC, no new syntax.

### One library, three areas

The deliverable is a single optional modern C+ library: **core** (`string`,
`string_view`, `vector`, `slice`, `error`, `file`, `fs`, `time`, `log`,
`mutex`/`guard`, `listener`/`connection`), **adapters** (JSON, HTTP, async,
GUI/window/graphics/input, audio, image, video) and **the tooling around them**
(tests, coverage gate, benchmarks).  There is deliberately **no separate
"Safe CRT" product**: improving raw `malloc`/`strcpy` safety is the compiler's
job (Agent 1, #19/#16), and platform `strcpy_s` stays a platform API.  See the
scope note in PR #17.

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

Suite state on this branch:

```
tests/stdlib/run_stdlib_tests.sh   -> 29 passed, 0 failed   (incl. multi-TU; coverage gate green)
tests/run_tests.sh                 -> 109 passed, 1 failed  (pre-existing classic_bug34_longjmp)
tests/stress/cplus_breaker_v3      -> 25 passed, 0 failed
tests/stress/cplus_stress_10       -> 10 passed, 0 failed
tests/cp_breaker.sh                -> 24 passed, 0 failed
```

`classic_bug34_longjmp` is a pre-existing `develop` failure (the compiler warns
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
miniaudio, stb, Nuklear or SDL3.  The stdlib itself is header-only apart from
the compiled-once upstream units.
