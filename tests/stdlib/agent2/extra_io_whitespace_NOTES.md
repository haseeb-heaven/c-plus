# extra_io_whitespace — remaining UNTESTED `stdlib/io.hp` paths (agent 2)

## Owned files

- `tests/stdlib/agent2/extra_io_whitespace.cp`
- `tests/stdlib/agent2/extra_io_whitespace.stdin`
- `tests/stdlib/agent2/extra_io_whitespace_NOTES.md` (this file)

No header, existing test, runner, or other agent file was modified. No
`git add` / commit was performed.

## Scope: only the listed gaps, not a repeat of the deep io tests

`deep_io_1byte.cp` / `deep_io_token.cp` / `deep_io_task2.cp` already pin the
1-byte destination, `size > INT_MAX`, exact-fit vs overlong with stream
alignment, CRLF at the `fgets` boundary, the 70-digit token, the numeric
overflow/underflow/malformed/non-finite families, alloc-failure no-op, and
final EOF. This fixture deliberately targets the remaining paths:

| Group | Required case | Tags |
|---|---|---|
| A | **empty stdin and immediate EOF for every reader** (fresh empty stream per reader) | `a_line_eof`, `a_int_eof`, `a_long_eof`, `a_float_eof`, `a_double_eof`, `a_token_eof` |
| B | **whitespace-only input** (token readers hit EOF; `read_line` preserves spaces) | `b_int_ws_eof`, `b_token_ws_eof`, `b_line_ws`, `b_line_ws_next_eof` |
| C | **leading whitespace**, **trailing whitespace**, **explicit `+`**, **negative**, **INT_MIN/INT_MAX and ±1**, LONG_MIN/LONG_MAX, **decimal into integer parser**, **numeric token with trailing characters** | `c_lead_ws`, `c_trail_ws`, `c_plus`, `c_neg`, `c_i_min`, `c_i_max`, `c_i_max_1`, `c_i_min_1`, `c_l_min`, `c_l_max`, `c_dec_i`, `c_trail_chars` |
| D | **very long numeric token** — exact 63/64-byte internal-token boundary and a 200-digit token, alignment after each | `d_63_ok`, `d_64_fail`, `d_aligned`, `d_huge`, `d_huge_next` |
| E | **repeated reads after malformed input** (fail, fail, ok, fail, ok) | `e_bad1`, `e_bad2`, `e_good`, `e_bad3`, `e_good2` |
| F | **repeated reads after a truncated LINE — exactly which bytes come next**, including the one leftover whitespace byte after a token read and truncation at physical EOF | `f_trunc1`, `f_next1`, `f_trunc2`, `f_next_token`, `f_next_bytes`, `f_after2`, `f_trunc3`, `f_empty_after`, `f_tail_after`, `f_eof_trunc`, `f_eof_next` |
| G | **two-byte buffer boundary** (1-char, empty, overlong, CR terminator) | `g_one_char`, `g_empty`, `g_over`, `g_next`, `g_cr`, `g_cr_next` |
| H | **multiple consecutive empty lines** (line reader + token reader) | `h_empty1`…`h_empty4`, `h_end`, `h_token_after_empties`, `h_eof_line`, `h_eof_int` |

**Output parameters byte-for-byte unchanged after every failure** is asserted
with `memcmp` against immutable snapshots for all four numeric readers on
every non-success (EOF, `INVALID`, `RANGE`, token-too-long) — e.g.
`c_i_max_1`, `c_dec_i`, `d_64_fail`, `e_bad1`, `a_*_eof`. For `read_line` the
only writes the contract permits are: none on `INVALID`/`>INT_MAX`/EOF
(asserted via full-buffer `memcmp` in `a_line_eof`, `h_eof_line`,
`b_line_ws_next_eof`, `f_eof_next`), and a single NUL at byte 0 on a
recoverable BOUNDS truncation (asserted together with an untouched byte past
the passed size in `f_trunc*`, `g_over`, `f_eof_trunc`).

`f_next_bytes` is the sharpest stream-position check: after `read_int`
consumes `  42  ` it has eaten **one** trailing space, so the next
`read_line` returns the single leftover `" "` (from `" \n"`), and only the
read after that returns `AFTER2`. That pins the exact byte boundary rather
than merely asserting "the next line is correct".

`stdlib_io_token` is also called directly at immediate EOF (`a_token_eof`)
and on whitespace-only input (`b_token_ws_eof`): it returns `1` and performs
**no** write, so a fully sentinel-filled buffer is unchanged byte for byte.

## Fixture

`extra_io_whitespace.stdin` is 563 bytes / 44 lines, generated deterministically
(No randomness, no timestamps). It contains the exact 63-byte and 64-byte
tokens (61 and 62 leading zeros + `42`), a 200-digit token, a literal `0x0D`
in `w\r\n` for the two-byte CR-boundary case, and the overlong/truncated line
sequence. `od -c` verified the CR byte and the consecutive empty lines.

## Exact verification commands (run from the repository root)

`ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`,
`TMPD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/extra_io_ws`,
`PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`.

Mode order is the requested one: **STRICT `--sanitize=none` first**, then
default, then UBSan. Each build and each run is wrapped in `timeout 30`, and
each run consumes the `.stdin` fixture. Flags are separate argv operands
(quoting them as one string fails with
`clang: error: unsupported option '--strict --sanitize=none'`).

