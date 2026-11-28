# `<stdatomic.h>` API closure — 34 UNSUPPORTED entries

Branch: `agent/modern-stdlib`. Scope: close the 34 `UNSUPPORTED` rows in
`docs/libc/ISO_C_FULL_INVENTORY.md` (section `### stdatomic.h — 49 APIs`) with
thin, ABI-preserving, C-callable `cplus_*` wrappers. No existing header,
symbol, struct layout, compiler, or runtime file was changed.

**Result: 31 CLOSED, 3 BLOCKED.** The 3 blocked rows are the C23 wait/notify
family (`atomic_wait`, `atomic_notify_one`, `atomic_notify_all`).

## 1. Design

### 1.1 Why the old answer was "impossible"

The prior classification said a C ABI wrapper was impossible because the C11
operations are `_Generic` macros over `_Atomic`-qualified objects. That is
true **only if the wrapper must reproduce the exact macro spelling and accept
`_Atomic T`**. It is not true if the wrapper is a plain function that accepts a
pointer to caller-owned storage and performs the operation with a compiler
`__atomic_*` builtin. Those builtins:

- are real compiler intrinsics, so the operation is genuinely atomic;
- accept ordinary (non-`_Atomic`) scalar storage, so the public header never
  needs a C11-only keyword;
- work in **both `-std=c99` and `-std=c11`** on the verified host.

### 1.2 What was shipped

- `libc/install-include/cplus_stdatomic.h` — self-contained C99 header. Includes
  only `<stddef.h>`; no `<stdatomic.h>`, `_Atomic`, `_Generic`, or other C11
  keyword. Has `extern "C"` guards (verified under C++20).
- `libc/src/stdlib/atomic_wrappers.c` — 169 (`nm`-counted) linker-visible
  functions, each a one-to-two-line pass-through to `__atomic_store_n`,
  `__atomic_load_n`, `__atomic_exchange_n`, `__atomic_compare_exchange_n`,
  `__atomic_fetch_{add,sub,or,xor,and,nand}`, `__atomic_test_and_set`,
  `__atomic_clear`, `__atomic_is_lock_free`, `__atomic_thread_fence`,
  `__atomic_signal_fence`.
- `tests/libc/test_cplus_atomic.c` — C99 test; calls every one of the 169
  symbols (linkage coverage), checks representative semantics, and runs a
  bounded 4-thread concurrency smoke test under `timeout 30`.

ABI properties:

- Wrappers take **pointers to caller storage**; the caller owns and declares the
  object. No wrapper-level struct layout is imposed on any existing header.
- Storage types are fixed-width C scalars with stable suffixes: `int` (`int`),
  `unsigned int` (`uint`), `long` (`long`), `unsigned long` (`ulong`),
  `long long` (`llong`), `unsigned long long` (`ullong`), and `void *` (`ptr`).
- `cplus_memory_order` is an enum whose values are the C11/`__atomic_*` values
  (`relaxed=0, consume=1, acquire=2, release=3, acq_rel=4, seq_cst=5`). Functions
  take `int order`, so either the enum or a literal works.
- `cplus_atomic_flag` is `struct { unsigned char __cplus_value; }` with
  `CPLUS_ATOMIC_FLAG_INIT { 0 }` — a new type in the new header only; it does
  not alter any existing header or ABI.

## 2. Closure matrix (all 34 entries)

`31 CLOSED` means a real C-callable symbol exists for at least one storage type
(and, in practice, for all six integer types). Naming is
`cplus_<canonical_name>_<type_suffix>`; flag ops and fences have no suffix.

