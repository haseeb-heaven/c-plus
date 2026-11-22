# Compile-time stdlib capability-gap catalog (report only — no compiler edits)

This catalog synthesizes **all** manifests in `tests/stdlib/compile_time/*.json` into one
honest list of what the C+ compiler currently enforces and what it does not. It is a
report for Agent 1 (compiler owner). It does not modify `compiler/**`, `runtime/**`, or
the manifests, and it does **not** re-run the compiler: every observation below is the
result recorded in the manifests at the time they were written.

> **Addendum (post-catalog change).** `json.json` was added after this catalog was
> generated. Current on-disk totals are **8 manifests / 52 cases**, not 7 / 46:
> expected REJECT 9, ACCEPT 19, RUNTIME 24; observed (normalized) tagged REJECT 5,
> untagged PARTIAL 7, ACCEPT/compiles-clean 40. The six added JSON cases are
> `json_valid` (ACCEPT), `json_valid_ref` (ACCEPT), `json_access_after_destroy`,
> `json_builder_after_destroy`, `json_dangling_ref` (all RUNTIME, observed ACCEPT),
> and `json_invalid_transfer` (RUNTIME, observed untagged PARTIAL). The JSON family
> contributes **zero tagged REJECTs** and one extra tagged-diagnostic-tagging gap,
> and its report that including `stdlib/json.hp`/`yyjson.h` suppresses the AST safety
> pass is recorded in `json.json`. All per-family totals and the grand totals in the
> tables below are the original 46-case snapshot; use the addendum numbers for current
> status. No gap is counted as a pass in either snapshot.

**Reading rule — do not misread this document:** an `ACCEPT` in the *Observed* column
means "the compiler accepted the file," i.e. the safety property is **not** proven at
compile time. No gap case in this catalog passes compile-time enforcement. `RUNTIME`
cases are compile-time capability gaps whose behavior is covered by **separate runtime
suites**; this catalog makes no claim that those runtime checks pass or that the
programs are memory-safe beyond what the manifests state.

## 1. Manifest inventory and verification

All 7 manifests present at generation time were confirmed to exist and to parse
as JSON (an 8th, `json.json`, was added later — see addendum above). Case counts were
computed from the parsed `cases` arrays (not from the manifests' own `summary`
fields, which in two places disagree with their per-case notes — see §2).

| Manifest | Family (derived from filename/header/module/suite) | Cases | Expected REJECT | Expected ACCEPT | Expected RUNTIME |
|---|---|---:|---:|---:|---:|
| `error.json` | error (`stdlib/error.hp`) | 5 | 2 | 2 | 1 |
| `thread.json` | thread (`stdlib/thread.hp`) | 6 | 0 | 2 | 4 |
| `collections.json` | collections (`stdlib/collections.hp`) | 6 | 0 | 2 | 4 |
| `vector.json` | vector (`stdlib/vector.hp`) | 8 | 0 | 3 | 5 |
| `net.json` | net (`stdlib/net.hp`) | 5 | 1 | 2 | 2 |
| `string.json` | string | 8 | 4 | 3 | 1 |
| `file.json` | file (`stdlib/file.hp`) | 8 | 2 | 3 | 3 |
| **Total** | | **46** | **9** | **17** | **20** |

The manifests use different command paths (`c+ check --strict <file>` /
`c+ build --strict --sanitize=none <file> -o <out>`, `./build/c+ ...`, and `./build/c+`
with `CPLUS_CLANG=/usr/bin/clang` in `net.json`). These differences are recorded, not
reconciled; they do not change the classification rule below.

## 2. Classification rule (applied uniformly)

- **Only diagnostics containing a `[CPLUS-` token count as tagged C+ enforcement.**
  Plain Clang warnings, sysroot/linker noise, and untagged C+ lowering errors are
  **not** C+ enforcement and are **never** counted as tagged rejections.
