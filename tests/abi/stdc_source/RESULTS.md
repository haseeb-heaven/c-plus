# Standard C source integrity — safety integration is additive only

**Task:** prove the original standard C implementation files are unchanged in
this branch's safety-integration commits; safety is additive only.

**Strict file ownership:** only `tests/abi/stdc_source/run.sh` and this
`RESULTS.md` were created. No source, header, build file, or git index entry was
created or modified. No `git add` / `git commit` was run. All commands below are
read-only.

**Verdict vocabulary:** `UNCHANGED` = byte-identical (same git blob object id and
empty diff); `ADDED` = new path (`A`) in the change set; `MODIFIED` = existing
path with a content change (`M`), `D` = deleted, `R` = renamed.

---

## 1. Baseline resolution

| Item | Value |
| --- | --- |
| Working branch | `agent/modern-stdlib` |
| `git rev-parse HEAD` at analysis time | `ce336ab93e569ce25f223fad6db661f09109f1ea` (`ce336ab`) |
| `git rev-parse HEAD` at final re-check | `f58c33abd1e8cabd74d8c8a39a6870939a6245e6` (`f58c33a`) |
| Baseline | `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` (`cbcea57`) |
| Commits in `cbcea57..ce336ab` | **24** |
| Commits in `cbcea57..f58c33a` | **25** |
| `git merge-base --is-ancestor cbcea57 HEAD` | true (exit 0) |

