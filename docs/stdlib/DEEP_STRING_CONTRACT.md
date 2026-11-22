# Deep `stdlib/string.hp` contract

This document records the behaviour verified by the two independent deep tests
for `stdlib/string.hp`:

- `tests/stdlib/deep_string_append.cp` — append / reserve / drop / failure paths
- `tests/stdlib/deep_string_view.cp` — borrowed `string_view` lifetime

Neither test has a sibling `.expected` file: every assertion is self-checking,
and the exact stdout is pinned here instead.

No source change to `stdlib/string.hp` was required. All behaviour below was
reproduced first; nothing in the library was relaxed to make a test pass.

## Build / run matrix

| Mode | Command | Result |
| --- | --- | --- |
| default (Safe + UBSan) | `./build/c+ build tests/stdlib/<name>.cp -o /tmp/<name>` | exit 0 |
| strict, no sanitizer | `./build/c+ build tests/stdlib/<name>.cp --strict --sanitize=none -o /tmp/<name>_ns` | exit 0 |
| harness | `./tests/stdlib/run_stdlib_tests.sh deep_string` | 2 passed, 0 failed |

Default mode is the C+ Safe mode and adds
`-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all` to both
the lowered test and the in-tree runtime. `--strict --sanitize=none` keeps Safe
mode's warning set and `-DCPLUS_SAFE=1` but links the prebuilt unsanitized
runtime; no warnings-as-errors were triggered by either test.

