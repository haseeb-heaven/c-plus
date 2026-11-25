# RUN NOTES -- thread_async public-method coverage

## Files delivered (all under `tests/stdlib/method_tests/thread_async/`)

- `test_thread_methods.cp` -- 21 deterministic cases, one per `stdlib/thread.hp` callable.
- `test_async_methods.cp` -- 16 deterministic cases, one per `stdlib/async.hp` public callable.
- `fixture.txt` -- known content used by the `read_file` case.
- `methods.txt` -- per-method manifest with status.

No files outside this directory were created or modified; no `git add`/commit.

## Exact commands and results

All commands were run from the repository root
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`) on branch `agent/modern-stdlib`.

### thread.hp

```
./build/c+ build tests/stdlib/method_tests/thread_async/test_thread_methods.cp \
  --strict --sanitize=none -o build/thread_method_test
# build_rc=0  (no diagnostics)

timeout 30 ./build/thread_method_test
# run_rc=0
thread.hp mutex.ctor PASS
thread.hp mutex.is_valid PASS
thread.hp mutex.lock PASS
thread.hp mutex.unlock PASS
thread.hp mutex.native PASS
thread.hp mutex.drop PASS
thread.hp guard.ctor PASS
thread.hp guard.holds PASS
thread.hp guard.unlock PASS
thread.hp guard.drop PASS
thread.hp condvar.ctor PASS
thread.hp condvar.is_valid PASS
thread.hp condvar.wait PASS
thread.hp condvar.signal PASS
thread.hp condvar.broadcast PASS
thread.hp condvar.drop PASS
thread.hp thread.ctor PASS
thread.hp thread.is_joinable PASS
thread.hp thread.start PASS
thread.hp thread.join PASS
thread.hp thread.drop PASS
METHOD_TEST thread 21 0
```

### async.hp

libuv is discovered with `pkg-config --modversion libuv` -> `1.51.0`
(`--cflags` = `-I/opt/homebrew/Cellar/libuv/1.51.0/include`,
`--libs` = `-L/opt/homebrew/Cellar/libuv/1.51.0/lib -luv`). The equivalent
system-prefix flags were passed as **separate** compiler operands (the C+
driver collects each operand independently; a single combined string would be
reported unused).

```
./build/c+ build tests/stdlib/method_tests/thread_async/test_async_methods.cp \
  --strict --sanitize=none -I/opt/homebrew/include -L/opt/homebrew/lib -luv \
  -o build/async_method_test
# build_rc=0
# clang: warning: -luv: 'linker' input unused [-Wunused-command-line-argument]
# clang: warning: argument unused during compilation: '-L/opt/homebrew/lib' [...]
# (benign driver warnings; the binary links libuv and runs correctly)

timeout 30 ./build/async_method_test
# run_rc=0
async.hp loop.ctor PASS
async.hp loop.valid PASS
async.hp loop.native PASS
async.hp loop.run PASS
async.hp loop.stop PASS
async.hp loop.now_ms PASS
async.hp loop.drop PASS
async.hp timer.ctor PASS
async.hp timer.valid PASS
async.hp timer.start PASS
async.hp timer.stop PASS
async.hp timer.is_active PASS
async.hp timer.drop PASS
async.hp read_file PASS
async.hp sleep_ms PASS
async.hp uv_error_text PASS
METHOD_TEST async 16 0
```

Neither test needs `build/libc/libcplus_hardened.a`; both build and link with
the plain C+ runtime plus `-luv` for async.

## Determinism / stability

Each binary was executed 5 additional times under `timeout 30`:
`thread` -> `METHOD_TEST thread 21 0` on all 5 runs;
`async` -> `METHOD_TEST async 16 0` on all 5 runs. Zero FAIL lines, zero
timeouts, exit code 0 every time.

Nondeterminism controls used:
- Every spawn is followed by `join()` (thread) before the scope ends; `drop()`
  is verified to join implicitly for the `thread.drop` case.
- `condvar` tests use a predicate flag published under the external mutex
  before `signal()`/`broadcast()`, so an early notification cannot be lost.
- Timers use fixed 1 ms timeouts and stop the loop from inside the callback;
  the only time assertion is monotonic (`uv_now` must advance after
  `sleep_ms(5)`), never a wall-clock value.
- Fixed fixture content and a fixed missing path for `read_file`.

## Coverage accounting

| Header | Public callables | Invoked & verified | Failed | Skipped |
| ------ | ---------------- | ------------------ | ------ | ------- |
| thread.hp | 21 | 21 | 0 | 0 |
| async.hp | 14 (inventory headline) / 16 (source-inspection truth) | 16 | 0 | 0 |
| **Total** | **37** | **37** | **0** | **0** |

`async.hp` note: the inventory's lexical `entries()` parser only matches
single-line `) {` signatures, so its headline is 14. The module audit
(`tests/stdlib/agent2_audit/module_audits/async.hp.md`, §2) records two real
public callables it misses: `timer.start` and `read_file`. Both are invoked and
verified here, giving 16 executed methods. The `stdlib_timer_release_handle`
close callback is excluded by the inventory's `stdlib_` rule and is exercised
indirectly by `timer.drop()`; it is not counted as a public method.

No method was skipped: every callable listed above has a CALL plus an observable
result check in a `.cp` case.

METHOD_TEST thread_async 37 37 0 0