- Normalized *Observed* values used in the table:
  - **REJECT** — both commands fail **with** a `[CPLUS-` diagnostic (strict enforcement).
  - **PARTIAL** — both commands fail, but the rejection diagnostic is **untagged**
    (genuine compile-time rejection; no `[CPLUS-` token, so it does not count as
    tagged enforcement).
  - **ACCEPT** — both commands succeed; no rejection. For a RUNTIME-expected case,
    this is the gap itself: the violation compiles cleanly.
- Two manifest-level inconsistencies are resolved honestly in favor of the *notes*
  and the stated diagnostic policy:
  - `net_use_after_transfer.cp` has `"observed": "REJECT"`, but its only diagnostic
    is `tagged: false`, and `GAP-NET-03` says it would need reclassification if a
    literal `[CPLUS-` prefix is required. Recorded here as **PARTIAL (untagged)**.
    `net.json`'s own summary counts it as `reject: 1` with
    `tagged_cplus_diagnostics_observed: 0`, which is internally contradictory.
  - `file_invalid_borrow.cp` has `"observed": "REJECT"`, but its note says
    "the strict harness reports PARTIAL" and `file.json`'s summary says
    `observed_partial: 2, observed_reject: 0`. Recorded here as **PARTIAL (untagged)**.
- `net_use_after_close.cp` / `net_double_close.cp` have `"observed": "RUNTIME"`
  with `check_exit: 0`, `build_exit: 0` and no diagnostics; they are recorded as
  **ACCEPT** at compile time (the manifest's `RUNTIME` means "gap," not a different
  harness outcome).

## 3. Full case table

