# Runtime regression coverage for compiler capability gaps

Companion document for `tests/libc/test_compile_gaps_runtime.c`.

## Scope and method

`tests/stdlib/compile_time/*.json` record shapes that `c+ check --strict` and
`c+ build --strict --sanitize=none` **accept** because the compiler cannot prove
the corresponding lifetime/ownership property. Those cases are classified
`RUNTIME` (or `PARTIAL` when the rejection exists but its diagnostic is
untagged). This test does **not** re-run the compiler. It pins the *runtime*
side of the contract with ordinary hardened C APIs, using only **defined**
scenarios:

* allocation and release go through the tracked allocator (`cplus_malloc`,
  `cplus_calloc`, `cplus_free_checked`); stale addresses are saved as aliases
  but are **never dereferenced raw**;
* "use after drop" is attempted through a checked accessor that first probes
  `cplus_alloc_is_live()` and `cplus_alloc_check_generation()`;
* capacity-checked writes on freed/untracked pointers run with
  `cplus_libc_set_unknown_capacity_mode(1)`, so the call is **refused** with
  `errno = EINVAL` instead of being delegated to the host `memset`;
* the file double-close scenario uses `cplus_fclose()`'s closed-address table,
  which rejects a repeat close before any host `fclose()`;
* mutex "guards" are modelled with an `owns`/`valid` flag that is cleared
  before the OS primitive is touched, so no locked or destroyed mutex is ever
  handed to pthreads.

Every check asserts an exact return value, `errno`, or state (including counter
*deltas*). The whole battery runs twice in one process — once with libc logging
OFF and once with logging ON — and a digest of deterministic observables must
match byte-for-byte (logging ON/OFF parity). Output is a single line:

```
compile_gaps_runtime: 210/210 checks ok
```

Failures emit `FINDING:` lines on stdout and a nonzero exit status.

## Gap classes

| ID | Compile-time source | Runtime mitigation asserted by the test |
|---|---|---|
| G1 use-after-drop | `string_use_after_drop`, `vector_use_after_drop`, `collections_use_after_drop`, `file_read_after_close`, `net_use_after_close`, `error_use_after_reset` | `cplus_free_checked()` zeroes the handle; stale alias is not live, `cplus_alloc_get_meta()` returns `EINVAL` with a zeroed metadata record, the checked accessor returns `-1/EINVAL`, strict-mode bounded write returns `EINVAL` |
| G2 double-drop | `string_double_drop`, `net_double_close` | second `cplus_free_checked()` returns `EINVAL`, `errno = EINVAL`, handle becomes `NULL`/inert, `cplus_alloc_double_free_count()` grows by exactly 1; an unowned stack address is refused as an invalid free |
| G3 dangling views / slices / keys | `string_dangling_view`, `vector_invalid_slice`, `vector_const_oob`, `collections_dangling_key` | view carries base + generation; after drop the generation check fails, interior capacity is `(size_t)-1`, the checked read refuses; recycling the address cannot revive the view; an out-of-range checked index returns `-1/ERANGE`; a lookup with a released key is refused while a copied key still reads correctly |
| G4 double-close | `file_double_close`, `file_write_after_close` | first `cplus_fclose()` returns 0; second and third return `EOF` with `errno = EBADF`; `cplus_fclose(NULL)` returns `EOF`/`EINVAL`; a checked file wrapper refuses post-close reads/writes with `EBADF` without touching the `FILE*` |
| G5 mutex guards | `thread_double_unlock`, `thread_unlock_without_ownership`, `thread_guard_after_destroy`, `thread_invalid_mutex_owner` | guard clears `owns` on first unlock, so a second unlock is inert; an unowned/`NULL` guard unlock is a no-op; `cplus_mtx_lock/unlock(NULL)` return `CPLUS_THRD_ERROR`; an invalid `mtx_init` type returns `CPLUS_THRD_ERROR`; a guard over a destroyed/zero-initialised mutex never calls pthreads; a real lock is observable via `cplus_mtx_trylock() == CPLUS_THRD_BUSY` |
| G6 collection aliases / move | `collections_mutable_alias`, `collections_use_after_move`, `vector_mutable_alias`, `vector_use_after_move`, `string_invalid_transfer` | two mutable aliases share one owner; one drop makes both dead; `cplus_transfer_ownership()` bumps the generation, so a pre-move alias is detected **even though the address is still live**; transferring into an occupied destination is refused with `EINVAL` |
| G7 JSON borrowed lifetimes | `docs/stdlib/DEEP_JSON_CONTRACT.md` §2.4 (`json_view` has no owner reference) | the view carries the doc's base + generation; a checked read of an interior (child) value is refused after `drop()`; a re-created doc at a recycled address cannot revive the old view |
| G8 header surface | `cplus_uchar.h` | the `char16_t`/`char32_t` typedefs are exercised; the six conversion entry points are deliberately **not called** because this tree ships no backend for them (see Warnings) |

### Explicit "compiler proof unavailable" notes

For every class above the static proof is **unavailable in the current
compiler**; the runtime mitigation is the only enforcement today:

1. **G1 — compiler proof unavailable.** `drop()`/`close()` on a lightweight
   class handle is lowered to a plain method call; no `[CPLUS-LIFETIME]` or
   `[CPLUS-OWNER]` diagnostic is produced. Only the explicit liveness API
   (`cplus_check_live`) / `.len` is tagged. The runtime proves the dead state,
   not the absence of a source-level misuse.
