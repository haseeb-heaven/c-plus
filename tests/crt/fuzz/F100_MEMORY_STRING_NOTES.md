# F-001..F-020 — deterministic fuzz configs for memory/string APIs

## Catalog status

`docs/libc/FUZZ_100_CATALOG.md` did **not** exist when these configs were
created (parent session was still generating it). Per the task instruction,
this agent used its own numbering **F-001..F-020** for the memory/string
slice. Other parallel agents own later ranges (e.g. `f100_alloc_f021_*` is
not part of this deliverable). If the catalog assigns different IDs, only the
ID labels in the first line of each source comment and in the summary lines
need renaming; seeds, workloads and structure are stable.

## Files (20 self-contained C99 harnesses)

All files live in `tests/crt/fuzz/`. No existing harness, runner, header or
archive was modified. Each file is a standalone program with its own `main`
(no shared header, so there are no new header dependencies).

| ID | File | APIs under test | Seed | Iters | Length domain | Archive(s) |
| --- | --- | --- | --- | --- | --- | --- |
| F-001 | `f100_mem_memcpy_bounded.c` | `cplus_memcpy` | `0xF1000001` | 1200 | n 0..512 | hardened |
| F-002 | `f100_mem_memmove_bounded.c` | `cplus_memmove` (distinct objects) | `0xF1000002` | 1200 | n 0..512 | hardened |
| F-003 | `f100_mem_memmove_overlap.c` | `cplus_memmove` (overlap, both directions) | `0xF1000003` | 1200 | m 1..464, offsets 32/48 | hardened |
| F-004 | `f100_mem_memset_bounded.c` | `cplus_memset` | `0xF1000004` | 1200 | n 0..512 | hardened |
| F-005 | `f100_mem_memcmp_bounded.c` | `cplus_memcmp` (`<`, `=`, `>`) | `0xF1000005` | 1200 | n 0..512 | hardened |
| F-006 | `f100_mem_memcpy_s_dmax.c` | `cplus_memcpy_s` | `0xF1000006` | 1200 | dmax 1..512, smax 0..dmax | safecrt |
| F-007 | `f100_mem_memmove_s.c` | `cplus_memmove_s` (+ valid overlap) | `0xF1000007` | 1200 | dmax 1..512, smax 0..dmax | safecrt |
| F-008 | `f100_mem_memset_s.c` | `cplus_memset_s` | `0xF1000008` | 1200 | dmax 1..512, n 0..dmax | safecrt |
| F-009 | `f100_mem_memcmp_s.c` | `cplus_memcmp_s` | `0xF1000009` | 1200 | dmax 1..512, smax 1..dmax | safecrt |
| F-010 | `f100_mem_memzero_s.c` | `cplus_memzero_s` | `0xF100000A` | 1200 | dmax 1..512 | safecrt |
| F-011 | `f100_str_strlen_bounded.c` | `cplus_strlen` | `0xF100000B` | 1200 | strlen 0..512 | hardened |
| F-012 | `f100_str_strnlen_bounded.c` | `cplus_strnlen` | `0xF100000C` | 1200 | n, strlen 0..512 | hardened |
| F-013 | `f100_str_strcpy_bounded.c` | `cplus_strcpy` | `0xF100000D` | 1200 | strlen 0..512 | hardened |
| F-014 | `f100_str_strncpy_s.c` | `cplus_strncpy_s` | `0xF100000E` | 1200 | dmax 1..512, slen 0..dmax | safecrt |
| F-015 | `f100_str_strcat_s.c` | `cplus_strcat_s` | `0xF100000F` | 1200 | prefix/suffix 0..128 | safecrt |
| F-016 | `f100_str_strncat_s.c` | `cplus_strncat_s` | `0xF1000010` | 1200 | prefix 0..128, slen 1..sl | safecrt |
| F-017 | `f100_str_strcmp_s.c` | `cplus_strcmp_s` | `0xF1000011` | 1200 | dmax 1..512, strings 1..128 | safecrt |
| F-018 | `f100_str_strnlen_s.c` | `cplus_strnlen_s` | `0xF1000012` | 1200 | smax, strlen 0..512 | safecrt |
| F-019 | `f100_str_constraint_matrix.c` | 10 mixed `_s` scenarios | `0xF1000013` | 1200 | dmax 1..512, n 0..dmax | safecrt |
| F-020 | `f100_str_log_parity.c` | plain + `_s`, log OFF vs ON | `0xF1000014` | 1200 x2 | n/dmax/smax 0..512 | both |

