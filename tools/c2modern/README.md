# `c2modern` — conservative ordinary-C → modern-C+ source conversion

`tools/c2modern/convert.sh` is a small POSIX-`sh` tool (no Python) that turns an
ordinary `.c` file into a `.cp` file that the C+ driver accepts, while being
explicit about what it does and does not do.

> **Honesty first.** There is no standalone C→C+ rewriter in this repository.
> `./build/c+ build file.c --strict --sanitize=none` already accepts `.c`
> sources because C+ is a superset of C, and `c+ --emit-c` lowers C+ *to* C
> (the opposite direction). This tool therefore does **not** claim to translate
> pointer-heavy C into a provably safe C+ program. It performs a strictly
> additive, behavior-preserving source edit, records the checked-API mapping it
> *recommends*, and verifies the result with the C+ driver.

## Files

| File | Role |
| --- | --- |
| `convert.sh` | The converter (POSIX `sh`, executable). |
| `test_fixture.c` | Small deterministic C program (`memcpy`/`strcpy`/`strcat`/`memset`/`malloc`/`free`/`printf`) used for end-to-end verification. |
| `test_fixture.cp` | Generated converted source (kept as evidence). |
| `test_fixture.mapping` | Generated advisory ordinary→checked mapping report. |

## Exact usage

From the repository root:

```sh
tools/c2modern/convert.sh INPUT.c OUTPUT.cp
```

Example (the shipped fixture):

```sh
timeout 60 tools/c2modern/convert.sh \
  tools/c2modern/test_fixture.c tools/c2modern/test_fixture.cp
```

Arguments:

* `INPUT.c` — an existing ordinary C source file (must end in `.c`).
* `OUTPUT.cp` — the converted source (must end in `.cp`). The mapping report is
  written next to it as `OUTPUT.mapping`.

Environment overrides: `C2MODERN_ROOT` (repository root, default: two levels
above the script), `CPLUS_DRIVER` (default `$C2MODERN_ROOT/build/c+`), `CC`
(only used by the manual plain-clang check below; default `/usr/bin/clang`).

## What the converter actually does

1. **Verbatim copy.** `INPUT.c` is copied byte-for-byte. Every original byte is
   present, unchanged, after the generated preamble.
2. **Additive preamble.** It prepends a small comment block plus
   `#include "stdlib/error.hp"` (skipped if the original already has it). This
   adds the recoverable-error / checked-allocator surface without changing any
   function signature or program behavior.
3. **Advisory `.mapping` report.** It scans the original for a fixed table of
   ordinary calls and writes, for each site, the checked `_s` form and an
   explicit bound expression (`EXPLICIT` when the bound is recovered from a
   local array declaration or `malloc(N)`, otherwise `INFERRED` via
   `CPLUS_SAFE_OBJ_SIZE(first-arg)`). It **does not** rewrite those call sites
   in the `.cp` source.
4. **Verify.** It installs the converted source and runs exactly:

   ```sh
   ./build/c+ build OUTPUT.cp --strict --sanitize=none -I. -o <bin>
   timeout 30 <bin> c2modern argv 1
   ```

   (`-I.` is evaluated from the repository root so `stdlib/error.hp` resolves.)
5. **Report.** It prints input/output paths, original line count, lines added,
   number of mapped call sites, build result, run result, and program output.

## Why the mapping is advisory (the important limitation)

The natural implementation — adding `#include "stdlib/safe/safe_c.h"` and
rewriting `memcpy` → `cplus_memcpy_s_auto(..., bound)` — was investigated and
**cannot** be verified with the required fixed command in this checkout:

* `safe_c.h` includes `cplus_libc.h` and `cplus_safecrt_upstream.h`, which live
  in `libc/install-include`; the fixed command only supplies `-I.`, so the
  header is not even found.
* Adding `-Ilibc/install-include -Istdlib/safe -Ilibc` still fails: including
  `stdlib/error.hp` pulls in `cplus/cplus.h`, whose `cplus_log(...)` macro
  collides with the `cplus_log(double)` declaration in `cplus_libc.h`.
* The `_s` entry points (`malloc_s`, `cplus_memcpy_s_auto`,
  `cplus_strcpy_s_auto`, …) live in `build/libc/libcplus_hardened.a` and
  `build/safe/libcplus_safecrt.a`, which the fixed driver command does not
  link, so rewritten calls would fail at link time.

Consequently the converted source **documents** the `safe_c.h` include inside
an `#if 0` block and the `.mapping` file marks every entry
`ADVISORY-NOT-APPLIED`. Applying those suggestions is a manual, integrator-side
step that must change the build to the hardened CRT
(`-Ilibc/install-include -Istdlib/safe -Ilibc`, the two archives, and resolution
of the `cplus_log` collision). See `docs/ABI_SAFE_LAYER.md` and
`docs/SAFE_CRT.md` for that layer.

## Limits and non-goals

* **Not a full automatic translator.** It does not reason about aliasing,
  lifetimes, data flow, or C+ ownership/borrowing.
* **No fat pointers / no ABI change.** Pointers stay one word; no struct layout,
  public function signature, or exported symbol is changed.
* **Behavior-preserving.** Runtime output of the converted program is
  byte-identical to the original; the only active edit is the `error.hp` include.
* **Conservative/advisory mapping.** The `_s` table is generated, not applied;
  false positives from calls in strings/comments are possible.
* **Bound inference is best-effort.** `EXPLICIT` bounds come from simple local
  declarations; everything else falls back to `CPLUS_SAFE_OBJ_SIZE(...)` at the
  call site. `memcmp` would require a second bound when applied manually.

## End-to-end verification (reproduce, all under `timeout 60`)

```sh
cd <repo-root>

# 1. convert -> build -> run (the tool does all three)
timeout 60 tools/c2modern/convert.sh \
  tools/c2modern/test_fixture.c tools/c2modern/test_fixture.cp

# 2. original still compiles and runs with plain clang (no C+ frontend)
timeout 30 /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  tools/c2modern/test_fixture.c -o /tmp/c2m_clang
timeout 30 /tmp/c2m_clang fixed argv

# 3. original still compiles with the C+ frontend too
timeout 30 ./build/c+ build tools/c2modern/test_fixture.c \
  --strict --sanitize=none -I. -o /tmp/c2m_cplus
```

Observed results on the fixture (this checkout):

* convert: build **PASS (rc=0)**, run under `timeout 30` **PASS (rc=0)**;
  program output:
  ```
  c2modern-ok 32
  CHECKSUM=41
  ```
* original + plain `/usr/bin/clang`: build rc=0, run rc=0, identical output.
* original + C+ driver `--strict --sanitize=none -I.`: rc=0, identical output.
* byte preservation: the `.cp` file after the 18-line preamble is
  byte-identical to `test_fixture.c` (`cmp` verified).
* total converter wall time ≈ 1 s (well inside the 60 s budget).
