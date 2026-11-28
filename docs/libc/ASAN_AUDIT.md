# AddressSanitizer audit — allocator architecture and host runtime feasibility

Scope: this document is **runtime feasibility only**. It does not change the allocator,
build scripts, tests, or workflows. It records what was actually executed on this host
and the exact results, so a later agent can reproduce or supersede them.

Date: 2026-11-22. Host: macOS arm64 (`arm64-apple-macosx27.0.0`), Apple Command Line
Tools instance (no full Xcode).

## 1. Allocator architecture findings

Both hardened allocator implementations are **thin, side-table-instrumented wrappers
over the host allocator**. There is no arena, no pool, and no suballocation of a large
host block into smaller client blocks.

| API family | Source | Backend host call(s) for the client block | Metadata |
|---|---|---|---|
| `cplus_malloc` / `cplus_calloc` / `cplus_realloc` / `cplus_free` / `cplus_free_checked` | `libc/src/stdlib/alloc.c` | `malloc` (line 225), `calloc(1, total)` (line 261), `realloc` (line 309), `free` (line 366) | Separate open-addressed `meta_slot` table from `calloc(ncap, sizeof(meta_slot))` (line 136), plus a static `g_recent[META_RECENT_CAP]` array; guarded by `g_meta_lock` |
| `malloc_s` / `calloc_s` / `aligned_alloc_s` / `realloc_s` / `free_s` / `free_checked_s` | `stdlib/safe/src/safe_alloc.c` | `malloc` (line 173), `calloc` (line 207), `posix_memalign` (line 232), `realloc` (line 286), `free` (line 342) | Separate open-addressed `slot` table from `calloc(ncap, sizeof(slot))` (line 71), plus a static `g_recent[RECENT_SLOTS]` array; guarded by `g_lock` |

Key consequences for ASan:

- Every client pointer is **exactly a pointer returned by the host allocator**,
  unmodified. The metadata table is a **different** host allocation; client memory
  is never carved out of it, and no header/footer is prepended to client blocks.
- Therefore the shadow memory / redzones / quarantine maintained by the ASan
  interceptors for `malloc`/`calloc`/`realloc`/`free`/`posix_memalign` apply
  directly to these pointers. Confirmed experimentally in §3 experiment 5:
  a one-byte write past a `cplus_malloc(8)` block is reported as a genuine
  `heap-buffer-overflow`, and the report's allocation stack contains the
  interceptor `malloc` frame called from `cplus_malloc`.
- The metadata side-table thinks in terms of logical `byte_size`/`elem_size` /
  `generation` and deliberately implements its own double-free / invalid-free
  refusal semantics. Those are **library-level diagnostics**, not memory-safety
  mechanisms, and they are orthogonal to ASan's view of the heap.

## 2. Why NOT to add `asan_interface.h` poisoning/manual unpoisoning

Do **not** add `__asan_poison_memory_region` / `__asan_unpoison_memory_region`
(or any other `asan_interface.h` calls) to these allocators:

1. **Nothing to unpoison.** Poisoning is required when a library hands out
   pointers into a larger allocation it manages itself (arenas, pools, slab
   suballocation, stack-like rebound objects). Here each allocation is a direct
   host `malloc`/`calloc` call; ASan's interceptor already marks the entire block
   addressable and the surrounding redzones poisoned. Adding manual operations
   would duplicate, and potentially overwrite, the interceptor's own shadow
   state.
2. **It couples hardened libc to a sanitizer build.** The checked allocators are
   compiled into ordinary release libraries (`build/libc/libcplus_hardened.a`,
   `build/safe/libcplus_safecrt.a`, etc.). `asan_interface.h` calls would require
   an ASan runtime at link time and a no-op fallback in non-ASan builds; there is
   no benefit to pay that complexity for.
3. **It would hide real bugs from non-ASan builds.** The metadata table tracks
   logical sizes that can legitimately differ from the host allocation's rounded
   size (e.g. `size == 0` is allocated as 1 byte). Poisoning to the *logical*
   size would make ASan disagree with the real allocation and could manufacture
   false positives; poisoning to the *host* size is exactly what the interceptor
   already does.
4. **Verified sufficiency.** Experiment 5 below shows an out-of-bounds write
   through `cplus_malloc` is detected with no source changes at all.

Conclusion: ASan verification for this codebase requires **no allocator edits**.

## 3. Runtime feasibility — experiments and exact results

Test sources were compiled in a scratch directory outside the repository
(`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/asan-audit/`).
No project source, test, build script, or workflow was modified.

Toolchain facts (measured):

- `clang --version` -> `Homebrew clang version 23.1.6`, `Target: arm64-apple-darwin27.2.0`,
  installed at `/opt/homebrew/Cellar/llvm/23.1.6/bin`. Its default sysroot is broken:
  it looks for `MacOSX27.sdk`, which does not exist.
