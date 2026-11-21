# Modern C+ standard library

Owner: **Agent 2 — modern standard-library engineer** (branch `agent/modern-stdlib`).

Scope of this document: the application-facing library layer under `stdlib/`,
the vendored upstream C libraries under `third_party/`, their tests under
`tests/stdlib/`, and the interfaces this layer needs from the compiler/runtime
owned by Agent 1 (`agent/core-safety`).

The language stays small: syntax is C plus the C+ RAII constructs
(`resource`, `class`, `owned`, `managed`, `move`). The library adds no new
syntax, no new allocator, no exceptions and no `longjmp`.

## 1. What exists now

| header | kind | contents | status |
| --- | --- | --- | --- |
| `stdlib/error.hp` | value | `StdError`, `STDLIB_OK/EOF/FAIL` | done |
| `stdlib/io.hp` | functions | `println`, `echo_int`, `read_line`, `read_int`, `read_long`, `read_float`, `read_double` | done |
| `stdlib/string.hp` | class | `String` (owning), `StringView` (borrowed) | done |
| `stdlib/vector.hp` | class | `IntVec` | done |
| `stdlib/file.hp` | resource | `File` | done |
| `stdlib/thread.hp` | resource | `Mutex`, `Guard` | done |
| `stdlib/net.hp` | resource | `Listener`, `Connection` | done |
| `stdlib/json.hp` | resource | `JsonDoc`, `JsonRef`, `JsonBuilder` over yyjson | done |
| `stdlib/window.hp`, `graphics.hp`, `input.hp`, `audio.hp`, `image.hp`, `http.hp`, `async.hp`, `video.hp` | — | SDL3 / miniaudio / stb / libcurl / libuv / FFmpeg wrappers | **not started** (deliberate: foundation first) |

## 2. Ownership model

Every wrapper says which of these a handle is:

| state | how it is expressed | destructor effect |
| --- | --- | --- |
| **owned** | factory returns it (`File_open`, `Mutex_new`, `Listener_bind`, `JsonDoc_parse`) | releases the resource |
| **borrowed** | explicit constructor (`File_borrow`, `Listener_borrow`) or `StringView`/`JsonRef`/`Guard` | no release |
| **invalid** | failed factory (`handle == NULL`, `fd <= 2`, or a parse error) | no release |
| **moved-from** | after `drop`/`cplus_move`: handle nulled, `fd = -1` | no release |

Rules the library follows:

- Destructors tolerate a zeroed handle.  The compiler's RAII lowering
  zero-initialises a resource local that has no initialiser (`= {0}`), so a
  default-constructed handle must be inert.
- **Descriptors 0/1/2 are never closed by a destructor.**  A zero-initialised
  socket handle would otherwise carry `fd == 0` and close `stdin`; `net.hp`
  therefore closes only `fd > 2`, and only when the handle is not borrowed.
- `File_borrow(stdout)` lets a wrapper write to a standard stream without
  taking ownership of it.
- `String`, `IntVec` and the JSON documents own heap memory and free it
  through `cplus_release` / `yyjson_*_free` in their destructors.  No second
  allocator is introduced: `String`/`IntVec` use `cplus_alloc`, and the JSON
  layer frees exactly what yyjson allocated.
- Borrowed views (`StringView`, `JsonRef`) are documented as valid only while
  their owner lives; they never free anything.

## 3. Recoverable errors

Normal failures — bad input, end of input, a missing file, malformed JSON —
are values, not aborts:

```c
StdError err;
StdError_init(&err);
int n = 0;
if (read_int(&n, &err) != STDLIB_OK) {
    print("input failed:", StdError_message(&err), StdError_code(&err));
}
```

- Status convention: `STDLIB_OK` (0), `STDLIB_EOF` (1), `STDLIB_FAIL` (-1).
- `StdError` carries `failed`, a `cplus_exit_code` and a message.  Reusing the
  runtime's code enum keeps one namespace for libc-level and runtime failures.
- End of input is **not** an error: the readers return `STDLIB_EOF` with the
  error record cleared.
