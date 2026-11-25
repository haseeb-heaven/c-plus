# RED/BLUE 20 — final aggregate

Branch: `agent/modern-stdlib` @ `7edf2c6` (ABI source baseline `cbcea57`).
Host: `arm64-apple-darwin`, Apple clang 16.0.0; C+ driver via `./build/c+`.
This document is an **aggregation of sibling artifacts only**. Every count below
is copied from a landed `tests/redblue20/red_*/FINDINGS.md` or
`blue_*/PROOF.md` (or, where explicitly labelled, from a machine artifact such
as `results.json`). Nothing here is fabricated, and no artifact was edited by
this agent. Missing artifacts are marked **PENDING** with the exact command.

> **Evidence, not proof.** Every result below is evidence for the specific
> fixed-seed inputs, build and host recorded by the owning team. It is **not**
> a proof of absolute security, not a formal memory-safety/absence-of-UB
> theorem, and not a claim about live-game behaviour, Windows x86/MSVC/UCRT
> layout, calling convention, or arbitrary raw-pointer safety. A finite
> campaign cannot establish the absence of all defects.

---

## 1. Red teams (10)

Counts are the raw `RED20 <area> cases crash corrupt unexpected_ok refused`
lines. "Genuine" means the owning team classified at least one finding as a
real, reproducible defect.

| # | Team / artifact | Raw counts (cases/crash/corrupt/unexpected/refused) | Genuine findings? | Reproducers? | Status of artifact |
|---|---|---|---|---|---|
| 1 | `red_alloc/FINDINGS.md` (agent 1) | `3200 634 593 0 1973` | **YES** — 10 distinct findings (F-ALIGN-01; F-ARENA-02a/b; F-POOL-01/02/03; F-DS-01/02/03/04) | YES — `out/repro 1..11`, exact rc 139/138/107 or canary diagnostics | present |
| 2 | `red_containers/FINDINGS.md` (agent 2) | `6000 0 0 0 6000` | **NO** — `SELFCHECK 5 5`, two identical campaigns | self-test of classifier; `scenarios.log` SHA-256 | present |
| 3 | `red_io/FINDINGS.md` (agent 3) | `3600 0 78 101 3421` | **YES** — 2 (CR-at-exact-fit 78; embedded NUL 101) | YES — `probe_findings.cp` + byte-level reproducer | present |
| 4 | `red_netthread/FINDINGS.md` (agent 4) | `3000 0 0 0 3000` | **NO** — `SELFCHECK 5 5`; prior sink findings regression-confirmed fixed | self-test + 3000 deterministic cases | present |
| 5 | `red_safecrt/FINDINGS.md` | `3136 30 33 1175 1898` | **YES** — 3 (memcmp_s_auto NULL at `n==0` [14 of the 1175]; success while writing past `dmax` [33]; oversized scan bound over-read [30]) | YES — `./repro null\|oob\|unterm` | present |
| 6 | `red_format/FINDINGS.md` (agent 6) | `7200 0 440 56 6704` | **YES** — 3 (`%n` filter bypass 56; include-order capacity downgrade 360; raw-API `dmax` lie 80) | YES — probe 5 and `results/{format_first,io_first}.tsv` | present |
| 7 | `red_error/FINDINGS.md` (agent 7) | `2400 0 118 36 2246` | **YES** — 5 (E1 length-query truncation; E2 latched reentrancy guard; E3 callback rewrites record; E4 realloc bypasses failure injection; E5 release accepts foreign block) | YES — exact case IDs per family | present |
| 8 | `red_apps/FINDINGS.md` (agent 8) | `1272 0 0 0 1272` | **NO** — apps take no argv/stdin; classifier self-tested | `fuzz_results.json` + 6 classifier probes | present |
| 9 | `red_multimedia/FINDINGS.md` (agent 9) | `2520 24 33 180 2283` | **YES** — 5 groups (NULL-path silent success [120 genuine of the 180]; `audio_decoder::read` no capacity bound [33]; `gui::slider` abort [9]; decoder-outlives-media UAF [8]; absurd image dimensions [7]) | YES — focused repro + scenario IDs | present |
| 10 | `red_abi/FINDINGS.md` (agent 10) | `checks=6 mismatches=0 modified_core_files=8` | **YES for source integrity** — 8 committed `M` C files under `libc/src/`; **NO ABI mismatch** (fingerprint match, 0 symbol mismatches) | YES — `artifacts/core_modified.diff`, ABI fingerprint compare | present |

