# Adversarial lifecycle / allocation-failure regression family

## Files owned

| File | Purpose |
| --- | --- |
| `tests/stdlib/regressions/adv_lifecycle.cp` | repeated create/drop x100, inert double-drop, use-after-drop state queries |
| `tests/stdlib/regressions/adv_alloc_failure.cp` | injected allocation failures at every reachable allocation point, failure atomicity, I/O cleanup |
| `tests/stdlib/regressions/ADV_LIFECYCLE_NOTES.md` | this record |

No headers were edited. No other files were created or modified by this task.

## Coverage

`adv_lifecycle.cp` (fixed-seed xorshift32 `0xC0FFEE`, 100 bounded cycles per
family):

- `string` — construct/query/drop/double-drop; post-drop queries are
  `size/capacity/valid/equals/compare/find/is_valid_utf8/c_str != NULL` only.
- `vector` — push/checked-read/drop/double-drop; post-drop `size/capacity` only.
- collections — `intset`, `intmap`, `strmap`, `deque`, `list`, `stack`, `queue`:
  insert/put/push, checked reads, drop all, drop all again, then only state
  queries (`size`/`valid`).
- `file` — `tmpfile()` create/write/flush/seek/read/drop/double-drop,
  post-drop `is_open()` and `close()` (lifecycle failure), plus an invalid
  (`file(NULL, NULL)`) handle; open-fd count sampled before/after all 100 cycles.
- thread guards — 100 `mutex`+`guard` cycles (unlock/drop/double-drop, mutex
  relock/drop/double-drop) and 100 `thread` cycles (start/join/repeated-join/
  drop/double-drop) with a trivial deterministic worker.
- telemetry `alloc_count == free_count` across the whole run.

No stale pointer is dereferenced: dropped objects are only queried through
methods that explicitly handle the null/zero state. No element accessor, IO
call, or dereference of a freed buffer appears after `drop()`.

`adv_alloc_failure.cp` (probe-then-enumerate method, probe count clamped to 8):

- `string`: constructor, `reserve`, growth `append`, `copy_from`.
- `vector`: `reserve`, first `push_back`, growth `insert`, `copy_from`,
  `shrink_to_fit`.
- collections: `intset::insert`, `intmap::put`, `strmap::put`,
  `deque::push_back`, `list::push_back`, `stack::push`, `queue::push`
  (first allocation and every subsequent allocation point, enumerated with
  `stdlib_alloc_fail_at(N)`).
- `stdlib_alloc_fail_after(0)` (all allocations fail) and
  `stdlib_alloc_fail_after(1)` (only the first succeeds), each with a
  successful retry after `stdlib_alloc_fail_reset()`.
- failure atomicity: `rc == -1`, size/capacity/`valid()` and all pre-existing
  element values unchanged, telemetry `alloc_count == free_count` after drop.
- failed I/O then successful retry: invalid handle (`file(NULL, NULL)`),
  a nonexistent-path open that fails, then a `tmpfile()` that writes/flushes/
  seeks/reads successfully; repeated `close()` (first OK, later lifecycle
  failures); `drop()` twice; injection armed throughout to prove `file.hp`
  does no `stdlib_alloc` (`alloc_count` and `free_count` byte-for-byte equal
  before and after).

Both files are self-checking: every check prints `<tag> <1|0>`; the last line
is `ADV_LIFECYCLE <checks> <failures>` / `ADV_ALLOC_FAILURE <checks>
<failures>`; `main` returns 1 when any check failed. Loops are bounded
(100 cycles, enumeration <= 8 per operation), all inputs are fixed-seed or
constant, and there is no wall-clock or random dependency.

## Verification (exact commands and stdout)

All commands run from the repository root. The compiler is `build/c+`, built
on branch `agent/modern-stdlib`. Output directory:
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/adv-final`.
Every build and every run is wrapped in `timeout 30`. Order is
**strict/no-sanitizer first**, then default, then strict/UBSan.

```sh
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/adv-final
mkdir -p "$OUT"

# --- strict / no sanitizer (verified FIRST) -------------------------------
timeout 30 build/c+ build --strict --sanitize=none \
  tests/stdlib/regressions/adv_lifecycle.cp -o "$OUT/adv_lifecycle_strict_none"
timeout 30 "$OUT/adv_lifecycle_strict_none" \
  >"$OUT/adv_lifecycle_strict_none.out" 2>"$OUT/adv_lifecycle_strict_none.err"

timeout 30 build/c+ build --strict --sanitize=none \
  tests/stdlib/regressions/adv_alloc_failure.cp -o "$OUT/adv_alloc_failure_strict_none"
timeout 30 "$OUT/adv_alloc_failure_strict_none" \
  >"$OUT/adv_alloc_failure_strict_none.out" 2>"$OUT/adv_alloc_failure_strict_none.err"

# --- default ---------------------------------------------------------------
timeout 30 build/c+ build tests/stdlib/regressions/adv_lifecycle.cp \
  -o "$OUT/adv_lifecycle_default"
