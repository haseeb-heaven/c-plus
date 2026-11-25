# `extra_file_binary` — verification notes

## Scope and ownership

| Item | Value |
| --- | --- |
| Test source | `tests/stdlib/agent2/extra_file_binary.cp` |
| Notes | `tests/stdlib/agent2/extra_file_binary_NOTES.md` |
| Branch / date | `agent/modern-stdlib`, 2026-11-23 |
| Driver | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+` |
| Backend | `CPLUS_CLANG=/usr/bin/clang` exported for every invocation |
| Host | macOS arm64 (`arm64-apple-darwin`), Apple clang 16 |
| Time bounds | `timeout 30` on **every** build and **every** run |
| Run cwd | `/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/efb/bin/run_<mode>` (outside the repository) |
| Stdin | `/dev/null` (no stdin fixture; all inputs are file fixtures) |

Only the two files above were created. No header, existing test, expectation
fixture, or results artifact was modified. No git add/commit was performed.

The test closes the assigned remaining `file.hp` paths: binary data with NUL
bytes via `write_bytes`/`read` at exact length; single-byte file; empty line and
three consecutive empty lines; LF + CRLF in one file; final line without a
newline; partial reads; `read`/`read_line` after EOF; write to a read-only file;
`SEEK_SET`/`SEEK_END`/`SEEK_CUR` (positive and negative offsets); `flush` on a
write stream; close once/twice; drop once/twice; close→drop and drop→close;
borrowed handle left open by both explicit `drop()` and compiler-inserted scope
destruction (probed with `fgetc`); 50 open/close cycles with an `/dev/fd` count
check; resource cleanup after a write failure (fd count restored, on-disk
content unchanged, named file removed and absence re-probed). All named fixtures
use the process PID in their names and removal is asserted.

## Exact commands and per-mode results

Modes were executed in the required order: **STRICT `--sanitize=none` first**,
then default, then UBSan.

```sh
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/efb/bin
CPLUS=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
SRC=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/agent2/extra_file_binary.cp
export CPLUS_CLANG=/usr/bin/clang

# 1. STRICT, no sanitizer
timeout 30 $CPLUS build $SRC --strict --sanitize=none -o $OUT/extra_file_binary__strict_none
( cd $OUT/run_strict_none && timeout 30 $OUT/extra_file_binary__strict_none < /dev/null )

# 2. default build (no mode/sanitizer flags)
timeout 30 $CPLUS build $SRC -o $OUT/extra_file_binary__default
( cd $OUT/run_default && timeout 30 $OUT/extra_file_binary__default < /dev/null )

