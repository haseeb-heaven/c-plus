# PR #33 — independent verification record

Role: I am the **independent verifier** for issue #33 on branch
`agent/modern-stdlib`. I did not author the PR31/PR32/PR33 artifacts and made no
source, header, test, doc or workflow edits to them. I only created this file and
`tests/pr33_integration/verify.sh`. Nothing was added, committed or pushed.

Final verification command:

```sh
sh tests/pr33_integration/verify.sh
```

Final line (exit 0):

```
PR33_VERIFY 21 21 0 0
```

## 1. Reproduction of each sibling runner (authoritative final pass)

Host: macOS arm64 (`Darwin 27.2.0`), Apple clang 16.0.0. Each runner was
re-run independently with a wall-clock bound (90 s per runner via `timeout`),
with no `CPLUS_CLANG` override. Timings are from `date +%s` around the runner.

| Runner | Exit | Wall | Exact final line | Doc claims final line | Match |
| --- | --- | --- | --- | --- | --- |
| `sh tests/pr31_abi/run.sh` | 0 | 29 s | `PR31_LIBC 109 101 0 8` | `PR31_LIBC 109 101 0 8` | yes |
| `sh tests/pr32_portability/run.sh` | 0 | 38 s | `PR32_LIBC 10 7 0 3` | `PR32_LIBC 10 7 0 3` | yes |
| `sh tests/pr33_integration/run.sh` | 0 | 2 s | `PR33_GATE 7 6 0 1` | none (doc documents only `<checks> <pass> <fail> <not_tested>` format) | format yes |

PR33 runner detail: 6 pass, 0 fail, 1 `NOT_TESTED` (the opt-in cross-branch
combined gate). Its penultimate line is
`INDEPENDENT_REVIEW: NOT PERFORMED by this runner` — true for the script itself;
see section 5 for what *I* did.

PR32 runner detail: 7 pass, 0 fail, 3 `NOT_TESTED` (install smoke blocked by
C+ contract-validator diagnostics, ASan and TSan host runtimes abort with exit
134). PR31 runner detail: 101 pass, 0 fail, 8 explicitly `NOT_TESTED` boundaries.

### Reproducibility note (not an open finding)

Earlier re-runs during a busy period produced different tallies (PR32:
`10 7 3 0`, `10 4 6 0`, `10 4 5 1`, `10 1 9 0`, `10 7 0 3`; PR31 once
`109 101 1 7`; PR32 occasionally exit 124 from its own 60 s global bound).
With `CPLUS_CLANG=/usr/bin/clang` the documented PR32 line reproduced exactly;
with the ambient Homebrew clang on `PATH` sanitizer builds failed with
`'stdio.h' file not found`, because the current sanitizer probe deliberately
does not pass `-isysroot`. The final stable state reproduced the documented
lines for all three runners. Consumers should set `CPLUS_CLANG` explicitly to
get the documented outcome; this coupling is disclosed in the PR32 doc.

## 2. Claim audit table

| Artifact | Claimed | Independently observed | Verdict |
| --- | --- | --- | --- |
| `tests/pr31_abi/run.sh` | POSIX sh, bounded (58 s outer), no Python/`\|\| true`, final `PR31_LIBC ...` | Ran in 29 s; final line matches; no Python or `\|\| true` in non-comment lines; 8 explicit `NOT_TESTED` lines | verified |
| `docs/abi/PR31_LIBC_CROSS_MODULE.md` | 101 pass, 0 fail, 8 NOT_TESTED; exit 1 only from reused compiler tools; `cd <repository-root>` | Final line matches; doc now uses `<repository-root>`; fixture counts and `abi_compat` failure explanation match logs | verified |
| `tests/pr32_portability/run.sh` | POSIX sh, bounded 60 s, no Python/`\|\| true`, final `PR32_LIBC ...`, non-zero on fail | Final line matches; last executable line is `exit "$fail"`; no prohibited patterns | verified |
| `docs/abi/PR32_LIBC_PORTABILITY.md` | `PR32_LIBC 10 7 0 3`; 7 PASS / 0 FAIL / 3 NOT_TESTED; ASan/TSan NOT_TESTED; evidence "written under" `out/` | Final line and table now consistent; `out/asan.status` and `out/tsan.status` say NOT_TESTED with exact exit-134 errors; "written under" matches gitignore reality | verified (after fixes, §3) |
| `.github/workflows/libc-portability.yml` | Linux x86-64, macOS arm64, Windows PE **compile-only**; no Windows runtime claim; unsupported targets experimental | Three jobs with `timeout-minutes`; Windows job compiles a trivial PE, prints `RUNTIME NOT TESTED`, never starts Wine/game; no runtime-success wording; Linux arm64 absent and labelled EXPERIMENTAL in the doc | verified as written; workflow itself not executed here |
| `tests/pr33_integration/check_report.sh` | Rejects missing fields, placeholders, absolute paths; accepts valid report | Direct negative controls: missing field → exit 1, missing section → exit 1, absolute path → exit 1, valid → exit 0 | verified |
| `tests/pr33_integration/check_overlap.sh` | Rejects overlapping paths and duplicate claims | Direct negative controls: identical path + identical claim → exit 1 with both warnings; directory-prefix overlap → exit 1; disjoint → exit 0 | verified |
| `tests/pr33_integration/run_combined_gate.sh` | Merge conflict treated as FAIL; worktree/branch cleaned up; no success claim without review | Reproduced with `PR33_GATE_TIMEOUT=30`: exit 1, `FAIL combined-gate: merge conflict ...`, conflicts in `.gitignore`, `compiler/Driver/Driver.cpp`, `demos/run_demos.sh`, `docs/STATUS.md`; no leftover `pr33-combined` worktree; prints `INDEPENDENT_REVIEW: NOT PERFORMED` | verified |
| `tests/pr33_integration/run.sh` | 7 checks, negative controls, combined gate opt-in | `PR33_GATE 7 6 0 1`, exit 0; both validator negative controls and the missing-branch NOT_TESTED control pass | verified |
| `docs/abi/PR33_INTEGRATION_GATES.md` | Gate framework only; explicitly **not** evidence of independent review | Doc makes no completion/safety/review claim; says a human verifier must reproduce suites and review fixtures | verified |

