# EDGE100 — aggregate edge-case campaign and 100% API-reachability proof

Branch: `agent/modern-stdlib`
Snapshot taken: 2026-11-24 (UTC)
Owner: EDGE agent 10/10 (aggregator). Only `tests/edge100/run_all.sh` and this
file are owned by this agent; no source or header was edited for this task.

This document is the aggregation point for the nine EDGE100 sibling suites. It
reports **only values actually observed**, never inferred from a build and
never invented. Where a sibling artifact has not landed, the cell is marked
`PENDING` together with the exact command that produces it.

---

## 1. Scope — what this does and does not prove

**In scope (claimed here):**

- Each sibling area exercises edge/adversarial cases of the C+ standard
  library on **fixed seeds and the current host**, and emits one canonical
  record line:
  `EDGE100 <area> <apis> <cases> <passed> <failed>`.
- The aggregate runner executes every area under a hard bound, in a fixed
  environment, and fails the campaign if any area fails, times out, emits a
  malformed/missing record, or is missing entirely.
- `sh tests/stdlib/run_coverage.sh` demonstrates **100% API reachability**:
  every enumerated library API is exercised by at least one test (see §5).

**Explicitly out of scope (NOT claimed):**

- This is **not a proof of full memory safety, thread safety, or absence of
  undefined behavior**. EDGE100 is a bounded, seeded, host-specific edge-case
  campaign; it cannot enumerate the input space of a general-purpose standard
  library.
- A pass in the emulator/runner is **not** a claim of live-game or production
  success. No such claim is made here.
- "100% API reachability" means every API has a call-site exercised by the
  test corpus. It does **not** mean every branch, path, or input class of every
  API is verified.
- Results are tied to one host (macOS x86-64/arm64 toolchain as available),
  fixed PRNG seeds, and the branch revision recorded above. Re-running on a
  different host, seed set, or revision can legitimately change counts.

---

## 2. Runner contract

`tests/edge100/run_all.sh`:

- Expected areas (all nine required): `alloc`, `containers`, `io`, `netthread`,
  `safecrt`, `multimedia`, `adapters`, `examples`, `runtime`.
- Executes each `<area>/run.sh` from the repository root under
  `timeout -k 10 900` (900 s hard bound; a timeout is a failure, never a pass).
- Fixed environment:
  `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin`,
  `CPLUS_CLANG=/usr/bin/clang`, `LC_ALL=C`.
- Parses the **last** well-formed `EDGE100 <area> <apis> <cases> <passed>
  <failed>` line per area (strips any prefix such as `final=`), validates all
  five numeric fields and that the area token matches, and keeps the exact
  record plus full stdout/stderr under `build/edge100/`.
- Never uses `|| true`; it continues after any failure and exits:
  - `0` — all nine areas present and clean (`failed=0`, runner rc 0);
  - `1` — at least one real failure (missing runner, non-zero rc, missing or
    malformed line, area mismatch, `failed>0`, or `passed>cases`);
  - `2` — no area runner at all (campaign not present).

Regenerate everything with:

```sh
sh tests/edge100/run_all.sh
```

Syntax was validated with `sh -n tests/edge100/run_all.sh` (clean), and the
parsing/table/exit logic was exercised with a synthetic self-test under
`build/` covering: a clean area, a `failed>0` area, a prefixed `final=EDGE100`
line, a missing final line, and missing area runners. All behaved as specified.

---

## 3. Per-area results

At snapshot time the sibling artifacts (`tests/edge100/<area>/RESULTS.md` and
their `run.sh` drivers) had not landed; sibling agents were still producing
probe harnesses. No numbers are fabricated for them.

| Area | APIs enumerated | Cases | Passed | Failed | Status | Command to produce |
|---|---:|---:|---:|---:|---|---|
| alloc | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/alloc/run.sh` |
| containers | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/containers/run.sh` |
| io | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/io/run.sh` |
| netthread | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/netthread/run.sh` |
| safecrt | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/safecrt/run.sh` |
| multimedia | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/multimedia/run.sh` |
| adapters | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/adapters/run.sh` |
| examples | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/apps/legacy_examples/run.sh` |
| runtime | PENDING | PENDING | PENDING | PENDING | PENDING | `sh tests/edge100/runtime/run.sh` |
| **TOTALS (0/9 areas landed)** | PENDING | PENDING | PENDING | PENDING | **INCOMPLETE** | `sh tests/edge100/run_all.sh` |

