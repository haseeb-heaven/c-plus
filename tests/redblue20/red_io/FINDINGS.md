# RED-TEAM agent 3/20 — I/O, file, fs and JSON findings

Branch: `agent/modern-stdlib`. Targets: `stdlib/io.hp`, `stdlib/file.hp`,
`stdlib/fs.hp`, `stdlib/json.hp`. Only `tests/redblue20/red_io/` was modified;
no header, source, `git add` or commit.

## Headline

```
RED20 io 3600 0 78 101 3421
```

Columns: `cases crash corrupt unexpected_ok refused`.

| Family | Cases | CRASH | CORRUPT | UNEXPECTED_OK | REFUSED |
|---|---|---|---|---|---|
| `io.hp::read_line` (stdin) | 720 | 0 | 39 | 47 | 634 |
| `file.hp::read_line` | 720 | 0 | 39 | 54 | 627 |
| `file.hp` read/write/close/borrow/temp | 360 | 0 | 0 | 0 | 360 |
| `fs.hp` paths + `fs_mkdtemp` templates | 300 | 0 | 0 | 0 | 300 |
| JSON depth/length budget | 400 | 0 | 0 | 0 | 400 |
| JSON explicit-length parse | 400 | 0 | 0 | 0 | 400 |
| `json_parse_file` | 400 | 0 | 0 | 0 | 400 |
| `json_view` / `json_builder` | 300 | 0 | 0 | 0 | 300 |
| **total** | **3600** | **0** | **78** | **101** | **3421** |

Harness: `harness.cp`; runner: `run.sh`. Fixed SplitMix64 seed
`0x5245443230494F21`, one case per fork, `alarm(4)` watchdog, `0xA5` canaries
before/after every caller buffer plus a check that bytes at index `>= size`
stay untouched, temp files only under `out/tmp`, all case files unlinked by
each child. Classifier non-vacuity control: `SELFCHECK 5 5`. Per-case evidence:
`out/scenarios.log` (3600 lines + header + summary), SHA-256 in
`out/scenarios.log.sha256`. Wall time ~9 s, well inside the 60 s budget.

## Genuine finding 1 — CR at exact fit is misread as overflow; next line is swallowed (CORRUPT)

**Severity:** medium. **78 cases** (`39` in `io.hp`, `39` in `file.hp`;
`sub=3`, `got=-1`, `cls=3`).

`stdlib/io.hp:110-133` and `stdlib/file.hp:120-141`. Both use the same rule:

```c
if (n > 0 && n == size - 1) {
    char last; /* = buf[n-1] */
    if (last != '\n' && !feof(stream)) {
        int c = fgetc(stream);
        ...
        } else if (c != '\n' && c != EOF) {   /* <- c == '\r' handled only as peek */
            while (c != EOF && c != '\n') c = fgetc(stream);
            error_fail_cat(... "line longer than the buffer");
            buf[0] = '\0';
            return STDLIB_FAIL;
        }
    }
}
```

When `fgets()` fills the buffer exactly (`n == size-1`) and the **last byte in
the buffer is `\r`**, the code treats the first byte of the *next* line as
proof of overflow. A CR-terminated line that fits exactly is rejected as
overlong, the buffer is zeroed, and the whole next physical line is drained
and lost. This contradicts the header's own treatment of `\r` as a line
ending (the `size == 1` branch at `io.hp:84-89` / `file.hp:89-94` and the
`c == '\r'` peek branch both consume a lone CR as a terminator).

Exact reproducer (`file.hp::read_line`, 64-byte buffer surrounded by canaries,
`size = 4`, file contents `61 62 0d 63 64 0a` = `"ab\rcd\n"`):

```
F1_CR_EXACT size=4 rc=-1 failed=1
  in : 61 62 0d 63 64 0a    -> line "ab\r" (3 bytes) fits in 4 bytes
  buf: 00 62 0d 00 a5 a5 a5 a5
  rem:                       -> "cd\n" was consumed and lost
```

Expected by the documented line contract: `rc = STDLIB_OK`, `buf = "ab"`,
remaining stream `"cd\n"`. Observed: `STDLIB_FAIL`, `buf[0] = 0`, stream at
EOF. Canaries stayed intact (`0xA5` untouched), so this is classified
`CORRUPT` on **stream/data integrity**, not a buffer overrun.

## Genuine finding 2 — embedded NUL bypasses truncation detection, desynchronising the stream (UNEXPECTED_OK)

**Severity:** medium. **101 cases** (`47` in `io.hp`, `54` in `file.hp`;
`sub=8`, `got=0`, `cls=1`).

