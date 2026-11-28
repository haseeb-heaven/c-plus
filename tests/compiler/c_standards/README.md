# C standard matrix for c+ LEGACY C mode

This suite checks whether `c+ --legacy-c --sanitize=none -- -std=<std>` behaves
like the C reference compilers for every standard in the manifest, while also
proving that the lowered C emitted by `--emit-c` preserves the original fixture
byte-for-byte (apart from exactly one leading `#line` directive).

## Run

```sh
CPLUS_BIN=build/c+ python3 tests/compiler/c_standards/run.py
```

Exit status is 0 only when there are **zero failures**. Unsupported standards
are not failures: when both c+ and the primary reference explicitly reject a
`-std` value, the exact diagnostics are captured verbatim in `summary.txt`,
`summary.json`, `results/cases.json` and `results/diagnostics/`, and the case is
counted as `unsupported`. No shell failure-suppression idiom is used, and
build/run mismatches are never swallowed.

## Invocations

For each `(standard, fixture)` pair:

```
<CPLUS_BIN> build --legacy-c --sanitize=none \
    --emit-c results/generated/<std>__<fixture>.c \
    fixtures/<fixture>.c -o <binary> -- -std=<std>

<REF> -std=<std> -O2 [-isysroot <sdk>] fixtures/<fixture>.c -o <binary>
```

The c+ sources are passed before `--`; the `-std=` value is passed after it so
it reaches the C backend as a pass-through argument (c+ otherwise adds its own
backend default of `-std=gnu89` for LEGACY C mode).

## References and environment

- `CPLUS_BIN` — path to `c+` (default `build/c+`).
- `REF_CC` — primary reference (default `/usr/bin/clang`), the same backend c+
  selects by default on macOS.
- `HOMEBREW_CLANG` — optional secondary reference (default
  `/opt/homebrew/opt/llvm/bin/clang`); used when present under `-isysroot`
  resolved by `xcrun --sdk macosx --show-sdk-path` or the `ISYSROOT` override.
- `REF_ISYSROOT` — optional explicit `-isysroot` for the primary reference.

The `std_version` fixture is compared only against the primary reference:
Apple clang and newer Homebrew clang disagree on the numeric `__STDC_VERSION__`
reported for `-std=c2x`.

## Assertions per case

1. c+ binary stdout bytes and process exit code equal the reference's exactly.
2. The emitted C equals the fixture after removing one leading `#line`; this is
   checked even when a standard is unsupported, because c+ writes `--emit-c`
   output before the backend compiles.
3. Rejected standards produce an `unsupported` status with the unmodified
   compiler diagnostics recorded; any asymmetric or unexplained build failure
   is a hard `fail`.

`summary.txt` and `summary.json` are written next to `run.py`; the full
per-case record (including all raw diagnostics and outputs) is in
`results/cases.json`. Generated C and per-standard diagnostic files are kept
under `results/` for inspection.
