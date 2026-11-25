# The C+ language

C+ is C17 (compiled in `gnu17` mode) plus four additions. Everything not
described here is ordinary C and is passed through to the C compiler
unchanged.

- [`resource struct`](#resource-struct) — a struct with deterministic destruction
- [`class`](#class) — a struct plus ordinary functions, with call sugar
- [`managed`](#managed-memory) — scope-owned heap allocation
- [`owned`](#owned-declarations) — explicit spelling of an owning local

The reserved words in C+ source are `resource`, `class`, `managed`, `owned`,
`auto` and `self`. Plain C code that uses them as identifiers must be compiled with
`--legacy-c`.

## File extensions

- `.cp` — a C+ source file (recommended when using modern features)
- `.hp` — a C+ header, lowered automatically when included from a `.cp`/`.c` source
- `.c` / `.h` — ordinary C, still fully supported; C+ features may also be used

A `.hp` header can contain classes, constructors, destructors and other C+
syntax. The driver lowers it to a temporary `.h` with `#line` mapping, and
class/constructor metadata is shared with the including translation unit so
that `obj.method()` and `T(...)` work across the boundary.

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

#### Shadowing

Every declaration is a distinct cleanup obligation, even when it reuses the
name of an outer owner. Each binding is destroyed exactly once, at the end of
the scope it was declared in, so an inner owner is destroyed before the outer
one it shadows:

```c
File f = File_open("a");
{
    File f = File_open("b");   /* a different binding, not a redeclaration */
}                              /* f ("b") is closed here */
/* f ("a") is still open until the end of the function */
```

A `return` inside the inner block destroys the inner binding and then the
outer one. Because the C name `f` no longer denotes the outer object at that
point, the lowering saves its address (`&f`) immediately before the shadowing
declaration and drops through that saved pointer. Ownership state (moved,
explicitly dropped, borrowed) is likewise per declaration: moving or dropping
the inner `f` does not affect the outer one.

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

## Borrowing

A pointer to a local resource or managed allocation is tracked as a borrow:

```c
File f = File_open("x");
FILE *view = &f.handle;   /* borrow of f */
fprintf(view, "data\n");  /* valid while f is alive */
```

The borrow checker rejects two mistakes:

```c
FILE *bad(void) {
    File f = File_open("x");
    FILE *view = &f.handle;
    return view;              /* error: borrow cannot escape the function */
}

Handle a = {7};
Handle *view = &a;
Handle b = cplus_move(a);
printf("%d\n", view->id);     /* error: borrow used after move */
```

Borrows are valid until the owner is moved or its scope ends. Copy the data
or return the owner instead. This is a targeted subset, not Rust-style
borrowing: there are no lifetime annotations, no aliasing/exclusivity rules,
and no field-level or container-level tracking. See
[limitations](LIMITATIONS.md).

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

### Constructors and destructors

A method named after the class is a constructor, and a `void drop(void)`
method is the destructor:

```c
class Point {
    int x;
    int y;

    Point(int a, int b) {
        self->x = a;
        self->y = b;
    }

    int sum(void) { return self->x + self->y; }

    void drop(void) {
        printf("drop %d\n", self->sum());
    }
};

Point p = Point(2, 3);   /* lowers to Point_ctor(2, 3) */
```

Inside methods, both `self` and `this` refer to the instance pointer;
`this->field` is C++-style sugar for the lowered `self->field`. Constructors
also accept `this->`.

The constructor lowers to `Point Point_ctor(int a, int b)` that zero-initialises
an aggregate, runs the body with `self` pointing at it, and returns it.
`Point(...)` call sugar is rewritten to `Point_ctor(...)`. A constructor may
not declare a return type and may not return early.

The destructor lowers to `void Point_drop(Point *self)`, is forward-declared
before generated code, and makes `Point` an owning type: a local `Point p`
gets the same deterministic cleanup as a `resource struct`. Destructors must
tolerate a zeroed handle, just like resource destructors.

Not supported: inheritance, interfaces, virtual dispatch, access control and
methods declared outside the class body.

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
c+ build apps/legacy_demos/C_resource.c --emit-c /tmp/lowered.c --keep-temp -o demo
```

`#line` directives also mean `__FILE__` and `__LINE__` in your source keep
their original values.

## Array length and type inference

A fixed array has a `len` property that lowers to its compile-time extent as
an integer literal:

```c
int arr[] = {10, 20, 30};
int fixed[5];
printf("%d %d\n", arr.len, fixed.len);   /* 3 5 */
```

`arr.len` works for `T name[] = {...}` and `T name[N]`, including inside class
methods. It is a literal, not a runtime lookup, so it can be used in loop
bounds and constant expressions.

Pointers created by the C+ allocator also have `.len`. Their length is queried
from the allocation size table and divided by `sizeof(*ptr)`:

```c
int *a = (int *)cplus_alloc(sizeof(int) * 3);
int *b = cplus_new_array(int, 5);
Buffer *obj = cplus_new(Buffer);

printf("%zu %zu %zu\n", a.len, b.len, obj.len);  /* 3 5 1 */
```

When the allocation call is visible, `.len` lowers to
`(size_expression) / sizeof(*ptr)`, which is an integer constant expression
for a fixed size — the C compiler folds it to a literal, so there is no run-time
lookup at all:

```c
int *p = (int *)cplus_alloc(sizeof(int) * 3);
_Static_assert(p.len == 3, "folded at compile time");
```

If the size expression refers to variables, C+ does not copy it to the use
site: doing so could reference an out-of-scope name. Such expressions fall
back to the allocator's O(1) side-table query, which is always valid. Only
purely constant size expressions (numbers and `sizeof`) are folded into the
generated C.

Querying `.len` on a released or foreign pointer is a fatal error, not a
garbage value. The pointer must have a known element type (`void *` cannot be
used because `sizeof(*ptr)` is invalid). A pointer from plain `malloc` is not
tracked and has no `.len`.

`auto name = init;` infers the type of `init` and lowers to
`__typeof__(init) name = init;`:

```c
auto n = arr.len;        /* int */
auto p = &arr[0];        /* int * */
for (auto i = 0; i < n; i++) { ... }
```

`auto` with an owning resource type is not tracked by the ownership analysis;
use the explicit resource type for such declarations.

## Default arguments

Parameters may have defaults; the compiler inserts them at each call site:

```c
int *get_data(size_t size = 0) {
    if (size == 0)
        size = 1;
    /* ... */
}

int add(int a, int b = 2, int c = 3) {
    return a + b + c;
}

get_data();        /* get_data(0) */
add(1);            /* add(1, 2, 3) */
add(1, 5);         /* add(1, 5, 3) */
```

The definition keeps only the parameter names; default expressions are copied
into every visible call. Defaults should be trailing: a non-trailing default
with no available default for a later parameter is a compile error.

## Range-based for

```c
int values[] = {10, 20, 30};
for (auto v : values) {
    print(v);
}
```

This lowers to a counted loop over `values.len`, with `v` declared as
`__typeof__(values[idx])`. It works for fixed arrays and C+ allocations whose
`.len` is visible in the translation unit. The range expression must be a
simple lvalue; side-effecting expressions are not supported.

## print and scan

`print(...)` needs no format specifiers. Each value is dispatched by its C
type, values are separated by spaces, and a newline ends the line:

```c
int n = 42;
double ratio = 2.5;
char label[] = "demo";
print("n=", n, "ratio=", ratio, "label=", label);
/* n= 42 ratio= 2.5 label= demo */
```

Supported print types are the integer families, `float`/`double`/`long
double`, `_Bool`, `char` and C strings. Objects of struct/class type are not
printed automatically — print their fields or use a method that returns a
supported value.

`scan(...)` also needs no format string and no explicit `&`:

```c
int n;
double d;
char name[32];
scan(n, d, name);
```

It reads whitespace-separated values from standard input. Strings are copied
into the destination array up to its `sizeof` (or 256 bytes for a `char *`),
and a failed conversion is a fatal input error (exit 109).

Both macros reserve the names `print` and `scan` in C+ code.

## auto with classes

`auto` can infer a class type from a compound literal, and method-call sugar
still works:

```c
auto state = (State){0};
state.add(3);        /* lowers to State_add(&state, 3) */
```

## Compile modes

| mode | syntax lowering | runtime checks | safety claim |
| --- | --- | --- | --- |
| default (safe, strict policy) | yes | UBSan bounds/null/alignment/undefined, abort on first failure | memory safety for the supported subset; **every C+ warning is an error** |
| `--release` | yes | same checks, `-O2` | same policy: optimization only, no safety weakening |
| `--legacy-c` | yes | none | **none**; behaves like plain C, warnings are not promoted by default |

**Default policy (issue #34).** Ordinary invocations of `c+ build`, `c+ check`,
`c+ run` and the gcc-style `c+ file.c` form use Safe mode and promote every C+
diagnostic of severity Warning to a compilation error. The C backend is invoked
with `-Werror` in addition to the individually promoted safety warnings, so
enabled host-Clang warnings are fatal too. `-Werror` remains accepted but is
redundant. `--strict` is an explicit spelling for the fail-closed analysis gates;
it is not required to obtain the default safety policy.

`-w` is accepted for gcc compatibility, but in Safe mode it is consumed by the
driver and **never forwarded to the backend**: it cannot suppress C+ diagnostics,
safety gates or warning promotion. Its only backend scope is `--legacy-c`.
`--no-werror` is an explicit diagnostic-only opt-out from warning promotion; it
does not disable any C+ safety analysis or runtime check and is not evidence of
safety in STRICT mode. `--legacy-c` is the compatibility escape hatch: by default
it neither runs C+ safety analysis nor promotes warnings; an explicit `-Werror`
still promotes backend warnings there.

Strict mode enforces only the implemented, documented safe subset. It does not
claim complete memory safety or general use-after-free detection, and a clean
result is per-translation-unit (see `docs/compiler/semantic_safety_inventory.md`).

Evidence: `tests/compiler/default_policy/results/summary.txt` (14 cases, exact
commands, exit codes, stdout and stderr).

### `--auto-runtime` (opt-in)

By default a runtime safety violation prints its diagnostic and terminates with
the documented exit code (101–111). Passing `--auto-runtime` changes that only
while a C+ `try` frame is live: the same diagnostic is printed, then the
violation is raised as a catchable C+ exception, so a `catch` handler can run
and execution continues after the `try`. With no live handler the process still
terminates with the original code — a defect is never silently ignored. The
flag is a driver option: it defines `CPLUS_AUTO_RUNTIME=1` for safe-mode builds
only, never for `--legacy-c`, and it does not alter the C ABI or any pointer
representation. Evidence: `tests/compiler/auto_runtime/results/summary.txt`.
