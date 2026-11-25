# stdio adversarial attack — findings

Target branch: `agent/modern-stdlib` (C+ hardened libc + SafeCRT).
Test: `tests/libc/test_stdio_attack.c` (standalone C99, `main` returns 0,
prints `stdio_attack: ok`, clears all temp files, < 5 s).

## What was attacked and what held

All 8 requested scenarios are covered and **pass** (no FAIL lines):

1. **Overflow products** — `cplus_fread(buf, SIZE_MAX, SIZE_MAX, f)` and
   `cplus_fwrite(buf, SIZE_MAX, 2, f)` both return `0` items immediately with
   `errno == EOVERFLOW` (guard `nmemb > SIZE_MAX / size`); the 16-byte buffer
   stayed at its `0x5A` sentinel and neither call hung. Zero-dimension edges
   (`size==0`, `nmemb==0`) also return 0. Checked `cplus_fread_s/fwrite_s`
   with `bmax=16, size=SIZE_MAX, count=SIZE_MAX` clamp count to 0 and touch
   no memory.
2. **NULL arguments (guarded APIs only)** — `cplus_fopen`, `cplus_fopen_s`,
   `cplus_fread`, `cplus_fwrite`, `cplus_fclose`, `cplus_fclose_s`,
   `cplus_fread_s`, `cplus_fwrite_s`, `cplus_fprintf_s`, `cplus_snprintf_s`,
   `cplus_printf`, `cplus_fprintf` all return the documented error with no
   crash (`EINVAL`/`EOF`/`NULL`). Unguarded raw APIs are **not** invoked with
   NULL and are documented in-file as unavoidable raw C UB.
3. **Partial reads** — 5-byte file, request 10 items of size 1 → count 5,
   `feof` set, `ferror` clear, `buf[5..]` untouched; `clearerr` resets flags.
   Multi-byte: 5 bytes, request 10×2-byte items → count 2, `feof` set, bytes
   beyond the indeterminate partial element untouched.
4. **EOF/error state** — two reads past EOF keep `feof` set with `ferror`
   clear; `clearerr` resets both; EOF reasserts on next read. Checked
   `cplus_fwrite_s` to an `"r"` stream returns 0 and sets `ferror` (defined
   short write).
5. **Double close** — single-stream `cplus_fclose_s` double close returns
   `EOF` with `errno == EINVAL`, no crash. Raw `cplus_fclose` is never
   double-invoked (raw C UB).
6. **Format/truncation** — `fprintf_s`/`fprintf_s_checked` with NULL stream →
   `-1`/`EINVAL`; non-literal fmt and `%n` → `-1`; legitimate `snprintf_s`
   truncation returns would-be length 8 and NUL-terminates; `dmax` larger than
   the real object capacity is refused with `-1`. Violation trap disabled in
   test so violations return instead of `_exit(70)`.
7. **Resource cleanup** — 100 tmpfiles opened and closed; `/dev/fd` count
   returns to baseline (no leak) on the pristine-cache batch.
8. **Logging on/off** — failure return values are byte-for-byte identical with
   libc logging + safe logging OFF and at TRACE.

Build: exact verify command, **zero warnings**. UBSan
(`-fsanitize=undefined`) run: **no sanitizer diagnostics**.

## FINDING 1 — `cplus_fclose` falsely rejects a valid first close and leaks the fd

**Location:** `libc/src/stdio/stdio.c` (`cplus_fclose`,
`cplus_stdio_is_closed`, `cplus_stdio_mark_closed`).

**Cause:** the double-close detector's `g_closed_streams[256]` cache is keyed
only on the `FILE*` **address**. `cplus_fclose` refuses any stream that is not
in `g_live_streams` but whose address is in the closed cache. `cplus_fopen`
registers live streams (and unmarks closed ones), but `cplus_tmpfile`,
`cplus_fdopen`, `cplus_freopen` and raw `fopen` do **not** register. When the
allocator hands such a stream an address previously closed via `cplus_fclose`,
a legitimate first close is misdiagnosed as a double close.

