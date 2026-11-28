# Deep cross-family contract — `tests/stdlib/deep_cross_family.cp`

This is the independent, cross-family regression companion to the per-family
suites (`DEEP_STRING_CONTRACT.md`, `DEEP_ERROR_VECTOR_CONTRACT.md`,
`DEEP_COLLECTIONS_CONTRACT.md`, `DEEP_FILE_CONTRACT.md`, `DEEP_JSON_CONTRACT.md`,
`DEEP_THREAD_CONTRACT.md`). It exercises **interactions between** modern stdlib
headers rather than repeating a single-family matrix.

Scope of this change: **only** `tests/stdlib/deep_cross_family.cp` and this
document. No `.hp` header, compiler source, runner, or `.expected` file was
modified. The full runners were deliberately not executed.

## 1. What the test is

A single self-checking process. Every `cf_expect(cond, label)` increments a
check counter and, on a false condition, prints `CF-FAIL <label>` and increments
the failure counter. One summary line per scenario is printed as
`CF <n> <checks> <failures>`; the final line is

```
CROSS_FAMILY <checks> <failures>
```

and the process exits `0` only when `<failures> == 0`, `1` otherwise. There is
no `.expected` sidecar: the last line plus the exit code are the contract.

All owning locals use globally unique names (`cf1_*`, `cf2_*`, …). This is
required because the C+ RAII lowering tracks owned variables by name for a
whole function (`RaiiLowering::owned_`, see `DEEP_NET_CONTRACT.md` item 4 and
`DEEP_THREAD_CONTRACT.md` finding F3): a repeated name suppresses
zero-initialisation and the destructor of the later object.

No fatal-safety probe is executed. Every failure path used here is a documented
recoverable contract (`-1`/`STDLIB_FAIL`/invalid view/empty buffer). The test
never dereferences a stale pointer, never uses a moved-from owner, and never
uses a `json_view` after its document is dropped.

## 2. Scenario contracts

### CF1 — `string` key copied into `strmap` (13 checks)

| Step | Expected |
| --- | --- |
| `strmap::put(source.c_str(), 7)` | `0`, `size()==1`, `get("alpha")==7` |
| `source.clear()` (in place) | map key unchanged, old buffer byte becomes `'\0'` |
| `source.reserve(64)` + `append("beta")` (reallocates) | map still returns `7` for `"alpha"`; `"beta"` is absent |
| old buffer generation | `stdlib_alloc_check_generation(old, gen)==0` after reallocation |
| `source.drop()` | map still returns `7` for `"alpha"` |
| `map.remove("alpha")` | `0`; subsequent `get` is `-1`, size `0` |

This pins `strmap::put`'s owned-key copy (`stdlib_copy_key`) independently of
the source `string`'s lifetime — a cross-family ownership boundary.

### CF2 — vector of error codes, growth, injected OOM (15 checks)

`error::code()` values (`CPLUS_EXIT_IO`, `_RESOURCE`, `_BOUNDS`, `_OWNERSHIP`,
`_UNSUPPORTED`, …) are pushed into a `vector`. With the vector full at
capacity 8, `stdlib_alloc_fail_after(0)` makes the next `push_back` return
`-1` with size, capacity, contents, generation/slice freshness and telemetry
`alloc_failures` delta pinned (exactly one injected failure). After
`stdlib_alloc_fail_reset()` the same push succeeds with capacity 16 and the
old `slice` becomes stale and refuses reads. `reserve(64)` is then failed and
retried the same way. The final `free_count == alloc_count` check is taken
with telemetry enabled from the first allocation of the scenario (resetting
telemetry *after* allocations would erase the matching allocations and produce
a false imbalance).

No test-side out-of-range indexing is used: the expected-value array has 9
elements and the verification loop is bounded by 9. An earlier draft indexed
element 8 of an 8-element array; the C+ compiler correctly rejected that with
`[CPLUS-BOUNDS]` under `--sanitize=undefined`. The test was fixed by enlarging
the array, not by weakening the rule.

### CF3 — moved `intmap` + generation-observed stale alias (13 checks)

