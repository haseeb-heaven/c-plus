# BLUE20 / io — I/O, file and JSON safety proof

Agent 13/20 (blue team). Branch `agent/modern-stdlib`.
Ownership: only `tests/redblue20/blue_io/{proof.cp,run.sh,PROOF.md}` and its
gitignored `out/` artifacts were created or modified. No source, header, build
file or git index change; nothing was staged or committed.

## Scope and target APIs

| Area | Interfaces exercised |
| --- | --- |
| Bounded stream I/O | `cplus_fread_s`, `cplus_fread_s_checked`, `cplus_fwrite_s_checked`, `cplus_fgets_s_checked`, `cplus_fopen_s`, `cplus_fclose_s`, `cplus_io_stream_is_closed` |
| RAII file handle | `file::read`, `file::read_line`, `file::write`, `file::write_bytes`, `file::drop` on a zero-initialised handle |
| JSON text/file | `json`, `json_parse_budget`, `json_parse_file`, `json_view::get_int`, `json::drop` |
| Upstream parser | `yyjson_read_opts` with `YYJSON_READ_NOFLAG`, `YYJSON_READ_STOP_WHEN_DONE`, `YYJSON_READ_ALLOW_TRAILING_COMMAS` and a counting `yyjson_alc` |

`yyjson.c` is rebuilt locally in `blue_io/out` with explicit hardening flags:

```
-DYYJSON_READER_DEPTH_LIMIT=16384 -DYYJSON_WRITER_DEPTH_LIMIT=16384
-DYYJSON_DISABLE_NON_STANDARD=0 -DYYJSON_DISABLE_UTF8_VALIDATION=0
```

The same `-DYYJSON_*` flags are passed to the C+ proof build, so the archive
and its headers agree; the driver asserts `YYJSON_READER_DEPTH_LIMIT == 16384`
is in effect (the checked-in `build/ext/libyyjson.a` uses 0, i.e. no upstream
cap). 16384 nested arrays parse; 16385 are refused by `json.hp`'s budget and
by yyjson itself (`YYJSON_READ_ERROR_DEPTH`) under every rotated read flag.

## 2400 fixed-seed cases

`BLUE_SEED = 0x424C554532303230` (ASCII `BLUE2020`); per-case splitmix64 draws
plus the monotonic id are the only decision inputs. No clock, pid, `getenv`,
`rand()` or random temp name enters a decision. `family = id % 8`, so each
family receives exactly 300 cases:

| Family | Assertion | Result |
| --- | --- | --- |
| 0 | exact-fit `fread_s` reads `cap` bytes; the next read returns 0 at EOF with `feof` set | 300 pass |
| 1 | one-over `fread_s` refuses (`ERANGE` caller-bmax path, `EINVAL` tracked-allocation path); file position stays 0 | 300 pass |
| 2 | exact-fit `fwrite_s` writes exactly `cap` bytes; one-over refuses (`ERANGE`) and the file stays empty | 300 pass |
| 3 | `fgets_s` exact fit succeeds; one-over refuses (`ERANGE`) with no buffer write; empty file returns `NULL` and NUL-terminates the buffer | 300 pass |
| 4 | NULL handle, missing file, bad mode, closed `FILE*`, double `fclose_s` and a zero-initialised `file` all refuse | 300 pass |
| 5 | truncation, corruption, trailing junk, bad escape and whitespace-only JSON are refused by `json`, `json_parse_budget` and `yyjson_read_opts` | 300 pass |
| 6 | depth 16384 accepts; depth 16385 refuses with `STDLIB_CATEGORY_RANGE` / `CPLUS_EXIT_BOUNDS` and yyjson `DEPTH`; all rotated flags agree | 300 pass |
| 7 | `json_parse_file` accepts valid, refuses truncated, empty and missing files; descriptor count returns to baseline on every path | 300 pass |

Every case forks a child with `alarm(60)` and `_exit(124)` on `SIGALRM`; the
parent polls `waitpid(WNOHANG)` against a `CLOCK_MONOTONIC` 60 s deadline,
SIGKILLs/reaps on expiry and classifies exit 0 as pass, 1 as assertion
failure, 124 as timeout and any signal as crash. `run.sh` adds a 400 s
process-wide bound. All I/O and JSON buffers are heap allocations with 32-byte
`0x5A` guard bands on both sides; both bands and the declared body are checked
after each operation. Each case verifies its open-fd count (fd 0..255) and the
SafeCRT live-allocation count return to baseline, unlinks its fixture, and
calls `json::drop()` twice on success and failure paths. The yyjson counting
allocator returns to zero live blocks after every refused parse and after
every `yyjson_doc_free`.

The SafeCRT violation trap is disabled inside each child so documented
refusals are observable as return values; that is an observation choice, not
a library change. The 1260 `[CPLUS-SAFE] ...` lines in stderr are exactly
those deliberate refusal diagnostics; there were zero `C+ runtime error`
lines and zero timeouts or crashes.

## Exact command

From the repository root:

```sh
sh tests/redblue20/blue_io/run.sh
```

The script rebuilds `out/libyyjson_hardened.a`, compiles `proof.cp` with
`./build/c+ build ... --default --sanitize=none` (strict mode currently
rejects pre-existing header internals in `stdlib/json.hp` / `yyjson.h`),
then runs the same binary twice with different scratch directories and
compares the case logs byte-for-byte. Artifacts and logs are confined to
`blue_io/out/`.

## Exact results — verified run

`sh tests/redblue20/blue_io/run.sh` exited 0 in 89.27 s wall time; two
independent bounded runs both exited 0.

```
BLUE20 io 2400 2400 0
cases=2400 passed=2400 failed=0 rc1=0 rc2=0
digest=DIGEST b148d8fdab45b430
0b07d2ba7a5f8071a4ebd290380ea087f712f4c9ac8741b3c0cd7f8b2886be4d  /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/redblue20/blue_io/out/cases.log
```

- Cases: **2400**, passed: **2400**, failed: **0**, timeouts: **0**, crashes: **0**.
- Case log: 2400 `C <id> <family> <rc> <digest>` lines plus one `DIGEST` line.
  Per-family rc histogram: 300 zeros in each of families 0..7.
- Both runs produced byte-identical logs (`cmp -s` succeeded); aggregate digest
  `b148d8fdab45b430` and log SHA-256 above are stable across runs.
- First-run stdout/stderr: `out/run1.stdout`, `out/run1.stderr`;
  second: `out/run2.stdout`, `out/run2.stderr`; captured wrapper output:
  `out/final.stdout`, `out/final.stderr`.

## Negative control (harness is not vacuous)

A one-line fault-injection build (`-DBLUE_NEGATIVE`, case 0 forced to fail)
reported:

```
BLUE20 io 2400 2399 1
```

with `C 0 0 1 ...` as the first log line and process exit status 1. The
wrapper would therefore reject a failed case rather than report a false pass.

## Honest limits

This is evidence for this fixed corpus, this compiler/runtime build and this
host. It is not a formal proof of safety for arbitrary inputs, host libc
behaviour or filesystems not exercised here. The checked-in yyjson archive has
no reader depth cap; this proof deliberately rebuilds yyjson with the
compile-time caps listed above and proves the combined budget behavior. With
the stock archive, depth 16385 is still refused by `stdlib/json.hp` (verified
during development), but raw `yyjson_read_opts` alone would accept it.