Red total campaigns where a case count exists: **31 328 cases** with **688
crash**, **1295 corrupt**, **1548 unexpected_ok** classifications — but most of
those classifications are *not* genuine defects (e.g. `red_safecrt` reports
1161 of 1175 `UNEXPECTED_OK` as benign, `red_multimedia` 60 of 180 as defined
SDL behaviour). The per-team "Genuine" column is the authoritative one.

---

## 2. Blue teams (10)

| # | Team / artifact | Cases | Passed | Failed | Artifact status |
|---|---|---:|---:|---:|---|
| 1 | `blue_abi/PROOF.md` (agent 20, this agent) | 7 integrity properties | 7 | 0 | present — `BLUE20 abi 7 7 0`; raw unmodified runners `BLUE20 abi-raw 7 5 2` |
| 2 | `blue_alloc/PROOF.md` (agent 11) | 2400 | 2400 | 0 | present — `BLUE20 alloc 2400 2400 0` |
| 3 | `blue_containers/PROOF.md` (agent 12) | 2400 | 2400 | 0 | present — `BLUE20 containers 2400 2400 0` |
| 4 | `blue_io/PROOF.md` (agent 13) | 2400 | — | — | **PENDING**: the "Exact results" section is unfilled ("Filled in from the verified `run.sh` execution at the end of this record"), and recorded logs show in-flight failures (`out/dbg.stdout: BLUE20 io 2400 2037 363`; `out/run1.stdout: BLUE20 io 2400 1970 430`). Exact command: `sh tests/redblue20/blue_io/run.sh` |
| 5 | `blue_netthread/PROOF.md` (agent 14) | 2048 | 2048 | 0 | present — `BLUE20 netthread 2048 2048 0` |
| 6 | `blue_error/PROOF.md` (agent 17) | 2400 | 2400 | 0 | present — `BLUE20 error 2400 2400 0`; six injected mutants each detected 400 cases |
| 7 | `blue_format/PROOF.md` (agent 16) | 2100 | 2100 | 0 | present — `BLUE20 format 2100 2100 0`; non-vacuity mutant `2100 1104 996` |
| 8 | `blue_multimedia/PROOF.md` (agent 19) | 2400 | 2400 | 0 | present — `BLUE20 multimedia 2400 2400 0` |
| 9 | `blue_safecrt/PROOF.md` | 2400 | 2400 | 0 | present — `BLUE20 safecrt 2400 2400 0`; negative control `64 0 64` |
| 10 | `blue_apps/PROOF.md` (agent 18) | 106 000 | 106 000 | 0 | present — `BLUE20 apps 106000 106000 0`; 0 build failures, 0 signal/timeout/corrupt/nonzero, `findings=0` |

Blue total confirmed cases: **122 148 / 122 148 PASS / 0 FAIL** across the eight
case-based landed blue proofs (abi's 7 properties and the one PENDING team are
excluded from the case sum). One
blue artifact is PENDING as above; no count was invented for it.

---

## 3. Headline battle counts

```
RED20  abi       6 checks 0 mismatches 8 modified core files
BLUE20 abi       7 checks 7 passed 0 failed   (raw runners: 7 5 2)
```

