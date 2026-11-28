# C+ Only Build Audit

Scope: every active shell script (`*.sh`/`*.bash`) and GitHub Actions workflow audited for
literal direct invocations of `clang`, `gcc`, `tcc` (or the `cc` alias, including
`/usr/bin/`-qualified and cross-compiler variants) used to compile `.c`, `.cpp`, `.cp` or
`.hp` sources. Excluded by design: `docs/**`, `proof/**`, `third_party/**`, historical
evidence generators (`tests/c_standards/**`, `tests/known_gaps/*/results/**`,
`external_projects/*/verify*.sh`), comment/echo strings, and `$CC`/`$CXX` alias
invocations (counted separately below).

Definitions used below:

- **Load-bearing** = removing the call breaks the active build/test step unless a
  like-for-like `c+` build replaces it; **reference** = a plain-C baseline or probe whose
  removal does not break the C+ path; **unavoidable** = CMake bootstrap, cross toolchain,
  or a tool whose subject *is* the raw compiler.
- `C` = the C+ driver (`$CPLUS_BIN` / `build/c+`). Replacement shorthand (compile):
  `"$C" build <src> --legacy-c --sanitize=none -std=<std> <includes> -c -o <obj>`;
  (link) drop `-c` and pass archives after `--`. Sanitizer flags map to `--sanitize=...`.

## Findings — CI workflows

| File:line | Load-bearing | Concrete replacement |
| --- | --- | --- |
| .github/workflows/compiler.yml:94 | reference | Duplicate of the `c+ --legacy-c` smoke at :92; delete, or `"$SDK_PREFIX/bin/c+" build --legacy-c --sanitize=none -std=gnu89 "$SDK_PREFIX/smoke_plain.c" -o "$SDK_PREFIX/smoke_plain_clang"` |
| .github/workflows/compiler.yml:126 | load-bearing | `"$SDK_PREFIX/bin/c+" build --legacy-c --sanitize=none -std=gnu89 "$WORK/add.c" -c -o "$WORK/add.o"`; keep `ar rcs` |
| .github/workflows/compiler.yml:149 | load-bearing | `"$SDK_PREFIX/bin/c+" build --legacy-c --sanitize=none -std=gnu89 "$WORK/add.c" -c -o "$WORK/add.o"`, then link the .o with `-fPIC "$SHARED_FLAG"` flags |
| .github/workflows/compiler.yml:234 | reference | As :94 (second OS matrix leg) |
| .github/workflows/compiler.yml:265 | load-bearing | As :126 (second OS matrix leg) |
| .github/workflows/compiler.yml:288 | load-bearing | As :149 (second OS matrix leg) |
| .github/workflows/compiler.yml:376 | reference | As :94 (third OS matrix leg) |
| .github/workflows/compiler.yml:407 | load-bearing | As :126 (third OS matrix leg) |
| .github/workflows/compiler.yml:430 | load-bearing | As :149 (third OS matrix leg) |
| .github/workflows/libc-crt.yml:95 | load-bearing | `"$C" build tests/crt/fuzz/fuzz_all_safe_apis.c --legacy-c --sanitize=none -std=c99 -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include build/safe/libcplus_safecrt.a -o /tmp/fuzz_all` |
| .github/workflows/libc-crt.yml:101 | load-bearing | `"$C" build tests/safety/test_scenarios_c.c stdlib/safe/src/safe_runtime.c --legacy-c --sanitize=undefined,bounds,null,alignment -std=c99 -pthread -Istdlib/safe -Ilibc <third_party includes> build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -o /tmp/test_scenarios_c` |
| .github/workflows/libc-crt.yml:103 | load-bearing | Same shape with `--sanitize=undefined` for `test_scenarios_threads.c` |
| .github/workflows/libc-crt.yml:108 | load-bearing | `"$C" build tests/crt/unit/test_memcpy_s.c --legacy-c --sanitize=address,undefined -std=c99 -pthread -Istdlib/safe -Ithird_party/... build/safe/libcplus_safecrt.a -o /tmp/test_memcpy_asan` |
| .github/workflows/libc-crt.yml:110 | load-bearing | Same shape for `test_alloc_metadata_s.c` → `/tmp/test_meta_asan` |
| .github/workflows/libc-crt.yml:112 | load-bearing | Same shape for `test_auto_bounds_s.c` → `/tmp/test_auto_bounds_asan` |
| .github/workflows/libc-crt.yml:114 | load-bearing | Same shape for `test_bugs_catalogue_204.c` → `/tmp/test_catalogue_asan` |
| .github/workflows/libc-crt.yml:116 | load-bearing | Same shape for `fuzz_all_safe_apis.c` → `/tmp/fuzz_all_asan` |
| .github/workflows/libc-crt.yml:118 | load-bearing | Same shape with `--sanitize=thread` for `test_scenarios_threads.c` → `/tmp/test_threads_tsan` (TSan pass-through must be verified) |

## Findings — apps / examples

