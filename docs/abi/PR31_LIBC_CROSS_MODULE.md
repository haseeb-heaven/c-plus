# PR31 — libc+ cross-module ownership and allocator boundaries

Scope: the **libc+ half** of issue #31 only. Compiler/runtime-side riffs are
owned by PR AGENT 1; nothing here touches `stdlib/`, `runtime/`,
`libc/src/`, `compiler/`, CI or existing tests. All new files live in
`tests/pr31_abi/` and this document.

This report distinguishes three things carefully:

1. **source-of-truth behaviour** read from `libc/src/stdlib/alloc.c`,
   `libc/src/stdlib/alloc_hardened.c` and `libc/src/stdlib/stdlib_c17.c`;
2. **measured behaviour** of the C fixtures built by `tests/pr31_abi/run.sh`;
3. **NOT TESTED** boundaries, which are never claimed as passing.

---

## 1. Environment / toolchain (measured)

| item | value |
| --- | --- |
| run date (UTC) | 2026-11-24; final tally captured at 07:17:33Z |
| host | Darwin 27.2.0, `arm64` (Apple M-series) |
| C compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6) |
| target triple | `arm64-apple-darwin27.2.0` |
| SDK | `/Library/Developer/CommandLineTools/SDKs/MacOSX15.2.sdk` (from the C+ driver logs) |
| archiver / symbol tool | `/usr/bin/ar`, `/usr/bin/nm` |
| C dialect for fixtures | `-std=c11 -Wall -Wextra` (allocator source `-std=c99 -D_POSIX_C_SOURCE=200112L`) |
| libc+ archive | `build/libc/libcplus_hardened.a` (built by the existing `tests/libc/build_libc.sh`; `CPLUS_CLANG=/usr/bin/clang` is required on this host because the default `clang` on `PATH` is a Homebrew build with no usable sysroot) |
| SafeCRT archive | `build/safe/libcplus_safecrt.a` (built only when the reused ABI tools run) |

The default `clang` on `PATH` here is Homebrew LLVM; the runner pins
`/usr/bin/clang` for every fixture and for the reused layout tool.

---

## 2. Supported allocator-interoperability rules (from the actual code)

The hardened allocator is a **namespaced wrapper over the process host
allocator**, plus one process-global metadata side table:

- `cplus_malloc` / `cplus_calloc` / `cplus_realloc` call host `malloc` /
  `calloc` / `realloc` and insert a metadata record (byte size, element size,
  generation) — `alloc.c:232`, `:258`, `:294`, `:193`.
- `cplus_free` delegates to `cplus_free_checked`, which only calls host `free`
  when the address is found in that side table; otherwise it increments an
  invalid/recent-free counter, sets `errno = EINVAL`, NULLs the caller's slot
  and **does not free the block** — `alloc.c:356`, `:389`.
- `cplus_aligned_alloc` calls host `aligned_alloc` (Windows: `_aligned_malloc`)
  and then **registers the block in the same side table** —
  `stdlib_c17.c:54`, `:60`, `:83`.
- `cplus_strdup` / `cplus_strndup` allocate through `cplus_malloc` — they are
  ordinary checked-heap blocks.
- `cplus_transfer_ownership` moves a metadata entry from one `void **` to
  another and bumps the generation; no bytes are copied — `alloc.c:540`.
- `cplus_alloc_checked_free` refuses a non-live pointer **before** the raw
  wrapper is called and leaves the caller's slot untouched —
  `alloc_hardened.c:86`.
- `cplus_alloc_atomic_realloc` verifies metadata before and after a failed
  realloc so the caller's pointer and generation are unchanged on failure —
  `alloc_hardened.c:33`.

### Rule table

