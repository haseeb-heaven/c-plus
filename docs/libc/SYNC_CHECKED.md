# CHECKED synchronization destroy/join — additive runtime safety evidence

## 1. Gap addressed

`libc/src/thread/threads_c11.c` exports void forwarders:

- `:304` — `void cplus_mtx_destroy(cplus_mtx_t *)`
- `:348` — `void cplus_cnd_destroy(cplus_cnd_t *)`

both of which discard the `cplus_pthread_{mutex,cond}_destroy` return code, so a
second destroy is undefined behavior with no refusal. `cplus_thrd_join()` has
no double-join guard either. The Rust-examples survey marked the two
double-destroy cases **BLOCKED**.

This change is **additive only**. `threads_c11.c` was read but not modified; no
existing declaration, struct layout, pointer representation or exported symbol
changed. Three **new** symbols were added:

```
cplus_mtx_destroy_checked(cplus_mtx_t *)   -> int
cplus_cnd_destroy_checked(cplus_cnd_t *)   -> int
cplus_thrd_join_checked(cplus_thrd_t,int*) -> int
```

### Owned files created

| File | Purpose |
|---|---|
| `libc/src/thread/sync_checked.c` | implementation (new translation unit) |
| `libc/install-include/cplus_sync_checked.h` | public declarations + error contract |
| `tests/libc/test_sync_checked.c` | standalone C99 verification |
| `docs/libc/SYNC_CHECKED.md` | this record |

No git staging/commit was performed. No other file was created or modified.

## 2. Design

- **Metadata first, object second.** A fixed-capacity (64 slots), statically
  allocated side table is protected by a `PTHREAD_MUTEX_INITIALIZER` mutex.
  `claim()` runs under that lock and reserves a slot keyed by the *value bytes*
  of the mutex/cond address, or by all bytes of the by-value `cplus_thrd_t`
  handle. Only after a slot is reserved is the object passed to
  `cplus_pthread_mutex_destroy`, `cplus_pthread_cond_destroy` or
  `cplus_thrd_join`. A stale pointer is never dereferenced by the checker.
- **Repeat refusal.** A second call for an identity already in the table gets
  `EINVAL` immediately; the underlying pthread call is not made.
- **Generation token.** Every claim increments a monotonic `uint64_t`
  generation and stamps it into the slot, so a slot recycled after a
  rolled-back operation has a distinct `(identity, generation)` pair and is
  auditable (ABA boundary for the table itself). No public struct layout is
  changed — the table is entirely private to the new `.c` file.
- **Bounded refusal.** With no free slot the call returns `ENOMEM` without
  touching the object. Testing uses a dynamic probe up to
  `CPLUS_SYNC_CHECKED_CAPACITY + 8` so it does not assume how many slots earlier
  cases consumed.
- **Rollback.** If the underlying pthread call fails (e.g. `EBUSY` on a locked
  mutex), the reserved slot is released so a legitimate retry can succeed.
- **Return contract:** `0` success; `EINVAL` NULL/zeroed handle or repeat
  operation; `ENOMEM` table exhaustion; other values are the raw pthread error
  code (join maps `thrd_*` to `0/EBUSY/ENOMEM/ETIMEDOUT/EINVAL`).

## 3. Test scenarios and observed behavior

| Scenario | Observed result |
|---|---|
| mutex init + checked destroy | `0` |
| cond init + checked destroy | `0` |
| thread create + checked join, result `42` | `0`, `res == 42` |
| second checked mutex destroy | `EINVAL`, no crash |
| second checked cond destroy | `EINVAL`, no crash |
| second checked join of same handle | `EINVAL`, no crash |
| `cplus_mtx_destroy_checked(NULL)` / `cplus_cnd_destroy_checked(NULL)` | `EINVAL` |
| all-zero `cplus_thrd_t` (by-value NULL equivalent) | `EINVAL` |
| destroy a locked mutex, then unlock and retry | rollback lets retry return `0` |
| 4 threads destroy the same mutex concurrently | exactly 1 returns `0`, 3 return `EINVAL` |
| fill the table with fresh heap mutexes until full | `ENOMEM` refusal, capacity never exceeded |

