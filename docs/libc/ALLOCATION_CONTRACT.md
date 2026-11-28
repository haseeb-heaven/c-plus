# Allocation API correctness contract (P0 audit)

Scope: the two C allocation families shipped by this repository —

- **Hardened libc** (`libc/src/stdlib/alloc.c`, declared in
  `libc/install-include/cplus_libc.h`): `cplus_malloc`, `cplus_calloc`,
  `cplus_realloc`, `cplus_free`, `cplus_free_checked`.
- **SafeCRT** (`stdlib/safe/src/safe_alloc.c`, declared in
  `stdlib/safe/safe_alloc.h`): `malloc_s`, `calloc_s`, `realloc_s`, `free_s`,
  `free_checked_s`, `aligned_alloc_s`.

Both families keep a thread-safe metadata side table (byte size, element size,
generation) plus a bounded recent-free table. Every logical allocation maps to
one distinct host `malloc`/`calloc` block; the hardened product is *not* a
replacement CRT unless explicitly linked.

Audit outcome: **no defect reproduced.** The current implementation already
contains the two critical guards this audit looked for — an **ABA generation
guard** around `realloc` and a **reserve-before-realloc** table-growth step — so
no source change was made. All ten P0 properties below were confirmed by the
existing test suites and by a focused 46-check diagnostic probe.

---

## The ten P0 properties

### 1. `calloc(8,16)` yields a 128-byte capacity

Both families record `byte_size = count * size` and `elem_size = size`:

| Call | `byte_size` | `elem_size` | `count` |
| --- | --- | --- | --- |
| `calloc_s(8, 16)` | 128 | 16 | 8 |
| `cplus_calloc(8, 16)` | 128 | 16 | 8 |

Confirmed by the probe (`cplus_safe_ptr_size == 128`,
`cplus_safe_remaining_capacity == 128`, `cplus_alloc_size == 128`,
`cplus_alloc_length == 8`, and all 128 bytes zeroed). Existing tests cover the
same arithmetic with other factors (`calloc_s(4,4)=16`, `cplus_calloc(6,8)=48`,
`calloc(4,12)=48`, `calloc(7,9)=63`).

### 2. `realloc` preserves the original on failure

Failure is detected **before** the host `realloc` is allowed to change anything:

- invalid size / unowned pointer: rejected before any table or host mutation;
- injected failure: `inject_fail_locked()` runs while the metadata lock is held
  and returns before `realloc()`;
- bookkeeping OOM: `tab_reserve_one()` / `meta_reserve_one()` runs *before*
  `realloc()`, so a successful host realloc can never be followed by a table
  failure that would orphan the new block;
- genuine host OOM (`realloc()` returns `NULL`): the original block is still the
  live owner in the table.

In every case the original pointer stays live with its original `byte_size`,
`elem_size`, generation and payload, and `errno` is set (`EINVAL` / `ENOMEM`).
Confirmed by `test_alloc_metadata_s.c` §5, `test_alloc_metadata.c` §3, the fault
matrix (`realloc injected: owner preserved`, `owner size preserved`), and the
probe.

### 3. Successful `realloc` invalidates the old identity

On success the generation is always bumped (`++g_gen`), so a generation captured
before the call no longer matches. If the host moved the block, the old address
is removed from the live table (`tab_remove` / `meta_remove`) and the new address
is inserted, so the old address is no longer reported live. If the host returned
the same address, the slot's size and generation are updated in place.

Confirmed by the probe for both families:
`cplus_safe_ptr_check_generation(new, old_gen) == 0`, `new_gen > old_gen`, and —
when the address moved — `cplus_safe_ptr_is_live(old_addr) == 0` (likewise
`cplus_alloc_check_generation` / `cplus_alloc_is_live` for hardened libc).

### 4. Failed `realloc` does not invalidate the original

No metadata field is touched on any failure path, so generation and live status
are unchanged. Confirmed by the probe
(`cplus_safe_ptr_check_generation(original, g0) == 1`,
`cplus_alloc_check_generation(original, cg0) == 1` after injected failure).

### 5. Pointer-to-pointer APIs leave no stale ownership

