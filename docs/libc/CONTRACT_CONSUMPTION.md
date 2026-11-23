# Which libc contracts does the compiler actually consume?

Status: **measured report**. This document records what `./build/c+ check --strict`
actually emitted on branch `agent/modern-stdlib`. It does not edit the compiler,
the runtime, the contracts, or any existing test. The machine check is
`tests/libc/test_contract_consumption.py`.

## Why this question is separate from schema alignment

`tests/libc/test_compiler_contract_alignment.py` proves only that
`libc/contracts/compiler_libc_contracts.json` has the nine requested keys, the
expected API names, and diagnostic strings drawn from the five requested
`CPLUS-SAFE-*` categories. That is **schema alignment**, not evidence that a
compiler reads the file.

This branch does **not** contain the file those keys were written against:

```
compiler/Analysis/:
  BoundsAnalysis.cpp        (15 bytes, stub)
  OwnershipAnalysis.cpp     (15 bytes, stub)
compiler/Analysis/libc_contracts.json  -> ABSENT
```

Therefore the only direct evidence available here is *tagged diagnostic output*
from the real compiler binary. Strings embedded in `build/c+` (for example the
bare words `CPLUS-SAFE-CRT` or `CPLUS-SAFE-REALLOC` in its constant table) are
**not** treated as evidence of consumption. Untagged rejections are recorded but
are also **not** counted as consumption of a specific contract.

## Method

The script performs four steps on every run:

1. Load `compiler_libc_contracts.json`; report counts by diagnostic category and
   by a derived API family, with exact denominators (`n / total`).
2. For every declared category tag, search `tests/stdlib/compile_time/*.json`,
   `tests/stdlib/compile_time/*.cp`, and every text file in
   `tests/compile_fail/` for a literal reference such as `[CPLUS-SAFE-BOUNDS]`.
3. Run `./build/c+ check --strict` on five representative unsafe snippets, one
   per declared category. Each snippet is derived from the contract entry named
   in its probe metadata; the exact declared diagnostic string is looked up from
   the live JSON at run time rather than invented. A category is marked
   `compiler_consumption` **only if its exact tag appears in real compiler output**.
4. Print one machine-readable JSON document with the headline denominators.

Snippets live in a `TemporaryDirectory` and are deleted after the run.

### Honesty rules applied

- `compiler_consumption` requires literal tagged output from `build/c+`.
- A rejection carrying a *different* tag is reported in `tagged_diagnostics`
  but does not upgrade the verdict.
- Per-API rollups project category evidence onto APIs declaring the same tag;
  they do **not** claim the compiler loaded a per-API contract entry.
- Counts are computed from the JSON at run time. The contract file is being
  extended by parallel work on this branch, so the numbers below are the
  measured values for the run recorded at the end of this document, not a
  promise about a future file state.
- No percentages are reported; only exact fractions.

## Results (measured)

| Diagnostic category | Declared by (APIs / strings) | Test files referencing tag | Probe output tags | Verdict |
| --- | --- | --- | --- | --- |
| `[CPLUS-SAFE-BOUNDS]` | 6 / 6 | 2 | `[CPLUS-BOUNDS]`, `[CPLUS-SAFE-BOUNDS]` | **compiler consumption** |
| `[CPLUS-SAFE-LIFETIME]` | 7 / 7 | 4 | `[CPLUS-LIFETIME]`, `[CPLUS-SAFE-LIFETIME]` | **compiler consumption** |
| `[CPLUS-SAFE-FREE]` | 2 / 2 | 0 | `[CPLUS-OWNER]` only | schema alignment only |
| `[CPLUS-SAFE-CRT]` | 6 / 6 | 0 | `[CPLUS-FFI]`, `[CPLUS-BOUNDS]`, `[CPLUS-SAFE-BOUNDS]` | schema alignment only |
| `[CPLUS-SAFE-REALLOC]` | 2 / 2 | 0 | `[CPLUS-ALIAS]` only | schema alignment only |

Exact headline denominators from the final run:

```json
{
  "apis_mixed_evidence": "6/16",
  "apis_schema_alignment_only": "4/16",
  "apis_with_any_tagged_output": "12/16",
  "apis_with_diagnostics": "16/38",
  "apis_without_diagnostics": "22/38",
  "categories_schema_alignment_only": "3/5",
  "categories_with_compiler_consumption_evidence": "2/5",
  "categories_without_probe": "0/5",
  "contract_apis": 38,
  "diagnostic_categories": 5,
  "probes_rejected_under_any_tag": "5/5",
  "probes_run": "5/5",
  "probes_with_expected_tag": "2/5"
}
```

