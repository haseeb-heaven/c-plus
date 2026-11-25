# P1 cross-translation-unit analysis — minimal reproducers

Scope: report/fixtures only. No compiler, runtime, or header file was edited.
Nothing here was committed. All files live under
`tests/stdlib/agent1_handoff/cross_tu/`.

Handoff source: `docs/libc/AGENT1_PRIORITY_HANDOFF.md`, P1 row

> Cross-translation-unit analysis — compile-time suite is single-TU only;
> `multi_tu` linkage works but no lifetime diagnostics cross TU.

## How to reproduce

```sh
# from the repo root
sh tests/stdlib/agent1_handoff/cross_tu/run.sh
```

`run.sh` resolves the repo root from its own location, sets `CPLUS_BIN`
(override with `CPLUS_BIN=...`), exports `CPLUS_ROOT` to this checkout, runs
`c+ check` / `c+ build` from this directory, and writes the exact stdout,
stderr, and exit code of every invocation to `results/<step>.log`. The raw
concatenated transcript used for the notes below is what `run.sh` prints.

### Reproducibility detail: `CPLUS_ROOT`

Without `CPLUS_ROOT`, running from this directory makes the driver fall back
to its compiled-in source root (`../c-plus-core`), whose `stdlib/string.hp`
uses `String`/`StringView` names. The reproducers use the lowercase
`string`/`string_view` API of *this* checkout, so `run.sh` exports
`CPLUS_ROOT=$ROOT`. Verified difference, same command/compiler:

```console
$ ../../../../build/c+ check producer.cp
In file included from producer.cp:7:
./owner.hp:16:1: error: unknown type name 'string_view'
...
$ CPLUS_ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib ../../../../build/c+ check producer.cp
check: producer.cp ok
```

This is a header-resolution artefact of the harness, not one of the safety
findings.

## Driver limitation (explicitly recorded, per task)

`c+ check` accepts multiple input files, but it processes them **one TU at a
time**. In `compiler/Driver/Driver.cpp::runDriver`, the loop over `inputs`
constructs a fresh `DiagnosticEngine` per file and calls
`clearExternalClasses()` at the top of each iteration. Header preprocessing
also gets a fresh `processedHeaders` map per TU. There is no shared ownership,
borrow, or lifetime state between the TUs. `c+ build` additionally lowers and
compiles each TU independently and only passes object files to the linker, so
the link step carries no C+ semantic information.

Observed proof (step 06 and step 13 below): `check producer.cp consumer.cp`
returns two independent `check: ... ok` lines. Nothing is emitted at the point
where the two TUs meet.

## Case A — borrowed view returned by another TU

Files: `owner.hp` (declares `string_view produce_view(void)`),
`producer.cp` (returns `local.to_view()` for a local `string`, which frees its
buffer on scope exit), `consumer.cp` (calls `produce_view()` and reads
`view.size()`).

Expected: REJECT or WARNING at compile of `producer.cp` (view of a local
escaping) or of `consumer.cp` (use of a view whose owner is not visible), with
a `[CPLUS-BORROW]` / `[CPLUS-LIFETIME]` diagnostic.

Observed: **ACCEPT, no diagnostic** in default Safe mode, `--strict`, with
both TUs named in one `check` invocation, and at link.

```console
$ .../build/c+ check owner.hp
check: owner.hp ok
[exit code: 0]

$ .../build/c+ check producer.cp
check: producer.cp ok
[exit code: 0]

$ .../build/c+ check consumer.cp
check: consumer.cp ok
[exit code: 0]

$ .../build/c+ check --strict producer.cp
check: producer.cp ok
[exit code: 0]

$ .../build/c+ check --strict consumer.cp
check: consumer.cp ok
[exit code: 0]

$ .../build/c+ check producer.cp consumer.cp
check: producer.cp ok
check: consumer.cp ok
[exit code: 0]

$ .../build/c+ build producer.cp consumer.cp -o .../results/case_a
[exit code: 0]
```

Running the linked binary prints `view size 17` and exits 0 — the stale view
still happens to read intact heap memory. That is undefined behaviour, not a
pass.

## Case B — release in one TU, use via extern pointer in another

Files: `shared.hp` (`extern void *shared_resource;` plus
`void release_shared_resource(void);`), `dropper.cp` (allocates and
`cplus_release()`s the resource), `user.cp` (calls the release then subscripts
`shared_resource`).