**Exact behavior (deterministic, reproduced every run):**
```
a = cplus_tmpfile()            -> 0x1f6cca380, fd count 5
cplus_fclose(a)                -> 0,       fd count 4   (frees FILE + fd)
b = cplus_tmpfile()            -> 0x1f6cca380 (same address reused), fd count 5
cplus_fclose(b)                -> -1 (EOF), errno = 9 (EBADF), fd count 5  (LEAK)
fclose(b) [host]               -> 0,       fd count 4   (proves fd was leaked)
```
The in-test demonstrator `attack_close_reuse()` consistently prints:
`fresh b=... first close(b)=-1 errno=9 fd 5->5` then `host fclose(b)=0 fd 5->4`,
and emits a `FINDING:` line. Under scenario 7-style churn, a single 100-file
batch that reuses any previously closed address produces up to 100 `EBADF`
close failures and a proportional fd leak.

**Impact:** fd/resource leak and spurious `EOF`/`EBADF` on valid streams; any
caller that opens streams via `tmpfile`/`fdopen`/`freopen` and closes via
`cplus_fclose` is affected.

**Suggested fix:** track liveness by object identity, not a raw-address cache
that outlives the object; unregister a stream from the closed cache whenever a
new `FILE*` is created at that address (all creation paths, not just
`cplus_fopen`), or verify the pointer is still a live stream before refusing.

## FINDING 2 — `cplus_fclose_s` double-close protection defeated by hash collision (UAF risk)

**Location:** `stdlib/safe/src/safe_io.c` (`cplus_fclose_s`, `hash_stream`,
`g_live_streams[512]` / `g_closed_streams[512]`).

**Cause:** each cache slot holds a **single** `FILE*` with **no collision
resolution**. Two different streams that hash to the same 512-slot index
overwrite each other's live/closed state. Closing stream A then stream B
evicts A's "closed" marker from the shared slot; a second `cplus_fclose_s(A)`
is no longer recognized and falls through to `fclose(A)` on an already-freed
`FILE*` (use-after-free).

**Exact behavior (throwaway probe, 6000 `cplus_fopen_s` streams):**
```
A=0x7812c0c278 B=0x78128021e0 hash=474 (B opened after A)
1st close_s(A) = 0, errno 0
1st close_s(B) = 0, errno 0
2nd close_s(A) = -1, errno 9 (EBADF)   <-- expected EOF with errno 22 (EINVAL)
```
A correct double-close refusal (no collision) returns `-1` with
`errno == EINVAL` (22), as asserted in the test. Here the second close instead
reaches host `fclose()` on a freed pointer — undefined behavior that happens
to return `EBADF` on this host but is a genuine UAF and could crash under a
different allocator or ASan.

**Impact:** the checked close's advertised "double-close protection" is not
reliable; colliding streams can trigger a second `fclose` on freed memory.

**Suggested fix:** use collision chaining/open addressing, or store a generation
token, so each live/closed stream identity is tracked independently.

## Observations (not asserted as defects)

- `cplus_fwrite_s` / `cplus_fread_s` clamp `count = bmax / size` and divide by
  `size` only after a `size == 0` guard, so no divide-by-zero. They do not
  validate stream **mode**; writing to an `"r"` stream yields the C-defined
  short write (0) + `ferror`, which is acceptable but not a dedicated guard.
- `fread` may write the leftover bytes of a partial multi-byte element into the
  destination (C leaves those bytes indeterminate); the test only asserts the
  region beyond the partial element is untouched.
- The SafeCRT violation trap defaults to on (`_exit(70)`); tests that inspect
  defined error returns must call `cplus_runtime_set_violation_trap(0)` (done).
- Raw, unguarded `cplus_*` stdio wrappers delegate straight to the host C
  runtime; NULL/malformed pointers there are raw C UB and a wrapper cannot
  make them safe. They were deliberately not probed with invalid pointers.

## Temp-file hygiene

The committed test removes every named temp file it creates and relies on
`tmpfile()`'s auto-unlink; host `fclose()` is used only to reclaim the fd that
`cplus_fclose` leaks in the finding demonstrator. Probe scratch files under
`/tmp/cps_col*` were removed after investigation.