### 1. Categories with evidence of compiler consumption (2 / 5)

- **`[CPLUS-SAFE-BOUNDS]`** — a constant out-of-bounds store produces
  `error: [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 5 exceeds length 3`.
  The exact contract tag is present. Test references:
  `tests/stdlib/compile_time/vector.json`,
  `tests/stdlib/compile_time/vector_const_oob.cp`.
- **`[CPLUS-SAFE-LIFETIME]`** — a dereference after `free()` produces
  `error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: dereference of
  released pointer 'p'`. The exact contract tag is present. Test references:
  `error.json`, `file.json`, `string.json`, and
  `collections_dangling_key.cp` under `tests/stdlib/compile_time/`.

Note that the observed message text is not the literal contract string
(`copy of N bytes exceeds destination <var>` / `use-after-free`): the compiler
matches the **category tag**, not the full phrasing. That is still direct
evidence that the tag is consumed.

### 2. Categories with schema alignment only (3 / 5)

- **`[CPLUS-SAFE-FREE]`** — a double `free(p)` is rejected with
  `error: [CPLUS-OWNER] double free of 'p'`. The compiler rejects the bug but
  emits no `[CPLUS-SAFE-FREE]` tag, and no file in the scanned test corpora
  references that tag. This matches
  `docs/libc/AGENT1_COMPILER_GAPS.md`, where `[CPLUS-SAFE-FREE]` for double
  release via `_drop`/`_close` is still a *proposed* diagnostic.
- **`[CPLUS-SAFE-CRT]`** — `strcpy` into a 4-byte buffer is rejected with
  `[CPLUS-FFI]` (Clang string overflow) and
  `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] string copy of 13 chars plus NUL exceeds
  destination 'dst'`. The violation is enforced, but under the BOUNDS tag,
  not the declared CRT tag; no test corpus references `[CPLUS-SAFE-CRT]`.
- **`[CPLUS-SAFE-REALLOC]`** — reallocating and then writing through the stale
  alias yields `error: [CPLUS-ALIAS] realloc result and original pointer require
  explicit success/failure handling; alias state is unknown`. There is no
  `[CPLUS-SAFE-REALLOC]` output and no test reference.

All three probes exited non-zero (`5/5` probes rejected), so "schema alignment
only" here means *the declared tag is not what the compiler emits*, not "the
compiler accepts the bug".

### 3. Contracts with no diagnostic claim at all (22 / 38 APIs)

22 of the 38 APIs in the contract declare an empty `diagnostics` array
(read-only string and comparison functions, most format and stream APIs,
`malloc`, `calloc`, `fwrite`, and others). There is no diagnostic contract to
consume for these entries; their `dynamic` fields describe runtime checks only.

## Per-API projection (16 APIs declare at least one diagnostic)

| Rollup | Count | APIs |
| --- | --- | --- |
| `compiler_consumption` | 6 / 16 | memcpy, memmove, memset, memcmp, fread, memcpy_s |
| `schema_alignment_only` | 4 / 16 | strcpy, strncpy, strcat, strncat |
| `mixed` (some categories emitted, some not) | 6 / 16 | realloc, free, strcpy_s, snprintf_s, realloc_s, free_s |

For the `mixed` APIs the emitted evidence comes from the LIFETIME probe, and the
missing evidence from the FREE / CRT / REALLOC probes. This is a category-level
observation, not proof that the compiler loaded those specific entries.

## How to run

```sh
python3 tests/libc/test_contract_consumption.py            # report + baseline check
python3 tests/libc/test_contract_consumption.py --report-only   # report, always exit 0
```

Output is a single JSON object on stdout. Exit status is 0 when the five
category verdicts and the two branch-context facts match `FROZEN_BASELINE` in
the script, otherwise 1 with a `contract consumption baseline drift` message on
stderr. A drift means someone should re-run the probes and update this document
together with the baseline; it does not mean the compiler was modified by this
test.

The checks are explicit (no bare `assert`), so `python3 -O` behaves identically,
and paths resolve from `__file__`, so the script works from any cwd.

## Final-run provenance

Recorded on branch `agent/modern-stdlib` with
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+` (Mach-O arm64) and
`libc/contracts/compiler_libc_contracts.json` at 86196 bytes, mtime
2026-11-23 05:46:20. The script exited 0. No compiler, runtime, contract, or
existing test file was modified by this work.
