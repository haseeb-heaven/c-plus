# Native C+ stdlib coverage tool (`tests/stdlib/coverage.cp`)

`tests/stdlib/coverage.cp` is a native C+ port of `tests/stdlib/coverage.py`.
It produces the same per-library report using the same **static API-call-site
heuristic**, with no Python interpreter required.

## What this number means (and what it does NOT mean)

**It is static name reachability only.** The tool searches for a textual
`name(` token (or, for destructors, the owning type name) in a fixed set of
test/example sources. It does not compile, link, or run those sources.

Therefore:

- This is **NOT execution coverage**, NOT branch/path coverage, and NOT a
  statement-level coverage measurement.
- This is **NOT a correctness, safety, or security proof**. A call appearing in
  a comment, a string literal, an `#if 0` block, a dead branch, or a test that
  is never run counts exactly the same as a checked assertion in live code.
- Conversely, an API reached only indirectly (compiler-inserted call, function
  pointer, macro/template expansion, callback) may be reported uncovered even
  if it is genuinely exercised.
- "100% API coverage" means only that every regex-recognised public name is
  mentioned in the scanned text. It does not mean the API is fully executed or
  tested.
- The `80.0%` gate is an unchanged API-surface floor, identical to the Python
  tool. It has not been weakened.

For the longer list of false-positive/false-negative classes, see
`docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md` §1; it applies verbatim to this tool.

## Build and run

From the repository root:

```sh
./build/c+ build --strict --sanitize=none tests/stdlib/coverage.cp \
    -o build/stdlib-tests/coverage_native
build/stdlib-tests/coverage_native            # rc 0 when all libraries >= 80%
python3 tests/stdlib/coverage.py               # Python oracle, for comparison
```

`tests/stdlib/coverage.py` is no longer present in the working tree (it was
removed from the test paths but is still available as the oracle). Recover it
with `git show HEAD:tests/stdlib/coverage.py` when a comparison run is needed.

An optional first argument is a repository root to change into before
discovery (the Python tool derives its root from `__file__`):

```sh
build/stdlib-tests/coverage_native /path/to/repo-root
```

Without an argument, run from the repository root; the tool uses relative
`find` paths (`stdlib`, `tests/stdlib`, ...).

Exit codes: `0` = all libraries pass, `1` = at least one library below the
threshold (same as Python), `2` = discovery/parsing produced no data (hard
error; an empty report is never treated as a pass).

## Algorithm (mirrors `coverage.py`)

1. Shell `find` enumerates the `.hp` headers under `stdlib/`; output is sorted
   with `LC_ALL=C sort` and redirected to a PID-tagged temp file, which is
   read back and removed. The same mechanism enumerates `.cp` files directly
   under `tests/stdlib/`, under `tests/stdlib/multi_tu/`, and under
   `tests/stdlib/examples/`. These are the same three globs the Python tool
   uses; no other directory is scanned.
2. Each header is scanned line by line, emulating the Python regexes:
   - `CLASS` → `class NAME {` sets the current owning class; a bare `};`
     clears it.
   - `SIG` → an optional `static inline` prefix, a type/return spec, a name,
     `(params) {` on one line. The type part is scanned left-to-right for the
     first word boundary that leaves a valid identifier immediately before
     `(`; this reproduces the lazy `[A-Za-z_][\w \t\*]*?` behaviour, including
     `static inline int foo(...) {`.
   - `CTOR` → `NAME(params) {` on one line, tried only when `SIG` fails.
   - Preprocessor lines, `if/for/while/switch/do/else/return`, `main`, and
     `stdlib_*` names are excluded, exactly as in Python.
   - Headers with no recognised definitions are skipped, exactly as in Python.
3. The three source sets are concatenated into one text buffer. For every
   public entry:
   - `drop` owned by a class is credited when the class name appears as a
     standalone word (`\bCLASS\b`), because the compiler inserts the call;
   - `*_drop` is credited when its owner name appears as a standalone word;
   - every other name is credited when `\bNAME\s*\(` appears anywhere in the
     text (`\s*` includes newlines, matching Python).
4. Output formatting is byte-for-byte the Python format strings
   (`%-18s %8d %8d %6.1f%%`, the `BELOW 80%:` lines, and the comma-joined
   missing list). `worst` starts at `100.0` and is the minimum over printed
   libraries, as in Python.

`cplus_system()` in the hardened libc is a namespaced one-line wrapper around
POSIX `system()`. The tool declares an equivalent local shim so it links with
only the default C+ runtime and builds with the exact command above (no extra
`-l` flags). To use the real hardened wrapper instead, delete the local shim,
add `extern int cplus_system(const char *cmd);`, and link
`build/libc-install/lib/libcplus_hardened_libc.a` after `--`; the report is
unchanged.

## Verification performed

All commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on macOS (Apple clang 16).

1. Build, strict mode, sanitizers off:

   ```sh
   ./build/c+ build --strict --sanitize=none tests/stdlib/coverage.cp \
       -o build/stdlib-tests/coverage_native
   ```

   Result: rc `0`, no compiler diagnostics.

2. Headline comparison (current tree):

   ```sh
   python3 tests/stdlib/coverage.py > /tmp/python.out
   build/stdlib-tests/coverage_native > /tmp/native.out
   diff -u /tmp/python.out /tmp/native.out
   ```

   Result: both rc `0`; `diff` rc `0` — **byte-identical output**, 21 library
   rows, headline `all libraries >= 80% API coverage (worst 100.0%)`.
   Per-library covered/total pairs matched for all 21 headers, e.g.
   `collections.hp 63/63`, `json.hp 30/30`, `video.hp 25/25`.

