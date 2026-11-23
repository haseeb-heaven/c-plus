# Wave-3 real-world C compatibility (measured)

Wave-3 blue-defender result. Every number below comes from an executed run of
`tests/compiler/real_world_c/wave3/run_suite.py`, not from a scaffold or estimate.
Raw evidence: `tests/compiler/real_world_c/wave3/results/summary.{txt,json}`,
per-cell logs in `results/logs/`, emitted C in `results/generated/`.

## Command and toolchain snapshot

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_world_c/wave3/run_suite.py
```

| Item | Measured value |
| --- | --- |
| Date | 2026-11-23 |
| Branch / HEAD | `agent/core-safety` / `deafe46` |
| Binary | `build/c+`, `C+ 0.1.0`, host Clang/LLVM backend |
| Reference | `/usr/bin/clang`, Apple clang 16.0.0 (clang-1600.0.26.6) |
| Host | macOS 27.2.1, Darwin 27.2.0, arm64 |
| Harness exit | **1** (one hard-failure class, detailed below) |

## Headline counts

```
projects=12  repos=6  modes=3  standards=5  cells=180
PASS=161  MODE_REJECTED=14  HARD_FAIL=5
status_breakdown={"FAIL_PRESERVATION":5,"MODE_REJECTED":14,"PASS":161}
exact source preservation PASS=55/60 legacy-c cells
structural source preservation PASS=106/106 built safety-mode cells
projects_clean=11  projects_with_hard_fail=1
```

Each cell is one (project × standard × mode) build + run + clang comparison.
Modes: `--legacy-c`, `--default`, `--strict` (all with `--sanitize=none`).
Standards: `c89`, `c99`, `c11`, `c17`, `c2x`.

## Corpus

12 standalone, deterministic C programs, two from each of six repositories.
Exact URL, pinned commit, LICENSE text at that pin, SHA-256, stdin/args and
expected stdout are in `tests/compiler/real_world_c/wave3/manifest.json`.

| Repository (all MIT at pin) | Pinned commit | Cases |
| --- | --- | --- |
| `portfoliocourses/c-example-code` | `51cfc156d1406a55fa1638569ee6885d9ac5782d` | `pc_bubble`, `pc_avg` |
| `gouravthakur39/beginners-C-program-examples` | `373d27c131e35bacdbf5c500f79fd234c6d4ec9b` | `g_fib`, `g_bs` |
| `abhaysinghr516/Algorithms` | `1c87ef39118dfd593239e3a1eadc31d4b94735d9` | `a_euclid`, `a_binsearch` |
| `aniket-2003-das/C-Language-Programs` | `2b6cdda6e371154f35319730232d28cfee272d44` | `n_03`, `n_07` |
| `awkwardpy/c-language-programs` | `e7a979b7a82a4e3dbc4525bbaa4019be2066784a` | `ap_hello`, `ap_max` |
| `DivyPrakashTiwari/c_Language_Programs` | `158dd2a89e0a69f7b51fe8537b9c168393b81f0e` | `dp_d`, `dp_s` |

TheAlgorithms/C was deliberately excluded: the LICENSE at the pin used by the
older harness (`e5dad3f`) is **GPL-3.0**, not permissive. That older harness was
not edited.

## The one hard failure: legacy-c drops a trailing newline

`g_bs` (`BinarySearch.c`) fails exact source preservation in **all five**
legacy-c cells. Original file: 1166 bytes ending `}\n\n\n`; emitted payload:
1165 bytes ending `}\n\n`. The driver strips exactly one trailing newline.
First difference at byte 1165. Reproduction is pinned: compare
`cache/g_bs.c` with `results/generated/g_bs.c89.legacy-c.c`. All 55 other
legacy-c cells are byte-identical after the single leading `#line` directive.
Builds and stdout/exit still match clang in all five cells — this is a
source-fidelity regression, not a behavioral one.

## Documented mode rejections (14, not counted as behavioral failures)

| Class | Count | Exact diagnostic | Where |
| --- | --- | --- | --- |
| `c89_inline_prelude` | 9 | `error: unknown type name 'inline'` from the injected C99 arithmetic prelude | c89 + `--default` (5: `a_binsearch`, `g_bs`, `g_fib`, `pc_avg`, `pc_bubble`) and c89 + `--strict` (4: same minus `pc_avg`) |
| `safety_gate` | 5 | `error: [CPLUS-FFI] pointer parameter 'array' has unverifiable extent; subscript cannot be proven safe` | `pc_avg`, `--strict`, all five standards |

Each rejection is recorded with full rc and stderr in `summary.json` and
`results/logs/`. They are fail-closed gate behavior, not silent skips: the
harness reports them separately and never turns them into PASS.

## Behavioral comparison

All 106 built safety-mode cells and all 60 legacy-c cells that passed
preservation produced **exact** stdout bytes and exit status matching both the
manifest expectation and `/usr/bin/clang -std=<std> -O2`. No stdout or exit
mismatch occurred in this 12-project corpus. (Earlier probes with other,
GPL-licensed sources are not part of this corpus and are not counted here.)

## Limitations — read before quoting

1. **Not all C23.** The reference `/usr/bin/clang` is Apple clang 16: it
   rejects `-std=c23` and `-std=gnu23` (`error: invalid value 'c23' ...`).
   The current `c+` backend rejects the same two flags. The matrix therefore
   uses `-std=c2x`, the newest C23-draft dialect both accept. No claim about
   final C23 is made.
2. **Not all projects.** The corpus is 12 small, standalone, deterministic
   programs from six MIT repos. It excludes GPL/copyleft trees, multi-file
   projects, programs needing external libraries, and anything with
   clock/random/address/network/path-dependent output. No sanitizer other than
   `--sanitize=none` is exercised; no claim about ASan/UBSan behavior is made.
3. **`--legacy-c` is not memory-safe.** It disables C+ safety checks by design.
   A PASS means "same stdout and exit as clang for this pinned input on this
   host", not "free of UB, overflow, or out-of-bounds access".
4. **Safety-mode PASS is not a safety proof.** It only means the build closed
   and the observed output matched. `--default` warns but proceeds on
   unverified operations; `--strict` fails closed. Neither proves absence of
   bugs, and the 14 rejections above are expected gate outcomes, not
   regressions.
5. **Preservation differs by mode.** Byte-identity modulo `#line` is asserted
   only for legacy-c. In `--default`/`--strict`, `--emit-c` legitimately injects
   a safety prelude and may rewrite operators (e.g. `i++` → `CPLUS_INC`), so
   only one `#line 1 "<source>"` and the correct path are required.
6. **One host, one snapshot.** Results apply to macOS 27.2.1 arm64, Apple
   clang 16, and the `deafe46` build of `c+` only. Re-run the harness after any
   driver/rewriter/RAII change; do not carry these numbers forward.
