# EDGE100 containers -- results

## Command

```sh
sh tests/edge100/containers/run.sh
```

Build line (issued by the script from the repo root):

```sh
./build/c+ build tests/edge100/containers/harness.cp \
  --default --sanitize=none -I. \
  -o tests/edge100/containers/out/harness
```

`STDLIB_ALLOC_LIMIT` is pinned to 4096 in the harness so exact-fit, one-over and overflow boundaries are reachable. Telemetry and the error callback are disabled; allocation failure is injected with `stdlib_alloc_fail_at()` at each fallible step. Each case runs in a forked child; the verdict is a 16-byte struct over a pipe. A case counts as PASS only on clean exit, exact case ID match, complete read, intact canary and a PASS assertion bit.

## Verification

- Classifier selfcheck: `SELFCHECK 8 8` (all eight adversarial classifications succeed).
- Non-vacuity mutation: inverting the `string::append` valid assertion in a throwaway copy yields `EDGE100 containers 120 520 519 1` and a single `string.hp::string::append valid FAIL`; the unmutated harness yields zero failures.
- Per-case log: `edge_cases.log` (also digested into `API_MANIFEST.md`).

## Summary

```
EDGE100 containers 120 520 520 0
```

| Header | APIs | Cases | PASS | FAIL |
| --- | ---: | ---: | ---: | ---: |
| `stdlib/string.hp` | 27 | 134 | 134 | 0 |
| `stdlib/vector.hp` | 24 | 115 | 115 | 0 |
| `stdlib/collections.hp` | 69 | 271 | 271 | 0 |
| **total** | **120** | **520** | **520** | **0** |

## Per-API results