- Conversion is delegated to libc (`strtol`, `strtoll`, `strtof`, `strtod`,
  `errno`, `ERANGE`).  There is no hand-written number parser.
- Fatal safety violations (bounds, double free, null deref) remain
  `cplus_fatal`'s job; the library never converts them into return codes.

`scan()` keeps its fatal-on-failure behaviour for compatibility; the new
`read_*` functions are the recoverable path.

## 4. Multi-translation-unit linkage

`#include "x.hp"` is lowered per translation unit and inlined, so a `.hp`
definition that is emitted with external linkage becomes one definition per
including TU and the program fails to link with duplicate symbols.

**Fixed in the library:** every function defined in a `.hp` is written
`static inline`.  Two TUs that both include `io.hp`/`file.hp`/`thread.hp`/
`error.hp`/`net.hp` now link and run — see `tests/stdlib/multi_tu/`.

**Still open (compiler-side):** members lowered from a `class` are emitted
without internal linkage, so `String`/`IntVec` still duplicate when two TUs
include the same class-bearing header.  A minimal reproduction lives in
`tests/stdlib/known_gaps/class_linkage/` and is asserted by the test runner to
*fail to link* today; if it ever links, that test starts failing on purpose so
the fix gets noticed and this section updated.

Workaround while the gap exists: include a class-bearing header from exactly
one TU per program, or wrap the class in an application-owned `.hp`.

## 5. Runtime / compiler API requests (for Agent 1)

None of these are blocking the current library; each has a reproduction.

1. **Internal linkage for `.hp`-lowered classes.**
   *Need:* lowered class members (ctors, methods, `T_drop`) from a `.hp` are
   emitted `static inline` (or a header/`internal` marker is honoured), so a
   class header can be included from any number of TUs.
   *Repro:* `tests/stdlib/known_gaps/class_linkage/*.cp` — link fails with
   `duplicate symbol '_String_len'` and friends.
   *Contract:* per-TU private copies are acceptable; address identity of the
   functions across TUs is not required.  *Failure semantics:* unchanged.

2. **`AstSafetyCheck` false positive on out-parameters.**
   *Need:* a pointer initialised to `NULL` and then written through by an
   external function (`char *end = NULL; strtol(...,&end,...);`) is currently
   still treated as provably null, so `*end` is reported as a null-pointer
   dereference.
   *Repro:* revert `stdlib/io.hp` readers to `char *end = NULL;` and build
   `tests/stdlib/foundation.cp`; the diagnostic fires at the `*end != '\0'`
   comparison.  The library works around it by initialising `end` to the token.
   *Contract:* the checker should drop a variable from its "provably null" set
   when its address escapes (or when it is passed by address to an unknown
   function).

3. **Optional: a runtime recoverable-error ABI.**
   *Need:* if the runtime grows a non-fatal error/status type, `StdError`
   should delegate to it instead of carrying its own message buffer.
   *Contract:* `code` stays a `cplus_exit_code`; `message` stays borrowed or
   copied, never transferred.  *Failure semantics:* recoverable only.

## 6. Vendored upstreams

Only the approved subset is copied into `third_party/`; no upstream checkout is
committed wholesale.  Shallow checkouts live in
`../c-plus-library-upstreams/` for reference and are not part of the build.

| library | repo | revision | license | vendored here | compiled |
| --- | --- | --- | --- | --- | --- |
| yyjson 0.13.0 | `ibireme/yyjson` | `be9bd74bb3c96e00006c2075c330a6b8b90123dc` | MIT | `third_party/yyjson/{yyjson.c,yyjson.h,LICENSE}` | once, as a C TU (`libyyjson.a`) |
| miniaudio | `mackron/miniaudio` | `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6` | public domain / MIT-0 | not yet | not yet |
| stb | `nothings/stb` | `6b5799bae8a1fe3792691ef4b72ae26434c3e799` | public domain / MIT | not yet | not yet |
| SDL3 | `libsdl-org/SDL` | `313dd44068a19a42e008fe8fdd3b9cf5596531eb` | zlib | not yet | link, do not copy |
| libcurl | `curl/curl` | `cb870c11c9a71b62780aeb41d9add1a1b34045ea` | curl (MIT-like) | not yet | link, do not copy |
| libuv | `libuv/libuv` | `f08538cca14f73e30c89625e473fea3f5f55e300` | MIT | not yet | link, do not copy |
| FFmpeg | `FFmpeg/FFmpeg` | not checked out yet | LGPL/GPL (config-dependent) | never copied | link only |

