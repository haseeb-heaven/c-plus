# Issue #28 — RAII shadowing: Agent 2 verification record

**Agent 2 scope:** runtime, safe libc, modern stdlib, library-side reproducers and
tests. Agent 2 implemented the compiler-side lowering fix in `compiler/Sema/RaiiLowering.cpp` on `agent/modern-stdlib` (commit `7cd2020d27976e1d658aaaae83f2f4b281c33665`) after reproducing the defect; coordination with Agent 1 is still required for ownership/process tracking.

## Verdict

**Confirmed and fixed on `agent/modern-stdlib`.** Before the fix, when an owned local in a nested scope shadowed an owned local with the same identifier, the inner binding's cleanup obligation could be silently dropped. The fix gives each owned declaration a stable identity, resolves live bindings by lexical scope, tracks move/borrow by declaration id, and emits return/scope cleanup through stable address captures.

## Original reproducer

`tests/raii_shadowing/repro_shadow_basic.cp`:

```c
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int main(void) {
    R x = {1};
    { R x = {2}; print("INNER", x.id); }
    print("AFTER_INNER", x.id);
    return 0;
}
```

Observed (current compiler):

```
OUTER 1
INNER 2
AFTER_INNER 1
DROP 1          <-- DROP 2 is missing
```

Required (see `tests/raii_shadowing/expected/repro_shadow_basic.txt`):

```
OUTER 1
INNER 2
DROP 2
AFTER_INNER 1
DROP 1
```

## Root cause (read-only analysis, Agent 1's file)

In `compiler/Sema/RaiiLowering.cpp`, `Lowering` maintains:

- `std::map<std::string, OwnedVar> owned_` — keyed by **identifier name**;
- `std::set<std::string> moved_`, `borrows_` — also name-keyed;
- `std::vector<Scope> scopes_`, each with its own `vars` vector — the actual
  source of emitted cleanup calls in `dropsFor()`.

The decisive line is in `tryResourceDeclaration()`:

```cpp
const std::string name = toks_[nameIdx].text;
if (owned_.count(name))
  return;                 // <-- skips the SHADOWED declaration entirely
```

`owned_` is function-wide, so once the outer `x` is registered, the inner
`R x = {2};` is never passed to `registerOwned()` and never enters the inner
scope's `vars`. Consequently `dropsFor()` has no obligation to emit for it.

Secondary identity defects in the same file:

- `registerOwned()` deduplicates with `existing.name == v.name` and then does
  `owned_[v.name] = v` — an inner declaration overwrites the outer binding's
  metadata (drop symbol, managed flag, source location).
- `moved_.insert(toks_[arg].text)` and `borrows_` are name-keyed, so
  `cplus_move(x)`, `return x;`, borrow use-after-move and copy rejection can
  consult the inner binding's metadata after the inner scope has closed.
- `checkReturnValue()` uses `owned_.find(name)` and then excludes that **name**
  from cleanup — with shadowing this can exclude the wrong binding.

## Proposed fix for Agent 1 (handoff, NOT applied by Agent 2)

1. Give every registered obligation a stable declaration identity, e.g.
   `size_t declTokenIndex` (the token index of the declarator name) plus the
   scope index. Store obligations in `Scope::vars` keyed by that identity, not
   by name.
2. Replace the function-wide `owned_` name map with a name-to-identity stack:
   `std::map<std::string, std::vector<DeclId>> ownedBindings_`, where lookup
   returns the innermost *live* binding. Pop on scope close.
3. In `tryResourceDeclaration()`, never skip a declaration merely because the
   name was seen before; only deduplicate the exact same declarator token
   (resource-type marker scan + type scan discovering the same declaration).
4. Change `moved_` and `borrows_` values to `DeclId`; `dropsFor()` should receive
   an excluded `DeclId` rather than an excluded name.