```sh
# 1) STRICT --sanitize=none (FIRST)
timeout 30 build/c+ build tests/stdlib/agent2/extra_io_whitespace.cp \
  --strict --sanitize=none -o "$TMPD/strict_none"
timeout 30 "$TMPD/strict_none" \
  < tests/stdlib/agent2/extra_io_whitespace.stdin > "$TMPD/strict_none.out"

# 2) DEFAULT
timeout 30 build/c+ build tests/stdlib/agent2/extra_io_whitespace.cp \
  -o "$TMPD/default"
timeout 30 "$TMPD/default" \
  < tests/stdlib/agent2/extra_io_whitespace.stdin > "$TMPD/default.out"

# 3) UBSan
timeout 30 build/c+ build tests/stdlib/agent2/extra_io_whitespace.cp \
  --strict --sanitize=undefined -o "$TMPD/ubsan"
timeout 30 "$TMPD/ubsan" \
  < tests/stdlib/agent2/extra_io_whitespace.stdin > "$TMPD/ubsan.out"
```

Harness check (isolated results dir so `--clean` does not touch other agents'
artifacts):

```sh
AGENT2_RESULTS="$TMPD/results" \
  sh tests/stdlib/agent2/run_all.sh --test extra_io_whitespace --clean
# -> PASS=1 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=1
# harness exit 0
```

## Per-mode results

| Mode | Build rc (stderr bytes) | Run rc (stderr bytes) | Final stdout line | Verdict |
|---|---|---|---|---|
| strict / `--sanitize=none` (first) | 0 (0) | 0 (0) | `EXTRA_IO_WHITESPACE 57 0` | PASS |
| default | 0 (0) | 0 (0) | `EXTRA_IO_WHITESPACE 57 0` | PASS |
| strict / `--sanitize=undefined` (UBSan) | 0 (0) | 0 (0) | `EXTRA_IO_WHITESPACE 57 0` | PASS |

All three stdout files are byte-identical (`diff` clean). 58 stdout lines:
57 `<tag> <0|1>` check lines, every one `1`, then the final line. Exit 0 in
every mode; zero stderr bytes in every mode; no build warnings after replacing
the string-literal `fmemopen` arguments with mutable arrays.

**Fixture negative control:** running the same strict binary with
`< /dev/null` gives `EXTRA_IO_WHITESPACE 57 43` and exit 1 — 43 checks fail
without the fixture, confirming the fixture is genuinely exercised rather
than decorative.

## Exact stdout (all three modes, byte-for-byte)

```
c_lead_ws 1
c_plus 1
c_neg 1
c_trail_ws 1
c_i_min 1
c_i_max 1
c_i_max_1 1
c_i_min_1 1
c_l_min 1
c_l_max 1
c_dec_i 1
c_trail_chars 1
d_63_ok 1
d_64_fail 1
d_aligned 1
d_huge 1
d_huge_next 1
e_bad1 1
e_bad2 1
e_good 1
e_bad3 1
e_good2 1
f_trunc1 1
f_next1 1
f_trunc2 1
f_next_token 1
f_next_bytes 1
f_after2 1
f_trunc3 1
f_empty_after 1
f_tail_after 1
g_one_char 1
g_empty 1
g_over 1
g_next 1
g_cr 1
g_cr_next 1
h_empty1 1
h_empty2 1
h_empty3 1
h_empty4 1
h_end 1
h_token_after_empties 1
h_eof_line 1
h_eof_int 1
a_line_eof 1
a_int_eof 1
a_long_eof 1
a_float_eof 1
a_double_eof 1
a_token_eof 1
b_int_ws_eof 1
b_token_ws_eof 1
b_line_ws 1
b_line_ws_next_eof 1
f_eof_trunc 1
f_eof_next 1
EXTRA_IO_WHITESPACE 57 0
```

## Observed defects / findings

**No defect was found in `stdlib/io.hp` or the C+ runtime.** Every listed
path behaves as documented in `docs/stdlib/DEEP_IO_CONTRACT.md` §3 and
`DEEP_IO_TASK2_CONTRACT.md` §1; no header was changed. Three test-side
observations are recorded so they are not reintroduced:

1. **A one-past sentinel must be beyond the size passed to `read_line`, not
   beyond the array.** `fgets(buf, n, …)` writes its NUL at `buf[n-1]`, so a
   `char b[6]` passed as `read_line(b, 6, …)` has its `b[5]` sentinel
   clobbered. Minimal repro (initial version of this fixture):

   ```c++
   char b[6]; memset(b, 'Z', sizeof(b));
   read_line(b, 6, &err);   /* overlong line -> fgets writes NUL at b[5] */
   /* b[5] == '\0', not 'Z': the sentinel was INSIDE the passed size */
   ```

   Fix in the test only: `char b[7]`, pass size `6`, check `b[6] == 'Z'`.
   Initial symptom was `EXTRA_IO_WHITESPACE 57 4` (exactly the four
   truncation checks) in strict mode. No library change.

2. **`fmemopen(buf, 0, "r")` returns NULL on this macOS toolchain**, so it
   cannot be used to build an "empty stdin" stream. Minimal repro:

   ```c
   char buf[1] = {'Z'};
   FILE *f = fmemopen(buf, 0, "r");   /* f == NULL on macOS arm64 here */
   ```

   Fix in the test only: use `tmpfile()` (a genuinely empty stream) for
   group A, and `fmemopen` only with non-zero sizes (groups B and F2).

3. **On a BOUNDS truncation `read_line` writes NUL at byte 0 but leaves the
   partial `fgets` bytes in bytes 1..`size-2`** (e.g. `abcdefgh` into a
   6-byte buffer leaves `"\0bcde"` in `buf[0..4]`). This is the contract
   documented in `DEEP_IO_CONTRACT.md` §3 point 2, not a defect; callers
   must read the destination as a NUL-terminated string. The fixture asserts
   the documented byte-0 write and the untouched one-past byte, and does not
   overfit the stale tail bytes.
