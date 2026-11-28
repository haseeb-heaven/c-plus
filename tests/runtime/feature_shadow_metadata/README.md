# Issue #181 — disjoint bounds / shadow metadata regressions

`run.sh` compiles `test_shadow_metadata.c` (which includes the real
`runtime/cplus_runtime.c`) and verifies:

- disjoint `(base, bound)` lookup for base and interior pointers;
- CETS generation-checked interior validation and ABA invalidation;
- EffectiveSan-style size-class metadata (#182);
- pointer-slot seed/copy propagation;
- deterministic OOB (`101`), null (`102`) and UAF (`103`) traps via
  `fork()`/`waitpid()`;
- stack-frame key registration and invalidation;
- O(1) detection: 1000 instrumented slot checks must not increment the
  introspection containment-scan counter, while one interior introspection
  lookup must.

`test_reclaim.c` adds issue #184: with `CPLUS_RECLAIM_MODE=epoch`, `run.sh`
builds a second binary and checks (in forked children) that a retired address
survives multiple epoch boundaries while a stack word / registered global word
still references it, that neither `cplus_alloc` nor plain `malloc` reuses it
(the payload stays intact), that a second release still traps with 104, and
that unreferenced blocks are eventually reclaimed. The `gc-off` run proves the
default immediate-free behaviour is unchanged.

`test_driver_flag.sh` is an optional end-to-end smoke test for the opt-in
`-fcplus-disjoint-bounds` and `-fcplus-safe-gc` compiler flags. It skips unless
`CPLUS_BIN` points at an already-built `c+` executable:

```sh
sh tests/runtime/feature_shadow_metadata/run.sh
CPLUS_BIN=build/c+ sh tests/runtime/feature_shadow_metadata/test_driver_flag.sh
```

The driver flag is off by default. When enabled, the driver strips it from the
pass-through argument list, instruments the lowered C after all static safety
analysis, and prepends only the prototypes for the injected runtime calls.
Untracked pointers keep plain-C behaviour.

Scope/limits (by design, additive opt-in): propagation is tracked within a
function for pointer locals/parameters, their copies and simple pointer
arithmetic; cross-function shadow-stack propagation and side-effecting index
expressions are not instrumented. Pointers produced outside tracked C+
allocations (raw `malloc`, uninstrumented libraries, integer casts) remain
untracked so the plain-C ABI boundary is unchanged.
