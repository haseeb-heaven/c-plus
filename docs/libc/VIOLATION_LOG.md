# Bounded runtime-violation diagnostic log (`cplus_violation_log_*`)

## Purpose and scope

Make runtime safety events *diagnosable* without changing the existing
fatal/trap ABI. This work is **additive**: the existing fatal and trap symbols
keep their exact signatures and behaviour. A caller may *additionally* route a
description of a violation into an opt-in log file.

New files (the only files touched by this change):

| File | Role |
| --- | --- |
| `libc/src/runtime/violation_log.c` | implementation |
| `libc/install-include/cplus_violation_log.h` | public declarations |
| `tests/libc/test_violation_log.c` | standalone C99 proof |
| `docs/libc/VIOLATION_LOG.md` | this document |

No `runtime/**`, compiler, `libc/CMakeLists.txt`, or build-script core files
were modified.

## Survey of the existing safety ABI (with `nm`)

Before adding anything, the existing fatal/trap entry points were located with
`nm`. They are unchanged by this work:

```
$ nm -g --defined-only build/libcplus_runtime.a | grep -E 'cplus_fatal|cplus_check_bounds|cplus_check_nonnull'
0000000000000198 T _cplus_check_bounds
00000000000002bc T _cplus_check_nonnull
0000000000000000 T _cplus_fatal

$ nm -g --defined-only build/safe/libcplus_safecrt.a | grep -E 'cplus_runtime_get_violation_trap|cplus_runtime_get_failstop'
0000000000000050 T _cplus_runtime_get_failstop
0000000000000070 T _cplus_runtime_get_violation_trap
```

Source counterparts (read-only survey): `runtime/cplus_runtime.c`
(`cplus_fatal`, `cplus_check_bounds`, `cplus_check_nonnull`, each terminating via
`cplus_print_fatal` -> `_Exit(code)`) and `stdlib/safe/src/safe_runtime.c`
(`cplus_runtime_get_violation_trap`, `cplus_runtime_get_failstop`, volatile
loads). The new log deliberately does **not** call, wrap, or replace any of
these; it is an independent diagnostic channel.

## Public API

```c
int  cplus_violation_log_open(const char *path);
int  cplus_violation_log_event(int code, const char *kind,
                               const void *ptr, size_t size);
void cplus_violation_log_close(void);
unsigned long cplus_violation_log_count(void);
```

* `open` — `0` on success, `EINVAL` for a NULL/empty path, otherwise the
  underlying `open` errno. The caller supplies the path (nothing hard-coded).
  The file is opened `O_WRONLY|O_CREAT|O_APPEND` (mode `0644`); it is never
  truncated. Re-opening closes the previous destination without leaking its fd.
* `event` — `0` on success; `EINVAL` for a NULL/empty `kind`; `EBADF` when the
  log is not open; `EFBIG` when the bounded event cap (`1048576`) is reached;
  otherwise an I/O errno. Appends exactly one line:

  ```
  <code> <kind> 0x<lowercase-hex-pointer> <size>\n
  ```

  e.g. `101 bounds 0x1234 42\n`.
* `close` — closes the fd (no-op if none is open). Writes are unbuffered, so
  `close()` is the durability point; no explicit flush is needed.
* `count` — cumulative number of *successfully written* events since process
  start. Refused/failed events are not counted. Safe at any time.

### Safety properties

* **Thread-safe:** one process-wide `pthread_mutex_t` guards open/event/close
  and the count.
* **Bounded lines / no unbounded formatting:** `kind` is truncated to 96 chars
  with a `%.*s` precision; the whole line is rendered by a single `snprintf`
  into a fixed 256-byte stack buffer. `kind` is **never** used as a format
  string. The rendered line is far below `PIPE_BUF`, so one `write()` is an
  atomic append at the OS level; the mutex makes it atomic across threads too.
* **No hard-coded paths / hotkeys:** the only path comes from `open(path)`.
* **No heap allocation, no caller-memory writes:** a canary probe around a long
  `kind` buffer stays intact.