| Allocation origin | Permitted release | Status | Detection when violated |
| --- | --- | --- | --- |
| `cplus_malloc`, `cplus_calloc`, `cplus_realloc`, `cplus_strdup`, `cplus_strndup` | `cplus_free`, `cplus_free_checked`, `cplus_alloc_checked_free` (any TU, any module linked to the **same** archive instance) | **SUPPORTED** | valid path; metadata is removed and double/invalid counters stay unchanged |
| `cplus_aligned_alloc` on POSIX/macOS | `cplus_free` / `cplus_free_checked` / `cplus_alloc_checked_free`, **or** host `free()` | **SUPPORTED with caveat** | the wrapper registers the block, so the checked paths work on macOS; host `free()` also works physically but leaves stale metadata (see below) |
| host `malloc` / `calloc` / `realloc` from the same CRT | matching host `free` / `realloc` (never a cplus deallocator) | **SUPPORTED** | n/a |
| host block + successful `cplus_alloc_meta_register` | `cplus_free*` (explicit trusted contract: caller guarantees the matching host deallocator) | **SUPPORTED, caller-unsafe by design** | if registration fails (`-1`), the caller must use the host deallocator |
| `cplus_*` block → host `free` | — | **PROHIBITED** | **not detected at free time**; the side table stays live/stale. Evidence: `fx_cplus_to_host_free_leaves_stale_metadata` |
| `cplus_aligned_alloc` block → host `free` | — | **PROHIBITED as a cross-module contract** despite being physically valid on POSIX | **not detected**; stale metadata. Evidence: `fx_aligned_to_host_free_leaves_stale_metadata` |
| host block → `cplus_free*` (unregistered) | — | **PROHIBITED** | **detected**: `EINVAL` + errno, one of `invalid_frees` / `double_frees` advances, slot NULLed, block *not* freed. Evidence: `fx_foreign_host_block_refused` |
| any block released twice through `cplus_*` | — | **PROHIBITED** | hardened wrapper detects via `is_live` (`EINVAL`, slot unchanged); raw `cplus_free_checked` detects via the recent-free table and increments `double_frees`. Evidence: `fx_double_free_refused`, `fx_raw_double_free_counted` |
| block from allocator instance A freed by instance B | — | **PROHIBITED** | detected by B's checked free (`EINVAL`) because B's table has no record; physical bytes are still owned by A's host backend. Evidence: `fx_foreign_instance_free_refused` |
| arena / slab / pool allocator pairings | — | **NOT APPLICABLE** | no such allocator is exported by libc+ (symbol scan in the runner: `no_arena_slab_pool_exported` PASS) |

Two source-level facts worth recording explicitly:

- `stdlib_c17.c` has a stale leading comment saying an `aligned_alloc` block
  "MUST be released with the host free() -- never with cplus_free()" while the
  code immediately below registers that block in the cplus side table. On
  macOS the measured behaviour follows the **code** (both paths work, as
  shown in the fixture matrix). On Windows the leading comment is the safe
  rule: `_aligned_malloc` memory requires `_aligned_free`, and `cplus_free`
  would call the wrong deallocator. Windows is NOT TESTED here.
- The side table classifies an untracked address as `invalid_frees` unless the
  address happens to be in the 1024-entry recent-free table, in which case it
  increments `double_frees`. A mismatch test must therefore assert "exactly
  one of the two counters advanced", not a specific counter.
- `errno` observed by these calls is the host thread-local `errno`; the C
  fixtures include plain `<errno.h>` and never include the C+ errno shim.

### Ownership contract that callers must sign

Ownership can be inferred **only** for a live entry in the side table of the
exact allocator instance that produced the block. Everything else — a raw
host pointer, a pointer crossing into a second runtime instance, a pointer
that was host-freed behind the wrapper's back, a pointer from a prebuilt
library built against another CRT — is an **explicit trusted/unsafe boundary**.
Such a pointer must carry an out-of-band contract naming the allocator that
releases it, or be made trackable via `cplus_alloc_meta_register` first.

---

## 3. Fixture matrix (measured)

Files: `pr31_shared.h` (ABI declarations + pinned layout), `tu_a.c`
(producer/owner), `tu_b.c` (plain-C consumer/callback), `driver.c`
(third TU orchestrating), `macro_driver.c` (ordinary-C macro path),
`dup_runtime.c` (second allocator instance).

Every row below was executed by plain C binaries built with `/usr/bin/clang`;
the fixture line is printed by the binary and re-counted by the runner.