Attribution: `third_party/yyjson/LICENSE` is retained verbatim; yyjson's
copyright notice stays at the top of its sources.

## 7. Build and test

```sh
# once: the external C implementation
clang -c third_party/yyjson/yyjson.c -o build/ext/yyjson.o -O2 -Ithird_party/yyjson
ar rcs build/ext/libyyjson.a build/ext/yyjson.o

# a program that uses the JSON wrapper
build/c+ build my_app.cp -Ithird_party/yyjson -Lbuild/ext -lyyjson -o my_app
```

```sh
tests/stdlib/run_stdlib_tests.sh          # all stdlib tests
tests/stdlib/run_stdlib_tests.sh json     # one pattern
```

The runner builds `third_party/yyjson` once, then compiles each
`tests/stdlib/*.cp` with the real C+ compiler, runs it, and diffs stdout
against the recorded `.expected` when one exists.

| test | covers |
| --- | --- |
| `foundation.cp` | String (append/eq/compare/find/substring/split/view/capacity), IntVec (push/get/pop/clear), File (open/write/flush/read_line/borrow/missing), Mutex/Guard, all five readers |
| `negative.cp` | overflow-checked `reserve`, out-of-range `get`/`pop`/`field`/`substring`, missing file, null-handle write, null mutex |
| `eof.cp` | `read_int`/`read_line`/`read_double` on an exhausted stream report `STDLIB_EOF`, not a failure |
| `json_test.cp` | parse, borrowed reads, array access, missing key, file parse, serialise, builder, malformed JSON error |
| `multi_tu/` | one program from three TUs sharing `io.hp`/`file.hp`/`thread.hp` |
| `known_gaps/class_linkage/` | asserts the class-linkage gap still reproduces |

Evidence (this branch, worktree `../c-plus-stdlib`):

```text
sh tests/stdlib/run_stdlib_tests.sh   -> 6 passed, 0 failed
sh tests/run_tests.sh                 -> 109 passed, 1 failed (pre-existing)
```

The one existing-suite failure is `classic_bug34_longjmp`, a *pre-existing*
baseline failure on `develop`: the compiler now warns instead of rejecting
`setjmp`/`longjmp`, while the case still lives in `tests/compile_fail/`.  It
does not involve the standard library and is outside this branch's edit scope
(`tests/compile_fail/` belongs to the core compiler work).

## 8. Allocation measurements

`tests/stdlib/bench_stdlib.cp` exercises the two owning containers through the
C+ managed allocator (`c+` safe mode, `-O0` with UBSan, arm64 macOS, three
runs; lower is better):

| workload | run 1 | run 2 | run 3 |
| --- | --- | --- | --- |
| `IntVec` 200 000 pushes + 200 000 `at` reads | 17.0 ms | 11.0 ms | 8.2 ms |
| `String` 20 000 construct + append (12 bytes) | 7.7 ms | 5.6 ms | 4.3 ms |

`IntVec` settles at capacity 262 144 for 200 000 elements (geometric growth,
one live buffer), and the checksum is identical across runs, so no allocation
is lost or double-counted.  These numbers include the sanitizer instrumentation
that safe mode mandates; they are a regression baseline, not a claim about
optimised C.

## 9. Next phases

Order is deliberate — multimedia integration waits until the foundations are
green:

1. JSON (done), then filesystem/time/collections.
2. SDL3 window/graphics/input, miniaudio, STB image.
3. libcurl HTTP, libuv async, FFmpeg video.

Every upstream wrapper must follow the same contract as `json.hp`:
RAII for owned handles, borrowed types for views, `StdError` for recoverable
failure, `static inline` for header definitions, a compiled-once C unit for the
upstream implementation, and a test that exercises ownership, failure and
either multi-TU or API compatibility.