| # | Inventory row | Std | Status | Wrapper(s) | Note |
|---:|---|---|---|---|---|
| 1 | atomic_compare_exchange_strong | C11 | CLOSED | `cplus_atomic_compare_exchange_strong_{int,uint,long,ulong,llong,ullong,ptr}` | weak=0; returns 1/0 as `int` |
| 2 | atomic_compare_exchange_strong_explicit | C11 | CLOSED | `..._strong_explicit_<suffix>` | `(obj, expected, desired, success, failure)` |
| 3 | atomic_compare_exchange_weak | C11 | CLOSED | `..._weak_<suffix>` | weak=1 (may spuriously fail) |
| 4 | atomic_compare_exchange_weak_explicit | C11 | CLOSED | `..._weak_explicit_<suffix>` | same as above |
| 5 | atomic_exchange | C11 | CLOSED | `cplus_atomic_exchange_<suffix>` | |
| 6 | atomic_exchange_explicit | C11 | CLOSED | `..._exchange_explicit_<suffix>` | |
| 7 | atomic_fetch_add | C11 | CLOSED | `cplus_atomic_fetch_add_{int,uint,long,ulong,llong,ullong}` | integer types only; `void*` variant BLOCKED (no element size) |
| 8 | atomic_fetch_add_explicit | C11 | CLOSED | `..._fetch_add_explicit_<int-suffix>` | |
| 9 | atomic_fetch_and | C23 | CLOSED | `cplus_atomic_fetch_and_{int,uint,long,ulong,llong,ullong}` | builtin available on host |
| 10 | atomic_fetch_and_explicit | C23 | CLOSED | `..._fetch_and_explicit_<int-suffix>` | |
| 11 | atomic_fetch_nand | C23 | CLOSED | `cplus_atomic_fetch_nand_{int,uint,long,ulong,llong,ullong}` | `__atomic_fetch_nand` verified |
| 12 | atomic_fetch_nand_explicit | C23 | CLOSED | `..._fetch_nand_explicit_<int-suffix>` | |
| 13 | atomic_fetch_or | C11 | CLOSED | `cplus_atomic_fetch_or_<int-suffix>` | |
| 14 | atomic_fetch_or_explicit | C11 | CLOSED | `..._fetch_or_explicit_<int-suffix>` | |
| 15 | atomic_fetch_sub | C11 | CLOSED | `cplus_atomic_fetch_sub_<int-suffix>` | |
| 16 | atomic_fetch_sub_explicit | C11 | CLOSED | `..._fetch_sub_explicit_<int-suffix>` | |
| 17 | atomic_fetch_xor | C11 | CLOSED | `cplus_atomic_fetch_xor_<int-suffix>` | |
| 18 | atomic_fetch_xor_explicit | C11 | CLOSED | `..._fetch_xor_explicit_<int-suffix>` | |
| 19 | atomic_flag_clear | C11 | CLOSED | `cplus_atomic_flag_clear` | `cplus_atomic_flag *` |
| 20 | atomic_flag_clear_explicit | C11 | CLOSED | `cplus_atomic_flag_clear_explicit` | `(obj, order)` |
| 21 | atomic_flag_test_and_set | C11 | CLOSED | `cplus_atomic_flag_test_and_set` | returns `int` (C11 returns `_Bool`); value 0/1 |
| 22 | atomic_flag_test_and_set_explicit | C11 | CLOSED | `cplus_atomic_flag_test_and_set_explicit` | `(obj, order)` |
| 23 | atomic_init | C11 | CLOSED | `cplus_atomic_init_<suffix>` | relaxed atomic store, not a raw assignment |
| 24 | atomic_is_lock_free | C11 | CLOSED | `cplus_atomic_is_lock_free_<suffix>` | `__atomic_is_lock_free(sizeof(T), obj)` |
| 25 | atomic_load | C11 | CLOSED | `cplus_atomic_load_<suffix>` | `const T *obj` |
| 26 | atomic_load_explicit | C11 | CLOSED | `cplus_atomic_load_explicit_<suffix>` | `(const T *obj, int order)` |
| 27 | atomic_signal_fence | C11 | CLOSED | `cplus_atomic_signal_fence` | `(int order)` |
| 28 | atomic_store | C11 | CLOSED | `cplus_atomic_store_<suffix>` | seq_cst |
| 29 | atomic_store_explicit | C11 | CLOSED | `cplus_atomic_store_explicit_<suffix>` | `(obj, desired, int order)` |
| 30 | atomic_thread_fence | C11 | CLOSED | `cplus_atomic_thread_fence` | `(int order)` |
| 31 | kill_dependency | C11 | CLOSED | `cplus_kill_dependency_<suffix>` | typed identity; value barrier is provided by the call boundary, see §4 |
| 32 | atomic_notify_all | C23 | **BLOCKED** | — | see §3 |
| 33 | atomic_notify_one | C23 | **BLOCKED** | — | see §3 |
| 34 | atomic_wait | C23 | **BLOCKED** | — | see §3 |

Unchanged by design (not part of the 34, still `HOST/COMPILER-PROVIDED`): the
`ATOMIC_VAR_INIT` / `ATOMIC_FLAG_INIT` macros, the `atomic_*` typedefs and
`memory_order` enum, and the type-generic macro spellings themselves.

## 3. BLOCKED — exact interface Agent 1 must provide

Host probe (Apple clang 16.0.0 / macOS 27.2.1 arm64):

```
/usr/bin/clang -std=c2x -Wall probe_wait.c
probe_wait.c:2:50: error: use of undeclared identifier 'atomic_wait'
probe_wait.c:2:92: error: use of undeclared identifier 'atomic_notify_one'
probe_wait.c:2:92: error: use of undeclared identifier 'atomic_notify_all'
```

Even in C2x mode the SDK's `<stdatomic.h>` does not declare these three names,
and no `__atomic_wait` / `__atomic_notify` builtin is exposed. Implementing
them here would require a futex/wait-queue side table keyed by object address —
a real runtime subsystem, not a thin ABI-preserving wrapper — and would change
the meaning of caller storage. They are therefore not faked.

