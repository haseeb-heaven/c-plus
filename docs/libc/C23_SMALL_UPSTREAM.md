# Small C23 gaps: pinned-upstream import assessment

Scope: the three remaining small C23 interfaces tracked in
`libc/src/c23_small_gaps.c` — `memset_explicit` (`<string.h>`),
`timespec_getres` (`<time.h>`) and `FE_TIESAWAY` (`<fenv.h>`). Question answered
here: can any of them be **closed by copying a pinned upstream source**, using
the method "host first, then verbatim vendored upstream, otherwise report the
exact blocker; no hand-written implementation and no fabricated constant"?

Deliverables: `libc/src/c23_upstream_small.c` and
`tests/libc/test_c23_upstream_small.c`. No other files were changed; these new
files are **not** wired into `libc/CMakeLists.txt` (outside this task's file
ownership). Verification therefore used a direct compile.

## Result table

| API | Status | Pinned source | License | Reason / note |
| --- | --- | --- | --- | --- |
| `memset_explicit` | **CLOSED** | LLVM libc, tag `llvmorg-20.1.1`, `libc/src/string/memset_explicit.cpp` (blob `a3e4dad874397463cb5315679f99a312ac3a53fe`) | Apache-2.0 WITH LLVM-exception | Body copied; only the 5 documented minimal adaptations applied |
| `timespec_getres` | **BLOCKED** | none at either pin | n/a | No upstream source file or build target exists at musl `v1.2.6`/`master` or LLVM libc `llvmorg-20.1.1` (nor on `main`) |
| `FE_TIESAWAY` | **BLOCKED** | none usable | n/a | Compiler-provided macro, absent on this host and absent from LLVM libc's own pinned `fenv-macros.h`; publishing a number would be fabrication |

The host is `/usr/bin/clang` (Apple clang 16.0.0, `clang-1600.0.26.6`),
`arm64-apple-darwin27.2.0`, SDK
`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`. Fresh 2026-11-23 probes
confirm: `memset_explicit` and `timespec_getres` are undeclared, and
`FE_TIESAWAY` is undefined (`<fenv.h>` defines only `FE_TONEAREST` (= 0 here),
`FE_UPWARD`, `FE_DOWNWARD`, `FE_TOWARDZERO`).

## Exact URLs inspected

### musl (MIT — `libc/upstream/musl/COPYRIGHT`, vendored commit `7813e9119eb5fe681b6750b8a033a0490bca0941`)

- Requested `https://git.musl-libc.org/cgit/musl/plain/src/string/memset_explicit.c`
  and `.../plain/src/time/timespec_getres.c` → **404**. The tag is actually
  named `v1.2.6` (the bare `?h=1.2.6` form also 404s); re-checked at the right
  tag below, and the files simply do not exist.
- `https://git.musl-libc.org/cgit/musl/refs/tags` → `v1.2.6` exists (Rich Felker,
  ~8 months before this assessment).