3. Failure-path comparison (isolated sandbox containing copies of all
   `stdlib/*.hp` plus three trivial `.cp` files that call nothing):

   ```sh
   python3 "$SANDBOX/tests/stdlib/coverage.py" > "$SANDBOX/python.out"   # rc 1
   build/stdlib-tests/coverage_native "$SANDBOX" > "$SANDBOX/native.out" # rc 1
   diff -u "$SANDBOX/python.out" "$SANDBOX/native.out"                   # rc 0
   ```

   Result: byte-identical, including every `BELOW 80%:` row and the full
   `not called by any test/example:` missing lists (e.g. `string.hp (0.0%)`
   with 24 names, `vector.hp (0.0%)` with 21 names). Exit status `1` in both.

4. Empty-discovery guard (run from a directory with no `stdlib/` or
   `tests/stdlib/`):

   ```sh
   build/stdlib-tests/coverage_native      # from an unrelated cwd
   ```

   Result: rc `2`, stderr
   `coverage: no headers (0) or no test sources (0, 0 bytes)` — no green row
   and no success line. This closes the vacuous-pass failure mode.

## Strict-compiler rebuild (current tree)

The rebuilt strict compiler rejects subscripts on pointer parameters whose
extent cannot be proven. A full re-scan of `coverage.cp` found **five**
diagnostics (the initial report listed only the first two):

```
coverage.cp:90:27: error: [CPLUS-FFI] pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe
coverage.cp:121:9: error: [CPLUS-FFI] pointer parameter 'list' has unverifiable extent; subscript cannot be proven safe
coverage.cp:150:5: error: [CPLUS-FFI] pointer parameter 'buf' has unverifiable extent; subscript cannot be proven safe
coverage.cp:429:27: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
coverage.cp:430:59: error: [CPLUS-FFI] pointer parameter 'argv' has unverifiable extent; subscript cannot be proven safe
```

All pointer-parameter subscripts were replaced with the verified
`__builtin_memcpy` pattern already used in `stdlib/io.hp`, `stdlib/file.hp`,
`stdlib/log.hp`, and `stdlib/collections.hp`:
reads use `__builtin_memcpy(&local, ptr + index, sizeof(local))`, writes use
`__builtin_memcpy(ptr + index, &local, sizeof(local))`. The sixth pointer-
parameter subscript (`dst[n]` in `copy_span`, line 96) was converted
proactively even though the checker did not diagnose it, so the file has no
pointer-parameter subscripts left. Subscripts on locals, globals, and struct
members are unchanged. No threshold, discovery check, or algorithm was
altered.

Verification commands (from the repository root):

```sh
./build/c+ build tests/stdlib/coverage.cp --strict --sanitize=none -o /tmp/cov_native
/tmp/cov_native
```

Result: build rc `0` with **no remaining diagnostics** (empty stdout and
stderr); run rc `0`, empty stderr. Exact stdout (25 lines):

```
library             covered    total percent
------------------ -------- -------- -------
async.hp                 14       14  100.0%
audio.hp                 29       29  100.0%
collections.hp           63       63  100.0%
error.hp                  9        9  100.0%
file.hp                  13       13  100.0%
fs.hp                     8        8  100.0%
graphics.hp              16       16  100.0%
gui.hp                   18       18  100.0%
http.hp                  19       19  100.0%
image.hp                 10       10  100.0%
input.hp                 10       10  100.0%
io.hp                     6        6  100.0%
json.hp                  30       30  100.0%
log.hp                   14       14  100.0%
net.hp                   15       15  100.0%
string.hp                24       24  100.0%
thread.hp                21       21  100.0%
time.hp                   5        5  100.0%
vector.hp                21       21  100.0%
video.hp                 25       25  100.0%
window.hp                11       11  100.0%

all libraries >= 80% API coverage (worst 100.0%)
```

The Python oracle was recovered from `HEAD` and run from a temporary mirror of
the repository layout (no working-tree files were added). Comparison results:

- Current tree: native stdout **byte-identical** to the Python oracle
  (`diff -u` rc `0`, `cmp` rc `0`); both rc `0`. No empty discovery: 21 headers,
  all `.cp` sources discovered.
- Explicit-root argument (`/tmp/cov_native <repo-root>`, which exercises the
  repaired `argv` read): rc `0`, byte-identical to the oracle.
- Failure path (sandbox with all 21 headers copied and three trivial `.cp`
  files that call nothing): both native and Python rc `1`, stdout
  **byte-identical**, including every `BELOW 80%:` row and the full
  `not called by any test/example:` missing lists (e.g. `string.hp (0.0%)`,
  `vector.hp (0.0%)`). The 80.0% gate is unchanged.
- Empty discovery (run from an unrelated cwd with no `stdlib/` or
  `tests/stdlib/`): rc `2`, no stdout and no success line; stderr ends with
  `coverage: no headers (0) or no test sources (0, 0 bytes)`.

**Remaining diagnostics: none.**

## Equivalence caveats

- The regex-to-hand-scanner translation is line-based and ASCII-oriented.
  Python's `\w`/`\s` are Unicode-aware; every identifier and signature in the
  checked-in headers is ASCII, so results match. A future non-ASCII
  identifier or unusual whitespace could diverge.
- `find` replaces `pathlib.glob`. With the current ASCII file names and
  `LC_ALL=C sort` the ordering and membership match; paths containing spaces
  are not supported by the shell path list.
- Only the three documented source globs are read. API calls that live only
  in `demos/`, `scratch/`, or generated files are invisible to both tools.
- Fixed buffers (16 MiB of concatenated source text, 4 MiB per header,
  2048 paths) cover the current tree comfortably; exceeding them truncates
  input rather than growing. This is a practical limit, not a semantic change
  for the checked-in sources.
- The Python file is intentionally left in place as the oracle; this document
  and `tests/stdlib/coverage.cp` are the only files added.
