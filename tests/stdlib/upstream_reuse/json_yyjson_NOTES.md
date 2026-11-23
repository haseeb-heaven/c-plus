# Upstream yyjson reuse — JSON hardening integration notes

Branch: `agent/modern-stdlib`. Date: 2026-11-23.

## 1. Pin and local inventory

| Item | Value |
|---|---|
| Upstream | `ibireme/yyjson` <https://github.com/ibireme/yyjson> |
| Pinned commit | `be9bd74bb3c96e00006c2075c330a6b8b90123dc` |
| Version | `0.13.0` (`YYJSON_VERSION_STRING`; also `docs/libc/DEPENDENCY_PROVENANCE.md` row 3) |
| License | MIT (`third_party/yyjson/LICENSE`, YaoYuan 2020) |
| Vendored files | `third_party/yyjson/yyjson.c`, `yyjson.h`, `LICENSE` only |
| Local archive | `build/ext/libyyjson.a` (compiled by `tests/stdlib/run_stdlib_tests.sh:78-79`) |
| Link operands | `-Ithird_party/yyjson -Lbuild/ext -lyyjson` — always **separate** operands |

There is **no `test/` directory in the vendored tree**. The upstream tests were
therefore read from the exact pinned commit over the raw GitHub endpoint (the
GitHub trees API returned HTTP 403 on this host; `github.com/.../tree/<sha>/test`
listed the directory). Files inspected:

- `test/test_err_code.c` at `be9bd74b`
- `test/test_json_reader.c` at `be9bd74b`

Both were used as *evidence and technique sources only* — no upstream test file
or upstream implementation body was copied into this repository.

## 2. Exact upstream source lines (evidence)

### 2.1 Depth and size limits in the vendored source

| File:line | Content / meaning |
|---|---|
| `third_party/yyjson/yyjson.h:113-117` | `YYJSON_READER_DEPTH_LIMIT`; comment "Define to an integer to set a depth limit for reading nested arrays/objects. 0 disables the policy limit."; default `#define YYJSON_READER_DEPTH_LIMIT 0`. |
| `third_party/yyjson/yyjson.c:350-356` | Same two defaults in the implementation (`READER`/`WRITER` = 0). |
| `third_party/yyjson/yyjson.c:455` | `#define MSG_DEPTH "depth limit exceeded"`. |
| `third_party/yyjson/yyjson.c:5528-5562`, `5689-5692`, `5895` | Reader's `ctn_depth` counter, `ctn_depth >= YYJSON_READER_DEPTH_LIMIT` check, `fail_depth` → `return_err(cur, DEPTH, MSG_DEPTH)`. Equivalent checks recur at `5987-5990`, `6138-6141`, `6367` and `6985-6988`, `7102-7105`, `7303`. |
| `third_party/yyjson/yyjson.c:6396-6399` | `yyjson_read_opts()` parameter validation: NULL data → `INVALID_PARAMETER` "input data is NULL"; zero length → `INVALID_PARAMETER` "input length is 0". |
| `third_party/yyjson/yyjson.c:6401-6417` | Non-INSITU path allocates `len + YYJSON_PADDING_SIZE`; line 6407 guard `len >= USIZE_MAX - YYJSON_PADDING_SIZE` → `MEMORY_ALLOCATION`. |
| `third_party/yyjson/yyjson.c:5532-5537` | Initial value-buffer estimate `alc_len = hdr_len + dat_len / YYJSON_READER_ESTIMATED_MINIFY_RATIO (6) + 4` — parses grow by input size even without recursion. |
| `third_party/yyjson/yyjson.h:925-970` | `yyjson_read_code` constants, including `YYJSON_READ_ERROR_DEPTH = 15` (line 967) and the `yyjson_read_err { code, msg, pos }` struct. |
| `third_party/yyjson/yyjson.h:1185`, `1941-1949` | `yyjson_read_max_memory_usage()`, `yyjson_doc_get_read_size()` / `yyjson_doc_get_val_count()`; the latter documents that `[1,2,3]` has **4** values (root included). |
| `third_party/yyjson/yyjson.h:655` | `#define YYJSON_PADDING_SIZE 4`. |

**Finding:** the compiled archive has the depth cap *disabled*, so no caller can
ask yyjson to stop at a nesting bound. This matches the pre-existing documented
limitation in `tests/security/resource_exhaustion/re_json_nesting.cp:1-13`.

### 2.2 Upstream test lines at the pinned commit

