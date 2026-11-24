# BLUE20 / io — I/O, file and JSON safety proof

Agent 13/20 (blue team). Branch `agent/modern-stdlib`.
Ownership: only `tests/redblue20/blue_io/` was created or modified; no source,
header, build file or git index change, and nothing was staged or committed.

## Scope and target APIs

| Area | Interfaces exercised |
| --- | --- |
| Bounded stream I/O | `cplus_fread_s`, `cplus_fread_s_checked`, `cplus_fwrite_s_checked`, `cplus_fgets_s_checked`, `cplus_fopen_s`, `cplus_fclose_s`, `cplus_io_stream_is_closed` |
| RAII file handle | `file::read`, `file::read_line`, `file::write`, `file::write_bytes`, `file::drop` on a zero-initialised handle |
| JSON text/file | `json`, `json_parse_budget`, `json_parse_file`, `json_view::get_int`, `json::drop` |
| Upstream parser | `yyjson_read_opts` with rotated `YYJSON_READ_NOFLAG`, `YYJSON_READ_STOP_WHEN_DONE`, `YYJSON_READ_ALLOW_TRAILING_COMMAS` flags and a counting `yyjson_alc` |

`yyjson.c` is rebuilt locally in `blue_io/out` with explicit hardening flags:
`-DYYJSON_READER_DEPTH_LIMIT=16384 -DYYJSON_WRITER_DEPTH_LIMIT=16384
-DYYJSON_DISABLE_NON_STANDARD=0 -DYYJSON_DISABLE_UTF8_VALIDATION=0`.
The depth cap is verified at runtime (`YYJSON_READER_DEPTH_LIMIT == 16384`);
16384 nested arrays parse, 16385 are refused by both `json.hp`'s budget and
yyjson itself (`YYJSON_READ_ERROR_DEPTH`), under every rotated read flag.

## 2400 fixed-seed cases

`BLUE_SEED = 0x424C554532303230` (ASCII `BLUE2020`); per-case splitmix64 draws
plus the monotonic id are the only decision inputs. No clock, pid, `getenv`,
`rand()` or random temp name enters a decision. `family = id % 8`, so each
family receives exactly 300 cases:

| Family | Assertion |
| --- | --- |
| 0 | exact-fit `fread_s` reads `cap` bytes; the following read returns 0 at EOF with `feof` set |
| 1 | one-over `fread_s` refuses (`ERANGE` on the caller-bmax path, `EINVAL` on the tracked-allocation path); file position stays at 0 |
| 2 | exact-fit `fwrite_s` writes exactly `cap` bytes; one-over refuses (`ERANGE`) and the file stays empty |
| 3 | `fgets_s` exact fit succeeds; one-over refuses (`ERANGE`) with no buffer write; empty file returns `NULL` and NUL-terminates the buffer |
| 4 | NULL handle, missing file, bad mode, closed `FILE*`, and double `fclose_s` all refuse; zero-initialised `file` fails all reads/writes |
| 5 | truncation, corruption, trailing junk, bad escape and whitespace-only JSON are refused by `json`, `json_parse_budget` and raw `yyjson_read_opts` |
| 6 | depth 16384 accepts; depth 16385 refuses with `STDLIB_CATEGORY_RANGE` / `CPLUS_EXIT_BOUNDS` and yyjson `DEPTH`; all read-flag variants agree |
| 7 | `json_parse_file` accepts valid, refuses truncated, empty and missing files; the file descriptor count returns to baseline on every path |

Every case forks a child with `alarm(60)` and `_exit(124)` on `SIGALRM`; the
parent polls `waitpid(WNOHANG)` against a `CLOCK_MONOTONIC` 60 s deadline,
SIGKILLs/reaps on expiry and classifies exit 0 as pass, 1 as assertion
failure, 124 as timeout and any signal as crash. `run.sh` adds a global
`timeout 300` bound. All I/O and JSON buffers are heap allocations with 32-byte
`0x5A` guard bands on both sides; both bands and the declared body are checked
after each operation. Each case checks its open-fd count (fd 0..255) and the
SafeCRT live-allocation count return to baseline, unlinks its fixture, and
calls `json::drop()` twice on both success and failure paths. The yyjson
counting allocator returns to zero live blocks after every refused parse and
after every `yyjson_doc_free`.

## Exact commands

From the repository root:

```sh
sh tests/redblue20/blue_io/run.sh
```

The script rebuilds `out/libyyjson_hardened.a`, compiles `proof.cp` with
`./build/c+ build ... --default --sanitize=none` (strict mode currently rejects
pre-existing header internals in `stdlib/json.hp` / `yyjson.h`), then runs the
same binary twice with different scratch directories and compares the case
logs byte-for-byte. Artifacts and logs are confined to `blue_io/out/`.

## Exact results

Filled in from the verified `run.sh` execution at the end of this record.

## Honest limits

This is evidence for this fixed corpus, this compiler/runtime build and this
host. It is not a formal proof of safety for arbitrary inputs, host libc
behaviour or filesystems not exercised here. The SafeCRT violation trap is
disabled inside each child so documented refusals are observable as return
values; that is an observation choice, not a library change.
