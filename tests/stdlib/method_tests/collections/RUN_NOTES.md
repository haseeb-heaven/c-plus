# collections.hp — per-method test run notes

## Scope

`stdlib/collections.hp` has **63** public API entries, matching the repository's
own enumerator (`tests/stdlib/coverage.cp`: its `is_skipped()` excludes control
keywords, `main`, and `stdlib_*` internal helpers — the native coverage tool
reports `collections.hp 63 63 100.0%`). The manifest `methods.txt` contains
exactly those 63 entries, fully qualified as `<class>.<method>` to disambiguate
names shared by several classes (`size`, `put`, `get`, `contains`, `remove`,
`clear`, `drop`, `at_checked`, `push`, `pop`, `push_back`, `push_front`,
`pop_front`, `pop_back`). The internal `stdlib_*` helpers (6 free functions plus
per-class `stdlib_probe` / `stdlib_rebuild` / `stdlib_copy_key` /
`stdlib_make_room`) are deliberately not counted, by the same rule.

Every manifest entry has exactly one dedicated `static void t_<...>` case in
`collections_methods.cp`. Each case invokes that method and checks a concrete,
deterministic observable result (return code plus state/element values).
Coverage per method includes insert / erase / find / positional access /
bounds rejection / NULL-argument rejection / lifecycle (construct, clear, drop,
double-drop, reuse after drop) and ring-wraparound paths where applicable.

No randomness is used, so there is no seed to vary; inputs are fixed constants
or fixed deterministic loops. All runs were bounded by `timeout 30`.

## Exact commands

From the repository root
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`):

```sh
./build/c+ build tests/stdlib/method_tests/collections/collections_methods.cp \
    --strict --sanitize=none \
    -o tests/stdlib/method_tests/collections/collections_methods
```

Build result: exit 0, no diagnostics.

```sh
timeout 30 tests/stdlib/method_tests/collections/collections_methods \
    > tests/stdlib/method_tests/collections/aggregate.txt 2>&1
echo $?   # 0
```

Two additional confirmation runs (same binary, same 30 s cap) produced
byte-identical stdout:

```sh
timeout 30 tests/stdlib/method_tests/collections/collections_methods > /tmp/collections_run2.txt 2>&1  # exit 0
timeout 30 tests/stdlib/method_tests/collections/collections_methods > /tmp/collections_run3.txt 2>&1  # exit 0
diff -u tests/stdlib/method_tests/collections/aggregate.txt /tmp/collections_run2.txt
diff -u /tmp/collections_run2.txt /tmp/collections_run3.txt
# no differences: deterministic
```

Exit-status behaviour was also exercised during authoring: with four incorrect
expected-size assertions the program printed `FAIL` lines, printed
`METHOD_TEST collections 63 59 4 0`, and returned exit 1. After correcting
those four test expectations, the final run returned exit 0. So a real failure
is not hidden behind a zero exit code.

## Results

- Total methods: **63**
- Passed: **63**
- Failed: **0**
- Skipped: **0**
- Per-method lines: 63 `collections.hp <method> PASS`, 0 `FAIL`
- Build: clean under `--strict --sanitize=none`
- Runtime: well under the 30 s cap

Final line of `aggregate.txt` (and of the program's stdout):

```
METHOD_TEST collections 63 63 0 0
```

## Notes for maintainers

- The C+ lowering resolves method calls using a file-wide variable-name table,
  so two functions must not declare different container classes under the same
  local name. This file uses unique names per class: `im` (intmap), `sm`
  (strmap), `iset` (intset), `dq` (deque), `li` (list), `stk` (stack), `qu`
  (queue). Reusing `m` for both intmap and strmap makes `m.put(1, 2)` bind to
  `strmap::put(const char*, int)` and fails compilation.
- `clear()` and `drop()` are `void`; their cases verify observable state after
  the call (`size`, `valid`, `capacity`, rejected `get`/`at_checked`/`pop`, and
  successful reuse). `drop()` cases call drop twice to prove idempotence.
- `NULL`-out and out-of-range calls are used only to check the documented
  recoverable `-1` paths; no case performs undefined behaviour.

## Delivered files (all under this directory)

- `collections_methods.cp` — 63 per-method cases + aggregate result printer
- `methods.txt` — the 63-entry manifest
- `aggregate.txt` — the single aggregate run output (63 PASS lines + final line)
- `RUN_NOTES.md` — this file