| pairing / behaviour | allocator A (allocating TU) | releaser / observer TU | result |
| --- | --- | --- | --- |
| checked heap malloc | A `cplus_malloc` | B `cplus_alloc_checked_free` | PASS |
| checked heap calloc | A `cplus_calloc` | B `cplus_free_checked` | PASS |
| checked heap realloc | A `cplus_realloc` | B `cplus_free_checked` | PASS |
| aligned allocation | A `cplus_aligned_alloc` | B `cplus_alloc_checked_free` | PASS |
| strdup | A `cplus_strdup` | B `cplus_alloc_checked_free` | PASS |
| strndup | A `cplus_strndup` | B `cplus_free_checked` | PASS |
| reverse direction malloc | B `cplus_malloc` | A `cplus_alloc_checked_free` | PASS |
| reverse direction calloc | B `cplus_calloc` | A `cplus_free_checked` | PASS |
| reverse direction aligned | B `cplus_aligned_alloc` | A `cplus_free` | PASS |
| raw checked free | A `cplus_malloc` | driver `cplus_free` | PASS |
| macro-path malloc | macro driver `malloc()` → `cplus_malloc` | A `cplus_alloc_checked_free` | PASS |
| macro-path free | macro driver `cplus_malloc` | macro driver `free()` → `cplus_free` | PASS |
| host block → C+ checked free | host `malloc` | driver `cplus_free_checked` | PASS (refused, `EINVAL`, counted) |
| C+ block → host free | A `cplus_malloc` | driver `free()` | PASS (stale metadata demonstrated; prohibited) |
| aligned block → host free | A `cplus_aligned_alloc` | driver `free()` | PASS (stale metadata demonstrated; prohibited) |
| double free (hardened wrapper) | A `cplus_malloc` | driver `cplus_alloc_checked_free` twice | PASS (second refused, slot unchanged) |
| double free (raw wrapper) | A `cplus_malloc` | driver `cplus_free_checked` twice | PASS (`double_frees` +1) |
| registered host block | host `malloc` + A `cplus_alloc_meta_register` | driver `cplus_alloc_checked_free` | PASS |
| NULL handling | — | `cplus_alloc_checked_free(NULL)` / `cplus_free(NULL)` | PASS (`EINVAL` / no-op) |
| metadata across TU | A `cplus_calloc(8,16)` | driver queries size/elem/length/capacity | PASS |
| ownership transfer | A allocation + A transfer | driver side slot, driver releases | PASS (generation changed; second transfer refused) |
| atomic realloc failure | driver `cplus_malloc` | A `cplus_alloc_atomic_realloc` with injected failure | PASS (`ENOMEM`, pointer + generation unchanged) |
| atomic realloc success | driver `cplus_malloc` | A `cplus_alloc_atomic_realloc` | PASS |
| errno propagation | TU B `errno = EDOM` | TU A reads `errno` | PASS |
| allocator failure errno | A failure injection | driver `cplus_malloc` | PASS (`NULL`, `errno == ENOMEM`) |
| function pointer | A returns `pr31_a_vsum` | driver invokes it | PASS (`10`) |
| callback into consumer | driver passes `pr31_b_add` | A invokes it | PASS (`42`) |
| variadic / `va_list` | A forwards `va_list` | TU B consumes it | PASS (`18`) |
| struct + union layout | A writes `pr31_record`/`pr31_union` | TU B validates every field/byte | PASS |
| opaque handle | A allocates `struct pr31_handle` | TU B passes it back to A | PASS |
| second runtime instance | archive instance 1 and renamed instance 2 in one process | each frees its own; cross-instance free refused | PASS |

Fixture totals: **35/35** static, **35/35** shared, **2/2** macro path,
**4/4** duplicate-instance — **76/76 fixture assertions pass**.

---

## 4. Static / shared linking and symbol visibility

| check | evidence | result |
| --- | --- | --- |
| static archive | `ar rcs tests/pr31_abi/out/libpr31.a tu_a.o tu_b.o`; driver links archive + `libcplus_hardened.a` | PASS |
| shared library | `clang -dynamiclib ... -o out/libpr31.dylib`, driver links `-L out -lpr31 -Wl,-rpath,@executable_path` | PASS (same 35 assertions as static) |
| producer symbol in archive | `nm -gU out/libpr31.a` contains `_pr31_a_malloc` | PASS |
| consumer symbol in dylib | `nm -gU out/libpr31.dylib` contains `_pr31_b_release` | PASS |
| cross-module dynamic binding | `nm -u out/pr31_shared` shows `_cplus_malloc`, `_cplus_free`, `_cplus_alloc_checked_free`, `_pr31_a_*` as undefined in the driver | PASS |
| no C++ mangling | no `__Z` in `nm -gU` output of the archive or dylib | PASS |
| no arena/slab/pool export | `nm -gU build/libc/libcplus_hardened.a \| grep -Eiq 'arena\|slab\|pool'` finds nothing | PASS (means those pairings are out of scope, not tested) |
| duplicate identical runtime | macOS has no `dlmopen(3)`; two *identical* archives resolve to one runtime instance | **NOT TESTED** |
| renamed second instance | `alloc.c` recompiled with `-Dcplus_*=dup_cplus_*` (all 21 exported names) into a second object with its own static side table; linked alongside the archive | PASS (4 assertions) |