Same two functions. `strlen(buf)` is used to compute `n` after `fgets()`.
If the physical line contains an embedded NUL inside the first `size-1`
bytes, `strlen` stops at the NUL, so `n != size-1`; the truncation check is
skipped entirely. The reader reports success with a truncated string and
leaves the remainder of the physical line in the stream, even though the
header promises "The remainder is always drained so the stream stays
aligned" (`io.hp:107-109`, `file.hp:114-119`). A later `read_line()` then
returns the tail of the *same* physical line as if it were a new line.

Exact reproducer (`file.hp::read_line`, `size = 4`, file contents
`61 61 00 62 62 62 0a` = `"aa\0bbb\n"`, 7 bytes > 3-byte capacity):

```
F2_EMBEDDED_NUL size=4 rc=0 failed=0
  in : 61 61 00 62 62 62 0a
  buf: 61 61 00 00 a5 a5 a5 a5   -> "aa" (truncated at the embedded NUL)
  rem: 62 62 62 0a               -> "bbb\n" left in the stream
```

Expected: `STDLIB_FAIL` with category `BOUNDS` and the whole line drained.
Observed: `STDLIB_OK` with no error and a misaligned stream. Byte-level
reproducer source: `probe_findings.cp` (kept beside the deliverables).

## Attacks that produced no finding

* **Truncation / bad handles** (`file.hp`): exact-fit and one-over `read`,
  `read(buf, 0)`, `read(NULL, n)`, zero-initialised/failed-open handle, double
  `close`, `drop` then `close`, borrowed-handle `close` (must not `fclose`
  the caller's `FILE*`), `write_bytes(NULL, n>0)`, invalid `whence`,
  `flush`/`read` after close, `file_temp` round trip — all defined refusals
  or correct operations, guards intact. A deliberately bogus non-NULL
  `FILE*` was **not** counted: the API only promises to reject `NULL`, so a
  wild handle is documented caller UB, not a contract breach.
* **Filesystem** (`fs.hp`): file/dir/missing/NULL paths, `fs_size` -1 + error,
  remove/rename/mkdir edge cases, and `fs_mkdtemp` with size 6 (no NUL room),
  wrong suffix, embedded NUL and a valid `XXXXXX` template — no crash or
  acceptance of an invalid template. (An early harness revision expected
  `fs_mkdtemp("XXXXXX", 7, ...)` to fail; that expectation was wrong — a
  relative 6-X template is valid POSIX `mkdtemp` input — so the test was
  corrected rather than reported as a finding.)
* **JSON depth/length budget**: depths 1, 4096, 16384, 16385, 20000,
  20001+, random; bracket/brace bytes hidden inside strings and behind
  escaped quotes; `NULL`/zero-length input; `SIZE_MAX` and near-`SIZE_MAX`
  lengths; custom small depth budgets — all refused/allowed exactly as
  promised. No depth-budget bypass found.
* **JSON parse / parse_file**: trailing content, leading zeros, trailing
  commas, `NaN`/`Infinity`, unquoted keys, single quotes, hex/plus/dot
  numbers, lone surrogate, invalid UTF-8 (`C3 28`, `E0 80 80`, stray `80`,
  `F0 80 80 80`), embedded NUL, truncated documents, 16000/17000-19000/
  20000-deep files, an oversized sparse file (`cap + 1` bytes via
  `ftruncate`) and a directory path — all refused; valid inputs accepted.
  The previously reported `json_parse_file` budget bypass is fixed in this
  branch and stays fixed on every file case here.
* **JSON view/builder**: `at(SIZE_MAX)`, `at(999999)`, `member(NULL)`,
  missing members, wrong-typed getters, idempotent `drop`, NULL keys,
  non-finite doubles, write/`json_free` — all defined, guards intact.

## Reproduce

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/redblue20/red_io/run.sh
# prints: RED20 io 3600 0 78 101 3421
```

`run.sh` uses `$ROOT/build/c+` by default and honours `CPLUS_BIN` to select a
different compiler binary. During this campaign a sibling agent rebuilt the
C+ compiler concurrently; that new build reports false
`[CPLUS-LIFETIME] field '__u' ... uninitialized` errors inside the SDK's
`math.h` for every translation unit including `stdlib/json.hp`. The final
numbers above were reproduced from this exact harness source with a known-good
compiler via `CPLUS_BIN=... sh tests/redblue20/red_io/run.sh`; no header or
source file in the repository was changed to work around it.