Archives used as-is (not rebuilt):

- `build/libc/libcplus_hardened.a` (plain `cplus_*` APIs, `cplus_libc_log_set_enabled`)
- `build/safe/libcplus_safecrt.a` (checked `cplus_*_s` APIs, `cplus_safe_*`)

## Common design

- **RNG:** fixed-seed LCG `state = state*1664525 + 1013904223 (mod 2^32)` using
  `uint32_t`, so wrap is platform-independent. State is reset at `main` entry
  (and again per pass in F-020). No `rand()`, no time, no ASLR-dependent data.
- **Forced edges:** iteration 0 forces zero length, iteration 1 forces exact-fit
  512, iteration 2 forces 511 wherever a length is generated. F-003 forces a
  maximum-size valid overlap at iteration 2.
- **Guards:** every operand is a stack object with 8 `0xA5` canary bytes before
  and after the payload. Both canaries and the untouched destination tail are
  re-checked after every call. Source/destination are distinct objects except
  in the two dedicated overlap harnesses (F-003, F-007).
- **No raw UB in the harness:** all pointers are valid object addresses, all
  lengths fit the guarded payloads, all strings are NUL-terminated inside their
  objects. NULL / zero / `SIZE_MAX` are passed **only** to the checked `_s`
  APIs, which are specified to reject them; the constraint mode is explicitly
  set to `CPLUS_SAFE_CONSTRAINT_IGNORE` so no abort handler fires.
- **Digest:** FNV-1a 64-bit over operation tags, sizes, result codes and full
  initialized guarded objects (payload + canaries). Raw pointer values are
  never hashed. Every harness prints exactly one summary line.
- **Noise control:** the vendored ignore handler writes to **stdout**. Harnesses
  with constraint cases temporarily `dup2` stdout to `/dev/null` around those
  calls and restore it before printing the summary; assertions on return codes
  and canaries are unaffected. F-020 redirects **fd 2** to `/dev/null` for
  both passes because both loggers write there.

## Verification commands (exact)

Run from the repository root. Plain mode:

```sh
for f in tests/crt/fuzz/f100_mem_*.c tests/crt/fuzz/f100_str_*.c; do
  b="/tmp/f100_$(basename "$f" .c)"
  /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
    -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
    -Ithird_party/safeclib/include \
    "$f" build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
    -o "$b" || exit 1
  timeout 30 "$b" || exit 1
done
```

UBSan mode (same flags plus two sanitizer flags and `halt_on_error`):

```sh
for f in tests/crt/fuzz/f100_mem_*.c tests/crt/fuzz/f100_str_*.c; do
  b="/tmp/f100_ubsan_$(basename "$f" .c)"
  /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
    -Ithird_party/safeclib/include \
    "$f" build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
    -o "$b" || exit 1
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 timeout 30 "$b" || exit 1
done
```

Both archive orders were exercised; linking hardened then safecrt on one line
works for all 20 (no duplicate global symbols). Binaries were written under a
temp directory, never into the repository.

## Per-mode results

Plain (`-std=c99 -Wall -Wextra -pedantic`): **20/20 compiled with zero warnings,
20/20 exited 0 under `timeout 30`.**

UBSan (`-fsanitize=undefined -fno-sanitize-recover=all`): **20/20 compiled with
zero warnings, 20/20 exited 0 under `timeout 30`, zero UBSan diagnostics.**