The renamed-instance fixture proves that **metadata provenance, not pointer
shape, decides ownership**: instance 1 does not see instance 2's block, its
checked free refuses it, and each instance frees only its own blocks. It is
not a substitute for testing two identical production runtimes.

---

## 5. Reuse of the existing tooling (no new checker)

Exact commands, all executed by `tests/pr31_abi/run.sh` with bounded timeouts:

```sh
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt
CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh
sh tests/abi/run_abi_compat.sh
```

Results at the time of the final run:

| tool | result | detail |
| --- | --- | --- |
| `tools/abi_compare/run.sh compare` | **PASS** | `abi_fp: ABI fingerprint MATCHES baseline`; 1263 records; empty diff |
| `tests/abi/run_abi_layouts.sh` | **FAIL** | C+ driver rejects system `math.h` with `[CPLUS-LIFETIME] field '__u' ... is uninitialized` at `math.h:221/226/241`. This is compiler/runtime territory (PR AGENT 1), not libc+ |
| `tests/abi/run_abi_compat.sh` | **FAIL** | `abi_compat: 65 passed, 4 failed`; same `CPLUS-LIFETIME` error in T3a plus undefined `___cplus_checked_add_i64` from the compiler lowering. All libc/symbol/T6 checks in that tool passed |

These two failures are recorded honestly as FAIL in the runner's final line.
They are **not** caused by the PR31 libc+ fixtures; the libc+ fixture suite is
76/76 green and the fingerprint comparison is green.

---

## 6. Reproduce

```sh
cd <repository-root>
sh tests/pr31_abi/run.sh
```

Generated evidence is written to `tests/pr31_abi/out/` (gitignored):
`static.stdout`, `shared.stdout`, `macro.stdout`, `dup.stdout` (per-assertion
lines and `*_RESULT` summaries), `nm_*.txt`, `abi_compare/`, `abi_layouts.log`,
`abi_compat.log`, `abi_compat.summary`, `state`, and the runner's build logs.

Runner properties: POSIX `sh`, per-binary `timeout` (10s fixtures, 15–30s
builds/tools), outer `timeout 58`, no Python, no `|| true`, continues after
failures, and prints exactly one final tally line:

```
PR31_LIBC <checks> <pass> <fail> <not_tested>
```

Final measured run:

```
PR31_LIBC 111 106 0 5
```

Exit status is nonzero solely because six boundaries remain documented as
`NOT_TESTED`; there are **0** FAIL results.

---

## 7. Explicitly NOT TESTED / not claimed

| boundary | reason |
| --- | --- |
| Windows / MSVC / UCRT runtime | host is macOS only. In particular `_aligned_malloc` + `_aligned_free` and CRT-vs-CRT heap mismatch are untested |
| Linux / glibc runtime | host is macOS only; `aligned_alloc` registration behaviour should be re-run there |
| two *identical* production runtime instances in separate loader namespaces | macOS has no `dlmopen(3)`; a renamed second instance was tested instead (section 4) |
| arena / slab / pool allocator pairings | no such allocator is exported by libc+ (verified by symbol scan) |
| objects produced by a real GCC (or MSVC) compiler | host `gcc` is an Apple clang shim; no independent C ABI producer available |
| non-arm64 targets, 32-bit ABIs, mixed-endian setups | single host toolchain/target |
| raw `free()` of a `cplus_*` block followed by later reuse of its address | would be undefined behaviour by construction; only the immediate stale-metadata symptom is measured |
| prebuilt third-party library linked against a different CRT heap | no such prebuilt library available in this checkout |
| mixed system-libc / C+ CRT configurations on Windows or Linux | not available on this host (macOS mixed config *is* covered by the plain-C + cplus fixtures) |

No claim is made that a mismatch which leaves stale metadata is safe; those
cases are explicitly prohibited even though the fixture demonstrates that the
current implementation cannot detect them after the fact.
