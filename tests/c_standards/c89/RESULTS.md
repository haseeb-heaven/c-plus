# C89 / ISO C90 support in the C+ compiler driver

**Classification: PARTIAL**

The C+ driver accepts and forwards all four tested C90 `-std` spellings to
its Clang backend, and genuine C89 source compiles and runs identically to a
reference Clang build. However, the driver does **not** enable strict C90
diagnostics by default: C99-only constructs (e.g. mixed declarations) are
accepted under `-std=c89` unless the caller also passes `-pedantic-errors`.
So "C89 mode" is really "pass `-std=c89` to Clang", not "enforce ISO C90".

Evidence below is from `sh run.sh`; the full machine-readable transcript is
in `run.log` (timestamp `2026-11-23T06:54:29Z`).

Environment:
- driver: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+`
- backend/references: Apple clang 16.0.0 (`/usr/bin/clang`)
- All commands were run with a 30-second timeout.

---

## 1. Test sources (both under this directory)

| File | Purpose |
|------|---------|
| `hello89.c` | Genuine ISO C90: block comments only; all declarations at block start; no C99 types, no C99 initializers, no VLAs, no compound literals. Prints 3 lines plus a total. |
| `probe_c99.c` | Negative control: a C99 mixed declaration (`int j = 2;` after a statement). Reference Clang rejects it under `-std=c89 -pedantic-errors`. |

`hello89.c` is also accepted by plain `/usr/bin/clang -std=c89 -pedantic-errors`
(exit 0), confirming it is truly C89 rather than merely tolerated C.

---

## 2. Exact command matrix

```
Root = /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
C+   = $Root/build/c+
```

### 2.1 Reference build (genuine C89)

| # | Exact command | Exit | Output |
|---|---------------|------|--------|
| R1 | `/usr/bin/clang -std=c89 -pedantic-errors hello89.c -o hello89.ref` | 0 | none |

### 2.2 C+ driver, genuine C89, all C90 `-std` spellings

| # | Exact command | Exit | Output |
|---|---------------|------|--------|
| A1 | `./build/c+ build hello89.c -std=c89 --sanitize=none -o hello89.cplus.c89` | 0 | none |
| A2 | `./build/c+ build hello89.c -std=c90 --sanitize=none -o hello89.cplus.c90` | 0 | none |
| A3 | `./build/c+ build hello89.c -std=iso9899:1990 --sanitize=none -o hello89.cplus.iso9899:1990` | 0 | none |
| A4 | `./build/c+ build hello89.c -std=gnu89 --sanitize=none -o hello89.cplus.gnu89` | 0 | none |

### 2.3 Same, with `-pedantic-errors` passed through to the backend

| # | Exact command | Exit | Output |
|---|---------------|------|--------|
| B1 | `./build/c+ build hello89.c -std=c89 -pedantic-errors --sanitize=none -o hello89.ped.c89` | 0 | none |
| B2 | `./build/c+ build hello89.c -std=c90 -pedantic-errors --sanitize=none -o hello89.ped.c90` | 0 | none |
| B3 | `./build/c+ build hello89.c -std=iso9899:1990 -pedantic-errors --sanitize=none -o hello89.ped.iso9899:1990` | 0 | none |
| B4 | `./build/c+ build hello89.c -std=gnu89 -pedantic-errors --sanitize=none -o hello89.ped.gnu89` | 0 | none |

All four spellings are accepted by Clang. (`-std=bogus` is rejected by the
backend: `error: invalid value 'bogus' in '-std=bogus'`, plus Clang's list of
valid standard names — exit 1. The driver itself performs no spelling
validation; it simply forwards the flag.)

### 2.4 Negative control — is C90 actually enforced?

| # | Exact command | Exit | Key diagnostic |
|---|---------------|------|----------------|
| C1 | `/usr/bin/clang -std=c89 -pedantic-errors -fsyntax-only probe_c99.c` | 1 (expected) | `probe_c99.c:11:9: error: mixing declarations and code is a C99 extension [-Werror,-Wdeclaration-after-statement]` |
| C2 | `./build/c+ build probe_c99.c -std=c89 --sanitize=none -o probe.cplus.c89` | **0** | none — C99 mixed declaration is **accepted** under `-std=c89` |
| C3 | `./build/c+ build probe_c99.c -std=c89 -pedantic-errors --sanitize=none -o probe.cplus.ped` | 1 (expected) | same `-Wdeclaration-after-statement` error as C1 |

**Finding:** `-std=c89` is forwarded to the backend, but strict C90 conformance
is *not* enabled by the driver. Without `-pedantic-errors`, Apple Clang treats
C99 constructs as extensions under `-std=c89` and compiles them. With
`-pedantic-errors` explicitly passed (the driver forwards unknown dash flags
verbatim), the C99 construct is rejected, proving the standard flag does reach
Clang.

### 2.5 Real projects with `-std=c89`

All three downloaded projects are C99-era code; none is valid C89. Reference
Clang rejects all three under `-std=c89 -pedantic-errors`, so a C+ failure is
**not** a C+ regression. Exact results:

| Project | `clang -std=c89 -pedantic-errors -fsyntax-only` | `c+ build … -std=c89 --sanitize=none` (default Safe mode) | `c+ build … --legacy-c -std=c89 -pedantic-errors --sanitize=none` |
|---------|--------------------------------------------------|------------------------------------------------------------|------------------------------------------------------------------|
| `external_projects/game_a/original/snake.c` | exit **1**, 10 errors. First: `snake.c:16:40: error: mixing declarations and code is a C99 extension [-Werror,-Wdeclaration-after-statement]`; `snake.c:25:9: error: variable declaration in for loop is a C99-specific feature [-Werror,-Wc99-extensions]` | exit **1**, 16 C+ Safe-mode errors *before* std matters. First: `snake.c:44:39: error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] negative array index on 'sx'`; `snake.c:31:16: error: [CPLUS-LIFETIME] variable 'c' is uninitialized` | exit **1**, 11 errors matching the reference exactly: `mixing declarations and code is a C99 extension`, `variable declaration in for loop is a C99-specific feature`, … |
| `external_projects/game_b/original/2048.c` | exit **1**, 20 errors. First: `2048.c:11:27: error: // comments are not allowed in this language [-Werror,-Wcomment]`; `2048.c:31:24: error: initializer for aggregate is not a compile-time constant [-Werror,-Wc99-extensions]`; `2048.c:129:1: error: '_Bool' is a C99 extension` (via `stdbool.h`) | exit **1**, 8 C+ Safe-mode errors. First: `2048.c:433:2: error: [CPLUS-FFI] 'printf' is not async-signal-safe`; `2048.c:61:36: error: [CPLUS-LIFETIME] variable 'fg' is uninitialized` | exit **1**, 20 errors matching the reference exactly (`// comments`, `_Bool`, non-constant aggregate initializer, …) |
| `external_projects/cli_app/original/xdump.c` | exit **1**, 9 errors. First: `xdump.c:14:21: error: variadic macros are a C99 feature [-Werror,-Wvariadic-macros]`; `xdump.c:22:1: error: // comments are not allowed in this language`; `xdump.c:62:26: error: variable length arrays are a C99 feature [-Werror,-Wvla-extension]` | exit **1**, 3 C+ Safe-mode errors. First: `xdump.c:62:26: error: [CPLUS-OWNER] variable-length stack allocation has no bounds check; use cplus_alloc`; `xdump.c:184:29: error: [CPLUS-LIFETIME] variable 'endptr' is uninitialized` | exit **1**, 10 errors matching the reference exactly (variadic macros, `//` comments, VLAs, for-loop declarations, …) |

