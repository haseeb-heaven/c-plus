# Handoff notes -- P0 "CFG-based ownership analysis" minimal reproducers

Owner: handoff agent 1 (parallel handoff batch). Files in this directory are the
only files this agent created or modified. No git add/commit, no compiler,
runtime, header, or test-runner edits. All observations below come from the
checked-in repo compiler; nothing is fabricated.

## Environment and method

- Compiler: `./build/c+` (repo root), `C+ 0.1.0`, backend host Clang/LLVM.
- Repo HEAD at observation time: `a90b69c` (branch `agent/modern-stdlib`).
- Command observed, run from the repo root for every case:

  ```sh
  ./build/c+ check --strict tests/stdlib/agent1_handoff/cfg_ownership/<case>.cp
  ```

- `--strict` is accepted by the driver and passed through to the C backend; it
  does not itself change the C+ static-analysis result (checked: identical
  output with and without it).
- `build --strict --sanitize=none` was also observed for classification, with
  `-o` pointed at a temp directory outside the repo so no build artifact is
  created in the worktree. `build` was never executed as a program.
- Classification rule is the one used by
  `tests/stdlib/compile_time/run_compile_time.sh`: **REJECT** requires
  `check` rc != 0 AND `build` rc != 0 AND a `[CPLUS-`-tagged diagnostic in the
  output; both rc == 0 is **ACCEPT**; a rejection with no `[CPLUS-` tag is
  **GAP-UNTAGGED**.

Every file is annotated `EXPECT: REJECT (tagged C+ diagnostic)`. The expected
family is `[CPLUS-LIFETIME]` (use-after-free / use-after-move), matching the
repo's tagged lifetime diagnostics. The **observed** classifications are
recorded honestly below: three false negatives and one untagged rejection.

None of the four reproducers contains runtime UB by itself: `string::at()`
returns `'\0'` on a dead owner and `vector::size()` returns 0 on a zeroed
moved-from owner. The violations are static ownership/lifetime violations that
a CFG-based analysis must reject, deliberately checkable without executing the
program.

## Summary

| Case | Scenario | check rc | build rc | `[CPLUS-` tag | Expected | Observed |
| --- | --- | ---: | ---: | --- | --- | --- |
| `conditional_drop.cp` | conditional drop | 0 | 0 | no | REJECT | **ACCEPT** (false negative) |
| `conditional_move.cp` | conditional move | 1 | 1 | no | REJECT | **GAP-UNTAGGED** |
| `mixed_path_init.cp` | mixed-path release at join | 0 | 0 | no | REJECT | **ACCEPT** (false negative) |
| `loop_carried_release.cp` | loop-carried release | 0 | 0 | no | REJECT | **ACCEPT** (false negative) |

## 1. conditional_drop.cp -- conditional drop

Command:

```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/cfg_ownership/conditional_drop.cp
```

Exact stdout (stderr empty, exit **0**):

```
check: tests/stdlib/agent1_handoff/cfg_ownership/conditional_drop.cp ok
```

`build --strict --sanitize=none` exits 0 with no output. **Classification:
ACCEPT**, not the expected tagged REJECT.

**CFG fact Agent 1 must track.** `s.drop()` is guarded by `if (flag)`. At the
post-`if` join the owner state is the lattice join `Live ⊔ Released =
MaybeReleased`, because the true edge is feasible (`flag = 1`). A use
(`s.at(0)`) in `MaybeReleased` must be rejected. A linear token scan cannot see
this: it either never records a release inside a guarded statement or does not
propagate it to the join. This needs an edge-sensitive merge at the `if`
join, not just "a drop occurred somewhere earlier in the function".

## 2. conditional_move.cp -- conditional move

Command:

```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/cfg_ownership/conditional_move.cp
```

Exact stdout (empty); exact stderr; exit **1**:

```
tests/stdlib/agent1_handoff/cfg_ownership/conditional_move.cp:25:29: error: use-after-move: use of moved-from resource 'a'
      print("n", n);
help: the handle was zeroed by cplus_move
```

`build --strict --sanitize=none` exits 1 with the same three lines.
**Classification: GAP-UNTAGGED** (rejected, but no `[CPLUS-` prefix), not the
expected tagged REJECT.

**CFG fact Agent 1 must track.** `b = cplus_move(a)` is guarded. At the join
`a` is `MovedFrom ⊔ Live = MaybeMovedFrom` and `b` is `NonOwning ⊔ Owning =
MaybeOwning`. `a.size()` must be rejected on the may-move path, and the
auto-drop of `b` at scope exit must be treated as "maybe owning" rather than
unconditionally owning. The current check is path-insensitive: it flags the
use as soon as `cplus_move(a)` appears earlier in the token stream (hence the
rejection), emits no `[CPLUS-` tag, and points line 25 col 29 at the following
`print("n", n);` statement rather than at `a.size()` on line 24. See the probe
below showing the same tracker flags a use that lives in the `else` branch,
where no move occurred at all.