- `cplus_free_checked(void **inout)` sets `*inout = NULL` on **every** path (a
  valid free, a refused double free, a refused invalid free, and a `NULL`
  handle), so the caller's handle can never retain a stale pointer. It returns
  `0` on success, `EINVAL` with `errno = EINVAL` on refusal.
- `cplus_transfer_ownership(void **dst, void **src)` sets `*src = NULL` and
  `*dst = old_src` only on success. On refusal (either address `NULL`, `*src`
  not live, `*dst` already non-`NULL`, or `dst == src`) both handles are left
  untouched, so ownership is neither duplicated nor lost.
- `free_checked_s(void *)` is a **by-value** API: it deliberately does not
  rewrite the caller's alias (documented difference; see below). Its pointer-
  to-pointer analogue for ownership transfer is
  `cplus_safe_transfer_ownership`, which follows the same null-on-success /
  unchanged-on-refusal rule.

Confirmed by `test_alloc_metadata_s.c` §2, `test_alloc_metadata.c` §2/§4, the
fault matrix (`cplus_free_checked`: handle nulled, stale alias → `EINVAL`,
`NULL` no-op; `free_checked_s`: alias unchanged), and the probe.

### 6. Invalid operations are not converted to success

- Freeing an unowned pointer (stack, static, interior, or raw `malloc` memory)
  returns `EINVAL` (`CPLUS_SAFE_EINVAL` for SafeCRT) and does not call host
  `free`.
- `realloc_s` / `cplus_realloc` on an unowned pointer return `NULL` with
  `errno = EINVAL` and leave the pointer alone.
- `transfer_ownership` on a non-live source returns `EINVAL`.
- Out-of-range sizes are refused rather than clamped or wrapped.

Confirmed by `test_alloc_metadata_s.c` §7, `test_alloc_metadata.c` §4, the fault
matrix, and the probe.

### 7. Zero-size behavior (documented, defined)

A zero size is **not** an error and is **not** a zero-byte allocation. It yields
a tracked, zeroed (for calloc), 1-byte block in both families:

| API | Zero-size form | Result |
| --- | --- | --- |
| `malloc_s(0)` / `cplus_malloc(0)` | `size == 0` | 1-byte tracked block, `errno == 0` |
| `calloc_s(0, n)` / `calloc_s(n, 0)` | either factor 0 | 1-byte tracked **zeroed** block |
| `cplus_calloc(0, n)` / `cplus_calloc(n, 0)` | either factor 0 | 1-byte tracked zeroed block |
| `realloc_s(p, 0)` / `cplus_realloc(p, 0)` | size 0 | 1-byte tracked block (old payload preserved up to 1 byte) |
| `aligned_alloc_s(a, 0)` | size 0 | 1-byte tracked block (alignment still validated) |

`realloc(NULL, 0)` routes through the malloc path and returns a 1-byte block.
Confirmed by the fault matrix (`zero-size` rows for every family) and
`aligned_alloc_s` zero-size case.

### 8. Multiplication overflow is checked

- `calloc_s` checks `req_count > SIZE_MAX / req_size` **and**
  `req_count * req_size > RSIZE_MAX_MEM`, returning `NULL` with `errno = EOVERFLOW`.
- `cplus_calloc` checks `nmemb != 0 && size > SIZE_MAX / nmemb`, returning
  `NULL` with `errno = ENOMEM`. (It guards arithmetic overflow against
  `SIZE_MAX`; it does not additionally apply the SafeCRT `RSIZE_MAX_MEM` limit —
  see "Differences" below.)

No partial allocation or table entry is created. Confirmed by
`test_calloc_s.c` (`(size_t)-1, 2` and `SIZE_MAX, 1`), `test_alloc_metadata.c`
(`cplus_calloc(SIZE_MAX, 4)`), the fault matrix (both overflow shapes), and the
probe.

### 9. Allocation failure is deterministic