| API | Cases | PASS | FAIL | Failed cases |
| --- | ---: | ---: | ---: | --- |
| `string.hp::stdlib_utf8_validate` | 5 | 5 | 0 | - |
| `string.hp::stdlib_string_calculate_growth` | 5 | 5 | 0 | - |
| `string.hp::string_in` | 5 | 5 | 0 | - |
| `string.hp::string_view::string_view(char_ptr,size_t)` | 3 | 3 | 0 | - |
| `string.hp::string_view::size` | 3 | 3 | 0 | - |
| `string.hp::string_view::data` | 3 | 3 | 0 | - |
| `string.hp::string_view::equals` | 4 | 4 | 0 | - |
| `string.hp::string_view::is_valid_utf8` | 5 | 5 | 0 | - |
| `string.hp::string::string(char_ptr)` | 5 | 5 | 0 | - |
| `string.hp::string::size` | 4 | 4 | 0 | - |
| `string.hp::string::capacity` | 5 | 5 | 0 | - |
| `string.hp::string::valid` | 4 | 4 | 0 | - |
| `string.hp::string::at` | 8 | 8 | 0 | - |
| `string.hp::string::c_str` | 4 | 4 | 0 | - |
| `string.hp::string::equals` | 6 | 6 | 0 | - |
| `string.hp::string::compare` | 6 | 6 | 0 | - |
| `string.hp::string::find` | 4 | 4 | 0 | - |
| `string.hp::string::reserve` | 7 | 7 | 0 | - |
| `string.hp::string::append` | 7 | 7 | 0 | - |
| `string.hp::string::clear` | 3 | 3 | 0 | - |
| `string.hp::string::copy_from` | 6 | 6 | 0 | - |
| `string.hp::string::is_valid_utf8` | 5 | 5 | 0 | - |
| `string.hp::string::substr` | 8 | 8 | 0 | - |
| `string.hp::string::count_fields` | 5 | 5 | 0 | - |
| `string.hp::string::field` | 8 | 8 | 0 | - |
| `string.hp::string::to_view` | 3 | 3 | 0 | - |
| `string.hp::string::drop` | 3 | 3 | 0 | - |
| `vector.hp::vector_allocation_bytes` | 5 | 5 | 0 | - |
| `vector.hp::vector_calculate_growth` | 5 | 5 | 0 | - |
| `vector.hp::vector_in` | 4 | 4 | 0 | - |
| `vector.hp::slice::slice(int*,size_t)` | 3 | 3 | 0 | - |
| `vector.hp::slice::size` | 3 | 3 | 0 | - |
| `vector.hp::slice::stale` | 3 | 3 | 0 | - |
| `vector.hp::slice::valid` | 4 | 4 | 0 | - |
| `vector.hp::slice::at_checked` | 7 | 7 | 0 | - |
| `vector.hp::slice::sub` | 7 | 7 | 0 | - |
| `vector.hp::vector::vector()` | 2 | 2 | 0 | - |
| `vector.hp::vector::size` | 4 | 4 | 0 | - |
| `vector.hp::vector::capacity` | 3 | 3 | 0 | - |
| `vector.hp::vector::reserve` | 7 | 7 | 0 | - |
| `vector.hp::vector::push_back` | 5 | 5 | 0 | - |
| `vector.hp::vector::at` | 7 | 7 | 0 | - |
| `vector.hp::vector::at_checked` | 7 | 7 | 0 | - |
| `vector.hp::vector::pop_back` | 5 | 5 | 0 | - |
| `vector.hp::vector::insert` | 7 | 7 | 0 | - |
| `vector.hp::vector::erase` | 7 | 7 | 0 | - |
| `vector.hp::vector::clear` | 3 | 3 | 0 | - |
| `vector.hp::vector::copy_from` | 6 | 6 | 0 | - |
| `vector.hp::vector::shrink_to_fit` | 5 | 5 | 0 | - |
| `vector.hp::vector::view` | 3 | 3 | 0 | - |
| `vector.hp::vector::drop` | 3 | 3 | 0 | - |
| `collections.hp::stdlib_map_hash_int` | 3 | 3 | 0 | - |
| `collections.hp::stdlib_map_hash_text` | 4 | 4 | 0 | - |
| `collections.hp::stdlib_map_next_pow2` | 5 | 5 | 0 | - |
| `collections.hp::stdlib_map_needs_room` | 5 | 5 | 0 | - |
| `collections.hp::stdlib_map_rebuild_capacity` | 5 | 5 | 0 | - |
| `collections.hp::stdlib_ring_copy` | 6 | 6 | 0 | - |
| `collections.hp::intmap::intmap()` | 2 | 2 | 0 | - |
| `collections.hp::intmap::size` | 4 | 4 | 0 | - |
| `collections.hp::intmap::valid` | 3 | 3 | 0 | - |
| `collections.hp::intmap::grow` | 7 | 7 | 0 | - |
| `collections.hp::intmap::put` | 6 | 6 | 0 | - |
| `collections.hp::intmap::get` | 4 | 4 | 0 | - |
| `collections.hp::intmap::contains` | 4 | 4 | 0 | - |
| `collections.hp::intmap::remove` | 4 | 4 | 0 | - |
| `collections.hp::intmap::clear` | 3 | 3 | 0 | - |
| `collections.hp::intmap::drop` | 3 | 3 | 0 | - |
| `collections.hp::strmap::strmap()` | 2 | 2 | 0 | - |
| `collections.hp::strmap::size` | 4 | 4 | 0 | - |
| `collections.hp::strmap::valid` | 3 | 3 | 0 | - |
| `collections.hp::strmap::grow` | 7 | 7 | 0 | - |
| `collections.hp::strmap::put` | 6 | 6 | 0 | - |
| `collections.hp::strmap::get` | 3 | 3 | 0 | - |
| `collections.hp::strmap::contains` | 3 | 3 | 0 | - |
| `collections.hp::strmap::remove` | 3 | 3 | 0 | - |
| `collections.hp::strmap::clear` | 3 | 3 | 0 | - |
| `collections.hp::strmap::drop` | 3 | 3 | 0 | - |
| `collections.hp::intset::intset()` | 2 | 2 | 0 | - |
| `collections.hp::intset::size` | 4 | 4 | 0 | - |
| `collections.hp::intset::valid` | 3 | 3 | 0 | - |
| `collections.hp::intset::grow` | 7 | 7 | 0 | - |
| `collections.hp::intset::insert` | 6 | 6 | 0 | - |
| `collections.hp::intset::contains` | 4 | 4 | 0 | - |
| `collections.hp::intset::remove` | 4 | 4 | 0 | - |
| `collections.hp::intset::clear` | 3 | 3 | 0 | - |
| `collections.hp::intset::drop` | 3 | 3 | 0 | - |
| `collections.hp::deque::deque()` | 2 | 2 | 0 | - |
| `collections.hp::deque::size` | 4 | 4 | 0 | - |
| `collections.hp::deque::capacity` | 3 | 3 | 0 | - |
| `collections.hp::deque::reserve` | 7 | 7 | 0 | - |
| `collections.hp::deque::push_back` | 5 | 5 | 0 | - |
| `collections.hp::deque::push_front` | 5 | 5 | 0 | - |
| `collections.hp::deque::pop_front` | 4 | 4 | 0 | - |
| `collections.hp::deque::pop_back` | 4 | 4 | 0 | - |
| `collections.hp::deque::at_checked` | 7 | 7 | 0 | - |
| `collections.hp::deque::clear` | 3 | 3 | 0 | - |
| `collections.hp::deque::drop` | 3 | 3 | 0 | - |
| `collections.hp::list::list()` | 2 | 2 | 0 | - |
| `collections.hp::list::size` | 4 | 4 | 0 | - |
| `collections.hp::list::push_back` | 3 | 3 | 0 | - |
| `collections.hp::list::push_front` | 3 | 3 | 0 | - |
| `collections.hp::list::pop_front` | 4 | 4 | 0 | - |
| `collections.hp::list::pop_back` | 4 | 4 | 0 | - |
| `collections.hp::list::at_checked` | 7 | 7 | 0 | - |
| `collections.hp::list::clear` | 3 | 3 | 0 | - |
| `collections.hp::list::drop` | 3 | 3 | 0 | - |
| `collections.hp::stack::stack()` | 2 | 2 | 0 | - |
| `collections.hp::stack::size` | 4 | 4 | 0 | - |
| `collections.hp::stack::push` | 5 | 5 | 0 | - |
| `collections.hp::stack::pop` | 4 | 4 | 0 | - |
| `collections.hp::stack::top_checked` | 4 | 4 | 0 | - |
| `collections.hp::stack::clear` | 3 | 3 | 0 | - |
| `collections.hp::stack::drop` | 3 | 3 | 0 | - |
| `collections.hp::queue::queue()` | 2 | 2 | 0 | - |
| `collections.hp::queue::size` | 4 | 4 | 0 | - |
| `collections.hp::queue::push` | 5 | 5 | 0 | - |
| `collections.hp::queue::pop` | 4 | 4 | 0 | - |
| `collections.hp::queue::front_checked` | 4 | 4 | 0 | - |
| `collections.hp::queue::clear` | 3 | 3 | 0 | - |
| `collections.hp::queue::drop` | 3 | 3 | 0 | - |

