# C-ABI non-regression gate (issue #186)

One command, fail-closed, on every PR and build:

```sh
CPLUS_BIN="$PWD/build/c+" sh tests/compiler/abi_gate/run.sh
```

The runner executes these suites and exits 1 if **any** of them fails. Logs are
written to `tests/compiler/abi_gate/results/`.

| Suite | What it protects |
| --- | --- |
| `abi_safety` | pointer width, `sizeof`/`_Alignof`/`offsetof`, signatures, callbacks, symbol names in LEGACY/DEFAULT/STRICT |
| `abi_mixed` | c+-compiled and clang-compiled objects linked in both directions |
| `abi_real_world` | real C headers/libraries through c+ vs clang, byte/symbol/behavior parity |
| `cross_compiler` | same-program behavior across available toolchains |
| `source_preservation` | LEGACY emitted C stays byte-identical to the input |
| `host_abi_purity` | host C structs identical with/without C+ and hardened headers; runtime/libc/safe structs match plain-C mirrors; macro hygiene |
| `hp_boundary` | all `stdlib/*.hp` headers discovered and parse-checked; native interop boundary policy recorded |
| `c_to_cp` | precompiled plain-C archive called from STRICT C+ and C+ memory/callbacks called from C |
| `precompiled` | C library compiled once; C+ app rebuilt twice without touching the saved binary |
| `safe_s_catalog` | catalog vs shipped headers and the three dedicated `_s` docs (issues #188/#190) |

## Coverage map

- `runtime/` — `cplus_cleanup`, `cplus_scope`, `CPlusExceptionFrame` mirror
  assertions in `tests/abi/host_abi_purity/host_types.c`.
- `libc/` — public header layout/enum/signature records and the
  `cplus_sc_upstream_*` / `cplus_v_*` declarations in the catalog.
- `stdlib/safe/` — all 47 shipped `_s` prototypes are catalogued and checked;
  `safe_c.h` macro mappings are exercised by the precompiled labs.
- `stdlib/*.hp` — every header is discovered by `run_hp_boundary.py`; the C
  boundary rule is documented in [STRICT_C_ABI_BOUNDARY.md](STRICT_C_ABI_BOUNDARY.md).

The older text fingerprint (`sh tools/abi_compare/run.sh compare
tests/abi/fingerprints/baseline.txt`) remains a host-specific manual audit
(Apple clang 16 / arm64 baseline); it is not part of the portable gate because
its baseline includes toolchain-version and archive-symbol records that differ
legitimately on Linux CI. The portable `host_abi_purity` suite replaces those
records with host-derived equality assertions that run on both runners.

## Adding a case

1. Add the assertion to `tests/abi/host_abi_purity/host_types.c` (with a
   matching plain-C mirror) or to the relevant existing suite; never relax a
   passing suite to make a change green.
2. If a new `_s` symbol is shipped, add it to `safeSAbiCatalog` and to the three
   docs, then run
   `python3 tests/compiler/abi_gate/check_safe_s_catalog.py`.
3. Run the one command above and record its final line. A `BLOCKED` label for
   an uninstalled optional third-party header is explicit in `hp_boundary`,
   never a silent skip; any other failure fails the gate.

## Non-claims

This is an ABI *non-regression* gate for the exercised subset on the selected
CI hosts. It does not prove Windows/MSVC ABI, bitfield packing across vendors,
or live-game behaviour. See [compatibility-matrix.md](compatibility-matrix.md)
for the historical snapshot and its explicit non-claims.