## 3. mixed_path_init.cp -- mixed-path initialization / release at join

Command:

```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/cfg_ownership/mixed_path_init.cp
```

Exact stdout (stderr empty, exit **0**):

```
check: tests/stdlib/agent1_handoff/cfg_ownership/mixed_path_init.cp ok
```

`build --strict --sanitize=none` exits 0 with no output. **Classification:
ACCEPT**, not the expected tagged REJECT.

**CFG fact Agent 1 must track.** Two branches write to the same owner: the
then-branch releases it, the else-branch keeps it live and appends. The join
must merge per-predecessor states: `{Released, Live} -> MaybeReleased`. The
use after the join (`s.at(0)`) is invalid on the then-branch path. This is the
canonical "phi node over ownership state" case; Agent 1 must key the state by
the owner, not by the source line, and must join on all predecessors of the
merge block (not just fallthrough).

## 4. loop_carried_release.cp -- loop-carried release

Command:

```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/cfg_ownership/loop_carried_release.cp
```

Exact stdout (stderr empty, exit **0**):

```
check: tests/stdlib/agent1_handoff/cfg_ownership/loop_carried_release.cp ok
```

`build --strict --sanitize=none` exits 0 with no output. **Classification:
ACCEPT**, not the expected tagged REJECT.

**CFG fact Agent 1 must track.** The release sits inside an `if` inside a loop
body. The loop header is a merge of the entry state and the back-edge state, so
the analysis must iterate to a fixed point: after `i == 0` the owner is
`Released`, and that fact flows along the back-edge to the header and then past
the loop exit, giving `MaybeReleased` at `s.at(0)`. A straight-line scan loses
the release entirely because of the nested `if` plus loop scope. Agent 1 must
model the back-edge and reach a fixed point; one pass over the loop body is not
sufficient in general.

## Supplementary probes (exact observations, temp files outside the repo)

These isolate *why* the four cases are misses. They are evidence only; they are
not part of the manifest and were not added as repo test files.

**Probe A -- tagged diagnostics do exist, but only in straight-line code.**

```c
int *p = cplus_new(int);
cplus_check_live(p);
cplus_release(p);
cplus_check_live(p);
```

`./build/c+ check --strict <probe>` exits **1** with:

```
<probe>:7:5: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: liveness check on released pointer 'p'
      cplus_check_live(p);
      ^~~~~~~~~~~~~~~~
help: suggested correction: do not dereference after release; copy the data or transfer ownership with cplus_move()
```

**Probe B -- same release, but guarded by `if`:** `if (flag) cplus_release(p);
cplus_check_live(p);` exits **0** ("ok") with no diagnostic.

**Probe C -- same release, inside a loop with a nested `if`:** exits **0**
("ok") with no diagnostic.

Together A/B/C show the tagged liveness checker is linear, not CFG-based: it
recognizes a release only when no branch/back-edge intervenes.

**Probe D -- the move tracker is path-insensitive (false positive).** With
`if (flag) b = cplus_move(a); else print("safe", a.size());`, `check --strict`
exits **1**:

```
<probe>:12:36: error: use-after-move: use of moved-from resource 'a'
      b.drop();
help: the handle was zeroed by cplus_move
```

The use is in the `else` branch where no move happened, yet it is still
rejected. The tracker also mis-locates the diagnostic (it points at the
following `b.drop();` line). A real CFG analysis must suppress this while
still rejecting case 2.

## What a correct fix must change

1. Build an owner-state lattice (`Live`, `Released`, `MovedFrom`, `Owning`,
   `NonOwning`, and their "Maybe" joins) keyed by owner SSA-ish identity, not
   by source line.
2. Join states at every control-flow merge (`if` join, `if/else` join, loop
   header via fixed point), and propagate through back-edges.
3. Reject a use of an owner in `MaybeReleased`/`MaybeMovedFrom` and emit a
   tagged `[CPLUS-LIFETIME]` diagnostic at the *use* site.
4. Preserve the positive controls: unguarded single `cplus_move` plus no use of
   the source must stay accepted, and straight-line release + `cplus_check_live`
   must stay rejected (probe A).
5. Reconsider the high-level API gap: `.drop()`/`.at()`/`.size()` on `string`/
   `vector` class instances are currently not modeled at all, so cases 1, 3
   and 4 cannot be diagnosed until class-method effects are summarized for the
   ownership analysis.
