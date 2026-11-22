# Logging and telemetry

C+ logging is **disabled by default**. It uses the MIT-licensed `clog` library
vendored from `0xA1M/clog` (license preserved in `third_party/clog/LICENSE`).

## Compiler logging

```sh
c+ build main.cp --log --log-level info -o main
c+ build main.cp --log --log-level debug -o main   # verbose pipeline logs
```

`--log-level` accepts `trace`, `debug`, `info`, `warn`, `error`, `fatal`.
With no `--log`, no logging code runs and no output is emitted.
`--verbose` controls backend command printing; `--log-level debug` controls
pipeline logging.

## Runtime/stdlib logging

When `--log` is used, the driver passes `-DCPLUS_LOG_RUNTIME=1` and the chosen
level to the generated C. Every standard-library method (`String`, `IntVec`, `File`, `Mutex`/`Guard`,
I/O and sockets) and key runtime entry points emit scoped logs, for example:

```text
[DEBUG] String ctor len=3  (string.hp:15 in String_ctor)
```

With logging disabled, every `cplus_log(...)` call expands to `((void)0)`, so
there is no runtime cost.

## C libc logger (`cplus_libc_log`) — exact guarantees

`libc/log/log.c` implements the low-level, signal-facing logger used to
instrument the hardened C libc wrappers. Its contract is deliberately narrow:

- **Opt-in, off by default.** `cplus_libc_log_set_enabled(1)` enables it via a
  single atomic flag; `(0)` restores silence. A disabled call is a no-op.
- **Exact line format:** `"[CPLUS-LIBC] <api>: <msg>\n"`. Only the two string
  fields `api` and `msg` are emitted — never caller buffers, file contents or
  secrets. Both must be immutable, NUL-terminated strings for the lifetime of
  the call (a handler can interrupt a caller mutating them).
- **Bounded, allocation-free, stdio-free.** One fixed 4096-byte stack buffer;
  no heap allocation, no `FILE*`, no `fprintf`. Output goes straight to file
  descriptor **2** with `write(2)`. Strings longer than the body cap are
  truncated and the line is terminated with a `"...\n"` marker; the largest
  line emitted is **4095 bytes** (truncation still leaves a well-formed line).
- **`errno` is preserved.** `errno` is saved on entry and restored on every
  return path. A failed `write(2)` (e.g. `EBADF` on a closed fd 2, or `EPIPE`)
  never leaks its `errno` into the caller. This keeps callers that inspect
  `errno` after a logging probe correct.
- **Whole-line delivery is attempted.** A single `write(2)` may return a short
  count, `-1/EINTR`, or `-1/EAGAIN` (nonblocking fd). The logger loops until
  every byte of the line has been written or it hits a hard error
  (`EBADF`/`EPIPE`/`ENOSPC`/...) / a zero-byte write / a bounded consecutive
  retry budget (`CPLUS_LOG_MAX_RETRIES`, reset on progress, so it cannot spin
  forever from a signal handler). A short write never silently drops the rest
  of the line.
- **No signal-disposition changes.** SIGPIPE is *not* suppressed: if the reader
  of fd 2 closes, the process sees the normal SIGPIPE/`EPIPE` behaviour of any
  other `write(2)`. Ownership and lifetime of fd 2 belong to the caller/host;
  the logger never opens, closes, or redirects it.

### Async-signal-safety caveat

The design uses only operations that are individually async-signal-safe on the
supported POSIX hosts (`write`, `sched_yield`, an `atomic_int` gate, and a
stack buffer), and it adds no locks or allocation. **Async-signal-safety is a
design goal, not a formally proven property.** It has not been established by
a complete async-signal-safety proof across every supported platform; callers
that require a hard guarantee should treat logging from a handler as
best-effort. This caveat applies to `cplus_libc_log` only — the higher-level
`cplus_safe_log` / telemetry / runtime-report paths use `fprintf`/mutexes and
are **not** async-signal-safe.

## Verification

```sh
./tests/logging_test.sh
```

The `cplus_libc_log` errno-preservation and short-write-delivery regressions
live in `tests/libc/test_logging_telemetry.c` (sections 8 and 9).
