# ABI preservation evidence — allocator work

Scope: **host-only evidence** collected on this machine. All commands below ran
in the repository root `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on
branch `agent/modern-stdlib` at `HEAD = cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a`
("Merge remote-tracking branch 'origin/agent/modern-stdlib' into agent/modern-stdlib").

**No Windows or Linux ABI verification was performed or is claimed.** Every
result below is for the local Darwin/arm64 host only.

## 1. Environment (exact)

| Item | Value |
| --- | --- |
| `uname -m` / `uname -s` | `arm64` / `Darwin` |
| `CPLUS_CLANG` (layouts run) | `/usr/bin/clang` |
| `/usr/bin/clang --version` | `Apple clang version 16.0.0 (clang-1600.0.26.6)`, `Target: arm64-apple-darwin27.2.0`, `Thread model: posix`, `InstalledDir: /Library/Developer/CommandLineTools/usr/bin` |
| `./build/c+ --version` | `C+ 0.1.0`, `Language: C+ (C with memory safety, deterministic RAII, lightweight classes)`, `Backend: host Clang/LLVM` |
| `PATH` inside `run_abi_compat.sh` | `/usr/bin:/bin` (script pins it) |
| Compat run timestamp (`results.log`) | `date_utc=2026-11-23T03:56:52Z` |

## 2. Required suites — exact commands and results

### 2.1 Public-type layout agreement (host C vs C+ driver)

Command:

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh
```

Observed stdout/stderr (verbatim):

```
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```

Exit code: **RC=0**. The two `linker input unused` warnings are the known, benign
artifacts of the driver's compile step receiving archive paths (the link step
consumes them); they are not failures. `build/abi/host.out` and
`build/abi/cplus.out` both end with `abi_layouts: 70/70 layouts match`, and the
script's `diff -u` produced no differences.

### 2.2 Comprehensive ABI-compatibility suite

Command:

```sh
sh tests/abi/run_abi_compat.sh
```

Final line: **`abi_compat: 69 passed, 0 failed`**, exit code **RC=0**.

All step labels printed `PASS`: `build_libc`, `build_safecrt`,
`T1_pointers_{compile,run,summary}`, `T2_layouts_{compile,run,summary}`,
`T3a_{cplus_lib_compile,cplus_lib_archive,c_driver_compile,c_driver_run,c_driver_summary}`,
`T3b_{c_part_compile,c_part_archive,direct_link_probe,cplus_caller_build,cplus_caller_run,cplus_caller_summary}`,
all `T5_libc_symbols:*`, `T5_safecrt_symbols:*`, `T5_compat_symbols:*`,
`T5_no_cxx_mangling:{nm_libc.txt,nm_safecrt.txt,nm_compat.txt}`, and
`T6_neutral_{plain_compile,cplus_compile,plain_run,cplus_run,diff,line_count}`.

Allocator-relevant symbol checks that passed include `cplus_malloc`,
`cplus_calloc`, `cplus_realloc`, `cplus_free`, `cplus_free_checked`,
`cplus_alloc_get_meta`, `cplus_alloc_remaining_capacity`,
`cplus_alloc_is_live`, `cplus_alloc_size`, `cplus_alloc_length`,
`cplus_alloc_query_length`, `cplus_alloc_live_count`,
`cplus_transfer_ownership`, plus Safe-CRT `cplus_safe_alloc_get_meta`,
`cplus_memcpy_s`, `cplus_strcpy_s`, and `cplus_sc_memcpy_s`. No ` T __Z`
(C++-mangled) exports were found in any audited archive.

Full command/exit-code log: `build/abi/compat/results.log` (ends with
`RESULT: abi_compat: 69 passed, 0 failed`).

### 2.3 Task 4 fingerprint tool (present as source, runner not yet present)

The Task 4 sources `tools/abi_compare/abi_fp_probe.c` and
`tools/abi_compare/fp_to_json.c` are present (untracked, still in progress).
No `run.sh` and no committed baseline fingerprint were present at the time of
this run, so no baseline-vs-current diff could be performed; the fingerprint
below is a **current-state capture only**.