An `intmap` is filled with 8 entries and moved to a second owner with
`cplus_move`; the moved-from owner is never read again (strict mode treats
that as use-after-move). A separate `stdlib_calloc(8, sizeof(int))` block is
populated from the map via `get()` — `stdlib_calloc` records `elem_size=4`, so
`stdlib_alloc_length()==8` (a plain `stdlib_alloc` would report byte length
32, which was the cause of an initial failing check). That block is handed to
a new owner with `stdlib_transfer_ownership`: `src` becomes `NULL`, `dst`
receives the same pointer, and the saved alias reports
`stdlib_alloc_check_generation(alias, old_gen)==0` while the new owner has a
fresh, non-zero generation. The payload is read through the **new** owner only
and compared with the moved `intmap`; a second transfer from the now-NULL
source is rejected. Releasing the block sets its generation to 0.

### CF4 — `file::read_line` boundaries and category (18 checks)

A `tmpfile()` holds `abc\nx\n\nABCDEFGH\nOK\nlast`. Verified with a 1-byte,
5-byte and 8-byte `char` buffer:

| Line / buffer | Return / error |
| --- | --- |
| `"abc\n"`, size 5 | `STDLIB_OK`, `"abc"`, error `OK` |
| `"x\n"`, size 1 | `STDLIB_FAIL`, empty buffer, `BOUNDS` / `CPLUS_EXIT_BOUNDS`, stream drained |
| `"\n"`, size 1 | `STDLIB_OK`, empty buffer, error clean |
| `"ABCDEFGH\n"`, size 8 | `STDLIB_FAIL`, empty buffer, `BOUNDS`, next line still aligned |
| `"OK\n"`, size 8 | `STDLIB_OK`, `"OK"` |
| full-buffer line at EOF, size 5 | **contract difference**, see below |

Two *recoverable* cross-family discrepancies are pinned by comparing
`file::read_line` with `io::read_line` on identical content. To feed `io.hp`
without a stdin fixture, the test temporarily assigns the process-global
`stdin` to a `tmpfile()` and restores it immediately after each read.

### CF5 — JSON value copied from a temporary string (11 checks)

`string("persist-me") -> string_view -> json_builder::add_string` (which uses
`yyjson_mut_obj_add_strcpy`, i.e. copies). The source string is dropped before
the builder is serialised; the text is parsed into a `json`, and the
`json_view` of member `"k"` still returns `"persist-me"`. A second case parses
directly from a temporary `string::c_str()` with `YYJSON_READ_NOFLAG`; the
reader copies the input into the document, so dropping the source string
before reading the view is safe. Views are consumed while their document is
alive; after `drop()` only `valid()==0` is observed, never a view dereference.
`json` and `json_view` roots are stored in named locals before calling
`.member()` — method calls on a returned temporary are not lowered by C+.

### CF6 — one callback for every family + telemetry (19 checks)

A single `stdlib_set_error_callback` counts calls, per-family phase calls,
category bitmask and last `(category, code)`. One documented failure is
triggered from each family: `string::append(NULL)`, `vector::at_checked` out of
range, `strmap::put(NULL, …)`, `file::read_line(NULL, 0, …)`, `json_view::get_int`
on a string, and `log_sink::info` on an invalid sink. Three additional notes
come from the allocator (`stdlib_alloc_fail_after(0)`), ownership
(`stdlib_transfer_ownership` with NULL handles) and the error family
(`error_fail_cat` with `STDLIB_CATEGORY_IO`). A successful `log_sink::info` to
a borrowed `tmpfile()` sink must not fire the callback.

Totals are deterministic: 9 notes → `api_failures==9`, `callback calls==9`,
`invalid_ops==6` (the five INVALID notes plus the IO note, which
`stdlib_note()` buckets into the "invalid" counter), `bounds_violations==1`,
`alloc_failures==1`, `ownership_violations==1`, `lifecycle_failures==0`, and
the category bitmask contains INVALID, BOUNDS, MEMORY, OWNERSHIP and IO. The
callback is reset to `NULL` and telemetry disabled before exit.

## 3. Contract defect reproduced and fixed (CF4)

Both exact-fit discrepancies had the same root cause: `fgets()` stops after
writing `size-1` bytes **without** setting `feof()`. `file.hp::read_line`
decided "line did not fit" with `n == size-1 && buf[n-1] != '\n' && !feof()`,
so it drained the remainder and reported `STDLIB_FAIL` / `BOUNDS` even though
the content fitted. `io.hp::read_line` peeks the next byte with `fgetc()` and
treats `'\n'`, `'\r'` or `EOF` as an exact fit.