# 3. UBSan in strict mode
timeout 30 $CPLUS build $SRC --strict --sanitize=undefined -o $OUT/extra_file_binary__ubsan
( cd $OUT/run_ubsan && timeout 30 $OUT/extra_file_binary__ubsan < /dev/null )
```

| Mode | Exact flags | Build exit | Run exit | Build stdout/stderr | Run stderr | Result line |
| --- | --- | --- | --- | --- | --- | --- |
| `strict_none` | `--strict --sanitize=none` | 0 | 0 | empty / empty | empty | `EXTRA_FILE_BINARY 81 0` |
| `default` | *(none)* | 0 | 0 | empty / empty | empty | `EXTRA_FILE_BINARY 81 0` |
| `ubsan` | `--strict --sanitize=undefined` | 0 | 0 | empty / empty | empty | `EXTRA_FILE_BINARY 81 0` |

No build or run timed out. No sanitizer diagnostic was emitted in any mode.
The test is self-checking: 81 checks, 0 failures, process exit 0.

**Determinism:** three consecutive default-mode runs produced byte-identical
stdout (`shasum` / SHA-1 of the 81-line output is
`46e083fade76642a476abf9d4f357ae040b23210`), empty
stderr, and exit 0 each time. The strict_none, default, and ubsan stdout files
all hash to the same value. No `cplus_efb_*` file remained in any run cwd.

## Exact stdout (identical in all three modes)

```
binary-open-w 1
binary-write 1
binary-close-w 1
binary-read-exact 1
binary-eof 1
binary-cleanup 1
single-open 1
single-write 1
single-seek 1
single-read 1
single-eof 1
empty-setup 1
empty-seed 1
empty-line-1 1
empty-line-2 1
empty-line-3 1
empty-lines-eof 1
mixed-setup 1
mixed-seed 1
mixed-lf 1
mixed-crlf 1
mixed-last 1
mixed-eof 1
tail-setup 1
tail-seed 1
final-no-nl 1
final-no-nl-eof 1
partial-setup 1
partial-seed 1
partial-first 1
partial-rest 1
partial-eof 1
read-after-eof 1
readline-after-eof 1
write-ro-seed-open 1
write-ro-open 1
write-ro-bytes 1
write-ro-string 1
write-ro-content 1
write-ro-cleanup 1
seek-setup 1
seek-seed 1
seek-set 1
seek-end 1
seek-end-neg 1
seek-cur-plus 1
seek-cur-neg 1
flush-open 1
flush-write 1
flush-stream 1
flush-close 1
flush-verify 1
flush-cleanup 1
close-once-open 1
close-once 1
close-twice 1
drop-once-open 1
drop-once 1
drop-twice 1
close-drop-open 1
close-drop-close 1
close-then-drop 1
drop-close-open 1
drop-then-close 1
borrow-b1-open 1
borrow-drop-still-open 1
borrow-b2-open 1
borrow-scope-still-open 1
borrow-manual-close 1
cycle-baseline 1
cycle-opens 1
cycle-closes 1
cycle-no-fd-leak 1
cycle-cleanup 1
writefail-open 1
writefail-status 1
writefail-drop 1
writefail-fd-restored 1
writefail-content 1
writefail-cleanup 1
writefail-cleanup-absent 1
EXTRA_FILE_BINARY 81 0
```

stderr is empty in every mode. The final line is the required
`EXTRA_FILE_BINARY <checks> <failures>` record; exit is 0 only for zero failures.

## Observed failure during authoring — test bug, not a header defect

The first STRICT `--sanitize=none` run reported `mixed-last 0` and
`EXTRA_FILE_BINARY 81 1`. Minimal repro (the original fixture line):

```c
file efb_mixed = file_temp(&efb_err);
efb_mixed.write_bytes("lf\ncrlf\r\ntail\n", 12, &efb_err);  /* WRONG length */
efb_mixed.seek(0, SEEK_SET, &efb_err);
efb_mixed.read_line(buf, sizeof(buf), &efb_err); /* "lf"    */
efb_mixed.read_line(buf, sizeof(buf), &efb_err); /* "crlf"  */
efb_mixed.read_line(buf, sizeof(buf), &efb_err); /* "tai"   -- not "tail" */
```

Root cause: `"lf\ncrlf\r\ntail\n"` is **14** bytes (`3 + 6 + 5`), but the fixture
passed `12`, so `write_bytes` correctly wrote only the first 12 bytes
(`"lf\ncrlf\r\ntai"`). `read_line` then returned the final unterminated line
`"tai"`, so the `strcmp(buf, "tail")` assertion failed. This is an error in the
test fixture length, not in `file.hp`: `write_bytes` wrote exactly the requested
byte count and `read_line` returned exactly the bytes that were present. Fix
applied (no header edited):

```c
efb_mixed.write_bytes("lf\ncrlf\r\ntail\n", 14, &efb_err);  /* corrected */
```

After the fix all three modes report 0 failures. **No defect in `stdlib/file.hp`
was reproduced by this assignment.**

## Limitations / claim boundary

- These are behavioural, in-process checks of the documented contract. Passing
  them is evidence of conformance for the listed paths only; it is not a
  proof of memory safety or of all branches in `file.hp`. ASan is unavailable
  on this host, so no address-safety claim is made.
- `read()` returns `STDLIB_OK` for any short read and has no out-count
  parameter (documented `file.hp` limitation). The partial-read checks verify
  the expected prefix by `memcmp` rather than relying on an unavailable count.
- `close()` clears the handle even if `fclose` fails, and `file_temp()` failure
  is not injectable; neither path is exercised here.
- The borrowed-handle checks confirm the wrapper's `drop()` and the
  compiler-inserted scope destructor leave `owns == 0` streams open. They do
  not replace the separate compile-time resource/copy rules verified elsewhere.
- The fd-leak check uses the `/dev/fd` directory count (as in
  `deep_file_lifecycle.cp`); it observes descriptor balance, not a general leak
  analysis.
