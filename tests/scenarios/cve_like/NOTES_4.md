# NOTES_4 — safe variants and evidence for cases 09, 11, 12

Scope: `case_09.c` (user-controlled VLA / stack clash), `case_11.c` (arena
`a->offset + bytes` overflow), `case_12.c` (off-by-one heap copy).
Owner: parallel agent 4. No source outside the four owned files was modified;
no library source was rebuilt (the archives under `build/safe` and `build/libc`
were already present and were used as-is).

## Toolchain note (important for reproducing the baseline)

`/usr/bin/clang` (Apple clang 16.0.0) has a **broken ASan runtime in this
environment**: even a 1-line `printf("hello")` program compiled with
`-fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`
aborts with

```
AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)" (0x0, 0x0)
```

(exit 134). `-shared-libasan` does not help. Baseline ASan/UBSan evidence below
therefore uses **Homebrew clang 23.1.6** with the *same* `-isysroot`, which works.
The safe variants themselves were built with the exact requested
`/usr/bin/clang` include/link pattern, and additionally re-checked with the
working Homebrew clang under `-fsanitize=address,undefined`.

## Baseline evidence (original files)

Build: `<asan-cc> -O1 -g -std=c99 -fsanitize=address -isysroot <SDK> case_NN.c -o case_NN`

| Case | Example input (from attack shape) | Exit | First sanitizer error |
|------|-----------------------------------|------|-----------------------|
| 09 | `100000000` (`len`) | **134** | `AddressSanitizer: stack-overflow on address 0x…` in `__asan_alloca_poison` → `main case_09.c:7`; `SUMMARY: AddressSanitizer: stack-overflow case_09.c:7 in main`. The VLA allocation itself blows the stack. |
| 11 | `18446744073709551615` (`SIZE_MAX`, `size`) | **0** under ASan | **No ASan error.** Unsigned `a->offset + bytes` wrap is well-defined, not UB, and this `main` never writes out of bounds (post-wrap offset is always <= 4096). Additional inputs `4032`, `4024`, `4088`, `18446744073709551552`, `2^63` also exit 0 with empty stderr. **UBSan does find real UB:** `runtime error: store to misaligned address 0x… for type 'uint64_t' … requires 8 byte alignment` at `case_11.c:10` — the wrapped offset leaves the counter slot unaligned. |
| 12 | 24 × `A` + `\n` | **0 at -O1**, **134 at -O0** | At `-O1`: no ASan report at all (honest non-detection — the optimizer lowered the 25-byte copy). At `-O0`: `AddressSanitizer: heap-buffer-overflow … WRITE of size 25 … 0 bytes after 24-byte region …`, `SUMMARY: AddressSanitizer: heap-buffer-overflow case_12.c:8 in main`. This is the "heap corruption not detected by ASan" case, and it is optimization-level dependent. |

A control test (`memset` 25 bytes into a 24-byte `malloc`) confirms ASan on this
host *can* detect a 1-byte heap overflow, so the `-O1` non-detection in case 12
is a lowering/interception artifact, not a broken sanitizer.

## SAFE variant results

Build command used verbatim for every variant (as specified), from the repo root:

```sh
/usr/bin/clang -O1 -g -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/scenarios/cve_like/case_12_safe.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/c12s && /tmp/c12s
```

The plain-libc layer is selected by adding `-DPLAIN_LIBC` (the file then
includes `cplus_libc_safe.h` instead of `stdlib/safe/safe_c.h`). Both builds
succeed with `-Wall -Wextra` and produce **no source warnings**.