- `https://git.musl-libc.org/cgit/musl/tree/src/string?h=v1.2.6` and
  `https://git.musl-libc.org/cgit/musl/tree/src/string` (master) →
  `explicit_bzero.c` exists, **`memset_explicit.c` does not**.
  `https://git.musl-libc.org/cgit/musl/plain/src/string/explicit_bzero.c` is
  the familiar 8-line `memset(d, 0, n)` + `__asm__ __volatile__` barrier; it is
  *not* `memset_explicit` and adapting it would change the algorithm, so it was
  not copied. The 2024/2025 musl-list proposal adding `memset_explicit.c`
  (<https://openwall.com/lists/musl/2025/06/04/1>) was never merged into either
  pin, and an unmerged mailing-list patch is not pinned upstream source.
- `https://git.musl-libc.org/cgit/musl/tree/src/time?h=v1.2.6` and master →
  `timespec_get.c` exists, **`timespec_getres.c` does not**.
- `https://git.musl-libc.org/cgit/musl/plain/include/fenv.h?h=v1.2.6` and master
  → no `FE_TIESAWAY` (and no rounding-mode constants; those live in per-arch
  `<bits/fenv.h>`).
- The vendored snapshot in this repo has no relevant sources either: no
  `src/time/` directory at all, and `src/string/` contains only `memset.c`,
  not `memset_explicit.c`.

### LLVM libc (Apache-2.0 WITH LLVM-exception)

- `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/src/string/memset_explicit.cpp`
  → found; this is the imported body. Header:
  `https://api.github.com/repos/llvm/llvm-project/contents/libc/src/string/memset_explicit.cpp?ref=llvmorg-20.1.1`.
- Requested `.../llvmorg-20.1.1/libc/src/time/timespec_getres.cpp` → **404**.
  `https://api.github.com/repos/llvm/llvm-project/contents/libc/src/time?ref=llvmorg-20.1.1`
  has `timespec_get.h` but no `timespec_getres.{cpp,h}`. Cross-checked
  `libc/src/time/linux/` (only `timespec_get.cpp`), `libc/src/time/windows/`
  (only `clock_getres.cpp`), and the `libc/src/time/CMakeLists.txt` at both
  `llvmorg-20.1.1` and `main`: no `timespec_getres` target anywhere.
- `https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/include/llvm-libc-macros/fenv-macros.h`
  → defines `FE_DFL_ENV`, exception macros and `FE_DOWNWARD/TONEAREST/
  TOWARDZERO/UPWARD`, but **not `FE_TIESAWAY`**. So even the pinned upstream
  compiler-headers provide no value to import.

## Why `memset_explicit` could be copied

Upstream body (verbatim, LLVM license header included in the new source file):

```cpp
[[gnu::noinline]] LLVM_LIBC_FUNCTION(void *, memset_explicit,
                                     (void *dst, int value, size_t count)) {
  inline_memset(dst, static_cast<uint8_t>(value), count);
  asm("" : : "r"(dst) : "memory");
  return dst;
}
```

Dependency closure check: the file includes `src/string/memset_explicit.h`,
`src/__support/common.h`, `src/__support/macros/config.h` and
`src/string/memory_utils/inline_memset.h`. That last header pulls in
architecture-specific `inline_memset` implementations (x86_64 / aarch64 /
riscv / arm / gpu), `src/string/memory_utils/utils.h` and further
`src/__support/macros/*` headers, plus `LIBC_NAMESPACE_DECL` and
`LLVM_LIBC_FUNCTION`. **The closure is not self-contained.** It is, however,
explicitly permitted by the method for bodies that "use `cplus_*` functions we
already have": `libc/src/string/memory.c` already provides `cplus_memset`.

Minimal adaptations applied to the upstream body (nothing else changed):

1. `memset_explicit` → `cplus_memset_explicit` (required `cplus_*` rename).
2. `inline_memset(dst, static_cast<uint8_t>(value), count)` →
   `cplus_memset(dst, value, count)`. `cplus_memset` performs the identical
   `(unsigned char)` narrowing, so the observable result is unchanged. The
   external call also keeps the store live across the call; the upstream
   compiler barrier is retained below it.
3. `[[gnu::noinline]]` → `__attribute__((noinline))` behind
   `CPLUS_UPSTREAM_NOINLINE`: the original spelling is C++-only and is a C23
   extension in C99/C11; the GNU attribute has identical semantics and compiles
   warning-free under `-std=c99 -Wpedantic`.
4. `asm` → `__asm__`: bare `asm` is not a keyword in C99 and fails to compile.
   Same GCC/Clang extension, same barrier.
5. One opt-in `cplus_libc_log("memset_explicit", "enter")` marker, per the C+
   convention.

The upstream license header, the `// avoid dead store elimination` comments,
the barrier statement and the returned pointer are preserved.

NULL behaviour was **not** invented: with `n > 0`, `cplus_memset_explicit(NULL,
…, n)` reaches the existing `cplus_memset` guard, which returns `NULL` and sets
`errno = EINVAL`. The test asserts that delegated behaviour.

## Exact blockers

- `timespec_getres`: no pinned upstream implementation exists. musl has only
  `timespec_get.c`; LLVM libc has no `timespec_getres` file or CMake target at
  `llvmorg-20.1.1` or on `main`. Deriving one from `timespec_get`/`clock_getres`
  would be writing a new algorithm, which the method forbids. The new source
  publishes no `cplus_timespec_getres` and the test SKIPs with this reason.
- `FE_TIESAWAY`: it is a compiler-provided macro, not a function or a constant
  with a portable value. Neither this host's `<fenv.h>` nor LLVM libc's pinned
  `fenv-macros.h` defines it, and the host AArch64 FPCR rounding-mode field has
  no ties-away encoding. Choosing a number would fabricate host capability.
  The new source defines `CPLUS_C23_UPSTREAM_FE_TIESAWAY` only as a direct alias
  when the host defines `FE_TIESAWAY`; otherwise the capability macro is 0 and
  the test SKIPs.

## Verification (2026-11-23)

Direct compile of the new implementation with its two existing dependencies and
the new standalone test, then run under UndefinedBehaviorSanitizer:

```sh
SYS=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
/usr/bin/clang -isysroot "$SYS" -std=c99 -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=undefined -fno-omit-frame-pointer -g \
  -Ilibc -Ilibc/log \
  libc/src/c23_upstream_small.c libc/log/log.c libc/src/string/memory.c \
  tests/libc/test_c23_upstream_small.c -o /tmp/test_c23_upstream
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/test_c23_upstream
```

Result: **compile exit 0 with zero warnings** (`-Werror`); **run exit 0**, no
UBSan report. Also compiled cleanly at `-std=c11`. Output:

```
SKIP timespec_getres: no pinned upstream source: ...
SKIP FE_TIESAWAY: compiler-provided macro absent from this host <fenv.h> ...
c23_upstream_small: ok
```

Covered: normal fill, `n == 0`, `n == 1`, `uint8_t` narrowing (`0x1FF → 0xFF`),
NULL/EINVAL (delegated from `cplus_memset`), off/on logging parity of return
value, bytes and errno, and the `[CPLUS-LIBC] memset_explicit:` stderr marker;
the shared logger's own enabled/disabled marker parity is always exercised.

## Warnings for the integrating agent

- **Duplicate symbol risk on glibc ≥ 2.38.** `c23_small_gaps.c` defines
  `cplus_memset_explicit` when glibc declares the host symbol; the new file
  defines it unconditionally. Both cannot be linked into one library. On this
  Apple host there is no collision because the old definition is compiled out.
  Integration must choose one provider (recommended: keep the vendored body and
  remove/guard the host-delegating one), or guard the new body with the
  complementary host capability check.
- The new source is **not** in `libc/CMakeLists.txt`; adding it is required for
  the build/CI to see it. Only the three files listed in this task were created
  or modified.
- Verification here is a standalone compile plus UBSan, not the full CMake/CTest
  suite and not a live-game/runtime claim.
- Licensing: the imported body is **Apache-2.0 WITH LLVM-exception**, already a
  dependency category in this repo (LLVM libc excerpts); it is not MIT. No musl
  source was copied into these files.
