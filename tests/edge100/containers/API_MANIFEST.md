# EDGE100 containers -- API manifest
Every public API in the three headers is enumerated below with its probed cases and the observed result. N/A means the category cannot apply to that API; the reason column records why. Cases are produced from the harness dispatch table, one fork per (API, category) pair, with canary and clean-exit checks in `classify()`.
| Category | Meaning |
| --- | --- |
| `valid` | normal, well-formed operation |
| `null_empty` | NULL pointer / empty input |
| `exact_fit` | exact-fit size/capacity boundary |
| `one_over` | one-over size/capacity boundary |
| `size_max` | SIZE_MAX size or index |
| `self_alias` | source aliases the object's own buffer |
| `double_drop` | drop() called twice / reuse after drop |
| `invalid_index` | out-of-range index or missing key |
| `alloc_fail` | injected allocation failure; object must be unchanged |

## Scope

A public API is a free function or a class method declared in the three headers. Twelve implementation helpers are deliberately not enumerated: they are internal allocator/probe/rebuild plumbing, not part of the public contract. They are listed here so the enumeration is complete and auditable:

- stdlib/string.hp: `string::salloc`, `string::sfree`
- stdlib/vector.hp: `vector::valloc`, `vector::vfree`
- stdlib/collections.hp: `intmap::stdlib_probe`, `intmap::stdlib_rebuild`, `strmap::stdlib_copy_key`, `strmap::stdlib_probe`, `strmap::stdlib_rebuild`, `intset::stdlib_probe`, `intset::stdlib_rebuild`, `deque::stdlib_make_room`

Totals: **120 public APIs**, **520 cases**, **520 PASS**, **0 FAIL**.

## stdlib/string.hp (27 APIs)

### `string.hp::stdlib_utf8_validate`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::stdlib_string_calculate_growth`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string_in`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string_view::string_view(char_ptr,size_t)`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string_view::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string_view::data`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string_view::equals`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string_view::is_valid_utf8`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::string(char_ptr)`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::capacity`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::valid`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::at`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | PASS |

### `string.hp::string::c_str`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::equals`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::compare`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::find`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::reserve`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::append`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::copy_from`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `string.hp::string::is_valid_utf8`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::substr`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::count_fields`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::field`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::to_view`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `string.hp::string::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

## stdlib/vector.hp (24 APIs)

### `vector.hp::vector_allocation_bytes`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector_calculate_growth`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector_in`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::slice::slice(int*,size_t)`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::slice::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::slice::stale`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::slice::valid`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::slice::at_checked`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::slice::sub`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::vector()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::vector::capacity`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::reserve`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::vector::push_back`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::vector::at`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::at_checked`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::pop_back`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::insert`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::vector::erase`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::copy_from`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | PASS |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::vector::shrink_to_fit`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `vector.hp::vector::view`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `vector.hp::vector::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

## stdlib/collections.hp (69 APIs)

### `collections.hp::stdlib_map_hash_int`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stdlib_map_hash_text`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stdlib_map_next_pow2`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stdlib_map_needs_room`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stdlib_map_rebuild_capacity`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stdlib_ring_copy`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | N/A -- API is not an owning drop()/destructor path |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intmap::intmap()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intmap::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intmap::valid`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intmap::grow`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intmap::put`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intmap::get`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intmap::contains`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intmap::remove`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intmap::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intmap::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::strmap()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::strmap::valid`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::grow`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::strmap::put`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::strmap::get`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::contains`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::remove`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::strmap::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intset::intset()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intset::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intset::valid`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intset::grow`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intset::insert`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::intset::contains`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intset::remove`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intset::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::intset::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::deque()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::deque::capacity`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::reserve`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::deque::push_back`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::deque::push_front`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::deque::pop_front`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::pop_back`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::at_checked`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::deque::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::list::list()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::list::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::list::push_back`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::list::push_front`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::list::pop_front`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::list::pop_back`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::list::at_checked`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | PASS |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | PASS |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::list::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::list::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stack::stack()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stack::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::stack::push`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::stack::pop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stack::top_checked`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stack::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::stack::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::queue::queue()`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::queue::size`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::queue::push`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | N/A -- API has no NULL/empty pointer or zero-length input to probe |
| `exact_fit` | PASS |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | PASS |

### `collections.hp::queue::pop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::queue::front_checked`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | PASS |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::queue::clear`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

### `collections.hp::queue::drop`

| Case | Result |
| --- | --- |
| `valid` | PASS |
| `null_empty` | PASS |
| `exact_fit` | N/A -- API takes no size/capacity that has an exact-fit boundary |
| `one_over` | N/A -- API takes no size/capacity that has a one-over boundary |
| `size_max` | N/A -- API takes no size_t argument that can carry SIZE_MAX safely |
| `self_alias` | N/A -- API has no source pointer that can alias its own storage |
| `double_drop` | PASS |
| `invalid_index` | N/A -- API takes no index argument |
| `alloc_fail` | N/A -- API performs no allocation and cannot fail atomically |

