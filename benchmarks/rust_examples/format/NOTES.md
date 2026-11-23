# Rust format/scanf/conversion examples — build, nm evidence, results

Scope: runnable safe-Rust examples for every checked format/scanf/conversion API
exported by the two prebuilt archives, compared line-for-line against a C99
harness that calls the real APIs.

Files (all under `benchmarks/rust_examples/format/`):

| File | Role |
|---|---|
| `format.rs` | Safe-Rust only (`format!`/`write!`, `str::parse`, `Option`, checked arithmetic). No `unsafe`. Prints `<api> <case> <token>`. |
| `harness.c` | C99 harness calling each checked API for the same cases; prints matching tokens. Links both archives. |
| `compare.sh` | Symbol check via `nm`, builds both under `timeout 30`, runs both under `timeout 30`, diffs, prints the summary line. |
| `NOTES.md` | This file. |

## Toolchain (exact, recorded from this host)

- Host: `Darwin ... 27.2.0 ... arm64`, Apple M2 (`uname -a`, `2026-11-23`).
- Rust: `rustc 1.100.0 (0ef1bc386 2026-11-09)`, commit
  `0ef1bc386059596dada8fb65b50425aab2ea637a`, host `aarch64-apple-darwin`,
  LLVM 23.1.5 (`rustc --version --verbose`).
- C: `/usr/bin/clang`, `Apple clang version 16.0.0 (clang-1600.0.26.6)`,
  target `arm64-apple-darwin27.2.0`. The Homebrew clang on `PATH`
  (`/opt/homebrew/opt/llvm/bin/clang`, 23.1.6) was **not** usable: it points at a
  missing sysroot `/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk` and
  fails with `fatal error: 'errno.h' file not found`. `compare.sh` therefore
  prefers `/usr/bin/clang`, honoring an explicit `$CC` override.
- `nm`: `llvm-nm, compatible with GNU nm`, Apple LLVM 16.0.0.
- `timeout`: `/usr/bin/timeout` (verified with `timeout 30 true`). `compare.sh`
  also accepts `$TIMEOUT_BIN` / `gtimeout`.
- Archives (arm64, from `file` + `lipo -archs`):
  - `build/safe/libcplus_safecrt.a` — 458520 bytes, `arm64`.
  - `build/libc/libcplus_hardened.a` — 276128 bytes, `arm64`.

## Symbol verification (nm evidence)

Command:

```sh
nm -g build/safe/libcplus_safecrt.a | grep -E ' T _cplus_(sprintf_s|sprintf_s_checked|snprintf_s|snprintf_s_checked|vsnprintf_s|vsprintf_s|printf_s|printf_s_checked|fprintf_s|fprintf_s_checked|sscanf_s|sscanf_s_checked)$'
nm -g build/libc/libcplus_hardened.a | grep -E ' T _cplus_(strtol|strtoul|strtoimax)$'
```

Output (all 15 required symbols present, no missing symbol → nothing BLOCKED):

```
# build/safe/libcplus_safecrt.a
0000000000000918 T _cplus_fprintf_s
0000000000000aac T _cplus_fprintf_s_checked
0000000000000614 T _cplus_printf_s
000000000000077c T _cplus_printf_s_checked
00000000000003b0 T _cplus_snprintf_s
0000000000000434 T _cplus_snprintf_s_checked
00000000000016dc T _cplus_sprintf_s
0000000000001794 T _cplus_sprintf_s_checked
0000000000001360 T _cplus_sscanf_s
00000000000014c0 T _cplus_sscanf_s_checked
0000000000000000 T _cplus_vsnprintf_s
000000000000165c T _cplus_vsprintf_s

# build/libc/libcplus_hardened.a
00000000000001d0 T _cplus_strtoimax
0000000000000104 T _cplus_strtol
0000000000000150 T _cplus_strtoul
```

`comm` of the defined `T` symbols in both archives is empty — no duplicate
definitions, so linking both archives in one `clang` invocation is clean.

## Exact build and run commands

`compare.sh` performs these (with `timeout 30` around each build and run):

```sh
rustc -O benchmarks/rust_examples/format/format.rs -o "$TMP/format_rust"

/usr/bin/clang -std=c99 -O2 -D_DARWIN_C_SOURCE=1 \
    benchmarks/rust_examples/format/harness.c \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
    -o "$TMP/harness"

"$TMP/format_rust" > "$TMP/rust.out"
"$TMP/harness"     > "$TMP/c.out"
diff -u "$TMP/rust.out" "$TMP/c.out"
```

`-D_DARWIN_C_SOURCE=1` exposes POSIX `dup`/`dup2`/`fileno`. Those are used only
to keep `printf_s`'s own stdout out of the token stream. The harness calls
`cplus_safe_log_set_level(0)`, `cplus_runtime_set_failstop(0)` and
`cplus_runtime_set_violation_trap(0)` first, so refusal cases return `-1`
instead of `_exit(70)` (default violation trap is enabled — see
`stdlib/safe/src/safe_runtime.c`).

One-shot driver (from the repo root):

