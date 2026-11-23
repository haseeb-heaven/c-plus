# File utility examples with safety proofs (C+ safe stdlib)

Five single-file CLI utilities with deterministic, argv-driven behaviour. Every
build uses the repository's C+ driver in strict, no-sanitizer mode from the repo
root so the `stdlib/*.hp` includes resolve with `-I.`. No clock, network,
interactive prompt, user home path or shelling out from the apps themselves.

## Files

| File | Purpose |
| --- | --- |
| `fhash.cp` | FNV-1a-32 file hash with bounded, chunked reads |
| `fdiff.cp` | Bounded line-by-line unified-style diff of two files |
| `fcat.cp` | Cat with bounded line output and an exact byte-count guard |
| `fsize.cp` | Deterministic size listing of fixed paths inside a directory |
| `fcopy.cp` | Bounded copy with exact-fit/one-over checks and byte-exact verify |
| `fixtures/generate.sh` | Writes every fixed-byte fixture into a scratch dir |
| `run.sh` | Builds and runs all five; prints the `UTIL_FILE` summary |
| `.gitignore` | Keeps scratch binaries/fixtures in `.run/` out of git |

## Build commands (exact)

Run from the repository root
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`):

```sh
./build/c+ build examples/utilities/file_tools/fhash.cp  --strict --sanitize=none -I. -o examples/utilities/file_tools/.run/fhash
./build/c+ build examples/utilities/file_tools/fdiff.cp  --strict --sanitize=none -I. -o examples/utilities/file_tools/.run/fdiff
./build/c+ build examples/utilities/file_tools/fcat.cp   --strict --sanitize=none -I. -o examples/utilities/file_tools/.run/fcat
./build/c+ build examples/utilities/file_tools/fsize.cp  --strict --sanitize=none -I. -o examples/utilities/file_tools/.run/fsize
./build/c+ build examples/utilities/file_tools/fcopy.cp  --strict --sanitize=none -I. -o examples/utilities/file_tools/.run/fcopy
```

All five builds exited 0 with empty stdout and empty stderr (each
`.run/<tool>.build.log` is 0 bytes).

## Harness command and exact output

```sh
sh examples/utilities/file_tools/run.sh
```

```
== fhash ==
build ok   fhash
ok         fh_hello
ok         fh_exact
ok         fh_oversized
ok         fh_missing
ok         fh_badpath
== fdiff ==
build ok   fdiff
ok         fd_change
ok         fd_same
ok         fd_longline
ok         fd_bigfile
ok         fd_missing
ok         fd_badpath
== fcat ==
build ok   fcat
ok         fc_text
ok         fc_empty
ok         fc_oversized
ok         fc_longline
ok         fc_missing
ok         fc_badpath
== fsize ==
build ok   fsize
ok         fs_list
ok         fs_baddir
ok         fs_longpath
ok         fs_missingentry
== fcopy ==
build ok   fcopy
ok         fy_normal
ok         fy_byteexact
ok         fy_exact
ok         fy_oversized
ok         fy_missing
ok         fy_baddst
UTIL_FILE 5 27 0 17
```

Field meaning: `tools=5`, `passed=27` (all scenarios that passed), `failed=0`,
`refused=17` (hostile scenarios correctly refused; a subset of `passed`).
Every build and program invocation is bounded by `timeout 30`. A scenario
passes only when the exit code matches, stderr is empty and stdout matches the
expected bytes exactly (`cmp`); `fy_byteexact` is a direct `cmp` of source and
destination. Fixtures are regenerated
with fixed bytes on every run.

## Fixture generator

```sh
sh examples/utilities/file_tools/fixtures/generate.sh <target-dir>
```

| Fixture | Content / size |
| --- | --- |
| `hello.txt` | `hello\n` (6 bytes) |
| `alpha.txt` | `alpha\nbeta\ngamma\n` (17 bytes) |
| `alpha2.txt` | `ALPHA\nbeta\ngamma\n` (17 bytes) |
| `empty.txt` | 0 bytes |
| `hash_exact.bin` | 4096 zero bytes (fhash exact fit) |
| `hash_over.bin` | 4097 zero bytes (fhash one over) |
| `copy_exact.bin` | 1024 zero bytes (fcopy exact fit) |
| `copy_over.bin` | 1025 zero bytes (fcopy one over) |
| `cat_over.bin` | 2049 zero bytes (fcat byte cap +1) |
| `longline.txt` | one 129-byte line (line cap +1) |
| `manylines.txt` | 33 short lines (line count cap +1) |
| `tree/a.bin` | 10 bytes |
| `tree/b.bin` | 20 bytes |
| `tree/sub/c.bin` | 30 bytes |

All paths below are relative to the repo root; `$F` denotes
`examples/utilities/file_tools/.run/fixtures` and each binary lives in
`examples/utilities/file_tools/.run/`.

## Per-tool commands and exact outputs

### fhash.cp

```sh
B=examples/utilities/file_tools/.run/fhash
F=examples/utilities/file_tools/.run/fixtures
timeout 30 "$B" "$F/hello.txt"
```
```
FHASH f8934173 6
```
Exit 0. `f8934173` is byte-exact FNV-1a-32 of `hello\n` (cross-checked against
an independent Python implementation).

```sh
timeout 30 "$B" "$F/hash_exact.bin"
```
```
FHASH 76efddc5 4096
```
Exit 0: an exact-fit 4096-byte file is hashed through 64 full 64-byte chunks.

Oversized input is refused (exit 1):
```sh
timeout 30 "$B" "$F/hash_over.bin"
```
```
FHASH error oversized code=101
```

Missing file is refused (exit 1):
```sh
timeout 30 "$B" "$F/no-such-file"
```
```
FHASH error missing-file code=109
```

Bad path (a directory) is refused (exit 1):
```sh
timeout 30 "$B" "$F"
```
```
FHASH error bad-path code=109
```

### fdiff.cp

```sh
B=examples/utilities/file_tools/.run/fdiff
F=examples/utilities/file_tools/.run/fixtures
timeout 30 "$B" "$F/alpha.txt" "$F/alpha2.txt"
```
```
FDiff --- a
FDiff +++ b
FDiff @@ -3 +3 @@
FDiff -alpha
FDiff +ALPHA
FDiff  beta
FDiff  gamma
FDiff changes 2
```
Exit 0. Identical files give `FDiff changes 0`. The diff is index-aligned
(not an LCS) and labels are fixed, so no supplied path string leaks into output.

Overlong line, oversized line count, missing file and bad path are refused
(exit 1 each):
```
FDIFF error file::read_line: line longer than the buffer code=101
FDIFF error too-many-lines code=101
FDIFF error missing-file code=109
FDIFF error bad-path code=109
```

### fcat.cp

```sh
B=examples/utilities/file_tools/.run/fcat
F=examples/utilities/file_tools/.run/fixtures
timeout 30 "$B" "$F/alpha.txt"
```
```
FCAT 0 alpha
FCAT 1 beta
FCAT 2 gamma
FCAT size 17 lines 3
```
Exit 0. Empty file prints only `FCAT size 0 lines 0`.

Oversized file, overlong line, missing file and bad path are refused (exit 1):
```
FCAT error file-too-large code=101
FCAT error file::read_line: line longer than the buffer code=101
FCAT error missing-file code=109
FCAT error bad-path code=109
```

### fsize.cp

```sh
B=examples/utilities/file_tools/.run/fsize
F=examples/utilities/file_tools/.run/fixtures
timeout 30 "$B" "$F/tree" a.bin b.bin sub/c.bin
```
```
FSIZE a.bin 10
FSIZE b.bin 20
FSIZE sub/c.bin 30
FSIZE count 3 total 60
```
Exit 0. Relative paths may contain `/`, giving recursive-ish reach with no
directory-scanning API. Sizes come from `stat`; no timestamps are read or
printed.

Non-directory base, overlong path and missing entry are refused (exit 1):
```
FSIZE error not-a-directory code=109
FSIZE error path-too-long code=101
FSIZE error missing-entry code=109
```

### fcopy.cp

```sh
B=examples/utilities/file_tools/.run/fcopy
F=examples/utilities/file_tools/.run/fixtures
timeout 30 "$B" "$F/alpha.txt" "$F/dst.txt"
```
```
FCOPY copied 17
FCOPY verified 17
```
Exit 0. A subsequent `cmp "$F/alpha.txt" "$F/dst.txt"` exits 0 (the
`fy_byteexact` harness check). A 1024-byte exact-fit source prints
`FCOPY copied 1024` / `FCOPY verified 1024`.

Oversized source, missing source and invalid destination (directory) are
refused (exit 1):
```
FCOPY error oversized code=101
FCOPY error missing-file code=109
FCOPY error bad-destination code=109
```

## Refusal matrix (hostile cases)

| Tool | Case | Trigger | Output | Exit | Code |
| --- | --- | --- | --- | --- | --- |
| fhash | oversized input | 4097-byte file | `FHASH error oversized code=101` | 1 | 101 |
| fhash | missing file | nonexistent path | `FHASH error missing-file code=109` | 1 | 109 |
| fhash | bad path | directory as file | `FHASH error bad-path code=109` | 1 | 109 |
| fdiff | oversized input | 129-byte line | `FDIFF error file::read_line: line longer than the buffer code=101` | 1 | 101 |
| fdiff | oversized input | 33 lines | `FDIFF error too-many-lines code=101` | 1 | 101 |
| fdiff | missing file | nonexistent path | `FDIFF error missing-file code=109` | 1 | 109 |
| fdiff | bad path | directory as file | `FDIFF error bad-path code=109` | 1 | 109 |
| fcat | oversized input | 2049-byte file | `FCAT error file-too-large code=101` | 1 | 101 |
| fcat | oversized input | 129-byte line | `FCAT error file::read_line: line longer than the buffer code=101` | 1 | 101 |
| fcat | missing file | nonexistent path | `FCAT error missing-file code=109` | 1 | 109 |
| fcat | bad path | directory as file | `FCAT error bad-path code=109` | 1 | 109 |
| fsize | bad path | file as base dir | `FSIZE error not-a-directory code=109` | 1 | 109 |
| fsize | oversized input | 129-char rel path | `FSIZE error path-too-long code=101` | 1 | 101 |
| fsize | missing file | nonexistent entry | `FSIZE error missing-entry code=109` | 1 | 109 |
| fcopy | oversized input | 1025-byte source | `FCOPY error oversized code=101` | 1 | 101 |
| fcopy | missing file | nonexistent source | `FCOPY error missing-file code=109` | 1 | 109 |
| fcopy | bad path | directory as destination | `FCOPY error bad-destination code=109` | 1 | 109 |

## Checked-API notes / strict-mode findings

* `argv` is an FFI pointer: direct `argv[i]` is rejected by `--strict`
  (`CPLUS-FFI: unverifiable extent`). Every tool copies the slot with
  `__builtin_memcpy(&value, argv + i, sizeof(value))` first.
* The same rule rejects subscripting an argv-derived pointer (`path[0]`);
  path emptiness is tested with `strlen(path) == 0` instead.
* Passing a 2-D line buffer as a function parameter also loses provable extent,
  so `fdiff` uses two loaders that touch their own fixed globals directly
  (the same pattern as `examples/expanded/cli/text_tools.cp`).
* `error`, `file`, `string` and the POSIX-style `fs_*` helpers all come from the
  checked stdlib headers. Reads use `file::read` / `file::read_line` with an
  `error *`; writes use `file::write_bytes`; sizes use `fs_size`; path joining
  uses the owned `string::append`; line comparison uses `string::equals`.
* An overlong `read_line` result surfaces `CPLUS_EXIT_BOUNDS` (101) with the
  stable library message `file::read_line: line longer than the buffer`; the
  tools propagate that error instead of truncating.
* Exact-fit versus one-over is proved with a real size cap: `fhash` accepts
  4096 bytes and refuses 4097; `fcopy` accepts 1024 and refuses 1025; `fcat`
  guards 2048; `fdiff` caps line length, line count and total bytes.
* `printf` format types are checked under `--strict`: `fs_size` returns
  `long long`, so output uses `%lld` (not `%ld`).
* No wall-clock, random or environment-derived values affect any output.

## Verification evidence

Every command and output block above was observed by running it on this host;
nothing was inferred from a build result alone. The full harness was run
repeatedly and produced the identical `UTIL_FILE 5 27 0 17` summary each time,
with zero build-log bytes and zero scenario failures. No passes were
fabricated.