- `/usr/bin/clang --version` -> `Apple clang version 16.0.0 (clang-1600.0.26.6)`.
- Available SDK: `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk` ->
  `MacOSX15.2.sdk`. No full Xcode (`xcodebuild` unavailable).
- `CPLUS_CLANG` was unset in the audit shell; when the project sets it to
  `/usr/bin/clang`, that selects the non-working Apple clang 16 toolchain (§3.1).

Source for the trivial program (all experiments unless stated otherwise):

```c
int main(void){return 0;}
```

### 3.1 Experiment 1 — `/usr/bin/clang -fsanitize=address` (Apple clang 16)

| # | Exact command | Exit | First lines of output |
|---|---|---|---|
| 1a | `/usr/bin/clang -fsanitize=address trivial.c -o t1a` then `./t1a` | compile 0; run **134** | `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)" (0x0, 0x0) (tid=16239359)`<br>`    <empty stack>` |
| 1b | `/usr/bin/clang -fsanitize=address -shared-libasan trivial.c -o t1b` then `./t1b` | compile 0; run **134** | same CHECK failure at `sanitizer_malloc_mac.inc:189` (`tid=16239425`), `<empty stack>` |
| 1c | `env ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 ./t1a` | **1** | same CHECK failure at `sanitizer_malloc_mac.inc:189` (`tid=16239493`), `<empty stack>` |
| 1d | `env ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 ./t1b` | **1** | same CHECK failure at `sanitizer_malloc_mac.inc:189` (`tid=16239504`), `<empty stack>` |

Result: Apple clang 16's ASan runtime aborts during startup for even a trivial
program. `-shared-libasan` and `ASAN_OPTIONS` do not help. This is a toolchain/
runtime defect, not a property of this project's code.

### 3.2 Experiment 2 — Homebrew clang 23 with an explicit SDK

| # | Exact command | Exit | First lines of output |
|---|---|---|---|
| 2a | `clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk trivial.c -o t2a` then `./t2a` | compile 0; run **0** | *(no output)* |
| 2b | `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk trivial.c -o t2b` then `env SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk ./t2b` | compile 0; run **0** | *(no output)* |
| 2c | `clang -fsanitize=address -shared-libasan -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk trivial.c -o t2c` then `./t2c` | compile 0; run **0** | *(no output)* |
| 2d | `env ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 ./t2a` | **0** | *(no output)* |
| 2e | `clang -fsanitize=address trivial.c -o t2e` (no `-isysroot`) | compile **1** | `clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk' [-Wmissing-sysroot]`<br>`clang: warning: using sysroot for 'macOS 27' but targeting 'arm64-apple-macosx27.0.0' [-Wincompatible-sysroot]`<br>`ld: library 'System' not found`<br>`clang: error: linker command failed with exit code 1 (use -v to see invocation)` |

Result: Homebrew clang 23 links and runs ASan successfully **provided an
existing SDK is supplied with `-isysroot`**. `-shared-libasan` is optional here
(it is already the default on macOS for this toolchain). No `ASAN_OPTIONS`
tuning is needed.

### 3.3 Experiment 3 — installed ASan runtimes

Command: `find /Library/Developer/CommandLineTools /opt/homebrew -name '*asan*' | head -30`

First lines:

```
/Library/Developer/CommandLineTools/usr/lib/clang/16/include/sanitizer/asan_interface.h
/Library/Developer/CommandLineTools/usr/lib/clang/16/include/sanitizer/hwasan_interface.h
/Library/Developer/CommandLineTools/usr/lib/clang/16/lib/darwin/libclang_rt.asan_abi_xrossim.a
/Library/Developer/CommandLineTools/usr/lib/clang/16/lib/darwin/libclang_rt.asan_abi_tvos.a
/Library/Developer/CommandLineTools/usr/lib/clang/16/lib/darwin/libclang_rt.asan_xros_dynamic.dylib
```

Notable full paths (from the untruncated search):

- `/usr/bin/clang` runtime: `/Library/Developer/CommandLineTools/usr/lib/clang/16/lib/darwin/libclang_rt.asan_osx_dynamic.dylib`
  (also a clang 15.0.0 copy, and static `libclang_rt.asan_abi_osx.a`).
- Homebrew clang 23 runtime: `/opt/homebrew/Cellar/llvm/23.1.6/lib/clang/23/lib/darwin/libclang_rt.asan_osx_dynamic.dylib`
  (reached via `/opt/homebrew/opt/llvm/lib/clang/23/lib/darwin/...`).

`otool -L` on the working binaries (2a and 4b) confirms they load
`/opt/homebrew/opt/llvm/lib/clang/23/lib/darwin/libclang_rt.asan_osx_dynamic.dylib`.

So both toolchains have an ASan runtime installed; the Apple clang 16 runtime is
the one that fails at startup.

### 3.4 Experiment 4 — link the project's hardened allocator under ASan

