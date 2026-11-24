# CTL + Goose-style memory — verification index (Agent 2, issue #24)

This is the single entry point for the CTL / data-stack work. Every claim below
is backed by a command and a local artifact; nothing is inferred from upstream
marketing.

## Research (web, this session)

- **Goose** — `aardappel/goose`, Apache-2.0. Data stacks are large VM
  reservations with a bump pointer; a scope exit is one store; growth never
  moves memory; at most one resizable value is live per stack. Fetched from the
  official site/README and specification. Used as **architectural inspiration
  only** — see `GOOSE_MEMORY_INSPIRATION.md`, no Goose code was copied.
- **CTL** — `rurban/ctl` (fork of `glouw/ctl`), MIT, header-only typed C99/C11
  containers. Vendored locally at pinned commit
  `ede5f455392f2380dd16aead26835eece6cd20e4` with provenance in
  `third_party/ctl/PROVENANCE.md`.

## What is verified

| Item | Evidence |
| --- | --- |
| CTL functional suite | `sh tests/ctl/run_ctl_tests.sh` → `ctl tests: 11 passed, 0 failed` |
| CTL usable from C+ | build with `-Ithird_party/ctl`, run prints `CTL 2 9` |
| Goose-style native memory model | `stdlib/datastack.hp` VM bump stack + single-store `stdlib_scratch_scope`; `stdlib/mempool.hp` stable `slot_pool` |
| Demos | `examples/goose_memory/`, `examples/ctl_demo/` (see their `README.md` and exact logs) |
| Component adoption analysis | `docs/stdlib/CTL_ADOPTION_MATRIX.md` |

## Contract boundary

- CTL is plain C with raw `realloc`; it is **not memory-safe by itself**. C+
  RAII wrappers and the checked runtime provide deterministic cleanup.
- The Goose-inspired model is **native C+**, not Goose: C+ keeps its ordinary C
  ABI, checked heap and liveness hooks. No "no allocator / no destructors"
  claim is made for C+.
- No existing C+ container is removed; no compiler frontend file is modified.

## Reproduce

```sh
sh tests/ctl/run_ctl_tests.sh
sh examples/goose_memory/run.sh
sh examples/ctl_demo/run.sh
```

## Non-claims

- No equivalence with Goose's compiler-enforced memory model.
- No blanket CTL safety claim.
- No full-project claim from these focused suites; run the full gates separately.