The probe's documented build (`-Werror`) fails on a defect in the probe source
itself (nested `/*` inside a block comment at `tools/abi_compare/abi_fp_probe.c:17`):

```sh
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror \
  -Ilibc/install-include -Istdlib/safe \
  tools/abi_compare/abi_fp_probe.c -o /tmp/.../abi_fp_probe
# error: '/*' within block comment [-Werror,-Wcomment] (2 errors), COMPILE_RC=1
```

To obtain the fingerprint without editing another agent's file, the same probe
was rebuilt without `-Werror`, writing only to the pre-approved temp dir
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode`:

```sh
/usr/bin/clang -O2 -std=c11 -Wall -Wextra \
  -Ilibc/install-include -Istdlib/safe \
  tools/abi_compare/abi_fp_probe.c -o "$TMP/abi_fp_probe"   # RC=0, 2 -Wcomment warnings
"$TMP/abi_fp_probe" > "$TMP/abi_fp.txt"                     # RC=0, 101 records
shasum -a 256 "$TMP/abi_fp.txt"
# b9aff0a0a709fa5d2cfdeb28441ea3b92d0b35fe53e80821836a15863acc6d4f
```

The probe's compile-time `_Static_assert(__builtin_types_compatible_p(...))`
signature checks all passed (no assertion failures). Allocator-relevant records
from the canonical fingerprint:

```
L	size cplus_alloc_meta	40
L	align cplus_alloc_meta	8
L	offset cplus_alloc_meta.byte_size	0
L	offset cplus_alloc_meta.elem_size	8
L	offset cplus_alloc_meta.elem_count	16
L	offset cplus_alloc_meta.generation	24
L	offset cplus_alloc_meta.is_live	32
L	size cplus_safe_alloc_meta	40
L	align cplus_safe_alloc_meta	8
L	offset cplus_safe_alloc_meta.byte_size	0
L	offset cplus_safe_alloc_meta.elem_size	8
L	offset cplus_safe_alloc_meta.elem_count	16
L	offset cplus_safe_alloc_meta.generation	24
L	offset cplus_safe_alloc_meta.is_live	32
F	signature cplus_malloc	void *(size_t)
F	signature cplus_free	void(void *)
F	signature cplus_alloc_size	size_t(const void *)
F	signature cplus_alloc_get_meta	int(const void *, cplus_alloc_meta *)
F	signature cplus_alloc_remaining_capacity	size_t(const void *)
```

`fp_to_json.c` also compiled (`-O2 -std=c11 -Wall -Wextra`, RC=0) and converted
the 101 records to `{"format":"abi_fp_1",...}` (RC=0). Tooling limitation noted
for the handoff: the documented `-Werror` build of the probe is currently
blocked by the source comment defect above; this is a Task 4 source issue, not a
failure of either required ABI suite.

## 3. Header / struct data-member inspection

Commands used for the inspection (read-only):

```sh
git status --short
git diff --name-status -- '*.h' '*.hp' 'libc/**' 'stdlib/**'
git diff main...HEAD --name-status -- 'stdlib/*.hp' 'stdlib/safe/*.h'
git diff main...HEAD -- stdlib/memory.hp stdlib/safe/safe_alloc.h stdlib/error.hp
git diff main...HEAD -- 'libc/install-include/*.h' 'libc/include/*.h'
git show 9a061ae -- 'stdlib/*.hp' 'stdlib/safe/*.h' 'libc/install-include/*.h'
git show --stat --oneline 1df837f
grep -n -E 'struct|typedef' stdlib/memory.hp stdlib/safe/safe_alloc.h
```

Findings:

1. **No working-tree header diff.** `git diff` (unstaged, tracked files) was
   empty for all `*.h`/`*.hp` files at inspection time; only unrelated untracked
   `scratch/*.cp` files existed. There are therefore no uncommitted struct
   data-member edits to audit.
2. **No `allocator.hp` exists.** `find . -iname '*allocat*'` and
   `git log --all --oneline -- '**/allocator.hp'` found no such file in the tree
   or its history. The allocator work lives in `stdlib/memory.hp`,
   `stdlib/error.hp`, `stdlib/safe/safe_alloc.h`, and
   `libc/install-include/cplus_libc.h`; these are the files inspected.
