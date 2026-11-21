# Compiler patches held for Agent 1

## `agent1-hp-class-linkage.patch`

**Problem.** Members lowered from a `class` in a `.hp` header get external
linkage, so a header that defines a class cannot be included from two
translation units (`duplicate symbol '_string_size'`, ...).

**Patch.** Threads a `headerMode` flag from the driver (which knows it is
lowering an `#include`d `.hp`) into `rewriteCPlusSource`, and prefixes emitted
class members with `static inline` only in that mode. Defaults to off; ordinary
`.cp`/`.c` translation units are unchanged. ~120 lines, no runtime change.

**Apply and verify**

```sh
git apply docs/patches/agent1-hp-class-linkage.patch
cmake --build build -j4
sh tests/stdlib/run_stdlib_tests.sh multi_tu          # flip to positive
sh tests/stdlib/run_stdlib_tests.sh class_linkage     # flip to positive
```

**When it lands**, turn the two "expected duplicate symbol" sections in
`tests/stdlib/run_stdlib_tests.sh` back into positive build+run+diff tests
(the expected outputs and `multi_tu.stdin` are already committed:
`tests/stdlib/multi_tu.expected`, `tests/stdlib/known_gaps/class_linkage.expected`
is regenerated from a successful run).

**Ownership.** `compiler/**` belongs to Agent 1; this branch does not modify it.