Test program declares and calls `cplus_malloc`, `cplus_calloc`, `cplus_realloc`,
`cplus_free`, `cplus_alloc_live_count`. Archive:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a`
(the archive includes its own `cplus_libc_log`, so no extra project library is
required to link).

| # | Exact command | Exit | First lines of output |
|---|---|---|---|
| 4a | `/usr/bin/clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk alloc_test.c build/libc/libcplus_hardened.a -o a4a` then `./a4a` | compile 0; run **134** | `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)" (0x0, 0x0) (tid=16242721)`<br>`    <empty stack>` |
| 4b | `clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk alloc_test.c build/libc/libcplus_hardened.a -o a4b` then `./a4b` | compile 0; run **0** | `live=2`<br>`ok live=0` |

Note that 4a supplied a valid `-isysroot` too, so the Apple clang 16 failure is
not a missing-SDK problem — it is the same sanitizer runtime startup CHECK
failure seen in experiment 1. 4b shows the real hardened allocator runs
correctly under the Homebrew ASan runtime (live count returns to zero).

### 3.5 Experiment 5 — confirm ASan actually intercepts the project allocator

Program: `char *p = cplus_malloc(8); p[8] = 'X';` linked as in 4b.

```
clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk alloc_overflow.c build/libc/libcplus_hardened.a -o ovf_alloc
./ovf_alloc   # exit 134
```

First lines of the report:

```
=================================================================
==91360==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000958 at pc 0x000100c16794 ...
WRITE of size 1 at 0x602000000958 thread T0
0x602000000958 is located 0 bytes after 8-byte region [0x602000000950,0x602000000958)
allocated by thread T0 here:
    #0 0x0001011e140c in malloc+0x70 (libclang_rt.asan_osx_dynamic.dylib:arm64+0x5540c)
    #1 0x000100c1688c in cplus_malloc+0x60 (ovf_alloc:arm64+0x10000288c)
```

This is the direct evidence for §2: ASan's `malloc` interceptor covers the
client block obtained via `cplus_malloc`, with no manual poisoning.

### 3.6 Feasibility summary

| Configuration | Trivial program | Hardened allocator link | Verdict |
|---|---|---|---|
| Apple clang 16 (`/usr/bin/clang`), default | fails at startup | not attempted (same runtime) | **Broken** — CHECK failure |
| Apple clang 16, `-shared-libasan` | fails at startup | — | **Broken** — CHECK failure |
| Apple clang 16 + `ASAN_OPTIONS` | fails at startup | — | **Broken** — CHECK failure |
| Apple clang 16 + valid `-isysroot` + allocator lib | — | fails at startup | **Broken** — CHECK failure |
| Homebrew clang 23, default (no `-isysroot`) | link failure (missing `MacOSX27.sdk`) | — | **Broken** — bad default sysroot |
| Homebrew clang 23 + `-isysroot <CLT SDK>` | runs, exit 0 | runs, exit 0, real OOB detected | **WORKS** |
| Homebrew clang 23 + `-isysroot` + `-shared-libasan` | runs, exit 0 | — | **WORKS** |
| Homebrew clang 23 + `-isysroot` + `ASAN_OPTIONS` | runs, exit 0 | — | **WORKS** |

## 4. Recommended ASan invocation (this host)

ASan **can** run on this host. The recommended path is Homebrew clang 23 with an
explicit, existing SDK:

```sh
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk

# single-file sanity check
clang -fsanitize=address -g -isysroot "$SDK" trivial.c -o trivial_asan
./trivial_asan                       # exit 0

# link against the project's hardened allocator
clang -fsanitize=address -g -isysroot "$SDK" \
  alloc_test.c build/libc/libcplus_hardened.a -o alloc_test_asan
./alloc_test_asan                    # exit 0, live counts return to 0
```

Notes:

- Use the Homebrew `clang` binary explicitly (do **not** rely on the project's
  `CPLUS_CLANG=/usr/bin/clang` override for ASan runs; Apple clang 16's runtime
  aborts at startup).
- Passing `-isysroot "$SDK"` is mandatory with Homebrew clang 23 here; without
  it the link fails because `MacOSX27.sdk` is absent.
- Add `-shared-libasan` only if you specifically want to force the dynamic
  runtime; it is already the default for this toolchain.
- No `ASAN_OPTIONS` and no `asan_interface.h` changes are required.

If a future toolchain update changes the symptom, re-run experiments 1–5 and
replace this section with the new evidence rather than assuming the old result.

## 5. Cross-platform note

This host-specific workaround is **not** the intended long-term verification
path. The project's CI runs on Linux, where `-fsanitize=address` uses the
compiler-rt runtime tied to the Linux sysroot and does not exhibit the macOS
`asan_init_is_running` CHECK failure or the missing-SDK problem. Linux CI is the
authoritative ASan verification path for allocator changes; the macOS results
above are useful only as a local reproduction and as evidence that the hardened
allocator is ASan-compatible without source changes.