Across the ten red campaigns, **seven reported genuine findings** or integrity
changes and **three reported none**. Across the eight case-based fully landed
blue campaigns, **all 122 148 deterministic case executions passed** (plus
`blue_abi`'s 7/7 integrity properties); `blue_io` remains PENDING. The battle
is therefore **not** a clean sweep in either direction:
the red teams found real defects, and the blue teams proved strong but
strictly scoped properties.

---

## 4. OPEN findings and exact reasons

A finding is OPEN when no landed artifact proves it fixed. "Fix in progress"
means an **uncommitted sibling edit exists in the working tree** at aggregation
time but no blue proof has re-verified it — it is explicitly not counted as
closed.

| Finding | OPEN reason (exact) | Related uncommitted edit |
|---|---|---|
| `red_alloc` F-ALIGN-01, F-ARENA-02a/b, F-POOL-01/02/03, F-DS-01/02/03/04 | No blue re-proof of the exact reproducers landed. `blue_alloc/PROOF.md` states its 2400 cases cover alignment, arena chains, pool double-free/refusal, slot generation, scope and VM commit — **not** forged `old_size`, `SIZE_MAX` slab products, wild-address free, or over-alignment on the default path. `out/repro 1..11` has not been re-run against a fix. | `stdlib/arena.hp`, `stdlib/datastack.hp`, `stdlib/mempool.hp` have uncommitted edits — fixes in progress, unverified |
| `red_error` E1–E5 | `blue_error/PROOF.md` proves 2400 cases for the current invariants but does not re-run the five exact hostile variants (`F3/sub2`, `F3/sub4`, `F7/sub8`, `F7/sub4`, `F7/sub10`). | `stdlib/error.hp` has uncommitted edits (43 lines) — fix in progress, unverified |
| `red_format` F1/F2/F3 | `blue_format/PROOF.md` includes `safe_format.h` **first** (BOS type 1), so its 2100 cases exercise only the format-first configuration; it does not re-run the `%h1$n` family or the io-first include order, and uses truthful `dmax`. | `stdlib/safe/src/safe_format.c` and `stdlib/safe/safe_io.h` have uncommitted edits — fix in progress, unverified |
| `red_safecrt` §1 (`memcmp_s_auto` NULL at `n==0`) | `blue_safecrt` uses NULL source with non-zero `dmax`; it never exercises `n==0` NULL operands, so its 2400/2400/0 does not contradict the red finding. | `stdlib/safe/src/safe_memory.c` has an uncommitted edit that moves the NULL check above `n == 0` — fix in progress, unverified |
| `red_safecrt` §2/§3 (`dmax` lie / oversized scan bound) | Both require a caller-supplied bound larger than the real object. `blue_safecrt` declares truthful `dmax` and its own "Honest limits" exclude exactly this case; documented boundary, not closed. | none seen |
| `red_io` F1 (CR at exact fit) and F2 (embedded NUL) | No fix or re-proof landed; `blue_io/PROOF.md` is PENDING and does not address these two exact byte patterns. | none seen |
| `red_multimedia` five groups (NULL-path success, unbounded `audio_decoder::read`, `gui::slider` abort, decoder-after-media UAF, absurd image dims) | No fix landed. `blue_multimedia/PROOF.md` includes "null handles guarded" and "truncated media refused" cases but its NULL-path case list does not include `audio_decoder(NULL)`/`media(NULL)`, and it never requests more frames than its destination holds. | none seen |
| `red_abi` 8 modified core C files | The seven canonical core files used by `check_core_c.sh` / `stdc_source/run.sh` are byte-identical to `cbcea57`, but the 8 `M` files under `libc/src/` are real committed changes. ABI is intact (fingerprint match); the *absolute* "no core C source changed" claim remains false. This is a scope dispute, not an ABI regression. | those 8 are committed, not uncommitted |
| blue_abi official `abi_compat` runner: 66 passed / 3 failed | Toolchain drift: `./build/c+` wraps a **newer external `c-plus-core`** whose lowering emits `__cplus_checked_add_i64`; this repo's `runtime/cplus_runtime.c` / `build/libcplus_runtime.a` predate it and do not define that symbol, and the official T3a link line supplies no runtime. Linking the external core's whole runtime archive instead yields a duplicate `_cplus_realloc` against `libcplus_hardened.a`. `blue_abi` proves 69/0 by adding a test-only shim; the official harness itself is unchanged. | external compiler outside this repo |
| blue_abi official legacy labs: 52 pass / 2 fail | `cp_to_c`: same two undefined runtime helpers on its C-driver link line. `c_to_cp`: the newer external driver's `--strict` mode rejects `-Wno-unused-command-line-argument` and then the `.a` pass-through. Both labs pass in `blue_abi`'s drift-neutralized copies (37/0 and 24/0); the official runners are unchanged. | external compiler outside this repo |
| blue_abi `run_abi_layouts.sh` raw failure | Ambient shell `clang` is Homebrew and selects a non-existent `MacOSX27.sdk`; with `CPLUS_CLANG=/usr/bin/clang` the suite passes 70/70. Environment issue, disclosed by red_abi too. | n/a |

All OPEN items remain evidence gaps; none is closed by this aggregate.

---

## 5. Reproduction index (exact commands)

Red teams:

```sh
sh tests/redblue20/red_alloc/run.sh
sh tests/redblue20/red_containers/run.sh
sh tests/redblue20/red_io/run.sh
sh tests/redblue20/red_netthread/run.sh
sh tests/redblue20/red_safecrt/run.sh
sh tests/redblue20/red_format/run.sh
sh tests/redblue20/red_error/run.sh
sh tests/redblue20/red_apps/run.sh
sh tests/redblue20/red_multimedia/run.sh
sh tests/redblue20/red_abi/run.sh
```

Blue teams:

```sh
sh tests/redblue20/blue_abi/run.sh
sh tests/redblue20/blue_alloc/run.sh
bash tests/redblue20/blue_containers/run.sh
sh tests/redblue20/blue_io/run.sh          # PENDING: prove final BLUE20 line
sh tests/redblue20/blue_netthread/run.sh
bash tests/redblue20/blue_error/run.sh
sh tests/redblue20/blue_format/run.sh
sh tests/redblue20/blue_multimedia/run.sh
sh tests/redblue20/blue_safecrt/run.sh
sh tests/redblue20/blue_apps/run.sh        # PROOF.md landed: BLUE20 apps 106000 106000 0
```

---

## 6. Ownership / honesty statement

This aggregate was produced by BLUE-TEAM 20/20. Only
`tests/redblue20/blue_abi/` and `docs/security/RED_BLUE_20_FINAL.md` were
written by this agent. No source, header, test or config outside that directory
was edited by this agent; the uncommitted edits listed in §4 belong to sibling
agents and are reported, not claimed. No `git add` or `git commit` was run.
Where a sibling artifact is absent or incomplete, it is marked PENDING with the
exact command; no count or finding was inferred from expectation.

---

## POST-BATTLE FIX UPDATE (parent, after all fixes landed)

The counts in §1/§2 are the agents' ORIGINAL campaigns. The parent then applied
ABI-neutral fixes and re-ran the affected Red suites:

| Suite | Original | After fixes | Genuine items closed |
|---|---|---|---|
| red_alloc | 634 crash / 593 corrupt | **61 crash / 593 corrupt** | forged-size realloc guards (arena/datastack), pool slab raw overflow, slot_pool init overflow, foreign-pointer bounds check before header deref |
| red_error | 118 corrupt / 36 unexpected | **50 corrupt / 36 unexpected** | full-range `stdlib_safe_div`, E3 callback-before-copy fix |
| red_format | 440 crash / 56 corrupt | **0 crash / 0 corrupt** | F1 `%n` any-`$` refusal; F2 unified BOS type 1 |
| red_safecrt | 30 crash / 33 corrupt / 1175 unexpected | **30 / 33 / 1164** | memcmp_s_auto NULL-at-zero fix (14 benign cases removed) |

Remaining OPEN (documented, not hidden): red_alloc F-ALIGN-01 (default
allocator ignores extended alignment), F-DS-02/03/04 (caller-misuse forgery
cases), red_safecrt oversized-bound over-read and dmax-lie corruption (caller
supplies a bound larger than the real object — the documented BOS-unknown
boundary), red_multimedia NULL-path/dimension issues, red_io exact-fit/NUL
findings, and red_abi's 8 core files modified by the external Linux/glibc fix
commit `005ece3` (ABI fingerprint still MATCH, 0 mismatches).

Post-fix regressions: ABI fingerprint MATCH (empty diff), libc 109/109,
CRT 86/86. This remains EVIDENCE for the recorded inputs, not a proof of
absolute security; every genuine open finding is listed above.