* **Bounded log growth:** a total-event cap prevents a violation storm from
  filling the disk.

## Exact commands and results

Toolchain: Apple clang 16.0.0 (`/usr/bin/clang`), Darwin arm64. Hardened libc
archive: `build/libc/libcplus_hardened.a`.

Compile (warnings enabled; no warnings emitted) and run:

```
$ cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
    -Ilibc/install-include \
    tests/libc/test_violation_log.c libc/src/runtime/violation_log.c \
    build/libc/libcplus_hardened.a -lm -o /tmp/test_violation_log
COMPILE_OK
$ /tmp/test_violation_log
violation_log: 41 0
$ echo $?
0
```

Repeated under the required 30-second timeout (concurrency determinism):

```
run 0: violation_log: 41 0 (exit 0)
run 1: violation_log: 41 0 (exit 0)
run 2: violation_log: 41 0 (exit 0)
run 3: violation_log: 41 0 (exit 0)
run 4: violation_log: 41 0 (exit 0)
run 5: violation_log: 41 0 (exit 0)
run 6: violation_log: 41 0 (exit 0)
run 7: violation_log: 41 0 (exit 0)
run 8: violation_log: 41 0 (exit 0)
run 9: violation_log: 41 0 (exit 0)
```

### What the 41 checks prove

1. closed-state safety: `close()` on a closed log is a no-op, count starts 0;
2. `event()` refuses when not open (`EBADF`) and does not change the count;
3. `open(NULL)` and `open("")` return `EINVAL`;
4. `event()` with NULL/empty `kind` returns `EINVAL` and is not counted;
5. three known events produce the exact fixed-format lines, count == 3;
6. canary guards around a 159-byte `kind` buffer remain intact; every line is
   shorter than 256 chars (bounded formatting);
7. `close()` + re-`open()` appends (earlier lines preserved), count is
   cumulative (5);
8. concurrency: 2 pthreads x 100 events = 200; all 200 lines present
   (100 `alpha`, 100 `beta`), each well-formed (no interleaving), bounded;
9. a failed `open` (missing directory) returns nonzero and leaves the count
   unchanged;
10. canary guards still intact after the full run.

The test prints `violation_log: <checks> <failures>` and exits nonzero on any
failure. Path arguments are optional and relative (`argv[1]`/`argv[2]` default
to `violation_log_a.log` / `violation_log_b.log` in the working directory).

## `nm` evidence — only the four new symbols

The implementation exports exactly four global symbols and nothing else
(static helpers are local, so they are not visible):

```
$ /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
    -Ilibc/install-include -c libc/src/runtime/violation_log.c -o /tmp/violation_log.o
$ nm -g --defined-only /tmp/violation_log.o
0000000000000464 T _cplus_violation_log_close
0000000000000498 T _cplus_violation_log_count
0000000000000160 T _cplus_violation_log_event
0000000000000000 T _cplus_violation_log_open

$ nm -g --defined-only /tmp/violation_log.o | wc -l
       4
```

The new names do not collide with any existing symbol:

```
$ nm -g --defined-only build/libc/libcplus_hardened.a | grep -c 'cplus_violation_log'
0
```

## Integration note for the parent agent

This subagent was constrained to the four files above and did **not** edit
`libc/CMakeLists.txt` or `tests/libc/build_libc.sh`. Consequently the new
`libc/src/runtime/violation_log.c` is not yet a member of
`build/libc/libcplus_hardened.a`; the verification above compiled the source
directly into the test link while still linking the hardened archive (as
prescribed). To make the API ship in the library, add
`src/runtime/violation_log.c` to `LIBC_SOURCES` in `libc/CMakeLists.txt` and to
the compile loop in `tests/libc/build_libc.sh`. After that,
`tests/libc/test_violation_log.c` links against the archive alone with no
changes to the test source, and `run_libc_tests.sh` will pick it up normally.