| File:line | Load-bearing | Concrete replacement |
| --- | --- | --- |
| apps/legacy_examples/expanded/safety/run.sh:76 | load-bearing | `"$C" build "$SAFE/sync_demo.c" --legacy-c --sanitize=none -std=c99 -D_POSIX_C_SOURCE=200809L -I. -Ilibc/install-include build/libc/libcplus_hardened.a -pthread -o "$SAFE/sync_demo"` |
| apps/legacy_examples/secure_apps/auditlog/run.sh:25 | load-bearing | `"$C" build "$DIR/auditlog.c" --legacy-c --sanitize=none -std=c99 -pthread -Ilibc/install-include build/libc/libcplus_hardened.a -o "$BIN"` |
| apps/legacy_examples/screenshots/images/render.sh:68 | reference | Command appears only inside a screenshot `SUBTITLE=` caption; no build. Leave as evidence text; do not convert |
| examples/modern_c_ports/olive_demo/run.sh:26 | load-bearing (fallback) | Make `c+` mandatory: `"$C" build "$D/triangle_demo.c" --legacy-c --sanitize=none -std=c11 -I"$D" -o "$BIN"`; drop the `CC=${CC:-/usr/bin/clang}` branch |
| examples/modern_c_ports/olive_demo/run_sdl.sh:29 | load-bearing (fallback) | Same: require the `c+` branch, remove the `/usr/bin/clang` fallback |
| examples/modern_c_ports/olive_demo/run_examples.sh:31 | load-bearing | Replace `/usr/bin/clang -std=c11 -O2 ... "${n}_demo.c"` with `"$C" build "$D/examples/${n}_demo.c" --legacy-c --sanitize=none -std=c11 -I"$D" -o "$BIN"` |

## Findings — benchmarks (raw-C comparison baselines)

| File:line | Load-bearing | Concrete replacement |
| --- | --- | --- |
| benchmarks/safety_compare/run_bench.sh:17 | reference | `"$C" build "benchmarks/safety_compare/$w.c" --legacy-c --sanitize=none -O2 -o "$OUT/c_$w"`; note this changes the "plain C" baseline semantics — keep only if the label is updated |
| benchmarks/example_rust_compare/crt_memory/compare.sh:39 | reference | `"$C" build "examples/crt/$n.c" --legacy-c --sanitize=none -std=c99 -Istdlib/safe -Ilibc/install-include <archives> -o "$cbin"` |
| benchmarks/rust_examples/io/compare.sh:83 | reference | `cc -std=c99 ... harness.c` → `"$C" build "$HERE/harness.c" --legacy-c --sanitize=none -std=c99 -I"$REPO/stdlib/safe" "$SAFE" "$HARD" -lpthread -o "$OUT/harness"` |

## Findings — tests

| File:line | Load-bearing | Concrete replacement |
| --- | --- | --- |
| tests/core/perf_compare.sh:16 | reference | `"$C" build "$OUT/loop.c" --legacy-c --sanitize=none -O2 -o "$OUT/clang_o2"` (rename the column) |
| tests/core/perf.sh:17 | reference | `-fsyntax-only` probe → `"$C" check "$SRC" --legacy-c` |
| tests/core/perf.sh:27 | reference | `"$C" build "$SRC" --legacy-c --sanitize=none -O2 -o "$OUT/clang_o2"` |
| tests/core/libc_bench.sh:8 | reference | `"$C" build "$SRC" --legacy-c --sanitize=none -O2 -o "$OUT/clang_c"` |
| tests/core/libc_bench.sh:9 | reference | `"$C" build "$SRC" --legacy-c --sanitize=undefined,bounds,null,alignment -O2 -o "$OUT/clang_san"` |
| tests/core/coverage.sh:8 | load-bearing | `"$C" build "$ROOT/runtime/cplus_runtime.c" "$ROOT/tests/core/coverage_driver.c" --legacy-c --coverage -std=gnu17 -O0 -g -pthread -I"$ROOT/runtime/include" -o runtime_coverage` |
| tests/adversarial_1000/alloc/run.sh:37 | load-bearing | `CC_CMD` → `"$C" build "$HERE/adv1000_alloc.c" --legacy-c --sanitize=none -std=c99 -O1 -I"$ROOT/stdlib/safe" "$SAFE" "$LIBC" -lm -pthread -o "$BIN"` |
| tests/adversarial_1000/alloc/run.sh:40 | load-bearing | Same replacement at the direct invocation site (the `CC_CMD` copy at :37 is then redundant) |
| tests/redteam/json_net/redteam_run.sh:23 | load-bearing | `"$C" build "$HERE/redteam_vendored.c" --legacy-c --sanitize=none -std=c99 -O1 -g -Ilibc/install-include build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a -lm -pthread -o "$R/redteam_vendored"` |
| tests/cross_platform_apps/run_macos.sh:17 | load-bearing | `"$C" build "$f" --legacy-c --sanitize=none -O2 -Wall -Wextra -pedantic -o "$OUT/app_$i"` |
| tests/redblue20/blue_abi/run.sh:37 | reference | `"$C" build "$HERE/abi_probe.c" --legacy-c --sanitize=none -std=gnu11 -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe <archives> -o "$OUT/abi_probe"` |
| tests/redblue20/red_abi/run.sh:132 | load-bearing | `"$C" build "$P/probe_lib.c" --legacy-c --sanitize=none -std=c11 -c -o "$P/probe_lib.o"`; keep `ar rcs` |
| tests/redblue20/red_abi/run.sh:134 | load-bearing | `"$C" build "$P/probe_app.c" "$P/libprobe.a" --legacy-c --sanitize=none -std=c11 -I"$P" -o "$P/app_plain"` |
| tests/redblue20/red_abi/run.sh:136 | load-bearing | Same with `$INC -include stdlib/safe/safe_c.h <archives>` → `$P/app_safe` |
| tests/redblue20/blue_apps/run.sh:39 | load-bearing | `"$C" build "tests/cross_platform_apps/src/app_$i.c" --legacy-c --sanitize=none -O2 -Wall -Wextra -pedantic -o "$CPA/app_$i"` |
| tests/redblue20/red_apps/run.sh:35 | load-bearing | Same as blue_apps:39 (red leg) |
| tests/redblue20/blue_io/run.sh:23 | load-bearing | `"$C" build "$ROOT/third_party/yyjson/yyjson.c" --legacy-c --sanitize=none -std=c11 $YYFLAGS -I"$ROOT/third_party/yyjson" -c -o "$OUT/yyjson_hardened.o"` |
| tests/conformance/run_conformance.sh:184 | reference | `"$C" build "$c_file" --legacy-c --sanitize=none $CFLAGS $CLIBS -o "$c_exe"` (plain-C reference baseline; relabel if converted) |
| tests/stdlib/nuklear_cpp/run.sh:16 | load-bearing | `"$C" build build/ext/nuklear_core.c --legacy-c --sanitize=none -std=c11 -Ithird_party/nuklear -c -o <obj>` |