```sh
TMPDIR=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode \
    benchmarks/rust_examples/format/compare.sh
```

## Cases and coverage

Shared token vocabulary: `OK` (success), `TRUNC` (bounded output did not fit;
would-be length > `dmax-1`), `REFUSED` (null dest/stream/input, `%n`, unbounded
`%s`, numeric overflow).

| API family | APIs | Cases |
|---|---|---|
| Buffer format | `sprintf_s`, `sprintf_s_checked`, `snprintf_s`, `snprintf_s_checked`, `vsnprintf_s`, `vsprintf_s` | `valid`, `exact_fit`, `too_small`, `null_dest`, `percent_n` |
| Stream format | `printf_s`, `printf_s_checked`, `fprintf_s`, `fprintf_s_checked` | `valid`, `percent_n`, plus `null_stream` for `fprintf_s*` |
| Scan | `sscanf_s`, `sscanf_s_checked` | `valid`, `percent_n`, `unbounded_s`, `null_input` |
| Conversion | `strtol`, `strtoul`, `strtoimax` | `valid`, `overflow`, `invalid`, plus `null_input` for `strtoimax` |

`exact_fit` uses `dmax = 5` for `"AB42\0"`; `too_small` uses `dmax = 4` (one
byte too small) and the C side classifies the would-be length 4 as `TRUNC`.
Numeric overflow uses `"99999999999999999999999999"`; the C wrappers clamp and
set `errno == ERANGE`, which the harness maps to `REFUSED`.

## Results (real run, 2026-11-23)

- Rust build: clean, no warnings. C build/link: clean, no warnings.
- Both binaries exit 0; both print exactly **58** `<api> <case> <token>` lines.
- `diff -u` of the two outputs: **empty** (byte-identical).
- `compare.sh`:
  ```
  RUST_EXAMPLES format 15 10 58 0 0
  STATUS ok
  ```
  exit status **0** (15 APIs, 10 distinct cases, 58 matched, 0 mismatched,
  0 blocked).
- BLOCKED path exercised in an isolated temp copy with zero-length stand-in
  archives: it printed `BLOCKED: missing symbol <name> in <archive>` for all 15
  symbols, emitted
  `RUST_EXAMPLES format 0 0 0 0 15` / `STATUS blocked`, and exited 0 — i.e. an
  absent symbol is reported with nm evidence, never counted as a token match.
- Mismatch path exercised in an isolated temp copy with one token deliberately
  altered: emitted `RUST_EXAMPLES format 15 10 48 10 0` / `STATUS fail`, printed
  a unified diff, and **exited 1** — confirming nonzero exit on a real mismatch.
- No live game / injected DLL involved; this is a build-and-run equivalence
  check only.

## Non-equivalence statement (important)

The Rust and C outputs match at the level of **outcome classification tokens**,
not at the level of implementation, bytes, errno, or memory state. `format.rs`
never calls the C archives and never executes the checked API code paths. It
re-expresses each scenario with safe Rust:

- **Bounded write**: the C APIs truncate into a caller buffer, NUL-terminate,
  and return a C99-style would-be length; the Rust side uses a `fmt::Write`
  sink that reserves the final byte for NUL and sets a truncation flag. Same
  `TRUNC` classification, different return convention (no integer return).
- **NULL destination/stream/input**: C performs a runtime NULL check and
  returns `-1`/`EINVAL`; Rust models absence as `None` and the compiler makes
  a null reference unrepresentable. The refusal is structural, not a runtime
  check.
- **`%n` / unbounded `%s`**: C refuses via Annex-K-style format scanning and
  `cplus_runtime_violation`; Rust `format!` has no `%n` concept, so the Rust
  example scans the format *text* and refuses explicitly. This emulates the
  policy; it does not execute the C check.
- **Numeric overflow**: `cplus_strtol`/`cplus_strtoul`/`cplus_strtoimax` clamp
  to the type maximum and set `errno == ERANGE`; Rust `str::parse` returns
  `Err(ParseIntError)` and the checked-arithmetic helper returns no value.
  Both become `REFUSED`; the recovered value and `errno` are not compared.
- **Asymmetry in the hardened wrappers**: `cplus_strtol` and `cplus_strtoul`
  are thin libc passthroughs with no `nptr == NULL` guard, so calling them with
  NULL would be unsafe; the `null_input` case is therefore present only for
  `cplus_strtoimax`, which guards NULL explicitly. The Rust side can model
  `None` for all three but only the strtoimax case is emitted, to keep parity.
- **Violation trap**: the C harness disables the SafeCRT violation trap so
  refusals are observable as `-1`; the Rust examples have no trap to disable.

Consequently a token match demonstrates that a safe-Rust formulation reaches
the same *decision* as each checked C API for these cases. It is not proof of
ABI, errno, buffer-content, or runtime-fault equivalence, and it is not a
substitute for validating the C APIs in the Unicorn/live-game workflow. If an
archive is rebuilt without one of the 15 symbols, `compare.sh` reports BLOCKED
with nm evidence instead of claiming a match.
