# ABI documentation

This directory records the **ABI compatibility evidence** gathered for the
`agent/modern-stdlib` branch. It is documentation only: no compiler, runtime,
header, source or test files are owned by this directory.

## Documents

| Document | Purpose |
| --- | --- |
| [`compatibility-matrix.md`](compatibility-matrix.md) | Matrix of every ABI check by directory, exact platform tested, platform scope, commands, pass/fail/timeout counts with denominators, the external build blocker, and explicit non-claims. |
| [`source_audit.md`](source_audit.md) | Sibling source-level audit of recent stdlib/install-include header changes (verdicts `NO_ABI_IMPACT` / `POTENTIAL_ABI_IMPACT` / `ABI_CHANGE`); complementary to the runtime checks recorded in the matrix. |
| [`ABI_GATE.md`](ABI_GATE.md) | The mandatory one-command C-ABI non-regression gate: suites, coverage map, and how to add a case. |
| [`STRICT_C_ABI_BOUNDARY.md`](STRICT_C_ABI_BOUNDARY.md) | Current contract for host-struct purity, header/macro hygiene, `stdlib/*.hp` interop boundaries and the `_s` catalog. Supersedes the PENDING status recorded in the snapshot below. |
| [`ADDITIVE_RUNTIME_EXPORTS.md`](ADDITIVE_RUNTIME_EXPORTS.md) | Semantics of the seven additive runtime introspection exports (six new `cplus_*` functions plus the `cplus_shadow_info` type and its `CPLUS_SHADOW_*` flags): disjoint base/bound, CETS generation checks, and the total (never-`-1`) low-fat size-class mapping. |

## Snapshot provenance

| item | value |
| --- | --- |
| captured (UTC) | `2026-11-23T03:58:05Z` |
| branch | `agent/modern-stdlib` |
| HEAD | `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` |
| host | macOS `arm64-apple-darwin27.2.0`, Apple clang 16.0.0 |
| driver | `./build/c+` → `C+ 0.1.0` (backend host Clang/LLVM) |

## Headline result at capture time

* **Authoritative complete suite:** `tests/abi/run_abi_compat.sh` →
  `abi_compat: 69 passed, 0 failed`, 0 timeouts out of 69 runner steps
  (`build/abi/compat/results.log`).
* **One available sibling result:** the `c_to_cp` boundary probe built and ran;
  `tests/abi/c_to_cp/probe_run.log` contains 4/4 expected values, build and run
  both exited 0, 0 timeouts.
* **Still in flight:** `legacy_c`, `cp_to_c`, `precompiled` have probe sources on
  disk but no consolidated result log; `fingerprints` was empty. These are
  marked **PENDING** in the matrix — no pass/fail count is asserted for them.

Results are a snapshot of sibling-agent work "available at doc time" only. A
PENDING cell means the evidence was not present when this document was
captured; it is not a failure and not a pass. See the matrix for the refresh
procedure.

## Scope reminder

Evidence covers the **C ABI subset** exercised on macOS arm64 with the
supported `./build/c+` driver. It is not cross-platform proof, it does not
exercise C++ name mangling, and it does not claim any ABI change. See section 7
of the matrix for the full non-claim list.
