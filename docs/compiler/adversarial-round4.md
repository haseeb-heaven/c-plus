# Adversarial campaign — round 4 (evidence report)

**Method.** Ten red agents probed `build/c+` (mtime 08:46) only; artifacts are verbatim under
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/red4_*` ($R below).
Defenders edited source 09:52–10:0x; **the parent has not rebuilt** (`build/c+` still 08:46), so every result below is
from the stale binary. In the commands `$C=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/c+`.
Statuses: **FIXED_IN_SOURCE** = targeted diff present, unverified by a rebuilt binary; **FIXED_AND_VERIFIED** = post-fix result artifact; **OPEN** = no landing fix; **ABI_BOUND** = needs ABI/layout change. **No round-4 finding is FIXED_AND_VERIFIED yet.** The only post-source artifact,
`tests/compiler/red_blue_round/summary.txt` (09:59), ran against the stale binary: 51 PASS, 20
already-rejected, 22 KNOWN_OPEN — evidence of what remains open, not of the new fixes.

## Top findings (exact reproducers)

### Bounds
1. `$C check --strict $R/red4_bounds/c08b_canary_write.c` → `check: ... ok`, rc=0; `./c08bexe` → `canary=777` (silent OOB write). **FIXED_IN_SOURCE** — `CPlusRewriter.cpp` alias-extent guards; `StaticBoundsCheck.cpp` cast/zero-offset alias facts (still KNOWN_OPEN on stale binary).
2. `$C check --strict $R/red4_bounds/c46_realloc_write.c` → ok; `./c46_realloc_write.exe` → `q[3]=777`. **FIXED_IN_SOURCE** — realloc-shrink extent (`StaticBoundsCheck.cpp:4058`). `c47_vla_alias_write.exe` → `p[9]=777 canary?` likewise.

### Provenance
3. `$C check --strict $R/red4_prov/run_tag2.c` → ok; `./run_tag2` → `same=1 r8=222` (tagged-integer laundering; control `c_tracked.c` is rejected). **FIXED_IN_SOURCE** — `canonicalPointer()` now folds additive/bitwise tags into the deref/release paths.
4. `$C check --strict $R/red4_prov/r_global.c` → ok; `./r_global` → `g: same=1 r8=222`. **OPEN** — callee-side global/field store and helper-return alias propagation absent.

### Ownership
5. `$C check --strict --sanitize=none $R/red4_owner/f2_outparam_relay.c` → ok; ASan log `f2_outparam_relay.run.log`: `heap-use-after-free ... SUMMARY: f2_outparam_relay.c:7 in main`. **OPEN** (void out-param summary not landed; regression KNOWN_OPEN).
6. `$C check --strict --sanitize=none $R/red4_owner/h14_cplus_realloc.c` → ok; `h14_cplus_realloc.run.log`: `heap-use-after-free ... SUMMARY: h14_cplus_realloc.c:6 in main`. **FIXED_IN_SOURCE** — `isReallocName()` now includes `cplus_realloc`.

### RAII / exceptions
7. `$C build --strict --sanitize=none $R/red4_raii/01_throw_try_drop.cp -o x && ./x` → `drops=0` (want 1). **FIXED_IN_SOURCE** — `throwSkipsOwner()`/`throwHelpers_` fail closed.
8. Same with `15_throw_no_try_after_stale.cp` → `run rc=-10` (SIGBUS, stale frame). **FIXED_IN_SOURCE** — `CPlusRewriter` exception scope guard + `cplus_runtime.c` generation/live token; stale jump now fatal deterministically.
9. `s_rethrow.cp` (`$R/red4_config/`): build/run → `outer 1` (want 9). **FIXED_IN_SOURCE** — `cplus_rethrow_exc` re-raise.

### Arithmetic
10. `$C check --strict $R/red4_arith/P01_llmin_div.cp` → ok; build rc=0; `./X` → `-9223372036854775808`, rc=0. **FIXED_IN_SOURCE** — `wideConstOverflow()` exact APSInt `MIN/-1`.
11. `$C check --strict $R/red4_arith/H02_header_add.cp` → ok; build/run → `hadd=-2147483648` (header arithmetic unguarded). **OPEN** (regression KNOWN_OPEN).
12. `N05_compound_mul.cp`: `check=ok` but binary rc=110 (`signed integer overflow: multiplication`). **FIXED_IN_SOURCE** — compound opcodes normalized (check/build parity).

### Null / init
13. `$C check --strict $R/red4_null/b1_id_chain.c` → `check: ... ok`; run rc=102 (`null pointer dereference`). **OPEN** (null-return summary misses wrapper chains; no `OwnershipAnalysis.cpp` diff).
14. `$C check --strict $R/red4_null/e6_loop_noinit.c` → ok; unconditional read of `x`. **OPEN** (dead-loop must-analysis); array/field variant `e2_array.c` is **FIXED_IN_SOURCE** via BLUE6's aggregate definite-init.

### Interprocedural / contracts
15. `$C check --strict --sanitize=none $R/red4_inter/run_s04_lie_hdr_df/main.c` → `check: ok` + `[CPLUS-MULTITU]` warning; `./app` → `C+ runtime error: double free ...`, rc=104. **OPEN** (lying v2 header contract trusted); sibling-name exemption itself **FIXED_IN_SOURCE** in `Driver.cpp`.
16. `run_s03_x_tu_uaf/main.c body.c`: check ok, `run rc=7` (stale read). **OPEN** — per-TU effect summaries; ABI_BOUND only for the already-documented reused-address free case.

### FFI
17. `$C check --strict $R/red4_ffi/u05_percentn_write.c` → ok; `out.u05_percentn_write` → `canary=0` (arbitrary write via struct fnptr `%n`). **FIXED_IN_SOURCE** — indirect target resolution via `calleeNames()`.
18. `$C check --strict $R/red4_ffi/u01_canary_snprintf_fnp.c` → ok; binary rc=134 (canary clobber). Direct `v01` is rejected. **FIXED_IN_SOURCE** — `charDestOf()` bounded-write recovery. `s26_strcpy_unterm.c` → ok, rc=133: **OPEN**.

### Threads
19. `$C check --strict $R/red4_threads/s01_mutex_bypass.c` → ok; `./demo_mutex_bypass` → `shared=513554 expected=1000000 LOST UPDATES (RACE)`. **OPEN** — decoy lock still satisfies all detectors.
20. `$C check --strict $R/red4_threads/p03_tls.c` → error `unsynchronized write to shared global 'tls'`. **FIXED_IN_SOURCE** — TLS excluded from shared-state analysis.

### Config / gates
21. `$C check --strict -Wl,--wrap=__cplus_bounds_check $R/red4_config/g.cp` → `check: g.cp ok`, rc=0. **FIXED_IN_SOURCE** — comma-bearing `-W` rejected.
22. `$C check --strict -o @rsp.txt $R/red4_config/g.cp` → `check: g.cp ok`, rc=0. **FIXED_IN_SOURCE** — `@` response tokens rejected, including `-o` values; `CPLUS_ROOT`/`CPLUS_RUNTIME_LIB` ignore also landed. `-include evil.h`, `-DCPLUS_SAFE=0`, `-fsanitize-recover=all`, `-Wno-error=...` already reject in the stale binary.

## Defenders and owned files

| # | Owned files | Change |
|---|---|---|
| BLUE1 | `compiler/Sema/AstSafetyCheck.cpp` | pointer canonicalization/provenance, `cplus_realloc`, `charDestOf`, compound/wide overflow, macro fail-closed, TLS |
| BLUE2 | `compiler/Sema/StaticBoundsCheck.cpp` | call-site bounds, VLA/multidim folds, struct/row aliases, realloc shrink, FP hardening |
| BLUE3 | `compiler/Sema/RaiiLowering.cpp` | throw/throw-helper cleanup-skip + defer jump diagnostics |
| BLUE4 | `compiler/Sema/CPlusRewriter.cpp`, `SemaCPlus.h` | alias-extent guards, literal-safe `try`/`throw`, rethrow, exception scope guard |
| BLUE5 | `compiler/Driver/Driver.cpp` | response-file/comma-`-W`/reserved `-D` gates, env-root hardening, contract path, sibling exemption |
| BLUE6 | `compiler/Analysis/OwnershipAnalysis.{cpp,h}`, `libc_contracts.json` | aggregate definite-init (array/field elements), contract provenance/truth; `libc_contracts.json` unchanged |
| BLUE7 | `runtime/cplus_runtime.c`, `runtime/include/cplus/cplus.h` | frame generation/live token, deterministic stale-frame fatal |
| BLUE8 | `tests/compiler/red_blue_round/*` | 51 pinned cases/controls; 20 rejected, 22 KNOWN_OPEN (stale binary) |
| BLUE9 | `docs/compiler/adversarial-round4.md` | this report |
| BLUE10 | `tests/compiler/round4_integration/{run.py,run.sh,README.md}` | post-rebuild integration runner |

## Merge gate

**No merge** until the parent rebuilds and Linux + macOS CI are green, SonarCloud is passing, and
`tests/compiler/round4_integration/run.py` passes on `develop`. The 22 KNOWN_OPEN cases (null
wrappers, void out-params, header arithmetic, cross-TU UAF, mutex-bypass races, etc.) are tracked
as open, not claimed fixed; ABI_BOUND items need a separate ABI change, not a source patch.
