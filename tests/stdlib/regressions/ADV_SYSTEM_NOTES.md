# Adversarial system regressions — thread contention / JSON / filesystem / network

## Scope and ownership

This note records evidence for exactly two new self-checking regression programs:

| File | Family | Final line |
| --- | --- | --- |
| `tests/stdlib/regressions/adv_thread_contention.cp` | bounded mutex/guard contention under a watchdog | `ADV_THREAD_CONTENTION` |
| `tests/stdlib/regressions/adv_json_fs_net.cp` | malformed JSON, filesystem errors, local-only network faults | `ADV_JSON_FS_NET` |

No header, compiler, runtime, build script, or git state was modified. Nothing was
staged or committed. Other `.cp` files in `tests/stdlib/regressions/` belong to
parallel agents and were not read, edited, built, or run.

## Environment / provenance

- Repo: `c-plus-stdlib`, branch `agent/modern-stdlib`, HEAD `ed2ac0f`.
- Verification date: 2026-11-23 06:43 IST.
- Compiler: `./build/c+` → `C+ 0.1.0` (host Clang backend; `clang --version` →
  `Homebrew clang version 23.1.6`).
- Host: `Darwin 27.2.0`, arm64 (`arm64-apple-darwin`), effective UID `501`
  (unprivileged — the permission-denial case is deterministic here).
- Toolchain PATH exported for every command:
  `export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH`
- `timeout` is `/usr/bin/timeout`; every build and every run used
  `timeout -k 5 30` (30 s hard limit, SIGKILL 5 s later). No invocation timed out.
- JSON library operands were passed as **separate operands**, exactly as
  requested: `-Ithird_party/yyjson -Lbuild/ext -lyyjson`. (`libyyjson.a` was
  already present in `build/ext`.) The C+ driver emits two harmless
  `clang: warning: ... unused during compilation` lines for `-L`/`-l` during
  its lowering step; linking and execution succeed.

## Verification order

Per the task, strict/no-sanitizer was verified **first** for both programs; only
after those passed were default and UBSan variants built and run. Four modes
were exercised per program:

| Mode key | Compiler flags |
| --- | --- |
| `strict_none` | `--strict --sanitize=none` |
| `default` | *(no flags)* |
| `default_ubsan` | `--sanitize=undefined` |
| `strict_ubsan` | `--strict --sanitize=undefined` |

### Exact commands

Thread contention (strict/no-sanitizer first):

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
mkdir -p build/adv-regressions
timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_thread_contention.cp --strict --sanitize=none -o build/adv-regressions/adv_thread_contention_strict_none
timeout -k 5 30 build/adv-regressions/adv_thread_contention_strict_none

timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_thread_contention.cp -o build/adv-regressions/adv_thread_contention_default
timeout -k 5 30 build/adv-regressions/adv_thread_contention_default

timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_thread_contention.cp --sanitize=undefined -o build/adv-regressions/adv_thread_contention_default_ubsan
timeout -k 5 30 build/adv-regressions/adv_thread_contention_default_ubsan

timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_thread_contention.cp --strict --sanitize=undefined -o build/adv-regressions/adv_thread_contention_strict_ubsan
timeout -k 5 30 build/adv-regressions/adv_thread_contention_strict_ubsan
```

JSON/FS/NET (strict/no-sanitizer first; note the three library operands):

```sh
timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_json_fs_net.cp --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/adv-regressions/adv_json_fs_net_strict_none
timeout -k 5 30 build/adv-regressions/adv_json_fs_net_strict_none

timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_json_fs_net.cp -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/adv-regressions/adv_json_fs_net_default
timeout -k 5 30 build/adv-regressions/adv_json_fs_net_default

timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_json_fs_net.cp --sanitize=undefined -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/adv-regressions/adv_json_fs_net_default_ubsan
timeout -k 5 30 build/adv-regressions/adv_json_fs_net_default_ubsan

timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_json_fs_net.cp --strict --sanitize=undefined -Ithird_party/yyjson -Lbuild/ext -lyyjson -o build/adv-regressions/adv_json_fs_net_strict_ubsan
timeout -k 5 30 build/adv-regressions/adv_json_fs_net_strict_ubsan
```

## Per-mode results

All build exits were `0`; all run exits were `0`; all stderr streams were empty.
Output was byte-identical across the four modes for each program (verified with
the captured `.stdout` files in `build/adv-regressions/`).

| Program | Mode | Build rc | Run rc | Timeout? | SUMMARY | Result |
| --- | --- | ---: | ---: | --- | --- | --- |
| adv_thread_contention | strict_none (verified first) | 0 | 0 | no | `10 1 0 0` | PASS |
| adv_thread_contention | default | 0 | 0 | no | `10 1 0 0` | PASS |
| adv_thread_contention | default_ubsan | 0 | 0 | no | `10 1 0 0` | PASS |
| adv_thread_contention | strict_ubsan | 0 | 0 | no | `10 1 0 0` | PASS |
| adv_json_fs_net | strict_none (verified first) | 0 | 0 | no | `12 7 0 0` | PASS |
| adv_json_fs_net | default | 0 | 0 | no | `12 7 0 0` | PASS |
| adv_json_fs_net | default_ubsan | 0 | 0 | no | `12 7 0 0` | PASS |
| adv_json_fs_net | strict_ubsan | 0 | 0 | no | `12 7 0 0` | PASS |

Totals: **8/8 build+run combinations passed**, 30 self-checking CASE lines
(22 SUCCESS, 8 RECOVERABLE, 0 FATAL), 0 SKIPs, 0 timeouts, 0 sanitizer reports.

## Exact stdout (identical in all four modes)

`adv_thread_contention` — 11 CASE lines + 2 CHECK lines:

```
CASE mutex_valid 0
CASE guard_contention_no_lost_updates 0
CHECK counter 32000 32000
CHECK checksum 8016000 8016000
CASE contention_with_yield 0
CASE guard_unlock_then_drop 0
CASE guard_zero_and_null 0
CASE thread_wrapper_lifecycle 0
CASE condvar_bounded 0
CASE mutex_double_drop 1
CASE mutex_scopes_reentrant 0
CASE mutex_drop_idempotent 0
CASE sentinel_survived 0
SUMMARY 10 1 0 0
RESULT PASS 0
ADV_THREAD_CONTENTION
```

`adv_json_fs_net` — 19 CASE lines + 2 CHECK lines:

```
CASE json_malformed_repeated_parse_free 0
CHECK json_bad_inputs 17
CASE json_valid_roundtrip 0
CASE json_builder_lifecycle 0
CASE json_parse_missing_file 0
CASE fs_mkdtemp_invalid_template 1
CASE fs_missing_path_errors 1
CASE fs_create_and_size 0
CASE fs_invalid_mode 1
CASE fs_permission_denied 0
CASE fs_repeated_cleanup 0
CASE net_invalid_ports 1
CASE net_ephemeral_bind 0
CASE net_afunix_disconnect 1
CASE net_loopback_disconnect 0
CASE net_partial_write 0
CASE net_write_no_progress 1
CASE net_invalid_handles 1
CASE net_repeated_close 0
CASE net_no_fd_leak 0
CHECK fd_before_after 5 5
SUMMARY 12 7 0 0
RESULT PASS 0
ADV_JSON_FS_NET
```

### Build stderr (JSON programs, all modes)

```
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
```

These are driver-lowering warnings for the required separate library operands;
link step and runtime are clean. Thread builds produced empty build stderr.

## Coverage map

### adv_thread_contention (11 cases)

- **Contention**: 8 raw `pthread` workers × 4000 iterations, all increments and
  nested updates under `guard`s on two consistently ordered mutexes; exact
  counter/checksum equality proves no lost updates. A second wave of 4 workers
  yields while holding the lock (watchdog covers any hang).
- **Guard lifecycle**: explicit `unlock()` followed by destructor/drop (no
  double unlock), zero-initialised and `NULL` guards, guard over a dropped
  mutex (RECOVERABLE — see Observations), double `drop()` of a mutex.
- **Thread wrapper**: start/join/`is_joinable` on two distinctly named `thread`
  locals, idempotent `drop()`, `join()` after join reports `STDLIB_FAIL`.
- **condvar**: bounded predicate wait + signal + broadcast + double drop.
- **Sentinel fd**: `/dev/null` descriptor checked before and after to prove no
  accidental close of process descriptors.
- Watchdog: `SIGALRM` 25 s, `_exit(90)` with a message on stderr. Cancelled on
  the success path.

### adv_json_fs_net (19 cases)

- **JSON (4)**: 17 deterministic malformed/truncated inputs (empty, `{`,
  `{oops`, truncated strings/numbers/arrays, trailing comma, missing colon,
  `tru`/`nul`/`fals`, `01`, unbalanced braces) parsed **30 rounds** each with
  double `drop()`, byte-identical error text per input, and invalid-view
  accessor checks; a valid document round-trip with typed accessors and
  `json_free`; builder write/reparse/free + use-after-drop failures;
  `json_parse_file` on a PID-unique missing path.
- **Filesystem (6)**: PID-unique `/tmp/cplus_adv_jsfsnet_<pid>...` paths;
  invalid `fs_mkdtemp` templates (no `XXXXXX`, `NULL`, undersized); missing path
  queries and repeated failing `remove`/`rename`/`size` with IO category;
  `fs_mkdtemp` + file create/size; invalid modes (`mkdir` on a file path,
  `unlink` of a directory, re-`mkdtemp` on a consumed template); permission
  denial via `chmod 0000` (guarded by `geteuid() != 0`); repeated cleanup with
  deterministic second-removal failures and a final `fs_exists` check.
- **Network (9)**: invalid ports (`-1`, `70000`, `INT_MIN`) and double drop;
  ephemeral loopback bind; AF_UNIX socketpair peer close (read→EOF, write→EPIPE
  as IO failure with SIGPIPE ignored); loopback disconnect with exact bytes then
  EOF in a bounded forked peer; interposed short writes (512-byte chunks)
  delivering a 4096-byte payload with a content check in the child; forced
  zero-progress write reporting `connection::write made no progress` then
  recovering; invalid/zero-init/borrowed handles and NULL/0-capacity arguments;
  owning handles dropped twice with a sentinel fd check; `/dev/fd` leak check
  (`5 → 5`). No external server is contacted; all waits use `poll` timeouts or
  socket `SO_RCVTIMEO`/`SO_SNDTIMEO` plus a 25 s `SIGALRM` watchdog.

## SKIPs

**Final runs: none.** The permission case and the `/dev/fd` leak case each have
an explicit SKIP branch, but neither fired (`UID 501`, `/dev/fd` readable). The
loopback/socketpair cases also have SKIP branches; none fired. No scenario was
silently treated as proven — every branch prints a `CASE ... 3` plus a
`SKIP <name> <reason>` line before counting a skip.

## Observations (reproduced behaviors, not reported as defects)

No FATAL case, crash, corruption, hang, or sanitizer report was reproduced in
any mode; no header was edited. Two behaviors are worth recording because an
earlier draft assertion was wrong about them; both are reproducible minimally.

1. **`guard::holds()` tracks the owner pointer, not mutex liveness.**
   After `mutex::drop()` sets the handle to `NULL`, constructing
   `guard g = guard(&m)` still reports `holds() == 1`; the guard's
   `mutex_lock`/`mutex_unlock` calls are no-ops on the dropped handle, so this
   is crash-free. Minimal repro (contained in `adv_thread_contention.cp`, case
   `mutex_double_drop`):

   ```c
   mutex m = mutex();
   m.drop();
   m.drop();                 /* handle is NULL, m.is_valid() == 0 */
   guard g = guard(&m);
   print("holds", g.holds()); /* 1, not 0 */
   g.drop();
   ```

   Observed: `DBG valid_after_drop 0`, `DBG guard_holds 1`, child/process exits 0.
   Classified RECOVERABLE (`CASE mutex_double_drop 1`) rather than asserting a
   false expectation of `0`.

2. **Borrowed handles never close their descriptor.** After
   `connection_borrow(fd)`'s `drop()` twice, `fcntl(fd, F_GETFD)` still succeeds;
   the test must close `fd` itself. This is the documented `net.hp` contract and
   is asserted by `CASE net_afunix_disconnect 1`.

Additionally, a transient compiler-level probe (deleted after use; not part of
the committed family) confirmed that C+ rejects `fork()` after thread creation
(`[CPLUS-FFI]`) and passing a stack address to `pthread_create`
(`[CPLUS-BORROW]`) even in default mode. The committed thread test deliberately
uses a static shared context and creates all threads after any forking, so it
compiles cleanly in strict mode.

## Limitations

- No ThreadSanitizer run: the task specified strict/none, default and UBSan.
  The no-lost-updates assertion is a functional check, not a formal
  race-detector proof; all shared writes in the test are nevertheless guarded.
- `/dev/fd` counting is host-specific and degrades to SKIP if unavailable.
- Port-0 binding and `chmod(0000)` denial are host/UID dependent; both have
  explicit SKIP branches.
- Results are from the local `./build/c+` toolchain only; no cross-host claim is
  made.