`--sanitize=address` is **not available** with this backend (the driver probes
it and reports "sanitizer 'address' is not supported by the selected backend
(/usr/bin/clang)"). UBSan is therefore the only sanitizer evidence available;
ASan-specific findings are explicitly out of scope here.

`deep_string_append.cp` defines `STDLIB_ALLOC_LIMIT` to 4096 before including
`stdlib/error.hp`. This is the allocator's documented build-time override and
is used only so the `count + extra + 1 > LIMIT` overflow branch can be reached
with small inputs. It does not alter `string.hp`.

## Exact expected stdout and exit codes

`deep_string_append` (81 checks, 0 failures), both modes:

```
append-success 1
append-recoverable 1
append-fatal 1
append-checks 81
append-failures 0
```

`deep_string_view` (57 checks, 0 failures), both modes:

```
view-success 1
view-recoverable 1
view-fatal 1
view-checks 57
view-failures 0
```

Both return `0`. stderr is empty in the parent; the two deliberate fatal probes
in the append test and the one in the view test emit their `C+ runtime error:`
lines from forked children only (captured and discarded by the test harness).

## Contract decisions

### Constructors and null arguments

| Operation | Verified result | Decision |
| --- | --- | --- |
| `string(NULL)` | `valid()==1`, `size()==0`, `capacity()==1`, `c_str()==""` | NULL *constructor* text is documented as `""`; allocation still happens. |
| `string("")` | `valid()==1`, `size()==0`, `capacity()==1` | Empty is a valid owned string. |
| `append(NULL)` | `-1`, string byte-for-byte unchanged, INVALID note + `CPLUS_EXIT_UNSUPPORTED` (108) | NULL append is an invalid *operation*, not an empty append. |
| `equals(NULL)` | `0`, INVALID note + 108 | Never equal. |
| `compare(NULL)` | `1`, INVALID note + 108 | "never equal" ordering result. |
| `find(NULL)` | `-1`, **no** note | Silently not found; matches the guard in `find()`. |
| `copy_from(NULL)` | `-1`, INVALID note + 108 | Invalid source. |
| `substr(...,NULL,...)` / `field(...,NULL,...)` | `-1`, **no** note | Output-buffer guards are silent. |
| `string_view(NULL, 3)` | `size()==3`, `data()==NULL`, `equals(...)==0`, `is_valid_utf8()==0` | A view with count>0 and NULL data is inert but reports its snapshot size. |
| `string_view(NULL, 0)` / zero-init view | `size()==0`, `data()==NULL`, `equals("")==1`, `is_valid_utf8()==1` | Empty window is valid regardless of pointer. |
| `view.equals(NULL)` | `0` | Null is never equal, empty window included. |

### Append algebra

| Operation | Verified result |
| --- | --- |
| unrelated append `"ab" + "cdef"` | `"abcdef"`, `size()==6`, `capacity()==7` (doubling 3→6 clamped up to exact need 7) |
| append fitting spare capacity | pointer identity of `c_str()` unchanged; no reallocation |
| full self-append `"hello" + itself` | `"hellohello"`, `size()==10`, `capacity()==12` |
| six repeated self-appends of `"x"` | 64 `x` characters; alias path correct for every growth step |
| interior pointer `c_str()+3` of `"abcdef"` **with reallocation** | `"abcdefdef"`, `size()==9`, `capacity()==14` — the source is re-based through the saved offset before `memmove` |
| interior pointer `c_str()+7` of `"0123456789"` **without reallocation** | `"0123456789789"`, pointer unchanged; `memmove` handles the overlap |
| interior pointer at `count-1` | last character appended correctly |
| appending the terminator `c_str()+size()` | empty append; contents and capacity unchanged |
| `append("")` on empty string | returns 0, no growth |

The implementation's integer-address alias test intentionally avoids
relational comparison of unrelated pointers (UB). Interior pointers that lie
in `[chars, chars+count]` are re-based to an offset, so a growing append never
dereferences the freed source buffer.

### Reserve boundaries and failure atomicity

| Operation | Verified result |
| --- | --- |
| `reserve(want <= capacity)` | returns 0, no allocation, contents preserved |
| `reserve(64)` from capacity 3 | returns 0, `capacity()==64`, contents preserved |
| `reserve(LIMIT+1)` | `-1`, unchanged, RANGE category + `CPLUS_EXIT_BOUNDS` (101) note |
| `append` whose `count+extra+1 > LIMIT` | `-1`, unchanged, same RANGE/BOUNDS note |
| injected failure of the single allocation inside `append` | `-1`, pointer/size/capacity/content all unchanged, MEMORY category + `CPLUS_EXIT_RESOURCE` (107) note |
| `fail_at(2)` during append | succeeds: `append` performs exactly one allocation |
| injected failure inside `reserve` | `-1`, fully atomic |
| `fail_after(0)` (all later allocations fail) | append atomic, returns `-1` |
| injected failure of constructor allocation | `valid()==0`, `size()==0`, `capacity()==0`, `c_str()==""`, `equals("")==1`; later `append` re-initialises |

Note on categories: `string.hp` (like `vector.hp`) reports size-limit violations
with `STDLIB_CATEGORY_RANGE` and `CPLUS_EXIT_BOUNDS`. `stdlib_note()` buckets
RANGE into the "invalid" telemetry counter (`STDLIB_TM_INVALID`), so telemetry
alone cannot distinguish a range failure from a null-argument failure; the
callback's `(category, code)` pair and the `-1` return value can. This is a
library-wide convention, not a `string.hp` defect.

### Operations after drop

`drop()` sets `chars=NULL`, `count=0`, `space=0`. A second `drop()` is inert.
Afterwards `size()==0`, `capacity()==0`, `c_str()==""` (never NULL),
`equals("")==1`, `compare("x")==-1`, `find(...)==-1`, `at(...)=='\0'`.
`append()`, `reserve(n>0)` and `copy_from()` re-initialise the object and
restore `valid()==1`.

Contract nuance: `reserve(0)` after `drop()` is a successful *no-op* because no
buffer is needed, so `valid()` stays 0 until a positive reserve, append or
copy_from. The header's "re-initialises on the next successful reserve" should
be read as "the next successful reserve that allocates".

### `string_view` lifetime and invalidation

A view is a **borrowed, fixed window**: `data()` and `size()` are a snapshot of
`to_view()` time.

- While the owner is neither reallocated nor dropped, `data()` is stable and
  dereferenceable.
- An in-place append (fit in spare capacity) leaves `data()` pointing at the
  same live buffer and does **not** grow `size()`. A view of `"abc"` taken
  before `append("de")` still has `size()==3` and `equals("abc")==1`, and
  `equals("abcde")==0`.
- A reallocating append or `drop()` **invalidates** the view's pointer. The
  test detects this without dereferencing the stale pointer: the allocator's
  generation for the old buffer is no longer current
  (`stdlib_alloc_check_generation(old, gen)==0`) and the new buffer has a
  different generation.
- Calling `equals()` or `is_valid_utf8()` on a stale view is a contract
  violation (undefined behaviour / use-after-free) and is never done by the
  tests. It is a caller rule, not something the borrowed type can enforce.
- `string_view` owns nothing: it has no `drop()`, and its construction or
  destruction never allocates or frees.

### UTF-8 windows

Verified over borrowed windows: ASCII and multi-byte (`é`, `€`) accepted;
overlong `C0 80`, surrogate `ED A0 80`, above-U+10FFFF `F4 90 80 80`,
truncated `E2 82`, stray continuation `80`, and `C3 28` rejected; embedded NUL
and the maximal `F4 8F BF BF` accepted. `equals()` takes a NUL-terminated
argument, so a window containing an embedded NUL cannot equal the full window
string.

## Fatal boundary

The public `string` / `string_view` API produced **no fatal exit** for any
success or recoverable case in the matrix above. To pin the boundary, each test
forks a child that deliberately performs a raw operation the API guards
against and checks the exact `_Exit` status:

| Child probe | Fatal code | Observed |
| --- | --- | --- |
| two `stdlib_release` calls on one `stdlib_alloc` block | `CPLUS_EXIT_DOUBLE_FREE` = 104 | 104 |
| `cplus_check_live` on a stale `c_str()` / view buffer after `drop()` | `CPLUS_EXIT_TYPE_CONFUSION` = 105 | 105 |

This is why `string::drop()` nulls its handle and why a stale `string_view`
must not be dereferenced. The fatal messages appear only on the children's
stderr; the parent asserts `WIFEXITED && WEXITSTATUS == expected` and stays
alive.

## Latent nuance found but not changed

`append()` decides whether the source aliases its buffer using the range
`[chars, chars + count]`. If a caller takes an interior pointer *past* `count`
(possible after `clear()`, which keeps the buffer but sets `count=0`) and appends
from it, the alias flag is not set and a plain `memcpy` is used. On this
platform that memcpy direction is forward-copy and produced the correct result
in a direct probe (`string("abcdef"); clear(); append(c_str()+1)` → `"bcdef"`
in default, strict/no-sanitizer, and release builds), so **no defect was
reproduced** and `string.hp` was not modified.

For completeness: such a pointer can never force a reallocation, because after
`clear()` the source length is bounded by `space-1`, hence `need == extra+1 <=
space`. The only theoretical issue is a standard-level overlapping-`memcpy`
UB that this backend does not diagnose (ASan's `memcpy-overlap` check is
unavailable here). Changing the bound to `chars + space` would be a
robustness improvement, but the brief asks for fixes only for reproduced
defects, so it is recorded here instead of applied.