2. **G2 — compiler proof unavailable.** The `[CPLUS-OWNER] double free`
   analysis covers `cplus_release()`/managed pointers, not a second class
   `drop()` (nor a second `cplus_fclose()`). The runtime proves the *second*
   release is refused; it cannot stop the source from containing it.
3. **G3 — compiler proof unavailable.** No view→owner relationship, no
   pointer-extent analysis inside container methods, and no
   released-pointer-as-argument analysis; a constant container index is not
   diagnosed (a constant subscript on a fixed C array *is*). The runtime
   refuses through generation tokens and capacity checks.
4. **G4 — compiler proof unavailable.** No close-state flow tracking; a
   post-close read/write compiles cleanly. The checked wrapper and the
   `cplus_fclose()` closed-address table make the failure recoverable.
5. **G5 — compiler proof unavailable.** No guard ownership or guard→mutex
   liveness state is modelled by the compiler. The `owns`/`valid` flags in the
   typed C+ wrapper (modelled explicitly here) are the enforcement.
6. **G6 — compiler proof unavailable / PARTIAL.** No alias/exclusivity
   analysis for class owners. Use-after-move *is* genuinely rejected, but with
   an **untagged** diagnostic, so the strict harness counts it as `PARTIAL`,
   not `REJECT`. Generation checks detect the stale alias at run time.
7. **G7 — compiler proof unavailable.** `json_view` is a borrow with no owner
   reference; the compiler does not relate it to its `json` document, so a
   view used after the document is dropped is accepted. Generation-token
   checking is the only guard.
8. **G8 — not a lifetime proof.** The `<uchar.h>` typedefs are available; the
   conversion functions are declarations only (no backend). No proof of
   multibyte conversion safety is claimed.

The test therefore demonstrates *mitigation*, not static enforcement. It does
not change any compiler or runtime code and makes no claim about live-game or
injected-DLL behaviour.

## Verification

Compiler: `/usr/bin/clang` (Apple clang 16, SDK-aware). The default Homebrew
`clang` on this host carries a broken sysroot, so it cannot preprocess any
system header; the repository's integration runner uses the same `/usr/bin/clang`
override.

Libraries already built in `build/libc/libcplus_hardened.a` and
`build/safe/libcplus_safecrt.a`. Include set:
`-I. -Istdlib/safe -Ilibc -Ilibc/install-include` (the test also builds with the
narrower `-Ilibc -Ilibc/include -Ilibc/install-include` set used by
`tests/libc/coverage_libc.sh`, because its `safe_c.h` include is relative to the
source file).

| Configuration | Libraries | Sanitizer | Build | Run |
|---|---|---|---|---|
| both libs | hardened libc + SafeCRT | none | clean (no warnings) | `compile_gaps_runtime: 210/210 checks ok`, exit 0 |
| plain libc only | hardened libc | none | clean (no warnings) | `compile_gaps_runtime: 210/210 checks ok`, exit 0 |
| UBSan | hardened libc + SafeCRT | `-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero` | clean (no warnings) | `compile_gaps_runtime: 210/210 checks ok`, exit 0, no sanitizer diagnostics |
| plain libc + UBSan | hardened libc | same UBSan flags | clean (no warnings) | `compile_gaps_runtime: 210/210 checks ok`, exit 0 |

Reproduce:

```sh
cd <repo-root>
CC=/usr/bin/clang
$CC -O1 -g -std=c99 -Wall -Wextra -pedantic \
    -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
    tests/libc/test_compile_gaps_runtime.c \
    build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
    -lm -pthread -o build/libc/test_compile_gaps_runtime_both
./build/libc/test_compile_gaps_runtime_both
# repeat with only build/libc/libcplus_hardened.a for "plain libc only",
# and with the UBSan flags above for the sanitizer configurations
```

Determinism: the UBSan binary was run 12 times consecutively and produced the
identical `210/210` result every time; no `FINDING:` lines were emitted in any
configuration.

## Warnings / limits

* **Static proof is unavailable for every class above.** The compiler may
  still accept the unsafe source shape; this suite only proves that a caller
  using the checked APIs gets a defined refusal or inert state.
* **Untagged rejections do not count.** Use-after-move is rejected by the
  compiler, but its diagnostic lacks the `[CPLUS-` prefix, so
  `tests/stdlib/compile_time/*.json` records it as `PARTIAL`. No attempt is
  made to reclassify it here.
* **`cplus_uchar.h` conversion functions are declared, not implemented.**
  Calling `cplus_mbrtoc16`/`cplus_c16rtomb`/`cplus_mbrtoc32`/`cplus_c32rtomb`
  produces a link error in this tree; only the typedefs are exercised.
* **Heuristic bounds of the runtime table.** The allocator's double/free
  detection uses a bounded recent-free cache; very old stale aliases,
  host-allocator bypass, and cross-allocator use remain heuristics (see
  `docs/libc/C_BUG_CLASS_MATRIX.md`). The generation token is explicit in this
  C model; in C+ the view/class wrapper must carry it.
* **Toolchain warning.** Use `/usr/bin/clang` (or set `CPLUS_CLANG` to an
  SDK-aware compiler); the shell's default Homebrew clang reports a missing
  `MacOSX27.sdk` sysroot and cannot compile any test in this repository.
* **Scope of ownership.** Only `tests/libc/test_compile_gaps_runtime.c` and
  this document were added. No compiler, runtime, library, header, or harness
  file was modified, and no files were staged or committed.