## Observations

All probed cases honour their documented contracts. The following boundary behaviours were confirmed explicitly and are easy to mis-assume:

- `string_in(a, NULL)` and `string_in(a, "")` return a **valid** empty string (`valid()==1`, `capacity()==1`); a text length of `STDLIB_ALLOC_LIMIT` also leaves that valid empty state rather than an invalid string.
- On an injected allocation failure, `string_in` reports `valid()==0` but leaves `capacity()==1`: the initial `string("")` buffer pointer is overwritten with NULL without being released. This one-byte leak on the failure path is an implementation observation; the observable failure contract (`valid()==0`) holds.
- `find("")` on an empty-but-valid string returns 0; only a dropped/invalid string returns -1 for an empty needle.
- `pop_back(NULL)`, `pop(NULL)` and `pop_front(NULL)` are refused calls: they return -1 and do not remove an element.
- `hash_int(0) == 0` (the murmur finalizer maps zero to zero), and `hash(NULL) == hash("")` for `hash_text`.
- A pre-take `slice` becomes `stale()==1` after `drop()`, `clear()`, `reserve()` with growth, `insert`, `erase` or `copy_from`; `push_back` without reallocation does not invalidate it.
- `stack`/`queue` reach their half-limit guard only when the array is actually full at 1024 elements (4096 bytes / 4); below that a push may succeed without allocating.
- `string::append` exact-fit requires `need == capacity` (7 bytes for `"abc" + "def"`); reserve(6) still triggers the growth path.
