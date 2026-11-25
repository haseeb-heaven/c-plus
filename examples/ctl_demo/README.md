# CTL + C+ RAII examples

Two small, non-interactive programs that use the vendored **rurban/CTL**
headers (MIT, pinned `ede5f455392f2380dd16aead26835eece6cd20e4` in
`third_party/ctl/`, unmodified) together with the C+ language and the native
Goose-inspired scratch allocator.

Run everything with:

```sh
sh examples/ctl_demo/run.sh
```

The runner sets `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`, leaves
`SDKROOT` and `CPLUS_CLANG` unset, bounds each build at 180 s and each run at
30 s, compares stdout byte-for-byte with `expected/*.txt`, and prints exactly:

```
CTL_DEMO 2 2 0
```

It uses no Python and no `|| true`; any build failure, run failure, timeout or
output difference is reported as a failed case.

## Exact build and run commands

From the repository root:

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
unset SDKROOT CPLUS_CLANG

./build/c+ build examples/ctl_demo/ctl_collections.cp \
  --default --sanitize=none -I. -Ithird_party/ctl \
  -o ctl_collections -- build/libc/libcplus_hardened.a
./ctl_collections

./build/c+ build examples/ctl_demo/ctl_goose_hybrid.cp \
  --default --sanitize=none -I. -Ithird_party/ctl \
  -o ctl_goose_hybrid -- build/libc/libcplus_hardened.a
./ctl_goose_hybrid
```

The build emits warnings such as `[CPLUS-ALIAS]` at `vector.h:245` (raw
`realloc`), `[CPLUS-ALIAS]` at `deque.h:280`, `[CPLUS-ARITH]` in CTL's
`algorithm.h`, and a clang "linker input unused" note for the hardening
archive. They are expected for vendored plain-C CTL headers and are not
modified or suppressed.

## `ctl_collections.cp` — top-N leaderboard

A ten-event score stream feeds three distinct CTL container families:

| Container | Role |
| --- | --- |
| `vec_int` | unique player ids, using CTL `find` for de-duplication |
| `vec_entry` | cumulative totals, then CTL's in-place `sort` for the top three |
| `deq_int` | rolling window of the last three score deltas |
| `str` | formatted result line |

Exact stdout:

```
CTL_COLLECTIONS events=10 unique=5 window=12
TOP 2:11 5:10 3:9
CTL_FREE seen
CTL_EXPLICIT_DROP seen
CTL_FREE line
CTL_FREE window
CTL_FREE board
CTL_RAII frees=4
```

### RAII contract

Each CTL container is a private field of a small C+ `class`. The constructor
calls the matching CTL `*_init`; `drop()` calls the matching `*_free` **once**,
prints one `CTL_FREE <owner>` line and increments `g_ctl_frees`. The compiler
inserts `drop()` at scope exit in reverse declaration order.

Two guards make cleanup deterministic:

1. `released` is set before calling `*_free`.
2. The CTL resource pointer is checked (`vector`, `pages`, etc.).

The second guard matters because lowering an explicit `x.drop()` zeroes the
whole object and then the scope-exit `drop()` runs again. Here `ctl_seen` is
explicitly dropped once; the later implicit drop is inert, so there is exactly
one `CTL_FREE seen` line and `CTL_RAII frees=4`. A repeated free would change
the output and fail the diff.

## `ctl_goose_hybrid.cp` — CTL index + transient DataStack

A stream of document ids is aggregated into a persistent CTL `vec_score`
index. Inside one `stdlib_scratch_scope`, allocator-aware C+ `vector_in` and
`string_in` hold a five-id rolling window and its formatted label; the derived
"hot id" bonus is written back into the persistent CTL container before the
scope is left. `stdlib_scratch_scope` restores the data-stack top with one
store, so no per-block scratch free is needed.

Exact stdout:

```
HYBRID scratch_ok=1 window=5 bonus_id=2 bonus=2
HYBRID scratch_label=W2,3,1,4,2
HYBRID top_grew=1
HYBRID top_restored=1
HYBRID board=2:4 4:3 1:2 3:1
CTL_FREE board
HYBRID ctl_free=1
```

The scratch scope is declared **before** the scratch containers so its drop
runs **last**: the C+ scratch vector/string drop first, then the top is rewound.
`top_grew=1` and `top_restored=1` compare the thread data-stack offset against
a baseline taken before the scope. The persistent CTL vector outlives the
scope and is released once by `CtlBoard::drop()` (`CTL_FREE board`,
`ctl_free=1`).

## Honest scope

- CTL is **plain C**. Its containers call `realloc`/`free` directly; they have
  no allocator awareness, no ownership tracking and no runtime bounds or
  lifetime checks. CTL is **not memory-safe by itself**.
- The exact-once guarantee above is a **hand-written C+ contract** in these
  two files (constructor + guarded `drop()` + logged counter), not a compiler
  proof or an upstream CTL guarantee. The C+ compiler does not track class
  field lifetimes after `drop()`; use-after-`drop()` and double-`drop()` are
  not generally rejected. The logs are evidence for these fixed workloads.
- Do not use a wrapper after `drop()`; do not copy its CTL handle into another
  owner. Only the declared owner calls `*_free`.
- `ctl/string.h` is pre-instantiated (`str`); the other headers are
  instantiated here with `POD`/`NOT_INTEGRAL`/`T` as documented by CTL.
- `ctl/set.h` and `ctl/map.h` are deliberately **not** used: with `SDKROOT`
  unset, the C+ `--default` safe-lifetime analyzer rejects their red-black
  insert code (`[CPLUS-SAFE-LIFETIME] null pointer dereference`). The vector
  and deque headers only produce warnings, so the required build pattern works
  unchanged. `third_party/` was not edited.
- `sh tests/ctl/run_ctl_tests.sh` passing 11/11 shows those C tests build and
  run; it is not a memory-safety result and says nothing about the C+ wrappers.
- No game assets, raw game-memory offsets, live injection or Windows binaries
  are involved.

## Edge-case coverage (added 2026-11-24)

Both runners diff byte-for-byte against `expected/`. New edge cases:
`ctl_collections` adds `CTL_EDGE duplicate_accumulate` and `CTL_EDGE
double_drop`; `ctl_goose_hybrid` adds `HYBRID edge_empty_window` and
`HYBRID edge_bump_missing` (a `bump` on a missing id is a no-op). Verdict:
`CTL_DEMO 2 2 0`. Run `sh tests/ctl/run_ctl_demo_coverage.sh` for an
aggregate end-to-end coverage pass.

## Build mode (2026-11-25 update)

The runner builds these programs with `--legacy-c`: vendored CTL is plain C and
its vector header trips UBSan (`applying zero offset to null pointer`). The C+
safety/sanitizer layer is not a claim about third_party code; see
`docs/stdlib/CTL_ADOPTION_MATRIX.md`. `third_party/` was not edited.
