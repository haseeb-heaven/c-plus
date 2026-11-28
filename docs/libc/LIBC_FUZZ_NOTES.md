# Deterministic fuzz harness for hardened libc string/memory APIs

## Files

- `tests/libc/fuzz_libc.c` — the harness (replaces the earlier smoke-style version).
- `docs/libc/LIBC_FUZZ_NOTES.md` — this note.

No other source files were created or modified (verification left only local
test binaries under `build/libc/`). The C+ archives were **not** rebuilt; the
harness links the existing `build/libc/libcplus_hardened.a` and
`build/safe/libcplus_safecrt.a` directly.

## Scope: functions actually exported

Confirmed exported by the hardened archive with
`nm -g --defined-only build/libc/libcplus_hardened.a` and declared in
`libc/install-include/cplus_libc.h`:

| Symbol | Declaration (header line) |
| --- | --- |
| `cplus_memcpy` | 79 |
| `cplus_memmove` | 80 |
| `cplus_memset` | 81 |
| `cplus_memchr` | 83 |
| `cplus_memrchr` | 95 |
| `cplus_strnlen` | 85 |
| `cplus_strncpy` | 87 |
| `cplus_strncat` | 89 |

`cplus_libc_log_set_enabled` is declared at header line 140 and is exported by
the hardened archive (`libc/log/log.c`). The two archives define no duplicate
global symbols, so both can be placed on one link line.

## Design

- **RNG:** fixed-seed LCG, `state = state * 1664525 + 1013904223 (mod 2^32)`,
  seed `0x5EED1234`, using `uint32_t` so the wrap is platform-independent.
  State is reset at the start of every pass.
- **Workload:** 300 iterations × 8 API calls = **2400 operations per pass**;
  the stream runs twice (logging off, then logging on) for **4800 validated
  operations** total. Each call gets its own freshly initialized objects.
- **Bounds:** every size passed is in `[0, 256]`. The overlap `memmove` uses
  offsets 32/48 with lengths up to 128, all inside the 256-byte payload.
  No deliberately invalid pointers, no `SIZE_MAX`-style `n` — those cases are
  owned by the boundary/attack suites, and this harness never claims a bug for
  them.
- **Guarded objects:** each payload is wrapped by 8-byte pre/post canaries
  (`0xA5`). Canaries and the destination tail beyond `n` are compared after
  every operation. Search results are checked for first/last occurrence and
  NULL semantics; `strnlen`, `strncpy`, and `strncat` are checked against
  independently computed expectations on controlled NUL-terminated strings.
  Source and destination are distinct objects except in the dedicated overlap
  case.
- **Required edge cases:** iteration 0 forces `n = 0` for all eight APIs;
  iteration 1 forces `n = 256` (exact fit against the canary boundary);
  iterations 2, 19, 36, … exercise overlapping `memmove` in both directions
  (forward and backward copy paths), with iteration 2 using a 128-byte overlap.
- **Logged on/off parity:** the same stream is replayed with
  `cplus_libc_log_set_enabled(0)` and `(1)`. The opt-in logger writes to raw
  fd 2, so the harness temporarily redirects fd 2 to `/dev/null` for the
  logging-on pass and restores it afterwards; stdout is unaffected. Both
  passes must yield the identical FNV-1a 64-bit digest, and the harness asserts
  this. Sanity check with the silence step removed: the logging-on pass emits
  exactly 2400 `[CPLUS-LIBC] …: enter` records and still prints the same
  digest, so the parity assertion is non-vacuous.
- **Digest:** FNV-1a over operation tags, sizes, search indices, and full
  initialized guarded objects (payload + canaries). Raw pointer values are
  never hashed, so ASLR cannot perturb the result.

## Verification commands and results

Run from the repository root:

```sh
# plain build, both archives, exact required flags
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  tests/libc/fuzz_libc.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -o build/libc/fuzz_libc_verify
./build/libc/fuzz_libc_verify

# UBSan build (same flags plus sanitizer)
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  tests/libc/fuzz_libc.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -o build/libc/fuzz_libc_ubsan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./build/libc/fuzz_libc_ubsan
```

Observed output from both the plain and UBSan binaries:

```
fuzz_libc: digest=2b62150fb0777106
fuzz_libc: 4800 cases ok
```

- **N:** 4800 cases (2400 per pass × 2 logging modes).
- **Digest:** `2b62150fb0777106`.
- **Compiler warnings:** none with `-Wall -Wextra -pedantic` (both builds).
- **UBSan:** clean, exit status 0, no diagnostics.
- **Determinism:** 5 consecutive executions produced 5 byte-identical output
  pairs (1 unique output).
- All invariant assertions passed in both logging modes.

## AddressSanitizer status

ASan is **unsupported on this host** and was therefore not used for the
harness. A trivial probe (`int main(void){return 0;}`) compiled with
`/usr/bin/clang -fsanitize=address` but aborted at startup with:

```
AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)" (0x0, 0x0) ...
```

This is a toolchain/runtime failure unrelated to the harness; the canary and
bounds checks above provide the memory-safety coverage available here. The
harness is ASan-ready: all hashed memory is initialized and all accesses stay
inside the guarded objects.