`TOTALS` is intentionally not a numeric sum while any area is PENDING: summing
a partial campaign would overstate coverage. Once all nine `RESULTS.md` files
have landed, re-run the aggregate and replace this table with its verbatim
output; the runner prints the numeric `TOTAL` row itself.

---

## 4. Coverage cross-check — 100% API reachability

Command (run from the repository root; no Python in the path):

```sh
sh tests/stdlib/run_coverage.sh
```

Exit code observed: `0`. Verbatim stdout (captured at
`build/edge100/coverage.out`; stderr empty):

```text
library             covered    total percent
------------------ -------- -------- -------
allocator.hp              4        4  100.0%
async.hp                 14       14  100.0%
audio.hp                 29       29  100.0%
collections.hp           63       63  100.0%
datastack.hp              1        1  100.0%
error.hp                  9        9  100.0%
file.hp                  13       13  100.0%
fs.hp                      8        8  100.0%
graphics.hp              16       16  100.0%
gsl_adapt.hp             23       23  100.0%
gui.hp                   18       18  100.0%
http.hp                  19       19  100.0%
image.hp                 10       10  100.0%
input.hp                 10       10  100.0%
io.hp                      6        6  100.0%
json.hp                  33       33  100.0%
lifetime_adapt.hp         3        3  100.0%
log.hp                   14       14  100.0%
mempool.hp                1        1  100.0%
net.hp                   15       15  100.0%
string.hp                27       27  100.0%
thread.hp                21       21  100.0%
time.hp                   5        5  100.0%
vector.hp                26       26  100.0%
video.hp                 25       25  100.0%
window.hp                11       11  100.0%

all libraries >= 80% API coverage (worst 100.0%)
```

Summing the `covered`/`total` columns:

| Measure | Value |
|---|---:|
| Libraries | 26 |
| APIs covered | 424 |
| APIs enumerated | 424 |
| Reachability | **424/424 = 100.0%** |

The task brief cites a baseline of **416/416 = 100% API reachability**. The
live run at snapshot time reports **424/424** across 26 libraries; the exact
count has grown since that baseline was recorded, but the substantive
cross-check — **100% reachability of the enumerated API surface** — holds in
both. This is a reachability result, not a behavioral-safety guarantee (§1).

---

## 5. Evidence and audit trail

- Runner: `tests/edge100/run_all.sh` (this agent).
- Per-area exact records: `build/edge100/<area>.final`.
- Per-area full stdout/stderr: `build/edge100/<area>.log`.
- Coverage stdout/stderr: `build/edge100/coverage.out`,
  `build/edge100/coverage.err`.
- All of the above live under gitignored `build/`; no generated evidence file
  is committed and no source/header was modified.

---

## 6. Pending sibling artifacts (no fabrication)

The following are required for the per-area table in §3 to become numeric.
Each is `PENDING` until the owning sibling lands it and the command shown is
run. Running the aggregate is the single command that fills all of them at
once.

```sh
sh tests/edge100/run_all.sh
```

Per-area producers, for reference:

```sh
sh tests/edge100/alloc/run.sh
sh tests/edge100/containers/run.sh
sh tests/edge100/io/run.sh
sh tests/edge100/netthread/run.sh
sh tests/edge100/safecrt/run.sh
sh tests/edge100/multimedia/run.sh
sh tests/edge100/adapters/run.sh
sh tests/edge100/apps/legacy_examples/run.sh
sh tests/edge100/runtime/run.sh
```

Until those artifacts exist and are executed, the EDGE100 case-level headline
counts remain **PENDING (0/9 areas landed)**; only the API-reachability result
in §4 (100%) is a verified number in this document.
