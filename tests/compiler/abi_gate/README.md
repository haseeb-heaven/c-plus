# ABI preservation gate

One command that fails if the compiler ever changes the plain-C ABI:

```sh
CPLUS_BIN=$PWD/build/c+ sh tests/compiler/abi_gate/run.sh
```

It runs, with no suppression or skips:

1. `abi_safety` — pointer/struct sizes, offsets, signatures, symbol visibility.
2. `abi_mixed` — c+-compiled and clang-compiled objects linked in both directions.
3. `abi_real_world` — real C headers/libraries through c+ vs clang, byte/symbol/behavior checks.
4. `cross_compiler` — same-program behavior across the available toolchains.
5. `source_preservation` — LEGACY emitted C is byte-identical to the input after the single `#line` prefix.

The invariant: ordinary C pointers remain ordinary addresses, layouts and
calling conventions match the host C ABI, and exported application symbols are
identical. Additional `cplus_*` runtime exports are additive and documented;
they do not replace or rename application symbols. Any suite failure makes the
gate exit 1. Per-suite logs are written under `results/`.
