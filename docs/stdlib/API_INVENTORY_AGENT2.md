# Agent 2 — Public API inventory (source-verified)

**Branch:** `agent/modern-stdlib` · **HEAD at audit:** `5bf9d8e`
**Method:** inventory derived from actual header source and the native coverage
tool (`sh tests/stdlib/run_coverage.sh`), not from documentation. API *reachability*
is explicitly **not** runtime coverage — see §4.

## 1. Denominator and counts

| Item | Count | Source |
|---|---:|---|
| Public stdlib headers (`stdlib/*.hp`) | 26 | `find stdlib -maxdepth 1 -name '*.hp'` |
| Headers with a callable-API entry | 24 | coverage tool output |
| Public callable API entries (coverage parser) | **416** | sum of `total` column |
| Entries with a static test call-site | **411** (98.8%) | sum of `covered` column |
| Entries without a static test call-site | **5** | 416 − 411 |
| Broader method-test suite (distinct programs invoked) | 518 | `tests/stdlib/method_tests/RESULTS.md` |

The coverage parser uses `CLASS`/`SIG`/`CTOR` markers and skips names beginning
with `stdlib_` (internal free helpers). Raw function-definition grep finds more
symbols (e.g. `collections.hp` ~106) — that is a *different* denominator and is
not mixed into the 416 figure. `tests/stdlib/method_tests/` invokes 518 distinct
program-level methods because it also counts constructors, void members and
free helpers the parser skips; those are separate counting rules, not a
contradiction.

## 2. Per-module inventory (coverage-parser denominator)

| Header | Covered / Total | % | Classification |
|---|---:|---:|---|
| allocator.hp | 4/4 | 100 | IMPLEMENTED AND TESTED |
| async.hp | 14/14 | 100 | IMPLEMENTED AND TESTED |
| audio.hp | 29/29 | 100 | IMPLEMENTED AND TESTED |
| collections.hp | 63/63 | 100 | IMPLEMENTED AND TESTED |
| error.hp | 9/9 | 100 | IMPLEMENTED AND TESTED |
| file.hp | 13/13 | 100 | IMPLEMENTED AND TESTED |
| fs.hp | 8/8 | 100 | IMPLEMENTED AND TESTED |
| graphics.hp | 16/16 | 100 | IMPLEMENTED AND TESTED |
| gsl_adapt.hp | 20/23 | 87 | IMPLEMENTED BUT NOT FULLY TESTED |
| gui.hp | 18/18 | 100 | IMPLEMENTED AND TESTED |
| http.hp | 19/19 | 100 | IMPLEMENTED AND TESTED |
| image.hp | 10/10 | 100 | IMPLEMENTED AND TESTED |
| input.hp | 10/10 | 100 | IMPLEMENTED AND TESTED |
| io.hp | 6/6 | 100 | IMPLEMENTED AND TESTED |
| json.hp | 32/33 | 97 | IMPLEMENTED BUT NOT FULLY TESTED |
| lifetime_adapt.hp | 2/3 | 67 | IMPLEMENTED BUT NOT FULLY TESTED |
| log.hp | 14/14 | 100 | IMPLEMENTED AND TESTED |
| net.hp | 15/15 | 100 | IMPLEMENTED AND TESTED |
| string.hp | 24/24 | 100 | IMPLEMENTED AND TESTED |
| thread.hp | 21/21 | 100 | IMPLEMENTED AND TESTED |
| time.hp | 5/5 | 100 | IMPLEMENTED AND TESTED |
| vector.hp | 23/23 | 100 | IMPLEMENTED AND TESTED |
| video.hp | 25/25 | 100 | IMPLEMENTED AND TESTED |
| window.hp | 11/11 | 100 | IMPLEMENTED AND TESTED |

The 5 uncovered entries are in 3 additive headers (gsl_adapt 3, json 1,
lifetime_adapt 1). They are new helper overloads added after the deep-test
corpus was written; the method-test suites invoke the rest of each module.
No API is classified NOT IMPLEMENTED or PARTIALLY IMPLEMENTED in this
inventory: every declared callable has a body. That statement is about
*presence of an implementation*, not about complete semantic correctness or
full input-domain coverage.

## 3. Per-API attributes (module-level summary)

For all modules below, attributes were checked by reading the header source:

| Module | Ownership | Null handling | Failure | Bounds | ABI |
|---|---|---|---|---|---|
| allocator / error | side-table metadata, RAII scopes | null refused via `error*` | explicit category/code | generation + exact-base | additive inline; no layout change |
| memory | borrowed views, no ownership | null refused | `error*` fail-closed | subtract-form `off>cap / n>cap-off` | static inline only |
| string / vector / collections | owning RAII | null args refused | `error*`; commit-last growth | `at()`/`at_checked` + growth guards | same fields; additive helpers |
| io / file / fs | RAII handle, borrowed variants | null handle refused | `error*` on open/read/write | exact-fit + bounded read | flat C symbols |
| json | owning doc pointer | null input refused | parse error categories | lexical depth/length budget | one-pointer object unchanged |
| net / http | owning handles | null refused | error codes | bounded writes; sink OOB fixed | flat C signatures |
| thread / async | RAII guards | null handle refused | checked destroy/join side table | bounded joins | additive checked wrappers |
| time / log | stateless / stream sink | null guarded | defined returns | bounded formatting | additive |
| graphics/gui/image/video/window/input/audio | RAII wrappers | null handle guarded | `error*` | dimension/index guards | flat C backend symbols |
| gsl_adapt / lifetime_adapt | non-owning views / token owners | null refused | `error*` category | span count + generation | additive, one-word pointers |

## 4. Important limit

A static call-site count (411/416) proves API *reachability*, not that every
input class was exercised. Runtime evidence comes from separate suites:
`tests/stdlib/method_tests/RESULTS.md` (518/518 method invocations),
`tests/audit_phase3/RESULTS.md` (114 tests, 102 PASS), the upstream-reuse tests
(19 binaries, 18 PASS), the 1000-scenario and 10k-scenario fuzz suites, and the
red/blue adversarial campaign. None of these establishes 100% memory safety.

## 5. Reproduce

```sh
sh tests/stdlib/run_coverage.sh
sh tests/stdlib/method_tests/run_all.sh
sh tests/audit_phase3/run_all.sh
```