`test/test_err_code.c`:

- `:39-53` — NULL input and zero length must produce
  `YYJSON_READ_ERROR_INVALID_PARAMETER`, `err.pos == 0`.
- `:124-170` — `truncated_single_values[]` (`"-"`, `"-1."`, `"123e"`, `"t"`,
  `"tru"`, `"fals"`, `"nul"`, …): every one returns
  `YYJSON_READ_ERROR_UNEXPECTED_END` with `err.pos == len` (asserts at `:157-158`).
- `:203-240` — `valid_jsons[]`; `:219-238` loops `l = 1..len` and asserts every
  proper prefix is `UNEXPECTED_END` while the full text is `SUCCESS`.
- `:346-372` — invalid numbers (`"123.e12"`, `"000"`) → `INVALID_NUMBER`.
- `:587-603` — the depth case, guarded by `#if YYJSON_READER_DEPTH_LIMIT`:
  builds `n = YYJSON_READER_DEPTH_LIMIT + 1` nested arrays (`n * 2 + 1` bytes)
  and asserts `err.code == YYJSON_READ_ERROR_DEPTH`. It is **compiled out** in
  this vendored build, which is why the C+ wrapper supplies its own budget.
- Writer-side depth case at `:698` is likewise compiled out
  (`YYJSON_WRITER_DEPTH_LIMIT == 0`).

`test/test_json_reader.c`:

- `:53-61`, `:102-110`, `:176-184`, `:685-691` — success requires
  `yyjson_doc_get_read_size(doc) > 0` **and** `yyjson_doc_get_val_count(doc) > 0`;
  failure requires both to be 0 and `err.code != SUCCESS`, `err.msg != NULL`.
- `:94` — `yyjson_read_max_memory_usage(len, flg)` used to size a bounded pool.
- `:299-301` — the three hostile lengths:
  `yyjson_read_opts(NULL, 0, ...)`, `("1", 0, ...)`, `("1", SIZE_MAX, ...)`
  must all return NULL.
- `:317-325` — allocation-failure checks with a 64-byte pool allocator.

## 3. What was ported into `stdlib/json.hp`

A **pre-parse depth + length budget**, enforced before yyjson allocates:

- `stdlib/json.hp:42-47` — `CPLUS_JSON_MAX_DEPTH` (default 16384) and
  `CPLUS_JSON_MAX_INPUT_LENGTH` (default 64 MiB), both `-D`-overridable.
  Defaults exceed the largest checked-in fixtures (4096 levels / 8193 bytes
  in `json_deep_4096.json`; 1 MiB in `json_large_1mib.json`), so no existing
  caller behavior changes.
- `stdlib/json.hp:53-95` — `json_input_within_budget()`: rejects NULL/zero
  length (mirroring `yyjson.c:6398-6399`), rejects
  `len >= SIZE_MAX - YYJSON_PADDING_SIZE` (mirroring `yyjson.c:6407`) and
  `len > CPLUS_JSON_MAX_INPUT_LENGTH`, then counts `{`/`[` outside JSON
  strings with escape tracking, rejecting when `depth > max_depth`. The
  brace/bracket count mirrors the `ctn_depth` accounting at
  `yyjson.c:5528-5562`. Bytes are read with `__builtin_memcpy(&ch, text+i, 1)`,
  the repo's strict-mode-safe pattern (`stdlib/error.hp:686-695`,
  `stdlib/file.hp:107-112`).
- `stdlib/json.hp:239` — the existing `json(const char *, error *)` constructor
  now runs the budget check with default limits before calling
  `yyjson_read_opts()`. **Signature and behavior are unchanged** for NULL
  input (still a silent no-op, per `deep_json_task5.cp:143-151`).
- `stdlib/json.hp:286-298` — new additive `json_parse_budget(text, len,
  max_depth, err)`: explicit-length parse with a caller-supplied depth cap;
  input is not modified and need not be NUL-terminated.
- `stdlib/json.hp:256-264` — new additive `json::read_size()` and
  `json::value_count()`, exposing the two upstream metrics that
  `test_json_reader.c` asserts on.

**Public API / ABI:** purely additive. `json` still contains exactly one
`yyjson_doc *` data member; `json_view`, `json_builder`, all existing method
signatures, all error categories/codes and all existing call sites are
unchanged. No game/compiler/runtime or `third_party/` source was edited.

## 4. Regression test