## Findings — tools

| File:line | Load-bearing | Concrete replacement |
| --- | --- | --- |
| tools/abi_compare/run.sh:59 | reference | `$CLANG ... abi_fp_probe.c` → `"$C" build <probe>.c --legacy-c --sanitize=none -O2 -std=c11 -Wall -Wextra -Werror $INC -o <out>` |
| tools/abi_compare/run.sh:63 | reference | Same shape for `fp_to_json.c` |
| tools/dual/hexdump/run.sh:39 | reference | `"$CLANG" -std="$std" ... hexdump.c` → `"$C" build hexdump.c --legacy-c --sanitize=none -std="$std" -pedantic-errors -Wall -Wextra -O2 -o <out>` |

## Documented as unavoidable (no conversion)

- CMake/compiler bootstrap: `CMakeLists.txt`, `.github/workflows/compiler.yml:55`
  (`-DCPLUS_CLANG_BIN`), `.github/workflows/libc-crt.yml:55`, `tools/build-linux.sh:169`
  (`-DCMAKE_C_COMPILER=clang`), `tools/build-windows.sh:126,131,139,146,186,235`
  (cross-compile/target-triple and C++ probes), `tools/verify_toolchain.sh:93,109,135,137,146,147`
  (the script exists to test the raw C toolchain), `tests/pr31_abi/run.sh:295-296` (second
  *real* GCC object producer), `tests/crt/asan/run_asan.sh:49,51` (sanitizer capability probe).
- Cross compilers: `apps/cli_app/run.sh:106` and
  `tests/cross_platform_apps/run_windows_build.sh:13` use `x86_64-w64-mingw32-gcc`; no c+
  cross driver is available, so keep as build flags only.
- Link-only clang calls (no `.c/.cpp/.cp/.hp` input): `tests/raii_shadowing/run.sh:43,58`.
- Historical evidence / docs / vendored code: `docs/**` (249 Markdown files), `proof/**`,
  `third_party/**` (0 shell scripts), `tests/c_standards/**` (9 runner scripts + `RESULTS.md`
  files), `tests/known_gaps/*/results/**`, `external_projects/*/verify*.sh`
  (build the plain-C reference binary). These are intentionally raw-C comparisons.
- Alias family (not literal direct invocations): 150 scripts set
  `CC=${CPLUS_CLANG:-clang}` / `CLANG=/usr/bin/clang` / `CC=$CPLUS_CLANG`; 83 invocation
  lines call `$CC`/`$CXX`/`$CLANG`. Convert only by setting the `CPLUS_CLANG` build flag;
  the command itself is already configurable and is deliberately a plain-C baseline.

## Summary counts

- Literal direct compiler invocations with `.c/.cpp/.cp/.hp` sources in active scope: 49
  - CI workflows: 18 (9 compiler.yml + 9 libc-crt.yml)
  - apps/examples: 6
  - benchmarks: 3
  - tests: 19
  - tools: 3
- Load-bearing: 32; reference/non-load-bearing: 17; unavoidable (documented): 20 call sites.
- Deliberately excluded: 150 alias-family files / 83 alias invocation lines; 249 docs files;
  9 standards-evidence runners; 3 external-project verify scripts; all `proof/**` and
  `third_party/**` content.

## Report hash

The final file SHA-256 and the commit hash are recorded in the commit message and in the
subagent hand-off; the file itself cannot contain its own hash.