## 3. Genuine findings (all found during this audit, all resolved by the siblings before the final pass)

I did not hide these. Each was observed with a command/output at the time, then
re-checked after the sibling's edit and found fixed.

1. **Hardcoded machine path in PR31 doc.** At 12:35–13:08,
   `docs/abi/PR31_LIBC_CROSS_MODULE.md` §6 said
   `cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`. Finding at 12:45:
   `grep -rn '/Users/haseeb-mir' docs/abi/PR3*.md` matched line 202. Resolved at
   13:08:42; it now says `cd <repository-root>`. Re-checked: no machine-specific
   absolute path remains in any sibling doc or runner.
2. **False "committed evidence" claim in PR32 doc.** At 12:46 the doc said
   "Raw evidence is committed under `tests/pr32_portability/out/`", but
   `git check-ignore -v tests/pr32_portability/out/toolchain.txt` reported
   `.gitignore:145:out/` and `git ls-files tests/pr32_portability/out` was empty.
   Resolved by 13:04:37; the doc now says "written under".
3. **PR32 doc internally contradicted its own tally.** At 13:04 the final line
   said `10 7 0 3` (0 fail) while the results table still had three `FAIL` rows
   (`install_smoke`, `sdk_consumer`, `link_shared_sources`). Resolved at
   13:10:26; the table now has zero `FAIL` rows and matches the final line.
4. **PR32 sanitizer PASS claims not backed by artifacts.** At 13:04 the doc's
   table said `asan`/`tsan` PASS, while
   `tests/pr32_portability/out/{asan,tsan}.status` said NOT_TESTED (exit 134,
   host runtime blocked). Resolved at 13:10:26; those rows now say NOT_TESTED
   and cite `out/san_{asan,tsan}.run.log`.
5. **PR32 runner could exit 0 with failed checks.** At 12:45 the last executable
   line of `tests/pr32_portability/run.sh` was `exit 0` after `finish`, so a
   green process status did not imply green checks. Resolved at 13:08:08; it is
   now `exit "$fail"`.

No open findings remain in the final state; `verify.sh` reports 0 findings.

## 4. Prohibited-pattern and workflow audit

* Non-comment lines of all ten sibling `.sh` files were scanned for `|| true`
  and Python invocations: **none found**. Occurrences in comments/docs are
  descriptions, not operations.
* Machine-specific absolute paths in sibling docs and runners: **none** in the
  final state. (`/Users/...` strings in generated, gitignored dSYM `.yml` files
  under `tests/pr32_portability/out/` are compiler-generated debug metadata,
  not hardcoded script/doc paths.)
* No silently skipped tests: PR31 records 8 explicit `NOT_TESTED`, PR32 3, PR33
  1; each is printed in the runner's own summary.
* The workflow does **not** claim Windows runtime success: the Windows job is
  named "Windows PE compile-only (EXPERIMENTAL; no runtime claim)", prints
  `PR32_WINDOWS RUNTIME NOT TESTED`, and never invokes Wine/Highball. Linux
  arm64 has no job and is marked EXPERIMENTAL/UNSUPPORTED in the doc.

## 5. Did independent review actually occur?

**Yes — by me, in this audit.** I am a separate verifier session, not any of the
three implementing agents. I did not merely read the docs: I re-ran all three
runners with bounded timeouts and recorded exit codes/wall times/final lines;
re-ran the PR33 validators directly against freshly constructed bad reports
(missing field, missing output section, absolute changed path, overlapping
path, duplicate claim, directory-prefix overlap); reproduced the combined-gate
merge-conflict path and confirmed worktree cleanup; inspected the on-disk
status/log artifacts behind the "sanitizer passed", "committed evidence" and
"independent review" claims; and checked the workflow text line by line. The
five findings in §3 came from that work.

The gate scripts' own printed statement `INDEPENDENT_REVIEW: NOT PERFORMED` is
correct **about the scripts**; it does not deny that a human-in-the-loop
reviewer (this session) performed the audit. No sibling doc claims
"independently reviewed" status.

## 6. What I did NOT verify

* **Remote CI**: `.github/workflows/libc-portability.yml` was read and audited,
  but no GitHub Actions run was observed. The Linux x86-64 and macOS CI jobs and
  artifact uploads were not executed or checked.
* **Linux runtime**: no Linux host or runner available here. PR31/PR32 mark it
  explicitly NOT TESTED / CI-ONLY.
* **Windows runtime**: not available; only the compile-only PE probe exists and
  is explicitly EXPERIMENTAL with runtime BLOCKED. I did not run Wine/Highball.
* **Successful cross-branch merge**: the combined gate failed at the merge step
  in every attempt (shared hot-spot conflicts), and I did not resolve those
  conflicts or re-run to a green combined suite. No `supported`/release verdict
  is asserted by this review.
* **Correctness of the underlying C+/libc implementations**: I verified evidence
  integrity and gate behaviour, not the compiler, allocator or sanitizer
  internals. The reused ABI tools' own failures (PR31's `run_abi_layouts`/
  `run_abi_compat` in earlier states) were reported by the runner, not adjudicated
  by me.
* **Anything outside the listed sibling artifacts**: no other repo test suite,
  no live game/runtime workflow, and no network access was used.
