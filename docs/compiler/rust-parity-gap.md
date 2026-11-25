# What is missing for Rust-like compile-time safety

Evidence-based audit at branch `agent/core-safety` (`ac0fe14` + local changes).
Sources: implementation files, machine-generated result artifacts, and actual
`rustc 1.100.0 --emit=metadata` probes. Reading a design document is not code
reuse and is not counted as implementation.

## Measured agreement today

Two paired suites give different but consistent pictures:

- `tests/out/core/rust_compare/report.txt`: 27 paired scenarios.
  **C+ 23/27 compile-rejects, Rust 19/27, 23/27 outcomes agree** after adding
  call-site write-exclusivity (the only scenario where Rust was stricter,
  `mutable_alias`, now matches). 4 divergences remain, all C+ being stricter
  (`realloc_lost`, `vla_unbounded`, `arith_div_unknown`, `arith_helper`).
- `tests/compiler/memory_safety_compare/results/summary.txt`: 7 categories.
  C+ 6 compile-reject + 1 runtime trap; Rust 3 compile-reject + 4 runtime
  traps. Compile-stage agreement 3/7, runtime-stage agreement 1/7, 3/7 differ
  because C+ rejects runtime-valued null/overflow/shift while Rust panics.

These are bug-pattern outcomes, not parity of mechanisms.

## Guarantee-by-guarantee gap analysis

| Rust guarantee | C+ compile-time equivalent | Verdict |
| --- | --- | --- |
| Shared vs `&mut` aliases | `AstSafetyCheck.cpp:4327-4348` direct `*p=` heuristic; new call-site write-exclusivity using `paramWriteSlots_` | Same rejection for those shapes, not a general guarantee |
| Lifetimes / NLL | escape check `RaiiLowering.cpp:1009-1030`, `-Werror=return-stack-address` | Escape rejection only; no regions or last-use liveness |
| Moves / use-after-move | `RaiiLowering.cpp:774-786`, `AstSafetyCheck.cpp:4664-4696` | Works for tracked resource locals by identifier; no `Copy`/generic values |
| Drop checking | by-value owner copy rejected `RaiiLowering.cpp:1094-1099`; double release rejected | Resource locals only; containers/fields and raw `malloc` leaks unsupported |
| `Send`/`Sync` race freedom | two conservative global/pthread heuristics `StaticBoundsCheck.cpp:3122`, `AstSafetyCheck.cpp:7038-7092` | Same rejection for the direct-global case, no marker-trait model |
| Enum exhaustiveness | C enums are ints; `-Wswitch` is only a warning | Absent |
| `Option`/`Result` | provable-null rejection only | Type-level guarantee absent |
| Const overflow / bounds | own AST checks (`CPLUS-ARITH`, `CPLUS-SAFE-BOUNDS`), 13/13 bounds + 24/24 native matrix | Same rejection for provable cases; different mechanism, dynamic cases are runtime/UBSan |
| `str` validity / UTF-8 | `char *` is bytes | Absent |
| Trait bounds / const generics | no type system | Absent |
| `unsafe` boundary | whole-program mode (`--legacy-c`) and FFI contracts, not expression-level | Absent |
| Absence of UB in safe code | explicitly disclaimed in `README`/`docs/LIMITATIONS.md` | Absent |

## What C+ actually has

Name/alias-keyed bug detectors plus Clang dataflow (upstream
`runUninitializedVariablesAnalysis` is linked and called at
`OwnershipAnalysis.cpp:541`), token-level spatial checks, ABI-preserving native
guards, and deterministic cleanup lowering. There are **no reference types,
loan sets, regions, marker traits, or generic type system**. The source says so
directly: `AstSafetyCheck.cpp:3901-3902` ("Multiple aliases are valid C … do
not impose Rust-style exclusivity"), `RaiiLowering.cpp:1210-1211` ("deliberately
small borrow checker"), `docs/LIMITATIONS.md:21` ("Full Rust-style borrow
checker — not implemented").

## Compile-time-only improvements still achievable without ABI change

Ranked by value and feasibility:

1. **Loan expiry / NLL-style liveness** using the CFG already built in
   `OwnershipAnalysis.cpp:459-611`; replaces never-expiring string borrow maps.
2. **Decl-keyed move/drop state** unified across `movedVars_`, `movedOwners_`
   and `aliasGroups_` so alias copies inherit move state (currently misses
   `H *c = b` after a move).
3. **Path-sensitive drop checking** over lowered scopes (generalising the
   hand-coded realloc/conditional special cases), STRICT-only.
4. **Parameter-indexed effect signatures attached to declarations** (upgrade
   name-keyed `FunctionEffect`/JSON contracts), giving cross-TU ownership and
   a foundation for `Send`-like properties.
5. **Promote `-Wswitch` and add call-site array-size checks** (one-line flag
   changes in `Driver.cpp:937-941`), closing two easy rejection gaps.

## Structurally out of reach without a new language/ABI

True `&`/`&mut` exclusivity, region inference, `Send`/`Sync`, closed enums,
`Option`/`Result`, traits/const generics, a distinct `str` type, expression-level
`unsafe`, and UB-freedom in safe code. These require new types and signatures;
they cannot be recovered from C's raw-pointer ABI by a Sema pass.

## Conclusion

C+ can match Rust's **outcomes on a bounded set of programs** and already
exceeds Rust on several compile-time checks (unknown divisors, helper effect
inference, raw `realloc` loss, VLA). It cannot honestly claim Rust's
**guarantees** without type-system changes that would break the required C ABI
and ordinary C compatibility.

## Real unmodified C applications (measured)

`tests/compiler/real_apps/run_suite.py`: 10 pinned permissively-licensed C
apps/games, 120 configs (4 standards × LEGACY/DEFAULT/STRICT + clang + gcc-14):
**87 PASS, 27 SAFE_MODE_RULE, 6 REFERENCE_REJECTED, 0 DEFECT, 0 mismatches**;
source preservation 38/38, legacy symbols 38/38, safe-mode app symbols 49/49.

Two genuine DEFAULT-mode defects were found and FIXED:
- wrapped `free()` trapped 105 for allocations produced outside the tracked
  paths (helper/strdup/realloc); unregistered pointers now keep ordinary C
  behaviour while tracked double frees still trap 104;
- `--` was emitted as `CPLUS_INC(..., +1, ...)`, turning `j--` into an
  increment; decrements now use the `sub` helper.

`tests/compiler/real_world_c/wave3`: 175 PASS / 5 MODE_REJECTED / 0 hard fail.
`pc_avg` was a false positive and is now accepted in STRICT per call-site
pointer/count facts. The remaining 5 (`g_bs`) are conservative STRICT
rejections for a pointer bounded by `start`/`end` parameters with a computed
`sizeof/sizeof - 1` length — safe-mode rules, not unsoundness.

`tests/compiler/abi_mixed/run_suite.py`: 12/12 steps, 0 failures. Objects
built by c+ and clang from a real C helper link in BOTH directions and print
identical output — the previously unproven cross-compiler ABI claim.

`tests/compiler/rust_proof/run_proof.py`: 14 paired C+/Rust scenarios,
14/14 passed: AGREE_COMPILE 10, AGREE_TRAP 1, CPLUS_STRICTER 3, MISMATCH 0.
The three "stricter" cases are raw-pointer UAF/double-free/null where Rust
requires `unsafe` and accepts; C+ gives named compile-time diagnostics.
