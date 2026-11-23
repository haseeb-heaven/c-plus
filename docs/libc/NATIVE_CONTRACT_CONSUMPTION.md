# Native C+ contract-consumption report

`tests/libc/test_contract_consumption.cp` is the native C+ replacement for the
Python reporting test `tests/libc/test_contract_consumption.py` (which remains
in the tree and is not modified). It answers the same question — *is there any
tagged compiler output that corresponds to a `[CPLUS-SAFE-*]` diagnostic
declared in `libc/contracts/compiler_libc_contracts.json`?* — using only C+
libraries and the hardened libc.

## Implementation

- **Contract parsing:** `stdlib/json.hp` (`json_parse_file` over yyjson). The
  rewriter lowers methods only on a *named* receiver, so each `json_view` is a
  separate local and chained `member()/at()` calls are assigned to locals first.
- **Categories:** each diagnostic string is scanned for the literal pattern
  `[CPLUS-SAFE-` + uppercase letters + `]` (equivalent to Python's `TAG_RE`).
  Counts by category, by API and derived family carry exact denominators.
- **Probes:** five fixed unsafe snippets (double free, use-after-free, constant
  out-of-bounds, string copy overflow, stale realloc alias) are written with
  `native_write` and executed through `cplus_system` with stdout/stderr
  redirected to `build/native/probes/probe_N.{out,err}`. The command is
  `build/c+ check --strict <snippet>` wrapped as
  `{ <cmd> ; } >"<out>" 2>"<err>"` (the shared `native_run_to` helper's `{%s}`
  form is not valid shell syntax, so the redirection is built locally while
  still using `cplus_system`).
- **Evidence rule:** `captured output` is scanned for literal `[CPLUS-...]`
  tags with the same grammar as the Python `ANY_TAG_RE`. A category is
  `compiler_consumption` **only** if its exact tag appears in real compiler
  output. Otherwise it is a capability gap, reported as `GAP` /
  `capability_gap: true` with verdict `schema_alignment_only` (or `no_probe`).
  Rejection under a different tag (e.g. `[CPLUS-OWNER]`) is recorded but never
  counted as consumption.
- **Output:** machine-readable report at `build/native/contract_consumption.json`
  written via `stdlib/file.hp` with manual JSON escaping; compact single-line
  JSON on stdout.
- **Exit status:** 0 when the report was produced, even when capability gaps
  exist. Non-zero (2) **only** on execution failure: unreadable contract,
  unwritable snippet/report, or failed capture. Missing compiler is a GAP, not
  an execution failure. The Python `FROZEN_BASELINE` drift check is deliberately
  not reproduced; a change in real compiler behaviour must be reviewed, not
  silently fail an evidence report.

Two shared-helper quirks are handled locally: `native_read` measures `fread`
with `strlen`, so all destination buffers are zero-filled before each read
(otherwise stale bytes from a previous probe are counted); and the C+ rewriter
needs a named receiver plus non-chained calls for `json_view` methods.

## Verification (branch `agent/modern-stdlib`, repo root)

Build (exact command):

```sh
./build/c+ build tests/libc/test_contract_consumption.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -Ilibc/install-include \
  -o /tmp/contract_consumption -- build/libc/libcplus_hardened.a
```

Result: `BUILD_RC=0`. The only diagnostics are benign backend warnings
(`-lyyjson`/`.a` reported as linker-input-unused during the compile stage);
there are no C+ or C errors.

Run and exit status:

```sh
/tmp/contract_consumption
echo $?
```

Exact stdout (one line):

```json
{"status":"ok","contract_apis":38,"apis_with_diagnostics":"16/38","diagnostic_categories":5,"categories_with_compiler_consumption":"2/5","categories_gap":"3/5","probes_run":"5/5","probes_with_expected_tag":"2/5","gaps":["[CPLUS-SAFE-CRT]","[CPLUS-SAFE-FREE]","[CPLUS-SAFE-REALLOC]"]}
```

`RUN_RC=0`, stderr empty. `build/native/contract_consumption.json` parses as
valid JSON (`python3 -m json.tool` / `json.load`).

Execution-failure path (run from a directory where the relative contract path
does not exist):

```sh
cd /tmp && /tmp/contract_consumption
```

Result: `RC=2`, stderr `execution failure: cannot parse contract libc/contracts/compiler_libc_contracts.json`, no stdout. Capability gaps alone never
produce a non-zero exit.

### Equivalence with the Python report

The Python report was captured with:

```sh
python3 tests/libc/test_contract_consumption.py --report-only > /tmp/py_contract_report.json
```

The `headline` objects from both reports are byte-for-byte equal after JSON
normalisation:

| headline key | value |
| --- | --- |
| `contract_apis` | 38 |
| `apis_with_diagnostics` | 16/38 |
| `apis_without_diagnostics` | 22/38 |
| `diagnostic_categories` | 5 |
| `categories_with_compiler_consumption_evidence` | 2/5 |
| `categories_schema_alignment_only` | 3/5 |
| `categories_without_probe` | 0/5 |
| `apis_with_any_tagged_output` | 12/16 |
| `apis_schema_alignment_only` | 4/16 |
| `apis_mixed_evidence` | 6/16 |
| `probes_run` | 5/5 |
| `probes_with_expected_tag` | 2/5 |
| `probes_rejected_under_any_tag` | 5/5 |

Also verified equal: the full `contract_counts` object, per-probe
`declared_diagnostic`, `rejected`, `expected_tag_emitted`, and
`tagged_diagnostics` lists, and every category verdict
(`BOUNDS`/`LIFETIME` = `compiler_consumption`; `FREE`/`CRT`/`REALLOC` =
`schema_alignment_only`). Test-reference existence also matches.

Observed tag evidence (real compiler output, never inferred from strings in the
binary or schema):

| probe | literal tags in output | expected tag emitted |
| --- | --- | --- |
| double free | `[CPLUS-OWNER]` | no |
| use after free | `[CPLUS-LIFETIME]`, `[CPLUS-SAFE-LIFETIME]` | yes |
| constant out of bounds | `[CPLUS-BOUNDS]`, `[CPLUS-SAFE-BOUNDS]` | yes |
| string copy overflow | `[CPLUS-FFI]`, `[CPLUS-BOUNDS]`, `[CPLUS-SAFE-BOUNDS]` | no |
| stale realloc alias | `[CPLUS-ALIAS]` | no |

Differences from Python are intentional and constrained: the native report adds
`gaps`/`capability_gap` and `compiler_probe_index`, does not implement the
baseline-drift failure exit, and uses a fixed `build/native/probes` directory
instead of `TemporaryDirectory`. No compiler, contract, corpus or test file is
modified at runtime; only `build/native/` artefacts are written.