Expected: REJECT/WARNING at compile of `user.cp` or at link, naming the
cross-TU use-after-free.

Observed: **ACCEPT, no diagnostic** in default Safe mode, `--strict`, in the
multi-TU `check`, and at link.

```console
$ .../build/c+ check shared.hp
check: shared.hp ok
[exit code: 0]

$ .../build/c+ check dropper.cp
check: dropper.cp ok
[exit code: 0]

$ .../build/c+ check user.cp
check: user.cp ok
[exit code: 0]

$ .../build/c+ check --strict dropper.cp
check: dropper.cp ok
[exit code: 0]

$ .../build/c+ check --strict user.cp
check: user.cp ok
[exit code: 0]

$ .../build/c+ check dropper.cp user.cp
check: dropper.cp ok
check: user.cp ok
[exit code: 0]

$ .../build/c+ build dropper.cp user.cp -o .../results/case_b
[exit code: 0]
```

Running `results/case_b` exits with code 6 on this host (five consecutive
runs). That byte is read from freed memory; it is not a stable or meaningful
result.

### Positive control (same violation, one TU)

`single_tu_control.cp` performs the identical alloc / `cplus_release()` /
subscript sequence inside one function. The checker proves it and emits a
tagged diagnostic:

```console
$ .../build/c+ check single_tu_control.cp
single_tu_control.cp:10:12: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: subscript of released pointer
      return ((char *)shared_resource)[0];
             ^
help: suggested correction: do not use after release; copy the data or transfer ownership with cplus_move()
[exit code: 1]
```

This isolates the gap: the same operation is rejected when visible in one TU
and silently accepted once the release and the use are split across TUs.
A secondary control was also probed (alloc/release in a global function of
one file, read in `main` of the same file): it is likewise accepted, so part
of this gap is also the existing cross-function lifetime limitation; the P1
cross-TU split removes even the possibility of a single-TU proof.

## Case C — backend-only REJECT/WARNING (not C+ enforcement)

Files: `stack_owner.hp`, `stack_producer.cp` (returns a raw pointer to a stack
local), `stack_consumer.cp`.

This variant does produce the requested REJECT/WARNING, but it comes from the
C backend's `-Wreturn-stack-address` and only in the producer TU. It is an
untagged Clang diagnostic and is **not** evidence of C+ cross-TU analysis.

```console
$ .../build/c+ check stack_producer.cp
stack_producer.cp:6:12: error: address of stack memory associated with local variable 'local' returned [-Werror,-Wreturn-stack-address]
    6 |     return local;
      |            ^~~~~
1 error generated.
[exit code: 1]

$ .../build/c+ check --legacy-c stack_producer.cp
stack_producer.cp:6:12: warning: address of stack memory associated with local variable 'local' returned [-Wreturn-stack-address]
    6 |     return local;
      |            ^~~~~
1 warning generated.
check: stack_producer.cp ok
[exit code: 0]

$ .../build/c+ check stack_consumer.cp
check: stack_consumer.cp ok
[exit code: 0]
```

Per the counting rule in `docs/libc/AGENT1_COMPILER_GAPS.md` §0, the Safe-mode
error and the legacy-mode warning are untagged and therefore PARTIAL at best.

## Summary

| Case | Expected | Observed | Tagged C+ diagnostic |
|---|---|---|---|
| A: cross-TU borrowed view | REJECT/WARNING | ACCEPT (check, `--strict`, 2-TU check, link) | none |
| B: cross-TU extern release/use | REJECT/WARNING | ACCEPT (check, `--strict`, 2-TU check, link) | none |
| B control: same sequence in one TU | REJECT | REJECT (rc=1) | `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME]` |
| C: stack pointer in producer TU | REJECT/WARNING | REJECT (Safe) / WARNING (`--legacy-c`) in producer only | none (Clang `-Wreturn-stack-address`) |

Acceptance target for Agent 1: cases A and B must change from ACCEPT to a
stable tagged diagnostic that names the escaping borrow / the cross-TU
use-after-free, while `consumer.cp`, `user.cp`, and the existing 19 valid
control cases in `tests/stdlib/compile_time/` remain accepted.

No diagnostic text above was invented; every block was copied from the
`results/*.log` files produced by `run.sh` against the checked-in compiler at
`build/c+`.