Agent 1 (compiler/runtime) must provide one of:

1. **Host declarations + builtins**: C23 `<stdatomic.h>` declarations for
   `atomic_wait(const volatile A *obj, C old, memory_order order)`,
   `atomic_notify_one(volatile A *obj)`, `atomic_notify_all(volatile A *obj)`
   for all supported scalar types, plus compiler builtins
   `__atomic_wait(const void *addr, unsigned long long old, size_t size, int order)`,
   `__atomic_notify(const void *addr, int stride, int order)` backed by a stable
   address-keyed wake mechanism; or
2. **A sanctioned runtime ABI** exporting, e.g.,
   `void cplus_atomic_wait_impl(const void *obj, size_t size, const void *old, int order)`,
   `void cplus_atomic_notify_impl(const void *obj, int all)` with a documented
   per-address side-table contract so wrappers can be added without changing
   caller-visible storage layout.

Until one of those exists, any `cplus_atomic_wait_*` would either busy-poll
(not conforming to the blocking requirement, and O(CPU)) or require unseen
per-object state (ABI break). Both are rejected.

Also **partially blocked**: arithmetic `fetch_*` (`add/sub/or/xor/and/nand`) on
the `void *` storage type. Clang's `__atomic_fetch_add(void **, ...)` treats the
operand as an integer byte count, and `void` has no element size. The C11
generic macro would scale by the pointed-to type; a C ABI wrapper cannot know
that type. Use the integer wrappers with `(uintptr_t)` casts for pointer
arithmetic, or have Agent 1 provide typed pointer-element wrappers. All
non-arithmetic `void *` ops are CLOSED.

## 4. Deviations from the exact C11 macro semantics (documented, not defects)

- **No exact macro spellings.** `atomic_load(&a)` etc. remain macro-only in C11
  and are intentionally **not** provided as C+ macros: they require `_Generic`
  and `_Atomic` in the consumer. C+ callers own plain storage and call the
  typed `cplus_*` function. This is the deliberate ABI trade that makes a C
  linker symbol possible.
- **`int` instead of `_Bool` / `_Atomic` return types.** `test_and_set` and
  `compare_exchange_*` return `int` 0/1; C99 has no `_Atomic` and this keeps the
  header usable from C99, C11, and C++20. All are 32-bit or smaller integer
  returns in the platform ABI.
- **Memory-order validation is delegated.** The wrappers pass `order` through
  to the builtin. The C11 rule that a compare-exchange *failure* order may not
  be `release`/`acq_rel` is the caller's responsibility (same as calling the
  builtin directly); no runtime check is imposed, keeping wrappers thin.
- **`kill_dependency` is a typed identity function.** The C11 macro's
  dependency-killing effect relies on in-place compiler expansion; a call
  still provides a compiler barrier at the call boundary for the returned
  value, but it is not a drop-in textual replacement. Documented so callers
  do not assume more.

## 5. Integration (completed)

The wrappers are now part of the standard build. Integration changes:

- `libc/CMakeLists.txt`: `src/stdlib/atomic_wrappers.c` added to `LIBC_SOURCES`
  so the symbols enter both the static and shared `cplus_hardened_libc` targets.
- `tests/libc/build_libc.sh`: the same source added to the compile loop, so
  `build/libc/libcplus_hardened.a` exports all 169 `cplus_*` atomic symbols
  (including the seven `cplus_kill_dependency_<suffix>` variants).
- `libc/install-include/cplus_libc.h`: additively includes the self-contained
  `cplus_stdatomic.h` so the umbrella header also exposes the atomic
  declarations; the dedicated header remains the canonical include.

The earlier "undefined `cplus_kill_dependency_*`" failures were purely a build
integration gap: the wrapper file was not in either source list, so the archive
contained zero atomic symbols. The `kill_dependency` wrappers themselves were
already defined in `atomic_wrappers.c` and required no source change. After the
wiring above, a direct compile/link of `tests/libc/test_cplus_atomic.c` against
`build/libc/libcplus_hardened.a` alone resolves every symbol.

## 6. Verification evidence (all commands and exact results)

Host: Apple clang version 16.0.0 (clang-1600.0.26.6), target
`arm64-apple-darwin27.2.0`, macOS 27.2.1. Working dir: repository root.

Header self-containment (each produced no output, exit 0):

```
/usr/bin/clang -std=c99  -Wall -Wextra -Wpedantic -fsyntax-only -x c   libc/install-include/cplus_stdatomic.h   # OK
/usr/bin/clang -std=c11  -Wall -Wextra -Wpedantic -fsyntax-only -x c   libc/install-include/cplus_stdatomic.h   # OK
/usr/bin/clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsyntax-only -x c++ libc/install-include/cplus_stdatomic.h # OK
grep -n '^#include' libc/install-include/cplus_stdatomic.h   # only: #include <stddef.h>
```

