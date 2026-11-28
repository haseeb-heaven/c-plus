# Reprioritized safety handoff to Agent 1

Date: 2026-11-23. Owner of this document: Agent 2 (standard library). No
`compiler/**` or `runtime/**` file was edited. The project owner's guidance is
to stop expanding stdlib surface (new syntax/classes/GUI/networking) and make
the compiler enforce safety. The requirements below are therefore **Agent 1
work**; Agent 2 supplies reproducers, contracts, and regression fixtures only.

## Priority mapping (owner's order)

| Priority | Engineering requirement | Current Agent 2 evidence | Minimal reproducer directory |
|---|---|---|---|
| P0 | Complete UAF and double-free analysis | `docs/libc/AGENT1_COMPILER_GAPS.md` §2; `COMPILE_TIME_GAPS.md` 27 gaps; `string_double_drop.cp` tagged `[CPLUS-OWNER]` only for managed pointers | `tests/stdlib/agent1_handoff/uaf_double_free/` |
| P0 | CFG-based ownership analysis | `AGENT1_COMPILER_GAPS.md` §2/§4; compile-time cases accept use-after-drop and two mutable aliases | `tests/stdlib/agent1_handoff/cfg_ownership/` |
| P0 | Pointer arithmetic and bounds verification | `AGENT1_COMPILER_GAPS.md` §5/§6; only fixed-array literal subscripts get `[CPLUS-BOUNDS]` | `tests/stdlib/agent1_handoff/pointer_bounds/` |
| P0 | Sound alias analysis | `AGENT1_COMPILER_GAPS.md` §4; `vector_mutable_alias.cp`, `collections_mutable_alias.cp` accepted | `tests/stdlib/agent1_handoff/alias_analysis/` |
| P0 | Cross-function lifetime analysis | `AGENT1_COMPILER_GAPS.md` §2; `string_dangling_view.cp`, `json_dangling_ref.cp` accepted | `tests/stdlib/agent1_handoff/cross_function_lifetime/` |
| P1 | Cross-translation-unit analysis | compile-time suite is single-TU only; `multi_tu` linkage works but no lifetime diagnostics cross TU | `tests/stdlib/agent1_handoff/cross_tu/` |
| P1 | Definite initialization on every path | partial coverage: `[CPLUS-LIFETIME] uninitialized` on direct variables, silent on branch/path cases | `tests/stdlib/agent1_handoff/definite_init/` |
| P1 | Strict-safe mode (reject unproven operations) | `--strict` currently differs mainly by sanitizer settings; no "proof of safety" gate | `tests/stdlib/agent1_handoff/strict_safe_mode/` |
| P1 | Thread-safety analysis | `thread.hp` runtime guards exist; no static data-race analysis | `tests/stdlib/agent1_handoff/thread_safety/` |
| P2 | Fuzzing and compiler differential testing | stdlib fuzz 1200 cases, libc fuzz 4800, rust-vs-C+ 600; no compiler differential false-negative measurement | `tests/stdlib/agent1_handoff/fuzz_diff/` |
| P2 | Linux, macOS, Windows verification | macOS local; Linux CI jobs exist (UNVERIFIED remotely); Windows x86 DLL script only | `tests/stdlib/agent1_handoff/portability/` |
| P2 | Performance vs Clang and Fil-C | `benchmarks/safety_compare/RESULTS.md` has C+/Rust/the inspiration language medians; Fil-C toolchain absent | `tests/stdlib/agent1_handoff/performance/` |

## Current measured baseline (Agent 2 side)

- Compile-time suite: 8 manifests / 52 cases. Observed normalized: **5** tagged
  `[CPLUS-` REJECT, **7** untagged (PARTIAL) rejections, **40** compiles-clean.
  **27** cases are declared capability/tagging gaps, reported but never counted
  as passes. Run: `CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/compile_time/run_compile_time.sh`.