`tests/stdlib/upstream_reuse/json_yyjson.cp` (287 lines) is self-checking;
each check prints `<tag> <0|1>` and the last line is `JSON_YYJSON <checks>
<failures>`. It exits 0 only with zero failures. Coverage:

| Group | Checks | Upstream technique mirrored |
|---|---|---|
| Direct `yyjson_read_opts()` error codes | 14 | `test_err_code.c:39-53,124-170,346-372,587-603`; truncation, bad numbers/structure/characters/strings, trailing content, NULL/0/`SIZE_MAX` length |
| Wrapper malformed / truncated input | 8 + prefix loop | `test_err_code.c:203-240` (all proper prefixes of `[[[{}]]]` rejected, full document accepted) |
| Depth budget | 6 | `test_err_code.c:587-603`; includes depth-inside-strings and escaped-quote cases |
| Explicit length / size budget | 4 | `test_json_reader.c:299-301`; `SIZE_MAX` is refused without allocating |
| Default budget and large in-budget input | 3 | 1024 levels accepted, 16385 rejected; 8192-element array with upstream read/value-count metrics |

Note: upstream counts the root value, so an 8192-element array has 8193
values (`yyjson.h:1946-1949`); the test pins that explicitly.

## 5. Builds and results

Commands run from the repository root (operands kept separate):

```sh
./build/c+ build tests/stdlib/upstream_reuse/json_yyjson.cp \
  --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/stdlib-tests/json_yyjson.strict
./build/stdlib-tests/json_yyjson.strict
```

Result: build exit 0; run exit 0; **`JSON_YYJSON 35 0`** (35 checks, 0 failures,
empty stderr). Two benign clang driver warnings about `-lyyjson`/`-Lbuild/ext`
during an internal compile probe; the final link succeeds and the binary runs.

Existing JSON tests were rebuilt with the same strict/no-sanitizer operands
against the hardened header to confirm no regression:

| Test (source) | Build | Run | Result |
|---|---|---|---|
| `tests/stdlib/json_test.cp` | 0 | 0 | expected output, empty stderr |
| `tests/stdlib/deep_json_task5.cp` | 0 | 0 | `JSON_TASK5 23 0` |
| `tests/security/hostile_inputs/hostile_json.cp` (fixture dir arg) | 0 | 0 | `HOSTILE_JSON 29 0` |
| `tests/security/resource_exhaustion/re_json_nesting.cp` | 0 | 0 | `RESOURCE_EXHAUSTION 7 0` |

All four have empty stderr; in particular the 4096-level and 1 MiB hostile
fixtures still parse, and NULL-input error semantics are unchanged.

### UBSan status

`--strict --sanitize=undefined` **cannot be built on this host** — the driver
fails while compiling its own instrumented runtime, before any of my source is
compiled:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23: error:
  use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:137:21: error:
  use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

This is environmental and pre-existing: the unmodified
`tests/stdlib/json_test.cp` fails identically with `--sanitize=undefined`.
Per the strict file-ownership rule no compiler/runtime file was touched.

As supplementary evidence, the vendored parser itself was compiled directly
with `clang -fsanitize=undefined,alignment -fno-sanitize-recover=all` (with
`-isysroot $(xcrun --show-sdk-path)`) and run over the same hostile corpus
including 16385 balanced nesting levels: build exit 0, run exit 0,
`YYJSON_UBSAN_SMOKE ok`, empty stderr — no undefined-behaviour diagnostics.
That smoke driver and its objects were kept outside the repository in the
session temp directory.

## 6. Limitations

- The depth cap is enforced by a lexical pre-scan of bracket/brace characters,
  not by yyjson's own container state machine. It counts containers inside
  strings correctly (with escape tracking) but deliberately does not attempt a
  full JSON grammar; malformed-but-shallow input is still diagnosed by yyjson.
- Default budgets are generous by design. Deployments that need a tighter bound
  should rebuild with `-DCPLUS_JSON_MAX_DEPTH=...` and/or
  `-DCPLUS_JSON_MAX_INPUT_LENGTH=...`; the regression test assumes defaults and
  should be adjusted if those macros are overridden.
- `json_parse_file()` still goes through `yyjson_read_file()` and therefore does
  not apply the lexical depth budget to file input (its length guard is
  provided by the file API). File-path errors keep their existing IO category.
- No live-game / injected-DLL claim is made; this is a C+ standard-library
  hardening change verified by the C+ compiler and its self-checking tests.