| ID | Plain digest / result | UBSan digest / result |
| --- | --- | --- |
| F-001 | `2676a5a33087dc9b` exit 0 | `2676a5a33087dc9b` exit 0, clean |
| F-002 | `f71a3895b14f04a5` exit 0 | `f71a3895b14f04a5` exit 0, clean |
| F-003 | `9e1b86f42370f3a6` exit 0 | `9e1b86f42370f3a6` exit 0, clean |
| F-004 | `7f1d79a3197ca75f` exit 0 | `7f1d79a3197ca75f` exit 0, clean |
| F-005 | `b1abff247f1e4678` exit 0 | `b1abff247f1e4678` exit 0, clean |
| F-006 | `4193fca5486420e7` exit 0 | `4193fca5486420e7` exit 0, clean |
| F-007 | `257a78d4bf852a9d` exit 0 | `257a78d4bf852a9d` exit 0, clean |
| F-008 | `281bfeb3a3fa0190` exit 0 | `281bfeb3a3fa0190` exit 0, clean |
| F-009 | `fc7b45abee168400` exit 0 | `fc7b45abee168400` exit 0, clean |
| F-010 | `d6ffb899a135f591` exit 0 | `d6ffb899a135f591` exit 0, clean |
| F-011 | `0e65e9c2c8bd57a1` exit 0 | `0e65e9c2c8bd57a1` exit 0, clean |
| F-012 | `4784834be4441041` exit 0 | `4784834be4441041` exit 0, clean |
| F-013 | `2a6512217dbbfc55` exit 0 | `2a6512217dbbfc55` exit 0, clean |
| F-014 | `77d8aa757f3eeb77` exit 0 | `77d8aa757f3eeb77` exit 0, clean |
| F-015 | `7beaaa8aa4aeb2e1` exit 0 | `7beaaa8aa4aeb2e1` exit 0, clean |
| F-016 | `e4077e1a4118ca21` exit 0 | `e4077e1a4118ca21` exit 0, clean |
| F-017 | `8ad8f621fce6c541` exit 0 | `8ad8f621fce6c541` exit 0, clean |
| F-018 | `504744a068308c63` exit 0 (`zero_smax=1`) | `504744a068308c63` exit 0, clean |
| F-019 | `a7bac52b206ded47` exit 0 | `a7bac52b206ded47` exit 0, clean |
| F-020 | `042ade4afd442719` exit 0 (`cases=2400`) | `042ade4afd442719` exit 0, clean |

Additional checks:

- **Determinism:** each of the 20 plain binaries was run 3 times; every binary
  produced 1 unique output line (0 nondeterministic).
- **Log parity non-vacuity (F-020):** a temp copy of F-020 with the two
  `dup2` silence calls removed emitted **4800** `[CPLUS-LIBC]` records and
  **3600** `[SAFECRT]` records on stderr, while still printing the same digest
  `042ade4afd442719`. So the OFF/ON digest equality is a real observation, not
  an effect of both passes being silent. The 1200-iteration stream runs twice,
  giving 2400 validated cases for F-020.

## Reproduced defect

**None.** No harness assertion failed after the two model corrections described
below, and no sanitizer diagnostic was produced.

Non-obvious *documented* library behaviors that the harnesses pin down — these
are model corrections, **not** claimed defects:

1. `cplus_strncat_s(dest, dmax, src, 0)` returns `CPLUS_SAFE_OK` (0) but clears
   `dest[0]` (safeclib's "MSVCRT analog" path in `third_party/safeclib/src/str/strncat_s.c`,
   lines 149-155). Initial F-016 expected the prefix to be preserved and failed;
   F-016 now asserts `rc == 0 && dest[0] == '\0'` for this case in its constraint
   phase, and the 1200-case stream uses `slen >= 1`. Minimal repro:
   ```c
   char d[64] = "abcd";
   cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
   errno_t e = cplus_strncat_s(d, 64, "XY", 0);
   /* e == 0, d[0] == '\0' */
   ```
2. F-017 initially derived `a<b` by adding 8 to a lowercase letter, which can
   wrap past `'z'` and invert the comparison under unsigned char ordering.
   The harness now fixes the first differing byte to `'a'` vs `'z'`/`'A'`.
   This was a harness-model bug, not a library defect.

## Honesty / limitations

- Passing UBSan **does not prove safety**. UBSan detects a limited class of
  undefined behavior on the paths actually executed; it cannot prove pointer
  provenance, ownership, lifetime, alias freedom, or the absence of out-of-bounds
  accesses that happen to stay inside the guarded object. The canary/tail
  assertions and the constrained input domain provide the memory-safety
  coverage here. ASan is not used (it is unreliable on this host per
  `docs/libc/LIBC_FUZZ_NOTES.md`).
- These are deterministic bounded-input fuzz harnesses, not coverage-guided
  fuzzers and not libFuzzer targets. They intentionally avoid deliberately
  invalid pointers and huge lengths in the main loop; those belong to the
  boundary/attack suites. The checked `_s` constraint phase covers NULL/zero/
  overflow/truncation at fixed points only.
- Constraint-violation diagnostics are silenced by design; the return codes
  are asserted instead, so no diagnostic output is mistaken for a test failure.