- Existing compiler capability audit: 9 documented capabilities in
  `docs/libc/AGENT1_COMPILER_GAPS.md` with minimal reproducer, observed behavior,
  proposed stable `[CPLUS-*]` code, suggested compiler location, acceptance test,
  and an explicit split between "compiler must reject" and "runtime mitigation
  already exists (not enforcement)".
- Existing untagged-diagnostic problem: use-after-move and owner-copy are
  genuinely rejected by the RAII lowering pass but emit plain `error:` messages;
  under the suite rule these are PARTIAL, not enforcement.

## What Agent 2 will do for each P0/P1 item

1. Provide a minimal `.cp` reproducer and its exact `c+ check --strict` output.
2. State the expected stable diagnostic code and the invalid operation it must name.
3. Keep the runtime library contract unchanged so a compiler fix can be validated
   without library churn.
4. Add the reproducer to a manifest so Agent 1 can run it before/after each change.
5. Report results in `docs/libc/AGENT1_PRIORITY_HANDOFF.md` (this file) without
   attributing compiler defects to the stdlib.

## Definition of done for Agent 1 (proposed)

- P0: every reproducer in the P0 directories changes from ACCEPT/untagged to a
  stable tagged `[CPLUS-LIFETIME]`/`[CPLUS-OWNER]`/`[CPLUS-BOUNDS]`/`[CPLUS-ALIAS]`
  diagnostic that names the actual invalid operation. No false positives on the
  ACCEPT control cases in the compile-time suite (currently 19/19 as expected).
- P1: cross-TU ownership state is propagated; definite-init is checked on all
  CFG paths; strict-safe mode rejects operations lacking proven safety while
  leaving the 19 valid control cases accepted; data races are reported.
- P2: a differential harness measures false negatives against randomized valid
  programs; Linux/macOS/Windows jobs are observed post-push; performance is
  measured with identical workloads against Clang and (when available) Fil-C.

## Appendix A — current runtime protections (exact inventory)

The following table is the project owner's current inventory. It is reproduced
verbatim in substance so the handoff does not overstate what runtime checks
provide. **Every entry is dynamic or opt-in; none is a substitute for the P0
compiler enforcement above.**

| Protection | Mechanism | Limitation |
|---|---|---|
| Dynamic array bounds | Clang UBSan | Does not provide general heap-pointer bounds tracking |
| Null dereference | UBSan null instrumentation | Only detects violations reached during execution |
| Misaligned access | UBSan alignment instrumentation | Only detects executed violations |
| Signed integer overflow | UBSan undefined-behaviour instrumentation | Only detects executed violations |
| Managed allocation tracking | C+ allocation side table | Only applies to C+ managed allocations |
| Double release | Allocation registry and recently-freed tracking | Address reuse can defeat stale-address identification |
| Invalid release | Managed allocation registry | Does not protect arbitrary raw libc allocations |
| Explicit stale-pointer check | `cplus_check_live()` | Must be explicitly called |
| Allocation multiplication overflow | `cplus_alloc_array()` | Does not solve arbitrary pointer arithmetic |
| Resource cleanup | RAII runtime | Non-local exits remain a problem |

Consequences for the priority list:

- P0 UAF/double-free and CFG ownership are needed because the allocation registry
  is bounded and address-reuse-sensitive.
- P0 bounds verification is needed because UBSan does not track heap-pointer
  capacity; `cplus_alloc_array()` only checks the multiply, not later arithmetic.
- P0 cross-function and P1 cross-TU lifetime are needed because
  `cplus_check_live()` is explicit and RAII cleanup is bypassed by non-local exits
  (see `AGENT1_COMPILER_GAPS.md` capability 7, RAII around `longjmp`).
- P1 strict-safe mode is needed because the dynamic checks above observe only
  executed violations; unexecuted invalid operations compile cleanly.

## Explicit non-goals for Agent 2 in this cycle

- No new stdlib modules, syntax, GUI wrappers, or networking libraries.
- No compiler or runtime implementation changes.
- No manual ASan poisoning (previously audited and rejected: distinct host
  allocations and separate metadata tables).
- No claim that a sanitizer pass proves C+ safety enforcement.