Tag values: **`[CPLUS-`** = tagged [CPLUS- diagnostic; **`untagged`** = C+ lowering
error with no `[CPLUS-` token; **`none`** = no diagnostic emitted.

| Family | File | Expected | Observed (normalized) | Tag |
|---|---|---|---|---|
| error | `error_use_after_reset.cp` | RUNTIME | ACCEPT | none |
| error | `error_invalid_lifecycle.cp` | REJECT | REJECT | `[CPLUS-` |
| error | `error_null_pointer.cp` | REJECT | REJECT | `[CPLUS-` |
| error | `error_valid.cp` | ACCEPT | ACCEPT | none |
| error | `error_valid_reset.cp` | ACCEPT | ACCEPT | none |
| thread | `thread_double_unlock.cp` | RUNTIME | ACCEPT | none |
| thread | `thread_unlock_without_ownership.cp` | RUNTIME | ACCEPT | none |
| thread | `thread_guard_after_destroy.cp` | RUNTIME | ACCEPT | none |
| thread | `thread_invalid_mutex_owner.cp` | RUNTIME | ACCEPT | none |
| thread | `thread_valid_guard.cp` | ACCEPT | ACCEPT | none |
| thread | `thread_valid_lock.cp` | ACCEPT | ACCEPT | none |
| collections | `collections_use_after_drop.cp` | RUNTIME | ACCEPT | none |
| collections | `collections_use_after_move.cp` | RUNTIME | PARTIAL | untagged |
| collections | `collections_dangling_key.cp` | RUNTIME | ACCEPT | none |
| collections | `collections_mutable_alias.cp` | RUNTIME | ACCEPT | none |
| collections | `collections_valid.cp` | ACCEPT | ACCEPT | none |
| collections | `collections_valid_borrow.cp` | ACCEPT | ACCEPT | none |
| vector | `vector_const_oob.cp` | RUNTIME | ACCEPT | none |
| vector | `vector_use_after_drop.cp` | RUNTIME | ACCEPT | none |
| vector | `vector_use_after_move.cp` | RUNTIME | PARTIAL | untagged |
| vector | `vector_mutable_alias.cp` | RUNTIME | ACCEPT | none |
| vector | `vector_invalid_slice.cp` | RUNTIME | ACCEPT | none |
| vector | `vector_valid.cp` | ACCEPT | ACCEPT | none |
| vector | `vector_valid_slice.cp` | ACCEPT | ACCEPT | none |
| vector | `vector_valid_move.cp` | ACCEPT | ACCEPT | none |
| net | `net_use_after_close.cp` | RUNTIME | ACCEPT (manifest: `RUNTIME`, exit 0) | none |
| net | `net_double_close.cp` | RUNTIME | ACCEPT (manifest: `RUNTIME`, exit 0) | none |
| net | `net_use_after_transfer.cp` | REJECT | PARTIAL (manifest: `REJECT`, `tagged: false`) | untagged |
| net | `net_valid.cp` | ACCEPT | ACCEPT | none |
| net | `net_valid_borrow.cp` | ACCEPT | ACCEPT | none |
| string | `string_use_after_drop.cp` | REJECT | REJECT | `[CPLUS-` |
| string | `string_double_drop.cp` | REJECT | REJECT | `[CPLUS-` |
| string | `string_dangling_view.cp` | RUNTIME | ACCEPT | none |
| string | `string_uninitialized.cp` | REJECT | REJECT | `[CPLUS-` |
| string | `string_invalid_transfer.cp` | REJECT | PARTIAL | untagged |
| string | `string_valid_basic.cp` | ACCEPT | ACCEPT | none |
| string | `string_valid_view.cp` | ACCEPT | ACCEPT | none |
| string | `string_valid_transfer.cp` | ACCEPT | ACCEPT | none |
| file | `file_read_after_close.cp` | RUNTIME | ACCEPT | none |
| file | `file_write_after_close.cp` | RUNTIME | ACCEPT | none |
| file | `file_double_close.cp` | RUNTIME | ACCEPT | none |
| file | `file_use_after_move.cp` | REJECT | PARTIAL | untagged |
| file | `file_invalid_borrow.cp` | REJECT | PARTIAL (manifest: `REJECT`, note says PARTIAL) | untagged |
| file | `file_valid.cp` | ACCEPT | ACCEPT | none |
| file | `file_valid_borrow.cp` | ACCEPT | ACCEPT | none |
| file | `file_valid_move.cp` | ACCEPT | ACCEPT | none |

## 4. Summary counts by outcome

### Overall (normalized observed outcome)

| Observed outcome | Count | Meaning |
|---|---:|---|
| ACCEPT (compiles cleanly) | 35 | No compile-time rejection; RUNTIME cases in this set are capability gaps. |
| PARTIAL (untagged rejection) | 6 | Compile-time rejection exists but carries no `[CPLUS-` token; not tagged enforcement. |
| REJECT (tagged `[CPLUS-`) | 5 | Strict C+ enforcement; both commands fail with a tagged diagnostic. |
| **Total** | **46** | |

### Expected vs. observed cross-tab

| Expected | Cases → | Tagged REJECT | PARTIAL (untagged) | ACCEPT |
|---|---:|---:|---:|---:|
| REJECT | 9 | 5 | 4 | 0 |
| RUNTIME | 20 | 0 | 2 | 18 |
| ACCEPT | 17 | 0 | 0 | 17 |
| **Total** | **46** | **5** | **6** | **35** |

- Of the **9** cases that expected a compile-time REJECT, only **5** deliver tagged
  `[CPLUS-` enforcement. The other **4** (`net_use_after_transfer.cp`,
  `string_invalid_transfer.cp`, `file_use_after_move.cp`, `file_invalid_borrow.cp`)
  are rejected with untagged diagnostics only.
- The **2** PARTIAL cases that expected RUNTIME are `collections_use_after_move.cp`
  and `vector_use_after_move.cp`: the RAII lowering pass rejects them, but untagged.

### Per family

| Family | Cases | Expected R/A/RJ | Observed ACCEPT / PARTIAL / REJECT | Tagged REJECTs |
|---|---:|---|---|---:|
| error | 5 | 1 / 2 / 2 | 3 / 0 / 2 | 2 |
| thread | 6 | 4 / 2 / 0 | 6 / 0 / 0 | 0 |
| collections | 6 | 4 / 2 / 0 | 5 / 1 / 0 | 0 |
| vector | 8 | 5 / 3 / 0 | 7 / 1 / 0 | 0 |
| net | 5 | 2 / 2 / 1 | 4 / 1 / 0 | 0 |
| string | 8 | 1 / 3 / 4 | 4 / 1 / 3 | 3 |
| file | 8 | 3 / 3 / 2 | 6 / 2 / 0 | 0 |

## 5. Families with zero tagged REJECTs

**5 of 7 families have zero tagged REJECTs: `thread`, `collections`, `vector`, `net`,
`file`.** Only `error` (2) and `string` (3) produce any `[CPLUS-` rejection.

Important nuances, so this is not read as "those families are fine":

- `net` has a genuine compile-time rejection in `net_use_after_transfer.cp`, but it
  is untagged (`GAP-NET-03`); its tagged count is therefore 0.
- `file` has two genuine untagged rejections (`file_use_after_move.cp`,
  `file_invalid_borrow.cp`).
- `collections` and `vector` have untagged use-after-move rejections, but only on
  RUNTIME-expected cases; their tagged REJECT count is 0.
- `thread` has no rejection of any kind: all four unsafe guard/mutex shapes compile
  cleanly and are runtime-enforced by the header.

## 6. Exact diagnostic strings

### Tagged `[CPLUS-` diagnostics (verbatim from the manifests)

| Case | Verbatim diagnostic | Manifest detail |
|---|---|---|
| `error_invalid_lifecycle.cp` | `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: member access through released pointer` | both commands fail; residual gap: method-call sugar on a released `error*` lowers to a free-function call and is not checked. |
| `error_null_pointer.cp` | `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer dereference` | both commands fail; method calls on a null `error*` lower to free-function calls and are not caught. |
| `string_use_after_drop.cp` | `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: subscript of released pointer` | check rc=1, build rc=1; two diagnostics at line 12 (columns 18 and 65). |
| `string_double_drop.cp` | `[CPLUS-OWNER] double free of 'buf'` | check rc=1, build rc=1 at line 11 col 5; only the second `cplus_release()` is reported. |
| `string_uninitialized.cp` | `[CPLUS-LIFETIME] variable 's' is uninitialized (read before it is initialized)` | check rc=1, build rc=1; two diagnostics at line 10 (columns 66 and 90). |

### Untagged C+ lowering diagnostics — NOT C+ enforcement under the rule

| Case | Verbatim diagnostic (no `[CPLUS-` token) |
|---|---|
| `collections_use_after_move.cp` | `use-after-move: use of moved-from resource a`; `borrow ... used after that resource was moved`; owner-copy probe: `resource b cannot be copied from a` |
| `vector_use_after_move.cp` | `use-after-move: use of moved-from resource 'a'` (help: `the handle was zeroed by cplus_move`); owner-copy probe: `cannot be copied` |
| `net_use_after_transfer.cp` | `use-after-move: use of moved-from resource 'a'` (help: `the handle was zeroed by cplus_move`, location 15:28); borrow variant: `borrow 'view' of 'a' is used after that resource was moved` / `re-borrow the new owner instead` |
| `string_invalid_transfer.cp` | `borrow 'borrowed' of 'a' is used after that resource was moved` (line 11 col 24) |
| `file_use_after_move.cp` | `error: use-after-move: use of moved-from resource a` |
| `file_invalid_borrow.cp` | `error: borrow p of local f cannot escape the function` |
| `string_uninitialized.cp` (additional, untagged) | `'s' owns a resource and must be initialised` (line 9) |

### Tagged probes recorded in manifest notes (context, not case results)

- `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 9 exceeds length 3` — the same
  literal subscript on a local `int[3]` *is* rejected (`vector_const_oob.cp` note).
- `[CPLUS-LIFETIME]` from `cplus_check_live(key)` after `stdlib_release(key)`
  (`collections_use_after_move.cp` note): only the explicit liveness API is tagged.
- `[CPLUS-OWNER] double free of '<var>'` from `cplus_release()`/managed pointers
  (`net_double_close.cp` note; see `tests/compile_fail/double_release.c`).
- `thread.json` records a previously fixed `[CPLUS-BORROW]` false positive at
  `thread.hp:159` ("stack borrow passed to thread: arg"), removed externally by
  round-tripping `arg` through `uintptr_t` in `thread::start`; the current header
  compiles cleanly. No Clang warning is counted as C+ enforcement anywhere here.

## 7. Agent 1 hand-off — compiler capabilities needed

Each item below is a missing capability evidenced by the cases above. These are
requests, not claims that any gap currently passes.

1. **Class-field lifetime tracking.** C+ tracks raw managed pointers and resource
   structs, but not the buffers/fields inside a lightweight `class` wrapper. Needed
   to prove use-after-`drop()`/explicit-`close()`/double-`drop()` and reset-state
   misuse for `vector`, `collections`, `net`, `file`, `error` and `string` containers
   (e.g. `vector_use_after_drop.cp`, `collections_use_after_drop.cp`,
   `net_use_after_close.cp`, `net_double_close.cp`, `file_read_after_close.cp`,
   `file_write_after_close.cp`, `file_double_close.cp`, `error_use_after_reset.cp`).
   Manifest notes explicitly say `drop()` is lowered to a plain method call and is
   not tracked, unlike `cplus_release()`.

2. **Guard ownership state.** No model of a mutex guard's owner pointer, its
   unlocked/locked state, the guard→mutex liveness relationship, or mutex-handle
   validity. Needed for all four `thread` gap cases (`thread_double_unlock.cp`,
   `thread_unlock_without_ownership.cp`, `thread_guard_after_destroy.cp`,
   `thread_invalid_mutex_owner.cp`), currently runtime-enforced by NULL/owner guards
   in `thread.hp`.

3. **Alias analysis / exclusivity for class owners.** No pointer-alias or
   container-level borrow analysis for lightweight class owners: two mutable
   `vector*`/`intmap*` aliases compile cleanly, and returned slice/slice-like views
   cannot be linked to their source. Needed for `vector_mutable_alias.cp`,
   `collections_mutable_alias.cp`, `vector_invalid_slice.cp`, and the
   dangling-view/string_view variants (`string_dangling_view.cp`,
   `collections_dangling_key.cp`, where a released pointer passed as an ordinary
   function argument is also not analysed — only `p.len` / `cplus_check_live(p)`
   is tagged).

4. **Tagged diagnostics for use-after-move/owner-copy.** The RAII lowering pass
   already rejects moved-from reuse, escaping borrows and copied owners, but emits
   `use-after-move: ...`, `borrow ... used after that resource was moved`,
   `error: borrow p of local f cannot escape the function`, `cannot be copied`,
   etc. **without** `[CPLUS-` prefixes. Adding the tag would convert 6 PARTIAL
   outcomes into genuine tagged enforcement and remove the
   `net.json`/`file.json` summary contradictions noted in §2. Affected cases:
   `collections_use_after_move.cp`, `vector_use_after_move.cp`,
   `net_use_after_transfer.cp`, `string_invalid_transfer.cp`,
   `file_use_after_move.cp`, `file_invalid_borrow.cp`.

5. **Stdlib class bounds metadata.** Constant out-of-range indices passed to
   container methods (`vector::at_checked()`), and stale slices after reallocation,
   are invisible to the bounds pass; only direct subscripts of fixed-size C arrays
   get `[CPLUS-BOUNDS]`. Needed for `vector_const_oob.cp` and
   `vector_invalid_slice.cp` (and analogous checked container accessors). The
   `int[3]` probe shows the underlying check exists; it needs method-level
   length/capacity metadata for `class` containers.

### Explicit non-claims

- No RUNTIME case is counted as passing; `ACCEPT` for a RUNTIME case is the gap.
- No Clang warning is counted as C+ enforcement; only `[CPLUS-`-prefixed lines are.
- RUNTIME cases are owned by separate runtime suites; this document does not assert
  their pass/fail status.
- This synthesis did not execute the compiler or the test runner; all observations
  are as recorded in the seven manifests listed in §1.