3. **All allocator-related headers are new, additive files relative to `main`.**
   `git diff main...HEAD --name-status` reports status `A` for every
   `stdlib/*.hp` and `stdlib/safe/*.h` (including `memory.hp`, `error.hp`,
   `safe_alloc.h`) and for all changed `libc/install-include/*.h`. No
   pre-existing header had a struct member added, removed, reordered, or
   retyped on this branch.
4. **`stdlib/memory.hp` (new, 254 lines) declares no structs at all** — grep for
   `struct|typedef` returns no matches. It contains only `static inline`
   bounds/range/generation helpers (`stdlib_mem_capacity`, `stdlib_mem_count`,
   `stdlib_mem_token`, `stdlib_mem_check`, `stdlib_mem_check_range`,
   `stdlib_mem_copy/move/set/borrow`, etc.) operating on raw pointers plus
   explicit offsets; its own header comment states pointers are not redefined
   and no fat pointer or side table is introduced.
5. **New metadata structs are additive, not edits to existing layouts.**
   `cplus_safe_alloc_meta` (new in `stdlib/safe/safe_alloc.h`),
   `cplus_alloc_meta` (new in `libc/install-include/cplus_libc.h`), and
   `stdlib_telemetry_stats` / `stdlib_meta_slot` / `stdlib_state_t` (new in
   `stdlib/error.hp`) are type definitions introduced with the new headers.
   Each has stable members (`byte_size`, `elem_size`, `elem_count`, `generation`,
   `is_live` for the allocator metas). No previously shipped struct layout was
   modified.
6. **Commit-level confirmation.** `1df837f` ("ABI-preserving runtime safety…")
   *added* `stdlib/memory.hp` and `stdlib/error.hp` (+42 lines to `error.hp`);
   `9a061ae` ("…fix aligned allocation tracking") changed
   `libc/install-include/cplus_libc.h` only by adding three `#include`s and
   `cplus_nextup/nextdown*` declarations, and changed `libc/src/stdlib/alloc.c`
   (aligned-allocation registration in the side table) — no struct data-member
   edits in either commit.

## 4. Verdict

**NO_ABI_IMPACT.** Evidence:

- Both required host suites pass with zero failures: 70/70 layouts match and
  69 passed / 0 failed, exit code 0 on both.
- The public C-compatible struct layouts measured by the fingerprint tool are
  consistent across the two allocator metadata types (`size 40`, `align 8`,
  member offsets `0/8/16/24/32`) and all probed function signatures pass
  compile-time compatibility assertions.
- The header inspection found no struct data-member additions, removals,
  reorderings, or type changes to any pre-existing public struct; all
  allocator headers are new additive files, and `stdlib/memory.hp` contains no
  struct definitions.

Caveats (honest limitations):

- Evidence is Darwin `arm64` with Apple clang 16.0.0 only. Nothing here
  establishes Windows or Linux ABI behavior.
- The Task 4 fingerprint tool has no `run.sh` and no committed baseline in the
  tree yet, so its output is a current-state fingerprint (SHA-256 recorded
  above), not a baseline-to-current no-drift proof. Its documented `-Werror`
  build is blocked by a `-Wcomment` defect in the probe source; the tool ran
  successfully with `-Werror` removed. This is an in-progress tooling issue and
  does not affect the two required suites, which both passed.
- Newly introduced metadata structs are ABI-additive: consumers must compile
  against the new headers to use them; the audit only shows that no existing
  layout was changed.

Owner note: this document is the only file created/modified for this task; no
source, compiler, test, or git-staging changes were made.