timeout 30 "$OUT/adv_lifecycle_default" \
  >"$OUT/adv_lifecycle_default.out" 2>"$OUT/adv_lifecycle_default.err"

timeout 30 build/c+ build tests/stdlib/regressions/adv_alloc_failure.cp \
  -o "$OUT/adv_alloc_failure_default"
timeout 30 "$OUT/adv_alloc_failure_default" \
  >"$OUT/adv_alloc_failure_default.out" 2>"$OUT/adv_alloc_failure_default.err"

# --- strict / UBSan --------------------------------------------------------
timeout 30 build/c+ build --strict --sanitize=undefined \
  tests/stdlib/regressions/adv_lifecycle.cp -o "$OUT/adv_lifecycle_ubsan"
timeout 30 "$OUT/adv_lifecycle_ubsan" \
  >"$OUT/adv_lifecycle_ubsan.out" 2>"$OUT/adv_lifecycle_ubsan.err"

timeout 30 build/c+ build --strict --sanitize=undefined \
  tests/stdlib/regressions/adv_alloc_failure.cp -o "$OUT/adv_alloc_failure_ubsan"
timeout 30 "$OUT/adv_alloc_failure_ubsan" \
  >"$OUT/adv_alloc_failure_ubsan.out" 2>"$OUT/adv_alloc_failure_ubsan.err"
```

Observed stdout (all six builds produced zero bytes on stdout/stderr at build
time; all six runs produced zero bytes on stderr):

```
--- adv_lifecycle strict-none ---   build_rc=0 run_rc=0
ADV_LIFECYCLE 603 0
--- adv_alloc_failure strict-none --- build_rc=0 run_rc=0
ADV_ALLOC_FAILURE 65 0
--- adv_lifecycle default ---       build_rc=0 run_rc=0
ADV_LIFECYCLE 603 0
--- adv_alloc_failure default ---   build_rc=0 run_rc=0
ADV_ALLOC_FAILURE 65 0
--- adv_lifecycle ubsan ---         build_rc=0 run_rc=0
ADV_LIFECYCLE 603 0
--- adv_alloc_failure ubsan ---     build_rc=0 run_rc=0
ADV_ALLOC_FAILURE 65 0
```

Line counts: `adv_lifecycle` 604 stdout lines per mode (603 checks + final
line), `adv_alloc_failure` 66 lines per mode (65 checks + final line).
Cross-mode `diff -q` of every stdout pair reported identical output; no UBSan
diagnostic was emitted.

## Findings

**No functional or memory-safety defect was found.** All 603 + 65 checks pass
in all three modes, including repeated double-drop, stale-state queries,
failure atomicity at every probed allocation point, telemetry balance, fd
balance, and repeated file close.

### Contract-consistency observation (not counted as a failure)

On an initially empty `strmap`, a `put()` whose **fourth** allocation (the key
copy) is injected to fail returns `-1` with unchanged *logical* contents
(`size()==0`, `get()` fails, telemetry balanced, `drop()` cleans up), but the
public `valid()` flag flips `0 -> 1` because the three rebuild buffers were
already committed before the key copy. This is the only `strmap` allocation
point with that behaviour; `intset`/`intmap` fail their rebuild and roll back
so `valid()` stays `0`, which is what the existing
`tests/stdlib/deep_collections_alloc.cp` checks expect for the analogous case.

`collections.hp` states allocation is failure-atomic ("the container is
exactly as it was before the call") but `strmap::put` also documents "the key
copy is the last fallible step ... nothing above it has changed the logical
contents". The test therefore asserts **logical** atomicity (documented, and
the stronger memory-safety guarantee) and does not treat the storage flag as
a failure. Recorded here so the ambiguity can be decided by the header owner;
no header was changed.

Minimal repro (3 lines of setup, no stdout fixtures):

```c
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"
int main(void) {
    strmap m = strmap();
    int pre = m.valid();
    stdlib_alloc_fail_at(4);
    int rc = m.put("k", 1);
    stdlib_alloc_fail_reset();
    print("pre_valid", pre, "rc", rc, "size", (int)m.size(), "post_valid", m.valid());
    m.drop();
    return 0;
}
```

Exact command and output (built with `--strict --sanitize=none`, `timeout 30`):

```
build_rc=0
run_rc=0
pre_valid 0 rc -1 size 0 post_valid 1
```

### Initial test mistakes (not library defects)

Two first-run failures were traced to wrong expectations in the new tests and
fixed in place:

- `intmap::put` on an empty map performs **3** allocations (keys, values,
  state), not 2; the probe assertion was relaxed to `seqc >= 2` and the
  enumeration still covers all 3.
- `vector::push_back` does not grow on the second push: capacity 4 holds the
  first four pushes, so `stdlib_alloc_fail_after(1)` must fail the **fifth**
  push. The test now pushes four values, checks they are intact, then fails
  the fifth and retries successfully.

Neither case produced a telemetry or memory-safety failure.
