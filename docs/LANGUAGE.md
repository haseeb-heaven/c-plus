# The C+ language

C+ is C17 (compiled in `gnu17` mode) plus four additions. Everything not
described here is ordinary C and is passed through to the C compiler
unchanged.

- [`resource struct`](#resource-struct) — a struct with deterministic destruction
- [`class`](#class) — a struct plus ordinary functions, with call sugar
- [`managed`](#managed-memory) — scope-owned heap allocation
- [`owned`](#owned-declarations) — explicit spelling of an owning local

The reserved words in C+ source are `resource`, `class`, `managed`, `owned`
and `self`. Plain C code that uses them as identifiers must be compiled with
`--legacy-c`.

## Resource structs

```c
resource struct File {
    FILE *handle;
};

void File_drop(File *self) {
    if (self->handle != NULL) {
        fclose(self->handle);
        self->handle = NULL;
    }
}
```

`resource struct T` lowers to `struct T` plus `typedef struct T T;`, so the
type name can be used without the `struct` tag. The destructor must be a
function named `T_drop(T *self)` with external linkage and must be visible in
the translation unit before the resource is used.

Destructors must tolerate a zeroed handle: a resource local without an
initialiser is zero-initialised, and an explicitly moved-from handle is zeroed
by `cplus_move`.

### Lifetime rules

An owning local of a resource type is destroyed:

- at the end of its lexical scope, in reverse order of declaration;
- before `return`, `break` or `continue` that leaves its scope;
- never after an ownership transfer (`cplus_move`, or returning the local).

```c
int find(int id) {
    File f = File_open("index.txt");
    if (id < 0)
        return -1;              /* f is closed here */
    /* ... */
    return 0;                   /* f is closed here too */
}
```

The following are rejected at compile time:

- copying a resource (`File b = a;` when `a` is owning);
- assigning one owning resource to another (`b = a;`);
- redeclaring the same owner twice in one statement.

A local of a resource type without an initialiser is zero-initialised and the
destructor is still inserted; the destructor should treat a zeroed handle as
empty.

### Ownership transfer

```c
File f = File_open("x");
File g = cplus_move(f);   /* f becomes zeroed, g owns the handle */
```

Returning an owning local transfers ownership to the caller automatically;
no `cplus_move` is needed, and no destructor runs for the returned local.
`cplus_move` is a macro that uses a GNU statement expression and `memset`.

Borrowing is just a plain pointer (`File *borrowed = &f;`). It is not tracked;
see [limitations](LIMITATIONS.md).

## Class

```c
class Counter {
    int value;

    void add(int n) {
        self->value += n;
    }

    int get(void) {
        return self->value;
    }
};
```

This lowers to:

```c
struct Counter { int value; };
typedef struct Counter Counter;

void Counter_add(Counter *self, int n) { self->value += n; }
int  Counter_get(Counter *self) { return self->value; }
```

Fields are emitted verbatim into the struct and methods must be defined
inline in the class body. Inside a method, `self` is the instance pointer.
`p.method(args)` lowers to `Counter_method(&p, args)` and
`p->method(args)` lowers to `Counter_method(p, args)`.

Method-call sugar is resolved from simple declarations of the form
`Counter p;` and `Counter *p;` in the same translation unit. There is no type
inference or overload resolution: if the receiver's class cannot be
determined this way, write the plain function call.

Not supported: constructors, destructors as class members, inheritance,
interfaces, virtual dispatch, access control and methods declared outside the
class body. Use aggregate initialisation (`Counter c = {0};`) instead of
constructors.

## Managed memory

```c
managed Buffer *b = cplus_new(Buffer);
managed int *a = cplus_new_array(int, 64);
```

`managed` lowers to an ordinary pointer declaration with an ownership marker.
`cplus_new(T)` and `cplus_new_array(T, N)` allocate zeroed memory through the
C+ runtime. The allocation is released exactly once, at scope exit, with
`cplus_release` followed by assigning `NULL` to the local.

Rejected at compile time:

- returning a managed pointer from its owning scope;
- reassigning a managed local (the owner must stay unique);

detected at run time:

- `cplus_release` on a pointer that was not allocated by C+ (exit 105);
- releasing the same pointer twice (exit 104).

The allocator keeps live allocations in a hash table and a bounded cache of
recently freed addresses, so these checks do not scan an ever-growing list.
It does **not** detect a use-after-free where the freed address has already
been reused for another allocation; see [limitations](LIMITATIONS.md).

## Owned declarations

```c
owned File f = File_open("x");
```

`owned` is the explicit form of an owning resource local. It requires a
`resource struct` type and an initialiser; otherwise the compiler emits a
diagnostic. A plain declaration of a resource type (`File f = ...;`) has the
same ownership semantics but allows omitted initialisation (zero-init).

## Diagnostics and source mapping

The lowering stages emit `#line` directives so compiler diagnostics refer to
the original `.c` file and line, not to the generated temporary. Use
`--emit-c FILE` to inspect the exact C that was compiled:

```sh
c+ build demos/C_resource.c --emit-c /tmp/lowered.c --keep-temp -o demo
```

`#line` directives also mean `__FILE__` and `__LINE__` in your source keep
their original values.

## Compile modes

| mode | syntax lowering | runtime checks | safety claim |
| --- | --- | --- | --- |
| default (safe) | yes | UBSan bounds/null/alignment/undefined, abort on first failure | memory safety for the supported subset |
| `--release` | yes | same checks, `-O2` | same |
| `--legacy-c` | yes | none | **none**; behaves like plain C |

`--legacy-c` does not turn off the C+ source lowering; it turns off the safety
instrumentation and warnings. Use it only for compatibility experiments.
