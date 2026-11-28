# F-076 .. F-085 — deterministic fuzz configurations (string / string_view / file / memory)

Standalone, self-checking, fixed-seed fuzz harnesses on `agent/modern-stdlib`.
Each config has its own `main`, 1200 (> 1000) generated valid operations, an
independent shadow model where applicable, and a 64-bit FNV-1a digest printed
once on success.  No source outside `tests/stdlib/fuzz/` was touched.

## 1. Configuration inventory

| ID | Source | Family / focus | Seed | Shadow model | Digest |
| --- | --- | --- | --- | --- | --- |
| F-076 | `f100_strview_append.cp` | `string::append` plain / empty / bounded / self-append; reserve | `0xF076C0DE` | byte-exact string bytes | `a8b0cab0d7f10fd0` |
| F-077 | `f100_strview_reserve_copy.cp` | two strings: reserve, `copy_from` (cross + self), self-append, clear | `0xF077C0DE` | two byte-exact shadows | `21b16dab55f61fa9` |
| F-078 | `f100_strview_accessors.cp` | `at`/`substr`/`count_fields`/`field`/`find`/`equals`/`compare`/view | `0xF078C0DE` | shadow bytes + independent field decoder | `42f41ba54bd848a2` |
| F-079 | `f100_strview_utf8.cp` | `stdlib_utf8_validate` + `string_view::is_valid_utf8` on raw bytes | `0xF079C0DE` | independent code-point-decoding UTF-8 oracle + fixed vectors | `938a98c71794bb95` |
| F-080 | `f100_strview_stale.cp` | stale views across append/reserve/clear/drop/reinit | `0xF080C0DE` | owner bytes + immutable stale pointer/size | `ee546f9c53e9485f` |
| F-081 | `f100_file_write_read.cp` | `write_bytes`/`write`/`read` with NUL + high bytes, partial reads | `0xF081C0DE` | byte-exact file image + position | `07d376f5c2debb08` |
| F-082 | `f100_file_lines.cp` | `read_line` replay: empty, exact-fit, CRLF, overlong, EOF | `0xF082C0DE` | 32-line corpus model (status + text) | `7c4ac7387828c908` |
| F-083 | `f100_file_lifecycle.cp` | named/anonymous temp files, flush/seek, double close/drop, invalid handles | `0xF083C0DE` | per-case status + post-cleanup path absence | `6f2a7c923ca3603c` |
| F-084 | `f100_mem_guarded.cp` | `stdlib_mem_copy`/`move`/`set`/`borrow`, capacity, bounds refusal | `0xF084C0DE` | two byte-exact buffers + capacity/token checks | `44290440dbd30560` |
| F-085 | `f100_mem_tokens.cp` | generation tokens across realloc/transfer, stale refusal, untracked pointers | `0xF085C0DE` | buffer bytes + capacity + stale-token boolean | `dd1b9460d3b0b0cf` |

All sources mirror `fuzz_hp.cp`: LCG `state = state * 6364136223846793005 +
1442695040888963407`, high 32 bits; bounded lengths/capacities; no UB inputs;
only documented recoverable outcomes (EOF, absent field, bounds refusal, stale
token, double close) are asserted.  A crash, sanitizer report, invariant line
or digest change is therefore a real defect.

## 2. Exact verification commands

From the repository root (`build/c+` is the project compiler).  Strict/no-san
was run first for every config, then UBSan, each binary under `timeout 30s`:

```sh
mkdir -p build/f100-fuzz
for c in f100_strview_append f100_strview_reserve_copy \
         f100_strview_accessors f100_strview_utf8 f100_strview_stale \
         f100_file_write_read f100_file_lines f100_file_lifecycle \
         f100_mem_guarded f100_mem_tokens; do
  ./build/c+ build tests/stdlib/fuzz/$c.cp --strict --sanitize=none \
      -o build/f100-fuzz/${c}_strict_none
  timeout 30s ./build/f100-fuzz/${c}_strict_none

  ./build/c+ build tests/stdlib/fuzz/$c.cp --strict --sanitize=undefined \
      -o build/f100-fuzz/${c}_ubsan
  timeout 30s ./build/f100-fuzz/${c}_ubsan
done
```

Each run prints exactly two lines:

```
<config>_digest <16-hex-digits>
<config>: 1200 cases ok
```

## 3. Results (this branch)

| Check | Result |
| --- | --- |
| 10 / 10 `--strict --sanitize=none` builds | pass, exit 0 |
| 10 / 10 strict/no-san runs (`timeout 30s`) | pass, 1200 cases, stderr 0 bytes |
| 10 / 10 `--strict --sanitize=undefined` builds | pass, exit 0 |
| 10 / 10 UBSan runs (`timeout 30s`) | pass, 1200 cases, stderr 0 bytes |
| strict/no-san vs UBSan stdout | byte-identical for all 10 configs |
| Repeatability (3 additional strict/no-san runs per config) | identical digest on all 30 runs |
| Wall time | every run completed in well under 30 s (no timeouts) |

Digests are in the inventory table above.  No `INVARIANT` line, no sanitizer
text and no hang was observed in the final verification.

## 4. Notable harness behaviours / non-defects found while building the configs

1. **Host stdio short-read position (F-081, not a library defect).**  With an
   anonymous update stream, `fread(buf, 8, 1, stream)` at a position with only
   5 bytes left returned 5 bytes but a following `ftell()` reported the
   *requested* 8-byte advance (observed at case 297 of an early draft).
   `file::read()` still correctly returned `STDLIB_EOF` / `STDLIB_OK`; the
   wrapper does not promise a byte count, so callers using `ftell()` must
   resynchronise with `seek()` after a short read.  F-081 does that explicitly.
2. **Anonymous temp file retains length after a logical reset (F-081 harness
   bug, fixed).**  An early draft reset its shadow to zero and rewrote from
   offset 0 without truncating the file, so a later EOF check failed.  Fixed by
   sizing the shadow for the whole 1200-case write budget (16 KiB) and never
   logically resetting; no library change.
3. **`string_view` of an unwrapped temporary.**  `s.to_view().size()` does not
   compile in this dialect; the view must be stored in a local first.  All
   configs use locals (F-076 had to be adjusted).
4. **`error` objects are not reset by successful `stdlib_mem_transfer`.**  An
   earlier `error` value therefore remains stale after a successful transfer;
   F-085 decides success from the return code, not a leftover `err.ok()`.  This
   matches `memory.hp` (no `error_reset` in the transfer wrapper).
5. **Compiler FFI/lifetime diagnostics.**  `memset(buf, c, sizeof(buf))` on an
   array passed as an argument is rejected (use a named element count), and
   local `int`/`size_t` out-parameters used later must be initialised.  Fixed
   in the harness sources; no library change.

No library defect was found by F-076 .. F-085.  If one appears later, the repro
is the config ID plus the fixed seed and operation index printed by the
`INVARIANT` line (or the case number in the F-079 UTF-8 diagnostic).
