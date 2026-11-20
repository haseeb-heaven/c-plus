# C+ standard library (`.hp`)

The C+ standard library is a set of `.hp` headers written in C+ itself. It is
**a typed, RAII wrapper layer over libc, POSIX and the C+ runtime**, not a
rewrite of those libraries. Ordinary C headers (`stdio.h`, `stdlib.h`,
`string.h`, sockets, pthreads, ...) remain available and are used underneath.

Include a header with its normal path:

```c
#include "stdlib/string.hp"
```

The compiler lowers each `.hp` header to a temporary `.h` with `#line`
mapping, and shares class/constructor/owning-type metadata with the including
translation unit.

## Headers

| header | contents | underlying API |
| --- | --- | --- |
| `stdlib/string.hp` | `class String` with `len()`, `at()`, `c_str()`, RAII `drop()` | `strlen`, `memcpy`, `cplus_alloc` |
| `stdlib/vector.hp` | `class IntVec` with `push()`, `at()`, `len()`, RAII `drop()` | `cplus_alloc` / `cplus_release` |
| `stdlib/file.hp` | `resource File`, `File_open()`, `File_write()` | `fopen`, `fputs`, `fclose` |
| `stdlib/thread.hp` | `resource Mutex` / `Guard`, `Mutex_new()`, `Guard_lock()` | pthread wrappers in the C+ runtime |
| `stdlib/io.hp` | format-free `println()`, `echo_int()` | `print()` from `cplus/cplus.h` |
| `stdlib/net.hp` | `resource Listener` / `Connection`, bind/accept | POSIX sockets |

## Example

```c
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/thread.hp"

int main(void) {
    String name = String("C+");
    IntVec scores = IntVec();
    scores.push(10);
    scores.push(20);
    print("name", name.c_str(), "count", scores.len());

    Mutex mutex = Mutex_new();
    {
        Guard guard = Guard_lock(&mutex);
        /* critical section */
    }
    return 0; /* all destructors run */
}
```

## Design notes

- Every owning type has a `drop` (or `T_drop`) function and gets deterministic
  cleanup at scope exit, including early returns.
- `String` and `IntVec` allocate through the C+ allocator, so double release
  and invalid release are detected.
- The library does not hide C: you can still pass `FILE *`, `int` sockets and
  `pthread_mutex_t` to existing C APIs through the wrapper's fields.
- `.h` files remain fully supported; these headers are additive.
- This is a small, auditable MVP layer, not a complete replacement for the C
  standard library, C++ STL, or a framework.