| Case | Input | Default layer (`safe_c.h`) | Plain libc (`cplus_libc_safe.h`, `-DPLAIN_LIBC`) |
|------|-------|----------------------------|--------------------------------------------------|
| 09 | `100000000` (attack) | exit **2** — `OUTCOME: rejected oversized window (len=100000000, max=1048576)` | exit **2** — same line |
| 09 | `4` (benign) | exit **0** — `res: 3` / `OUTCOME: bounded heap window used safely (len=4)` | exit **0** — same |
| 09 | `0` | exit **2** — rejected | exit **2** — rejected |
| 11 | `18446744073709551615` (attack) | exit **2** — `OUTCOME: request refused by overflow-safe arena guard (size=18446744073709551615)` | exit **2** — same |
| 11 | `8` (benign) | exit **0** — `OUTCOME: bounded counter write completed (value=123)` | exit **0** — same |
| 11 | `63` (odd offset; UBSan UB in original) | exit **0** — bounded write via `memcpy`, no misaligned store | exit **0** — same |
| 12 | 24 × `A` + `\n` (attack) | exit **2** — `OUTCOME: bio refused (len=24 >= 24)` | exit **2** — same |
| 12 | 23 × `A` + `\n` (benign) | exit **0** — `OUTCOME: bounded bio copy completed (len=23)` | exit **0** — same |

Re-checking all three safe variants with Homebrew clang
`-fsanitize=address,undefined` (both attack and benign inputs) produced **zero**
ASan/UBSan diagnostics — in particular the case-11 misaligned-store UB is gone
because the counter is written with `memcpy` and the request that caused the
wrapped offset is refused.

## Caller-side fix vs library enforcement

| Case | Caller-side fix (the primary repair) | Library enforcement (second line of defence) | Classification |
|------|--------------------------------------|-----------------------------------------------|----------------|
| 09 | Remove the user-sized VLA entirely; explicit `MAX_WINDOW = 1<<20` bound; reject `len == 0` and `len > MAX_WINDOW` before allocating; heap `calloc` instead of stack VLA. | `calloc` is mapped to the overflow-checked tracked `calloc_s` / `cplus_calloc`; `free` is mapped to the checked deallocator. A huge `calloc((size_t)-1, 8)` returns `NULL` on both layers. | **Caller-side input validation + code-idiom change.** Stack-clash prevention is a compiler/idiom concern (no user-sized VLAs, `-fstack-clash-protection`, stack probing); libc cannot retroactively make a compiler-emitted VLA safe. The checked allocator only covers the heap window. |
| 11 | Overflow-free guard `if (bytes > a->size || a->offset > a->size - bytes) return NULL;` — no addition, so no wrap. Counter written with `memcpy` (alignment-agnostic). | The checked `memcpy` bounds the trailing write against the tracked arena block's remaining capacity (4096 bytes), including interior pointers. | **Caller-side arithmetic fix**, primarily. The library check is defence-in-depth; it cannot repair an already-wrapped `a->offset`. |
| 12 | Explicit capacity check `if (len >= 24) refuse;` so `len + 1 <= 24` always leaves room for the NUL. | The checked `memcpy` refuses any copy whose destination capacity is insufficient, so no NUL can pass the end even if the caller check is wrong. | **Caller-side off-by-one fix**, primarily; library enforcement independently enforces the 24-byte bound. |

Enforcement probe (both layers, tracked 24-byte heap block):

- `calloc((size_t)-1, 8)` → `NULL` (refused) on safecrt and plain libc.
- 25-byte checked `memcpy` into a 24-byte tracked block → refused on both.
  safecrt logs `[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 25 >
  capacity 24, ESNOSPC 406)` and terminates with `_exit(70)` (violation trap);
  plain libc returns `NULL`. Both are fail-closed, with different failure modes.

## Warnings / honest limitations

1. Linking the prebuilt `build/libc/libcplus_hardened.a` emits `ld: warning:
   object file … was built for newer 'macOS' version (27.0) than being linked
   (15.2)` for several objects. Pre-existing build-artifact issue, unrelated to
   these sources; no compile warnings from the safe variants.
2. Apple clang 16 ASan is unusable here; baseline sanitizer evidence comes from
   Homebrew clang 23 with the same SDK sysroot (documented above).
3. Case 12's baseline result depends on optimization level: ASan reports the
   overflow at `-O0` but not at `-O1`. Both outcomes are recorded rather than
   claiming a single universal result.
4. Case 11's `a->offset + bytes` wrap is **not** UB by itself (unsigned
   arithmetic); the genuine UB found is the misaligned `uint64_t` store that the
   wrap enables. The safe variant removes both.
5. No live-game/UI/native work is involved; all evidence is compile + harness
   execution only.