**Concurrent HEAD movement (disclosed):** while this read-only audit ran,
another agent committed `f58c33a` ("Finalize 1000-scenario adversarial
aggregate and security-log audit"), so `HEAD` moved from `ce336ab` to `f58c33a`.
I re-ran every check at the new tip. `git diff --name-status ce336ab..f58c33a`
shows only `tests/abi/c_to_cp/artifacts/results.log`,
`tests/abi/cp_to_c/artifacts/results.log`, `tests/adversarial_1000/RESULTS.md`,
`tests/adversarial_1000/SECURITY_LOGS.md`, and
`tests/adversarial_1000/run_all.sh` — **nothing under `libc/`, `runtime/`, or
`stdlib/`**. All results in this document are therefore identical at both tips:
18 `A` / 0 `M` in `libc/src/`, 19 `A` / 0 `M` in `libc/install-include/`, and all
7 core files byte-identical to the baseline. `run.sh` follows whatever `HEAD`
resolves to at invocation time (or `HEAD_REV` if supplied), so it remains
correct at either tip.

Why `cbcea57` is the correct baseline for "before the safety integration":

1. `git log --format='%h %p %s' -1 960c997` ->
   `960c997 cbcea57 Add Task 4 ABI lab, Task 3 system-safety suites, and Task 5 allocator work`.
   `cbcea57` is the **direct parent of the first commit** of the 24-commit
   integration series that ends at `ce336ab`. That series contains the ABI lab,
   security hardening, allocator work, safeclib vendoring, and runtime-safety
   wrappers (`960c997`, `4ec4ca2`, `de79021`, `2a2e13f`, among others).
2. `cbcea57` is a merge commit ("Merge remote-tracking branch
   'origin/agent/modern-stdlib' into agent/modern-stdlib"), i.e. a natural branch
   integration boundary.
3. The independent parallel header audit in `docs/abi/source_audit.md` also
   resolved `cbcea57` as its baseline. Using the same boundary avoids two
   different definitions of "the baseline" in this audit run.

If a different baseline is desired, rerun `run.sh` with `BASELINE=<rev>`; the
script re-derives every result below.

The 24 commits in the audited range at analysis time (`cbcea57..ce336ab`),
plus the 25th (`f58c33a`) that appeared at final re-check:

```
f58c33a Finalize 1000-scenario adversarial aggregate and security-log audit  (added later; tests/ only)
ce336ab Finalize multimedia 1000-scenario adversarial suite
5127e3c Finalize Rust examples and missing-API closure audit
e55c425 Untrack generated json_log Rust-example binaries
2a2e13f Close 27 missing ISO C APIs and add runtime-safety wrappers
2a72486 Untrack generated Rust-example binaries
4a38e65 Add safe-Rust examples and 1000-scenario adversarial suites
42cd699 Finalize 100% method-test aggregate runner and audit
8a00f33 Add 100% per-method test suites for all standard library headers
aca0abf Add per-method C+/Rust aggregate safety-outcome matrix
36e78b4 Add per-method C+/Rust memory-safety outcome comparisons
de79021 Vendor already-made safeclib implementations into the checked runtime
a11de7f Add deterministic fuzz harnesses for the checked ordinary-C layer
4ec4ca2 Add ABI-preserving ordinary-C checked layer and close release blockers
511afc9 Reconcile final benchmark validator and upstream reuse evidence
2294cb1 Finalize C+/Rust benchmark proof and untrack generated binaries
ce018ca Integrate proven upstream implementations into C+ stdlib
1112745 Untrack regenerable resource-exhaustion runner artifacts
a85f7f5 Add resource exhaustion evidence notes
4011336 Add resource exhaustion suite runner and restore probe
ec60ef5 Finalize allocation overflow evidence and clean generated security artifacts
c5d2582 Complete Task 6 security hardening and fix reproduced defects
ec8f2e2 Finalize system, allocator and security deliverables
664af8b Add Task 5 allocator architecture and Task 6 security audit deliverables
960c997 Add Task 4 ABI lab, Task 3 system-safety suites, and Task 5 allocator work
```

---

## 2. Exact git commands used

Run from the repository root (all read-only):

```sh
# identity and range
git rev-parse HEAD
git rev-parse cbcea57
git log --oneline cbcea57..HEAD
git rev-list --count cbcea57..HEAD
git log --format='%h %p %s' -1 960c997
git merge-base --is-ancestor cbcea57 HEAD

# core source identity: blob hash equality and literal diff
for f in libc/src/string/memory.c libc/src/string/string.c \
         libc/src/string/extra.c libc/src/string/bounded.c \
         libc/src/stdlib/alloc.c libc/src/stdio/stdio.c runtime/cplus_runtime.c; do
  git rev-parse cbcea57:"$f"
  git rev-parse HEAD:"$f"
  git diff cbcea57..HEAD -- "$f"
  git diff --name-only -- "$f"     # uncommitted working-tree check
done

# whole-tree change categorization
git diff --name-status cbcea57..HEAD -- libc/src/
git diff --name-status cbcea57..HEAD -- libc/install-include/
git diff --stat cbcea57..HEAD -- libc/src/
git diff --stat cbcea57..HEAD -- libc/install-include/

# full per-file history (disclosure of genuine pre-baseline modifications)
for f in <the seven core files>; do git log --format='%h %ad %s' --date=short -- "$f"; done

# per-commit attribution of new files
git log --reverse --format='COMMIT %h %s' --name-only --diff-filter=A \
    cbcea57..HEAD -- libc/src/ libc/install-include/
```

Reproduce everything with: `bash tests/abi/stdc_source/run.sh` (exit 0 = pass,
exit 1 = a non-additive change or modified core file was detected). It defaults
to `BASELINE=cbcea57`, `HEAD_REV=HEAD`.

---

## 3. Core files — byte-identical to baseline (no MODIFIED)

`git diff cbcea57..HEAD -- <file>` is **empty** for all seven, and the git blob
object id at `cbcea57` equals the object id at `HEAD` for all seven. Equal blob
ids are a byte-for-byte identity proof, not just a line-based diff result.

| Core file | Status | Blob id (identical at baseline and HEAD) |
| --- | --- | --- |
| `libc/src/string/memory.c` | UNCHANGED | `46de2624d29cfc15848d8d4d624b145192365148` |
| `libc/src/string/string.c` | UNCHANGED | `104accdec51d92dc88066b7e5271f7c3d451a574` |
| `libc/src/string/extra.c` | UNCHANGED | `4f98cca100bc503ebb8f93ba9464dbe61c75cf59` |
| `libc/src/string/bounded.c` | UNCHANGED | `231e7a9928efc992a0ca0c2666d2a468de68f281` |
| `libc/src/stdlib/alloc.c` | UNCHANGED | `cd2c44687e565ab6fec18520a4b985a4660f69fd` |
| `libc/src/stdio/stdio.c` | UNCHANGED | `830b32f212db11bcdab21f24509005157d84188f` |
| `runtime/cplus_runtime.c` | UNCHANGED | `c6a921d5c306a89594b667057c1d4a62a5d3cfab` |

All seven exist at both `cbcea57` and `HEAD`. Uncommitted working-tree check:
`git diff --name-only -- <core files>` is empty — none of them has a local,
uncommitted modification either.

**MODIFIED core files in `cbcea57..HEAD`: none.**

---

## 4. `libc/src/` — ADDED vs MODIFIED

`git diff --name-status cbcea57..HEAD -- libc/src/`: **18 × `A`, 0 × `M`,
0 × `D`, 0 × `R`**.

```
A  libc/src/math/c23_arith.c
A  libc/src/math/c23_hypot16.c
A  libc/src/math/c23_nextup.c
A  libc/src/runtime/violation_log.c
A  libc/src/safecrt_vendor/case_group.c
A  libc/src/safecrt_vendor/field_group.c
A  libc/src/safecrt_vendor/firstlast_group.c
A  libc/src/safecrt_vendor/mem_group.c
A  libc/src/safecrt_vendor/set_group.c
A  libc/src/safecrt_vendor/spn_group.c
A  libc/src/safecrt_vendor/stp_group.c
A  libc/src/safecrt_vendor/strchr_group.c
A  libc/src/safecrt_vendor/strl_group.c
A  libc/src/safecrt_vendor/strstr_group.c
A  libc/src/stdlib/alloc_hardened.c
A  libc/src/stdlib/c23_time_unicode.c
A  libc/src/string/upstream_safecrt.c
A  libc/src/thread/sync_checked.c
```

`git diff --stat cbcea57..HEAD -- libc/src/` reports
`18 files changed, 2375 insertions(+), 0 deletions(-)` — zero deletions is
consistent with a purely additive change set.

Per-commit attribution (`git log --diff-filter=A`):

| Commit | Rationale (commit subject) | New files |
| --- | --- | --- |
| `ce018ca` | "Integrate proven upstream implementations into C+ stdlib" | `libc/src/string/upstream_safecrt.c` (+ `cplus_safecrt_upstream.h`) |
| `de79021` | "Vendor already-made safeclib implementations into the checked runtime" | 10 × `libc/src/safecrt_vendor/*_group.c` |
| `2a2e13f` | "Close 27 missing ISO C APIs and add runtime-safety wrappers" | `libc/src/math/c23_arith.c`, `c23_hypot16.c`, `c23_nextup.c`, `libc/src/runtime/violation_log.c`, `libc/src/stdlib/alloc_hardened.c`, `c23_time_unicode.c`, `libc/src/thread/sync_checked.c` |

Note: `4ec4ca2` ("Add ABI-preserving ordinary-C checked layer and close release
blockers") is in the range but added no paths under `libc/src/` or
`libc/install-include/`; its changes are outside this task's file scope.

---

## 5. Public headers `libc/install-include/cplus*.h` — additions only

`git diff --name-status cbcea57..HEAD -- libc/install-include/`:
**19 × `A`, 0 × `M`, 0 × `D`, 0 × `R`**.

```
A  libc/install-include/cplus_alloc_hardened.h
A  libc/install-include/cplus_c23_arith.h
A  libc/install-include/cplus_c23_fenv_compat.h
A  libc/install-include/cplus_c23_hypot16.h
A  libc/install-include/cplus_c23_nextup.h
A  libc/install-include/cplus_c23_time_unicode.h
A  libc/install-include/cplus_safecrt_upstream.h
A  libc/install-include/cplus_sync_checked.h
A  libc/install-include/cplus_vendor_case.h
A  libc/install-include/cplus_vendor_field.h
A  libc/install-include/cplus_vendor_firstlast.h
A  libc/install-include/cplus_vendor_mem.h
A  libc/install-include/cplus_vendor_set.h
A  libc/install-include/cplus_vendor_spn.h
A  libc/install-include/cplus_vendor_stp.h
A  libc/install-include/cplus_vendor_strchr.h
A  libc/install-include/cplus_vendor_strl.h
A  libc/install-include/cplus_vendor_strstr.h
A  libc/install-include/cplus_violation_log.h
```

`git diff --stat cbcea57..HEAD -- libc/install-include/` reports
`19 files changed, 1211 insertions(+), 0 deletions(-)`.

**No existing declaration signature changed.** This follows directly from the
file-status set: every in-scope header is a brand-new file (`A`) — the diff
contains zero `M`/`D`/`R` entries, so there is no pre-existing header text in
which a declaration could have been added, removed, or re-typed. Pre-existing
public headers (e.g. `cplus_libc.h`, `cplus_stdatomic.h`) are untouched in this
range; the parallel audit `docs/abi/source_audit.md` separately concluded that
the earlier ancestor commit series that produced those headers was also
additive/inline-only with no `ABI_CHANGE`. I did not independently re-audit
those earlier header commits here.

The new headers were introduced by the same three commits in §4: `ce018ca`
(1 header), `de79021` (10 vendor headers), `2a2e13f` (8 C23/safety headers).

---

## 6. Honest disclosure — the core files WERE modified before the baseline

This is recorded so nothing is hidden. The seven core files are **not**
byte-identical to the *very first* versions ever committed on this repository;
they were changed by earlier hardening commits that all predate `cbcea57`.
Those changes are outside the audited `cbcea57..HEAD` range and are the reason
the correct claim is "unchanged since the safety-integration baseline", not
"never modified since the project began".

| Core file | All commits that touched it (oldest last) |
| --- | --- |
| `libc/src/string/memory.c` | `df3bc7b` (2026-11-22, harden SafeCRT/libc, C17 SDK), `42d604c` (2026-11-21, initial hardened libc slice) |
| `libc/src/string/string.c` | `5fe387d` (2026-11-23, fix HIGH/MEDIUM safety findings), `df3bc7b`, `42d604c` |
| `libc/src/string/extra.c` | `5fe387d`, `22129c9` (2026-11-21, add missing log calls), `bdc192e` (2026-11-21, expand hardened slice) |
| `libc/src/string/bounded.c` | `1df837f` (2026-11-23, ABI-preserving runtime safety / P0 Safe CRT hardening), `5fe387d`, `bc0c99d` (2026-11-22, runtime scenarios), `108f418` (2026-11-22, make plain libc layer safe for ordinary C) |
| `libc/src/stdlib/alloc.c` | `9a061ae` (2026-11-23, aligned allocation tracking), `bc0c99d`, `108f418`, `ccad805` (2026-11-22, fix defects from adversarial testing), `df3bc7b`, `42d604c` |
| `libc/src/stdio/stdio.c` | `5fe387d`, `ccad805`, `df3bc7b`, `9f823a4` (2026-11-22, stdio varargs wrappers), `22129c9`, `42d604c` |
| `runtime/cplus_runtime.c` | `1a3cd29` (logging instrumentation), `dc1f053`, `00529ff`, `dc2230c` (all 2026-11-21) |

Every hash above appears only in commits that are **ancestors of `cbcea57`**.
The exact per-commit rationale strings are quoted from `git log --format='%h
%ad %s' --date=short -- <file>` and reproduced verbatim by `run.sh`. If a
core file had been modified inside `cbcea57..HEAD`, `run.sh` would have printed
`MODIFIED` with the blob hashes and exited 1; it did not.

---

## 7. Conclusion

**Within the audited safety-integration series (`cbcea57..ce336ab` = 24 commits;
`cbcea57..f58c33a` = 25 commits after a concurrent, tests-only commit;
`cbcea57` = `ce336ab`'s first-integration-commit parent), the original standard
C implementation sources are unchanged and the safety work is additive only.
Every result below holds identically at both `ce336ab` and the current
`f58c33a` tip:**

- all **7** named core files (`memory.c`, `string.c`, `extra.c`, `bounded.c`,
  `alloc.c`, `stdio.c`, `runtime/cplus_runtime.c`) are **UNCHANGED** — identical
  git blob ids and empty diffs at both ends of the range, with no uncommitted
  local modifications;
- `libc/src/` has **18 ADDED, 0 MODIFIED, 0 DELETED, 0 RENAMED** files
  (2375 insertions, 0 deletions);
- `libc/install-include/cplus*.h` has **19 ADDED, 0 MODIFIED, 0 DELETED,
  0 RENAMED** files (1211 insertions, 0 deletions), so no existing declaration
  signature was changed;
- `bash tests/abi/stdc_source/run.sh` exits **0** with the `PASS` line.

**Scope limit (no overclaim):** this proves source-level integrity for the
audited range only. It does **not** claim these core files equal their
first-ever committed versions — §6 lists the genuine pre-baseline hardening
commits that changed them. It does not claim anything about compiled binaries,
runtime behavior, or the live game/runtime; no build, ABI-suite, or injection
result is asserted here. The header "additive only" verdict is based on git
file-status (all `A`, zero `M`), not on recompiling consumers. No new file was
vetted here for its own correctness or safety; only its additive relationship
to the pre-existing sources was checked.
