# system_safety/support — reusable system-test helpers

Header-only helpers for tests under `tests/stdlib/system_safety/`.  No build
system changes, no runtime changes, no game/assets involvement.

## Files

| File | Contents |
| --- | --- |
| `watchdog.hp` | bounded fork + alarm watchdog; exact child exit status |
| `alloc_fail.hp` | RAII scope around `stdlib_alloc_fail_at/after/reset` |
| `fd_count.hp` | fd snapshot via `opendir("/dev/fd")` |
| `temp_path.hp` | PID-unique temp path generator |
| `self_test.cp` | tiny self-checking program for all four helpers |

## Include path (important)

The C+ driver resolves `.hp` includes against: the including file's parent
directory, `.` (the repo root when the harness has `cd`ed there), `CPLUS_INCLUDE`,
`runtime/include`, and the source root.  Preferred forms:

* From a test **in this directory** (e.g. `self_test.cp`): use the local names
  `"watchdog.hp"`, `"alloc_fail.hp"`, `"fd_count.hp"`, `"temp_path.hp"`.
  These resolve via the source file's parent directory, so they work from any
  working directory.
* From a test in `tests/stdlib/system_safety/`: use `"support/watchdog.hp"`
  etc. (also cwd-independent, resolved via the source file's parent).
* From other `tests/stdlib/**/*.cp` built by the standard harnesses (cwd is the
  repo root): use the repo-root-relative path, e.g.:

```
#include "tests/stdlib/system_safety/support/watchdog.hp"
#include "tests/stdlib/system_safety/support/alloc_fail.hp"
#include "tests/stdlib/system_safety/support/fd_count.hp"
#include "tests/stdlib/system_safety/support/temp_path.hp"
```

The repo-root-relative form depends on cwd/source-root detection and will NOT
resolve if the compiler is invoked from an unrelated directory; use one of the
parent-relative forms above in that case.

> Compiler pitfall found while authoring these headers: the `.hp` preprocessor
> matches `#include` textually **even inside comments**.  A doc comment that
> contained a literal include directive made the header include itself
> recursively and crashed the compiler (rc 139).  Do not put include directives
> in comments in these headers or in tests that include them.

## API

### `watchdog.hp`

```c
typedef void (*ss_child_fn)(void);

struct ss_watchdog_result {
    pid_t child_pid;   /* fork() result, -1 on fork failure            */
    int reaped;        /* 1 when waitpid() reaped the child            */
    int timed_out;     /* 1 when the alarm backstop fired/kill used    */
    int exited;        /* 1 when WIFEXITED(raw_status)                 */
    int signaled;      /* 1 when WIFSIGNALED(raw_status)               */
    int exit_code;     /* WEXITSTATUS when exited, else -1             */
    int term_signal;   /* WTERMSIG when signaled, else 0               */
    int raw_status;    /* unmodified status word from waitpid()        */
};

struct ss_watchdog_result ss_run_watchdog(ss_child_fn child_fn,
                                          unsigned int hard_secs,
                                          int child_alarm_secs);
void ss_child_exit(int code);       /* child helper: _exit(code)       */
void ss_child_pause_forever(void);  /* child helper: blocks forever    */
```

Behaviour:

* `child_fn` runs in the forked child; `SIGALRM` is reset to `SIG_DFL` there,
  and `child_alarm_secs > 0` arms a fatal self-alarm (0 = none).  Returning
  from `child_fn` calls `_exit(0)`.
* The parent installs a non-`SA_RESTART` `SIGALRM` handler, re-blocking the
  previous disposition on return.  `hard_secs` is the hard backstop; on
  expiry it sends `SIGKILL` and reaps.  Any previously armed outer alarm is
  re-armed on a best-effort basis (elapsed seconds are not deducted).
* The parent cannot hang: the blocking `waitpid()` is interrupted by the
  alarm, and the post-`SIGKILL` reap is bounded.  `raw_status` is the exact
  wait status; `exited`/`signaled` are mutually exclusive.
* `hard_secs == 0`, a null `child_fn`, or a failed `fork()` returns a result
  with `reaped == 0` and `child_pid == -1`.
* No variable division/modulo; no pointer-parameter subscripting.

### `alloc_fail.hp`

```c
class alloc_fail_scope {
    alloc_fail_scope(void);
    void fail_at(long n);      /* n-th allocation from now fails once */
    void fail_after(long n);   /* n succeed, the rest fail             */
    int  is_armed(void);
    void disarm(void);         /* explicit early reset, idempotent    */
    void drop(void);           /* auto-called on scope exit            */
};

void ss_fail_at(long n);       /* manual, non-RAII equivalents        */
void ss_fail_after(long n);
void ss_fail_reset(void);
```

Usage:

```
{
    alloc_fail_scope scope_at = alloc_fail_scope();
    scope_at.fail_at(1);
    void *probe = stdlib_alloc(32);   /* NULL: injection is deterministic */
    ...
}                                    /* drop() -> stdlib_alloc_fail_reset() */
```

* **Give each instance a unique variable name.**  C+ RAII lowering dedups
  owned locals by name within a function; reusing a name in a sibling scope
  silently omits the generated `drop()` call (same rule as `guard` in
  `stdlib/thread.hp`).
* In a forked child that calls `_exit()`, use the manual `ss_fail_*` trio —
  destructors do not run there.
* `alloc_fail.hp` includes `stdlib/error.hp` via its own parent-relative path
  (`../../../../stdlib/error.hp`), which owns the injection state.  This keeps
  the helper cwd-independent; it was verified by compiling `self_test.cp` from
  `/tmp` with an absolute source path.

### `fd_count.hp`

```c
int ss_fd_count(void);       /* numeric entries in /dev/fd, -1 if unavailable */
int ss_fd_snapshot(void);    /* alias for ss_fd_count() */
int ss_fd_delta(int baseline, int observed);  /* -1 if either snapshot failed */
```

`opendir()` itself holds one fd while scanning, but every snapshot includes
that same one, so before/after deltas stay meaningful.  The single-byte read
of `dirent::d_name` goes through `__builtin_memcpy` because strict C+ cannot
prove a pointer parameter's extent for a raw subscript.  Guard on `< 0` for
hosts without `/dev/fd`.

### `temp_path.hp`

```c
int ss_temp_path(char *buf, size_t cap, const char *tag);
int ss_temp_path_n(char *buf, size_t cap, const char *tag,
                   unsigned int ordinal);
```

Write `"/tmp/cplus-ss-<pid>-<tag>"` (or `...-<ordinal>`) with `snprintf`
(safe truncation).  Returns snprintf's would-be length, or `-1` for a null
buffer / zero capacity.  The PID makes paths unique across concurrent test
processes; the tag / ordinal distinguishes paths inside one process.  No
variable division/modulo.  Paths are **not** created by these helpers.

## Strict-compiler constraints applied

* `__builtin_memcpy` for pointer-parameter element access (`fd_count.hp`).
* No variable modulo/division anywhere (verified by inspection).
* Unique variable names for every RAII local (`self_test.cp` uses
  `scope_at` / `after_probe` / `self_*` names; the C+ lowering dedups by name).
* No `#include` token inside comments (see pitfall above).
* All helpers are `static inline` (or `class`) in headers with include guards,
  so multi-TU inclusion is safe.

## Verification (exact commands and results)

Run from the repo root on branch `agent/modern-stdlib`, compiler
`./build/c+` (C+ 0.1.0).  Every build/run is bounded by `timeout 30`.

### 1. Strict + no sanitizer

```
timeout 30 ./build/c+ build tests/stdlib/system_safety/support/self_test.cp \
  --strict --sanitize=none -o build/stdlib-system-safety/self_test_strict
echo "build rc=$?"     -> 0

timeout 30 ./build/stdlib-system-safety/self_test_strict
echo "run rc=$?"       -> 0
stdout:
wd.reaped 1
wd.timed_out 0
wd.exited 1
wd.exit_code 23
wd.raw_status 5888
wd_hang.reaped 1
wd_hang.timed_out 1
wd_hang.signaled 1
wd_hang.term_signal 9
fd.open_ok 1
fd.delta 1
fd.restored 0
af.fail_at_null 1
af.armed 1
af.reset_after_scope 1
temp.len_ok 1
temp.has_pid 1
temp.has_tag 1
temp.canary 90
self_test done failures=0
stderr: (empty)
```

`5888 == 23 << 8` is the raw `WIFEXITED` status for exit code 23;
`9 == SIGKILL` confirms the hung child was killed and reaped.

### 2. Strict + UBSan

```
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  timeout 30 ./build/c+ build tests/stdlib/system_safety/support/self_test.cp \
  --strict --sanitize=undefined -o build/stdlib-system-safety/self_test_ubsan
echo "build rc=$?"     -> 0

timeout 30 ./build/stdlib-system-safety/self_test_ubsan
echo "run rc=$?"       -> 0
stdout: identical to the strict-none run above (20 lines)
stderr: (empty — no UBSan diagnostics)
```

**Environment note.**  Without `CPLUS_INCLUDE`, this checkout's `./build/c+`
finds `runtime/include/cplus/cplus.h` from this repo (which predates
`CPLUS_EXIT_ARITHMETIC`) while its compiled-in runtime source is
`c-plus-core/runtime/cplus_runtime.c` (which uses that enum); the UBSan build
then fails with `error: failed to compile instrumented C+ runtime`.  Pointing
`CPLUS_INCLUDE` at the matching runtime headers avoids the mismatch.  As a
cross-check, `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/c+` builds
and runs the same self-test under `--strict --sanitize=undefined` with identical
output and rc 0, no env override needed.  This is a toolchain-layout issue,
not a property of these headers; strict/no-sanitizer needs no override.

## Usage notes for other agents

* The watchdog owns `SIGALRM` for the duration of the call; avoid relying on
  a precise outer alarm deadline during that window.
* Use a small `hard_secs` (1–3) in tests so a failure is bounded well inside
  the 30 s harness timeout.
* `ss_fd_delta(before, after) == 0` is the expected no-leak result; `1` means
  one descriptor stayed open.
* The temp-path helpers only format strings; call `open`/`mkdir`/`unlink`
  yourself and clean up with the same path.