Final result: **`sync_checked: 156 0`**, exit status `0`.

## 4. Exact verification commands and results

All commands were run from the repository root on branch `agent/modern-stdlib`.
Compiler: `/usr/bin/clang` (Apple clang version 16.0.0,
`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`.
Scratch outputs were written under `/tmp/sync_checked_verify` (no repository
file other than the four owned files was written).

`build/libc/libcplus_hardened.a` was **pre-existing** and was not rebuilt or
modified. Its `tests/libc/build_libc.sh` source list is explicit and does not
yet name `sync_checked.c` (that build script is outside this task's ownership),
so for verification the new translation unit was compiled on its own and linked
alongside the archive. A temporary *copy* of the archive with the new object
appended was used for the nm symbol-diff evidence and for the "archive-only"
link check.

### 4.1 Strict C99 compile of the new source and test (no warnings)

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/thread/sync_checked.c -o /tmp/sync_checked_verify/sync_checked.o
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
  -Ilibc -Ilibc/install-include \
  -c tests/libc/test_sync_checked.c -o /tmp/sync_checked_verify/test_sync_checked.o
```

Output: **empty** (no diagnostics). Exit: `0` for both.

### 4.2 Link against `build/libc/libcplus_hardened.a` and run (30 s timeout)

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
  /tmp/sync_checked_verify/test_sync_checked.o \
  /tmp/sync_checked_verify/sync_checked.o \
  build/libc/libcplus_hardened.a \
  -o /tmp/sync_checked_verify/test_sync_checked
/usr/bin/timeout 30 /tmp/sync_checked_verify/test_sync_checked
```

Output:

```
sync_checked: 156 0
```

Exit: `0`.

### 4.3 Repeat runs (concurrency stability)

```
for i in 1 2 3 4 5; do /usr/bin/timeout 30 /tmp/sync_checked_verify/test_sync_checked || exit 1; done
```

Output (five identical lines), exit `0`:

```
sync_checked: 156 0
sync_checked: 156 0
sync_checked: 156 0
sync_checked: 156 0
sync_checked: 156 0
```

### 4.4 UBSan (`-O1`, halt on error)

```
/usr/bin/clang -std=c99 -O1 -Wall -Wextra -pedantic -pthread \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Ilibc -Ilibc/install-include \
  tests/libc/test_sync_checked.c libc/src/thread/sync_checked.c \
  build/libc/libcplus_hardened.a \
  -o /tmp/sync_checked_verify/test_sync_checked_ubsan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /usr/bin/timeout 30 /tmp/sync_checked_verify/test_sync_checked_ubsan
```

Output:

```
sync_checked: 156 0
```

Exit: `0`. No UBSan reports.

### 4.5 nm evidence — only NEW symbols were added

Baseline (pre-existing archive) and augmented copy (temporary, with the new
object appended) were normalised identically to `awk '$2=="T" || $2=="t"
{print $2, $3}' | sort -u`:

```
/usr/bin/nm -g --defined-only build/libc/libcplus_hardened.a \
  | awk '$2=="T" || $2=="t" {print $2, $3}' | sort -u \
  > /tmp/sync_checked_verify/before.txt          # 784 lines

cp build/libc/libcplus_hardened.a /tmp/sync_checked_verify/libcplus_hardened_augmented.a
ar rcs /tmp/sync_checked_verify/libcplus_hardened_augmented.a \
  /tmp/sync_checked_verify/sync_checked.o
/usr/bin/nm -g --defined-only /tmp/sync_checked_verify/libcplus_hardened_augmented.a \
  | awk '$2=="T" || $2=="t" {print $2, $3}' | sort -u \
  > /tmp/sync_checked_verify/after.txt           # 787 lines

diff -u /tmp/sync_checked_verify/before.txt /tmp/sync_checked_verify/after.txt
```

Result: `784 -> 787` defined text symbols; the complete diff is exactly three
added lines and **zero removed/changed lines**:

```
@@ -264,6 +264,7 @@
 T _cplus_cnd_broadcast
 T _cplus_cnd_destroy
+T _cplus_cnd_destroy_checked
 T _cplus_cnd_init
@@ -530,6 +531,7 @@
 T _cplus_mtx_destroy
+T _cplus_mtx_destroy_checked
 T _cplus_mtx_init
@@ -708,6 +710,7 @@
 T _cplus_thrd_join
+T _cplus_thrd_join_checked
 T _cplus_thrd_sleep
```

No existing export changed name, type or address class. Undefined references
of the test object were also enumerated with
`/usr/bin/nm -u /tmp/sync_checked_verify/test_sync_checked.o`; every
`cplus_*` reference (including the three new ones) is defined by either the
archive or the new object.

### 4.6 Link with the augmented archive only (no explicit new object)

Once the canonical build script includes the new translation unit, this is the
resulting link shape. Verified against the temporary augmented copy:

```
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
  /tmp/sync_checked_verify/test_sync_checked.o \
  /tmp/sync_checked_verify/libcplus_hardened_augmented.a \
  -o /tmp/sync_checked_verify/test_aug_only
/usr/bin/timeout 30 /tmp/sync_checked_verify/test_aug_only
```

Output: `sync_checked: 156 0`, exit `0`.

### 4.7 Negative control (the test is not vacuous)

A temporary `/tmp` stub implementing the three names with **no side table**
(direct `pthread_mutex_destroy` / `pthread_cond_destroy` / `cplus_thrd_join`)
was linked in place of `sync_checked.o`:

```
/usr/bin/timeout 30 /tmp/sync_checked_verify/test_negative
```

Output:

```
FAIL: zeroed thread handle refused (got 4, expected 22)
FAIL: checked double join refused (no crash) (got 4, expected 22)
sync_checked: 181 5
FAIL: exhaustion was reached within bounded probe
FAIL: exhaustion refusal code (got 0, expected 12)
FAIL: capacity bound respected
```

Exit: `1`. This confirms `CHECK`/`CHECK_EQ` count failures, print diagnostics
to stderr and drive a non-zero exit status. (On this Darwin host a second raw
`pthread_mutex_destroy` happens to return `EINVAL` without crashing, which is
exactly why the table-based *pre-call refusal* is valuable rather than relying
on the pthread implementation.)

## 5. Known limitations

1. **Address identity cannot see re-initialization.** If an object is
   checked-destroyed and a *new* object is later initialized at the **same
   address**, the table still reports that identity as consumed and refuses the
   new destroy (`EINVAL`). This is inherent to an address-keyed table with no
   init hook; the generation token is stamped internally but callers supply no
   generation argument, so it cannot distinguish a reinitialized object from a
   stale one. Workaround: use a distinct object/address, or use the legacy void
   API for such reclaimed storage. The required double-destroy/double-join
   cases are fully covered; reinit-reuse is deliberately out of scope.
2. **Best-effort, bounded detector.** After 64 successful checked
   destroy/join operations the table refuses further *new* identities with
   `ENOMEM` until those slots are recycled by a rolled-back operation. This is
   a bounded-resource trade-off, not an unbounded tracker.
3. **Not a lifetime or memory-safety proof.** The checker validates identity,
   not that the passed object is correctly initialized. It does not read or
   poison object bytes, so it cannot detect a dangling pointer that happens to
   carry an unrecorded address; it only guarantees that a *recorded* identity
   is never passed to pthread a second time.
4. **Join key includes start-context bytes.** The whole by-value handle is
   hashed, which makes a recycled `pthread_t` value unlikely to false-match a
   prior handle; an exact all-32-byte collision is still theoretically
   possible.
5. **Archive not rebuilt.** Per file ownership, `tests/libc/build_libc.sh` was
   not edited and `build/libc/libcplus_hardened.a` was not regenerated. The
   new source compiles cleanly and was linked additively (sections 4.2 and
   4.6); wiring it into the canonical build script is a separate change owned
   by that script's maintainer.
6. **UBSan instrumented the new translation unit and the test**, not the
   pre-existing archive objects. No ASan run was performed.