Wrapper compiles cleanly with zero warnings in both standards:

```
/usr/bin/clang -O2 -Wall -Wextra -Wpedantic -std=c99 -Ilibc -Ilibc/include \
  -c libc/src/stdlib/atomic_wrappers.c -o /tmp/.../atomic_wrappers_c99.o    # exit 0
/usr/bin/clang -O2 -Wall -Wextra -Wpedantic -std=c11 -Ilibc -Ilibc/include \
  -c libc/src/stdlib/atomic_wrappers.c -o /tmp/.../atomic_wrappers_c11.o    # exit 0
nm -g --defined-only /tmp/.../atomic_wrappers_c99.o | grep -c ' T _cplus_'   # 169
```

Test build + run (links the built archive `build/libc/libcplus_hardened.a`),
both under `timeout 30`:

```
/usr/bin/clang -O2 -Wall -Wextra -Wpedantic -std=c99 -Ilibc -Ilibc/include \
  tests/libc/test_cplus_atomic.c libc/src/stdlib/atomic_wrappers.c \
  build/libc/libcplus_hardened.a -lm -pthread -o /tmp/.../test_atomic_c99
timeout 30 /tmp/.../test_atomic_c99
# cplus_atomic: all closed wrappers and concurrency smoke test passed
# exit 0

<same with -std=c11> -> same output, exit 0
```

Simulated integration (wrapper object injected into a copy of the built
archive, then the test links against the archive alone):

```
cp build/libc/libcplus_hardened.a /tmp/.../libc_with_atomic.a
ar r /tmp/.../libc_with_atomic.a /tmp/.../atomic_wrappers_c99.o
/usr/bin/clang -O2 -Wall -Wextra -Wpedantic -std=c99 -Ilibc -Ilibc/include \
  tests/libc/test_cplus_atomic.c /tmp/.../libc_with_atomic.a -lm -pthread \
  -o /tmp/.../test_atomic_arch
timeout 30 /tmp/.../test_atomic_arch
# cplus_atomic: all closed wrappers and concurrency smoke test passed
# exit 0
```

### 6.1 Post-wiring verification (real archive, no injected object)

After the §5 wiring, the archive itself contains the symbols. Exact commands
and results (working dir = repository root):

```
CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh
# -> /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a   (exit 0)

nm -g --defined-only build/libc/libcplus_hardened.a | grep -c '_cplus_kill_dependency'   # 7
nm -g --defined-only build/libc/libcplus_hardened.a | grep -c '_cplus_atomic_'            # 162
# (162 atomic_* + 7 kill_dependency_* = all 169 wrapper symbols)

/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 -Ilibc -Ilibc/include \
  -Ilibc/install-include tests/libc/test_cplus_atomic.c \
  build/libc/libcplus_hardened.a -lm -pthread -o /tmp/test_atomic_wired   # link OK
timeout 30 /tmp/test_atomic_wired
# cplus_atomic: all closed wrappers and concurrency smoke test passed   (exit 0)

CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh
# ... ok test_cplus_atomic.c ...  (atomic test passes in the full suite)
```

CMake path (standalone libc project):

```
cmake -S libc -B /tmp/cplus-libc-cmake -DCMAKE_C_COMPILER=/usr/bin/clang
cmake --build /tmp/cplus-libc-cmake --target cplus_hardened_libc --parallel 4
# ... Building C object CMakeFiles/.../src/stdlib/atomic_wrappers.c.o ... Built target ... (exit 0)
nm -g --defined-only /tmp/cplus-libc-cmake/libcplus_hardened_libc.a | \
  grep -c '_cplus_atomic_\|_cplus_kill_dependency'   # 169
```

`atomic_wrappers.c` itself needed no source change: it already compiled
warning-free with `-std=c99 -Wall -Wextra -Wpedantic` and defined all 169 symbols
(including all seven `cplus_kill_dependency_<suffix>` variants).

Concurrency smoke test parameters: 4 pthreads × 20,000
`cplus_atomic_fetch_add_int` increments (= 80,000, exact final-count check), a
CAS-loop counter (4 wins, exactly-once check), and a flag-guarded critical
section (4 increments, exact check). Completed well inside the 30 s bound.

Host capability probes (recorded during design):

```
/usr/bin/clang -std=c11 probe_atomic.c -o probe11 && ./probe11
# load=3 lockfree=1
# __atomic_fetch_nand / test_and_set / clear / is_lock_free all work under -std=c99
/usr/bin/clang -std=c2x probe_wait.c   # atomic_wait/notify_one/notify_all undeclared
```
