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
| `stdlib/error.hp` | `StdError` recoverable error value, `STDLIB_OK`/`EOF`/`FAIL` | `cplus_exit_code` |
| `stdlib/string.hp` | `class String` (`len`, `at`, `c_str`, `eq`, `compare`, `find`, `append`, `reserve`, `clear`, `substring`, `field_count`, `field`, `view`, `drop`) and borrowed `StringView` | `strlen`, `memcmp`, `strstr`, `cplus_alloc` |
| `stdlib/vector.hp` | `class IntVec` (`push`, `pop`, `get`, `at`, `reserve`, `clear`, `len`, `capacity`, `drop`) | `cplus_alloc` / `cplus_release` |
| `stdlib/file.hp` | `resource File`, `File_open`, `File_borrow`, `File_read`, `File_read_line`, `File_write`, `File_write_bytes`, `File_seek`, `File_flush`, `File_valid` | `fopen`, `fread`, `fgets`, `fputs`, `fwrite`, `fseek`, `fflush`, `fclose` |
| `stdlib/thread.hp` | `resource Mutex` / `Guard`, `Mutex_new`, `Guard_lock`, `Mutex_valid`, `Guard_holds` | pthread wrappers in the C+ runtime |
| `stdlib/io.hp` | format-free `println()`, `echo_int()` and recoverable `read_line`, `read_int`, `read_long`, `read_float`, `read_double` | `print()` from `cplus/cplus.h`; `fgets`, `strtol`, `strtoll`, `strtof`, `strtod`, `errno` |
| `stdlib/net.hp` | `resource Listener` / `Connection`, `Listener_bind`, `Listener_borrow`, `Connection_accept`, validity checks | POSIX sockets |
| `stdlib/json.hp` | `resource JsonDoc`, borrowed `JsonRef`, `resource JsonBuilder` over yyjson | `third_party/yyjson` (MIT) |

Detailed ownership rules, the recoverable-error model, vendored-upstream
provenance and the open compiler-side gaps are in
[`MODERN_STDLIB.md`](MODERN_STDLIB.md).

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
- Recoverable failures (bad input, end of input, missing file, malformed JSON)
  are returned as `StdError` values with a status code; they never abort.
- Destructors are zero-init safe: a default-constructed handle closes nothing,
  and socket destructors never touch descriptors 0, 1 or 2.
- Every function defined in a `.hp` is `static inline`, so the same header can
  be included from several translation units without duplicate symbols.  Class
  members are the one remaining exception (see `MODERN_STDLIB.md`).
- `String` and `IntVec` allocate through the C+ allocator, so double release
  and invalid release are detected.
- The library does not hide C: you can still pass `FILE *`, `int` sockets and
  `pthread_mutex_t` to existing C APIs through the wrapper's fields.
- `.h` files remain fully supported; these headers are additive.
- This is a small, auditable MVP layer, not a complete replacement for the C
  standard library, C++ STL, or a framework.