5. Keep the emitted C referring to the lexical variable name (the C compiler
   resolves it), but pair it with the correct per-declaration metadata and
   ensure each live obligation is emitted exactly once at its owning scope exit.

Do **not** add runtime bookkeeping for this — the runtime must remain a plain C
ABI with no hidden metadata; the fix belongs in lowering.

## Test evidence

Runner: `sh tests/raii_shadowing/run.sh` (POSIX sh, bounded, exact-log diffs):

```
RAII_SHADOW 10 10 0
```

| Case | Expected | Observed | Verdict |
| --- | --- | --- | --- |
| `control_nonowning` | 3 lines | exact | PASS |
| `repro_shadow_move` | 4 lines | exact | PASS |
| `repro_shadow_basic` | DROP 2 + DROP 1 | exact | PASS |
| `repro_shadow_deep` | DROP 3, DROP 2, DROP 1 | exact | PASS |
| `repro_shadow_branch` | DROP 2 + DROP 1 | exact | PASS |
| `repro_shadow_loop` | DROP 11/12/13 + DROP 1 | exact | PASS |
| `repro_shadow_return` | DROP 2 + DROP 1 | exact | PASS |
| `repro_shadow_partial` | DROP 2 + DROP 1 | exact | PASS |
| `repro_shadow_struct` | DROP_INNER 2 + DROP_OUTER 10 | exact | PASS |
| `repro_shadow_crosstu` | DROP 2 + DROP 1 | exact | PASS |

Generated stress runner: `sh tests/raii_shadowing/run_generated.sh` (deterministic seeded corpus, 999 cases):

```
RAII_GEN 999 999 0
```

Coverage includes deep nesting (2..60), sequential shadow blocks, if/else branches, loops, early returns through nested shadowed scopes, move-in-shadow, break/continue, switch-case shadows, and 520 random nested/seq combinations with exact cleanup-log assertions.

Cross-TU case links `build/libcplus_runtime.a`, `build/libc/libcplus_hardened.a` and `build/safe/libcplus_safecrt.a` with `/usr/bin/clang`. Sanitizer note remains: UBSan does not detect missing destructors; exact cleanup-log assertions are the proof.

## Verified guarantees and unsupported cases

Verified in this environment (Apple clang 16 / arm64, C+ 0.1.0, commit `7cd2020d27976e1d658aaaae83f2f4b281c33665`):

- Shadowed owned locals now receive independent cleanup records and are destroyed exactly once in the verified shadowing suites (10 manual + 999 generated).
- Non-shadowed owned locals are destroyed exactly once in reverse declaration order (control case).
- An explicit `cplus_move` from the shadowed inner binding preserves the moved-from state and destroys the new owner exactly once (move case).
- Resource destruction is host/toolchain-observable through destructor logs; no runtime metadata was added.

NOT verified / explicitly unsupported:

- Full compiler/RAII suite, full repo regression matrix, and sanitizer matrix across all supported targets were **not** run in this pass.
- Nested resource fields are not auto-dropped by the containing resource's destructor in this implementation (baseline check with no shadowing shows only `DROP_OUTER`), so that is not asserted here.
- `goto`, `setjmp/longjmp`, `exit` and abnormal termination remain outside the cleanup guarantee (existing compiler warning for `goto`).
- No claim of full RAII correctness from this sample.

## Exact commands

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
unset SDKROOT CPLUS_CLANG
sh tests/raii_shadowing/run.sh
./build/c+ build tests/raii_shadowing/repro_shadow_basic.cp \
    --default --sanitize=none -I. \
    -o tests/raii_shadowing/out/repro_shadow_basic
./build/c+ build tests/raii_shadowing/repro_shadow_basic.cp \
    --default --sanitize=undefined -I. \
    -o tests/raii_shadowing/out/repro_shadow_basic_ubsan
UBSAN_OPTIONS=print_stacktrace=1 \
    tests/raii_shadowing/out/repro_shadow_basic_ubsan
```
