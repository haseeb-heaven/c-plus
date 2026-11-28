# Function-pointer dispatch bounds: Agent 1 handoff

## Measured behaviour on the current branch

| Form | Command | Result |
|---|---|---|
| `dispatch[5556]()` with a constant index | `./build/c+ check tests/compile_fail/case13_dispatch_const.c --default` | rejected: `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 5556 exceeds length 3` |
| `volatile size_t cmd = 5556; dispatch[cmd]()` | `./build/c+ build tests/runtime_fail/case13_dispatch_runtime.c -o /tmp/d` then run | aborts with `runtime error: index 5556 out of bounds for type 'handler_fn[3]'` — emitted by **Clang UBSan**, not by C+ |

The compile-time path is complete. The runtime path currently depends on
`-fsanitize=undefined` in the C+ backend; C+ itself inserts no bounds check for
an index it cannot prove statically. That is a compiler-side gap, not a libc
gap: the access happens in user code before any library call.

## Required tests added (`tests/`)

- `tests/compile_fail/case13_dispatch_const.c` + `.err` (`[CPLUS-BOUNDS]`)
- `tests/runtime_fail/case13_dispatch_runtime.c` + `.stderr` (`out of bounds`)

Both currently pass in `tests/run_tests.sh`, but the runtime fixture passes
because of UBSan. It must be changed to require a C+ diagnostic once Agent 1
adds instrumentation.

## Requested Agent 1 behaviour

1. Static: keep rejecting constant indices with `[CPLUS-SAFE-BOUNDS]`
   (arrays of function pointers, not just integers/characters).
2. Dynamic: when the index is not statically provable in range and the array
   extent is known, emit a check before the subscript:

```
C+ error: array index out of bounds (dispatch[5556], length 3)
```

3. Do not emit a check when the range is proven safe; for unknown pointer
   extents use a conservative check or a diagnostic as specified by safe mode.

## Reproduction commands

```
cd <repo>
PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build/c+ check tests/compile_fail/case13_dispatch_const.c --default
PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build/c+ build tests/runtime_fail/case13_dispatch_runtime.c -o /tmp/d
/tmp/d
```

Note: the default `clang` in this environment is Homebrew LLVM 23 with a
broken sysroot; use the PATH prefix above or `CPLUS_CLANG=/usr/bin/clang`.

Agent 2 has not modified `compiler/` or `runtime/`, per the ownership boundary.