| # | Content / buffer | `io::read_line` | `file::read_line` before fix | after fix |
| --- | --- | --- | --- | --- |
| 1 | `"last"` at physical EOF, size 5 | `OK`, `"last"` | `FAIL`, empty, `BOUNDS` | `OK`, `"last"` |
| 2 | `"abcd\n"` with size 5 | `OK`, `"abcd"` | `FAIL`, empty, `BOUNDS` | `OK`, `"abcd"` |
| 3 | `"abcdef\n"` with size 5 | `FAIL`, drained | `FAIL`, drained | `FAIL`, drained (unchanged) |

The fix ports `io.hp`'s peek logic into `file.hp::read_line`: after `fgets`, a
full buffer with no in-buffer terminator peeks one byte; `\n`, `\r` (with CRLF
consumed) or `EOF` is accepted as an exact fit, any other byte triggers the
existing drain-and-`BOUNDS` path. Genuine overlong lines still drain and fail.

The CF4 checks were updated from pinning the discrepancy to asserting the
aligned behavior: `cf4_file_full_buffer_eof_aligned`,
`cf4_file_exact_fit_aligned`, `cf4_cross_family_aligned` and
`cf4_eof_aligned`. Total remains 18 CF4 checks, 0 failures.

## 4. Build and run commands

From the repository root. The yyjson triplet is required because `json.hp`
includes and links yyjson:

```sh
mkdir -p build/stdlib-deep

# default
./build/c+ build tests/stdlib/deep_cross_family.cp \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o build/stdlib-deep/deep_cross_family_default
timeout 30 ./build/stdlib-deep/deep_cross_family_default \
    < /dev/null > build/stdlib-deep/deep_cross_family_default.stdout \
    2> build/stdlib-deep/deep_cross_family_default.stderr

# strict / no sanitizer  (pass the two flags as separate argv operands)
./build/c+ build tests/stdlib/deep_cross_family.cp --strict --sanitize=none \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o build/stdlib-deep/deep_cross_family_strict_none
timeout 30 ./build/stdlib-deep/deep_cross_family_strict_none \
    < /dev/null > build/stdlib-deep/deep_cross_family_strict_none.stdout \
    2> build/stdlib-deep/deep_cross_family_strict_none.stderr

# strict / UBSan
./build/c+ build tests/stdlib/deep_cross_family.cp --strict --sanitize=undefined \
    -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o build/stdlib-deep/deep_cross_family_ubsan
timeout 30 ./build/stdlib-deep/deep_cross_family_ubsan \
    < /dev/null > build/stdlib-deep/deep_cross_family_ubsan.stdout \
    2> build/stdlib-deep/deep_cross_family_ubsan.stderr
```

Under zsh, pass `--strict`, `--sanitize=none`, `-I…`, `-L…` and `-l…` as
separate **literal** argv operands. Storing several flags in one shell variable
and expanding it unquoted does not word-split in zsh; the compiler then sees a
single malformed argument and the build fails with `yyjson.h: file not found`.
This is a shell-invocation pitfall, not a test defect.

**Harness note (out of this file's ownership):**
`tests/stdlib/run_deep_tests.sh` adds the yyjson triplet only when the base
name matches `deep_json*`. `deep_cross_family.cp` does not match that case, so
a full-runner invocation would fail to link unless the harness case is extended
(e.g. to `deep_json*|deep_cross_family`). This test was verified only with the
explicit commands above, as instructed; no runner was modified or executed.

## 5. Verified results

Build warnings (all three modes, from the C+ backend's separate compile
invocations, not from test code, non-fatal):

```
clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
```

| Mode | Build rc | Run rc (`timeout 30`) | Runtime stderr | Final stdout line |
| --- | --- | --- | --- | --- |
| default | 0 | 0 | empty | `CROSS_FAMILY 89 0` |
| `--strict --sanitize=none` | 0 | 0 | empty | `CROSS_FAMILY 89 0` |
| `--strict --sanitize=undefined` | 0 | 0 | empty | `CROSS_FAMILY 89 0` |

Exact stdout (identical in all three modes):

```
CF 1 13 0
CF 2 15 0
CF 3 13 0
CF 4 18 0
CF 5 11 0
CF 6 19 0
CROSS_FAMILY 89 0
```

Totals: **89 checks, 0 failures** in every mode; no UBSan report, no runtime
fatal, no hang. `CF-FAIL` lines did not appear. No `.expected` file is needed.

An earlier draft (before the fixes described in CF2/CF3/CF4) produced
`CROSS_FAMILY 87 8` and a correct `[CPLUS-BOUNDS]` compile rejection of an
out-of-range array read; both were resolved in the test file only, without
touching any header or compiler rule.