In `--legacy-c -std=c89 -pedantic-errors` mode, C+ produces the same C99
extension diagnostics as plain Clang, confirming the `-std` value is passed
through unchanged. In default Safe mode the C+ safety analyser rejects these
programs for unrelated reasons before C89 conformance is decisive; this is
expected and is reported honestly above (no pass is claimed for these three
sources).

---

## 3. Runtime stdout comparison (timeout 30)

Each binary was executed with `timeout 30`. Captured stdout lives in
`stdout.ref` and `stdout.cplus.<spelling>`.

| Binary | Exit | stdout SHA-256 | Byte-for-byte vs reference |
|--------|------|----------------|----------------------------|
| `hello89.ref` (clang -std=c89 -pedantic-errors) | 0 | `3ef9faec661724985bd51ca5bfb9979612aca37e58172055ed66911b0e589536` | — |
| `hello89.cplus.c89` | 0 | `3ef9faec661724985bd51ca5bfb9979612aca37e58172055ed66911b0e589536` | **IDENTICAL** |
| `hello89.cplus.c90` | 0 | `3ef9faec661724985bd51ca5bfb9979612aca37e58172055ed66911b0e589536` | **IDENTICAL** |
| `hello89.cplus.iso9899:1990` | 0 | `3ef9faec661724985bd51ca5bfb9979612aca37e58172055ed66911b0e589536` | **IDENTICAL** |
| `hello89.cplus.gnu89` | 0 | `3ef9faec661724985bd51ca5bfb9979612aca37e58172055ed66911b0e589536` | **IDENTICAL** |

Common stdout (167 bytes, identical for all five binaries):

```
line 0: C89/ISO C90 source compiled by the C+ driver
line 1: C89/ISO C90 source compiled by the C+ driver
line 2: C89/ISO C90 source compiled by the C+ driver
total=3
```

No stderr was produced by any of the five runs.

---

## 4. Classification and rationale

**PARTIAL** — evidence summary:

*Supported:* genuine ISO C90 source (`hello89.c`) builds and runs correctly
through `c+ build -std=c89|c90|iso9899:1990|gnu89 --sanitize=none`, with
stdout byte-identical to a strict reference Clang build. All four spellings
are accepted; invalid spellings are rejected by the backend.

*Gap:* `-std=c89` alone does not make the driver enforce C90. A C99-only
construct (`probe_c99.c`) compiles successfully with `-std=c89` because the
driver does not pass `-pedantic-errors` (or `-Wdeclaration-after-statement`)
by default. Strict C90 rejection requires the caller to add
`-pedantic-errors`, which the driver forwards. This is a pass-through flag,
not first-class C89 mode enforcement.

*Real projects:* none of the three downloaded originals is C89 source; all
fail under strict reference Clang too. Through C+ in `--legacy-c` mode the
C99-extension errors match the reference; in the default Safe mode they fail
earlier on C+ safety diagnostics. No live C89 real-project success is claimed.

### Reproduction

```sh
cd tests/c_standards/c89
sh run.sh                 # writes run.log and stdout.* here
grep '^RESULT\|^sha256' run.log
```

`run.sh` uses private temp files and writes only inside `tests/c_standards/c89/`
apart from the driver's own auto-deleted `.cplus-tmp-*.c` files in each input
source directory.