The C-callable injection hooks are latching ("N successes, then every later
attempt fails until reset") and process-global:

- SafeCRT: `cplus_safe_alloc_fail_after(long)`, `cplus_safe_alloc_fail_reset()`,
  `cplus_safe_alloc_seq()`.
- Hardened libc: `cplus_alloc_fail_after(long)`, `cplus_alloc_fail_reset()`.

`fail_reset()` clears the latch and zeroes the SafeCRT attempt sequence; every
injected call increments that sequence. Logging on/off does not change return
value, `errno`, metadata size, or sequence (log-parity checks). The fault matrix
asserts all of this in 294 checks, plain and under UBSan. Confirmed by the probe
(`seq == 2` after reset + one success + one injected failure, then recovery).

### 10. Double release is refused

A second release of the same pointer is refused — it is never passed to host
`free` twice. The bounded recent-free table records the address and generation;
a matching second release increments the diagnostic counter (hardened) or sets
`cplus_safe_ptr_was_freed()` (SafeCRT), returns `EINVAL` /
`CPLUS_SAFE_EINVAL`, and leaves live counts unchanged.

Confirmed by `test_free_s.c`, `test_alloc_metadata_s.c` §7,
`test_alloc_metadata.c` §4, the fault matrix, and the probe (including the
hardened `double_free` counter incrementing exactly once).

---

## ABA guard and reserve-before-realloc (present, not newly added)

Both `realloc` implementations deliberately release the metadata lock around
the host `realloc()` call, then re-acquire it. To avoid metadata corruption in
that window they:

1. capture `old_addr` and `old_gen` before the call and reserve table room
   *before* it (`tab_reserve_one` / `meta_reserve_one`), so nothing can fail
   after a successful host realloc; and
2. on re-lock, only treat a slot at `old_addr` as *this* realloc's owner when
   `slot.generation == old_gen && old_gen != 0`. A newer allocation that reused
   the address has a different generation and is never removed.

This is the "ABA guard" and "reserve-before-realloc" behavior the audit expected
to find; it is confirmed present in `stdlib/safe/src/safe_alloc.c` (`realloc_s`)
and `libc/src/stdlib/alloc.c` (`cplus_realloc`). No change was required.

---

## Notable differences between the two families (asserted, not smoothed over)

| Aspect | Hardened libc | SafeCRT |
| --- | --- | --- |
| Size limit check | host allocator only (`SIZE_MAX` for `cplus_calloc` product) | `RSIZE_MAX_MEM` enforced |
| Overflow `errno` | `ENOMEM` | `EOVERFLOW` (calloc), `EINVAL` (malloc/realloc) |
| Checked free signature | `cplus_free_checked(void **)` — nulls caller handle | `free_checked_s(void *)` — alias unchanged |
| Success/refusal codes | `0` / `EINVAL` | `CPLUS_SAFE_OK` / `CPLUS_SAFE_EINVAL` |
| Attempt sequence API | none | `cplus_safe_alloc_seq()` |
| Double-free diagnosis | `cplus_alloc_double_free_count()` | `cplus_safe_ptr_was_freed()` |

## Honest limits

- Recent-free tables are bounded (1024 slots); double-free detection is a
  *diagnostic*, not a proof of lifetime safety. A very old freed address that
  was reissued can be indistinguishable from its new owner.
- The ABA guard protects the metadata table only. A concurrent **raw** host
  `free()`/reuse of a block still owned by these allocators is undefined at the
  host level and is outside the contract.
- `aligned_alloc_s` requires a power-of-two alignment `>= sizeof(void *)`;
  invalid alignment is `EINVAL`, independent of the zero-size rule.

---

## Verification performed (exact commands and results)

All commands run from the repository root on branch `agent/modern-stdlib` with
`/usr/bin/clang`. The two static libraries were rebuilt first so the tests
exercise the **current** sources, not stale artifacts:

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
# -> .../build/libc/libcplus_hardened.a   (rc 0)

CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh
# -> .../build/safe/libcplus_safecrt.a    (rc 0)
```

### Existing tests

```sh
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ithird_party/safeclib/include \
  -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_calloc_s.c build/safe/libcplus_safecrt.a \
  -lm -pthread -o build/alloc_audit/test_calloc_s
build/alloc_audit/test_calloc_s
# stdout: unit calloc_s: ok                      (rc 0, no warnings)

/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ithird_party/safeclib/include \
  -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_free_s.c build/safe/libcplus_safecrt.a \
  -lm -pthread -o build/alloc_audit/test_free_s
build/alloc_audit/test_free_s
# stdout: unit free_s: ok                        (rc 0, no warnings)

/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ithird_party/safeclib/include \
  -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_alloc_metadata_s.c build/safe/libcplus_safecrt.a \
  -lm -pthread -o build/alloc_audit/test_alloc_metadata_s
build/alloc_audit/test_alloc_metadata_s
# stdout: unit alloc_metadata_s: ok              (rc 0, no warnings)

/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_alloc_metadata.c build/libc/libcplus_hardened.a \
  -lm -pthread -o build/alloc_audit/test_alloc_metadata
build/alloc_audit/test_alloc_metadata
# stdout: test_alloc_metadata: ok                (rc 0, no warnings)

/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include -I. \
  tests/libc/test_alloc_fault_matrix.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/alloc_audit/alloc_fault_matrix
build/alloc_audit/alloc_fault_matrix
# stdout: alloc_fault_matrix: 294/294 checks ok  (rc 0, no warnings)
```

UBSan variant of the matrix (same source/libs, added sanitizer flags):

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=undefined \
  -Ilibc/install-include -I. \
  tests/libc/test_alloc_fault_matrix.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/alloc_audit/alloc_fault_matrix_ubsan
build/alloc_audit/alloc_fault_matrix_ubsan
# stdout: alloc_fault_matrix: 294/294 checks ok  (rc 0, no UBSan diagnostics)
```

`tests/stdlib/agent2/test_alloc_fault_matrix.c` was requested "if present":
it is **absent** on this branch, so it was not run.

### Focused contract probe

A throwaway diagnostic (not a committed test file) at
`$TMPDIR/opencode/alloc_contract_probe.c` exercised all ten properties directly,
including the exact `calloc(8,16)=128` case and the generation/identity checks
that the existing suites cover only indirectly:

```sh
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include -I. \
  "$TMPDIR/opencode/alloc_contract_probe.c" \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/alloc_audit/alloc_contract_probe
build/alloc_audit/alloc_contract_probe
# stdout: alloc_contract_probe: 46/46 checks ok  (rc 0, no warnings)
```

Note: the probe initially reported one failure
(`P0-9 injection: sequence deterministic (2)`). Root cause was the **probe**,
not the allocator: it called `fail_after(1)` without first calling `fail_reset()`,
so `g_seq` retained counts from earlier allocations in the process. Adding the
documented `fail_reset()` (which zeroes the sequence) produced 46/46. This is
recorded here so the false positive is not mistaken for a defect.

Summary: 5 existing test binaries passed, the fault matrix passed in both plain
and UBSan builds, and the focused probe passed 46/46. No source change was
needed.

---

## Proposed permanent regression test (description only — not created)

File ownership for tests belongs to another agent, so no test file was created.
The recommended permanent regression is:

- **Path:** `tests/libc/test_alloc_contract.c`
- **Runner:** picked up by the existing `tests/libc/run_libc_tests.sh` glob
  (`tests/libc/test_*.c`); add the same special case used for the fault matrix —
  `EXTRA_INC="-I."`, `EXTRA_LIB="build/safe/libcplus_safecrt.a"` — so it links
  both libraries. Add no new runner file.
- **Content:** port the 46 checks of the throwaway probe above into a
  self-checking C99 program with a `CHECK` macro that prints
  `FINDING: <property>: <detail>` and returns non-zero on any failure, following
  the style of `tests/libc/test_alloc_fault_matrix.c`. Group checks under the
  ten P0 headings so a failure names the broken contract property. Keep
  `cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF)` and
  `cplus_libc_log_set_enabled(0)` at start-up for quiet output.
- **Extra case worth adding beyond the probe:** force the "realloc moved the
  block" branch deterministically (allocate a large block, allocate a small
  guard block immediately after it, then grow the first so the host must move
  it) and assert the old address is no longer live while the new one is — the
  probe already asserts this whenever the host happens to move the block, but a
  forced move makes the assertion unconditional.
