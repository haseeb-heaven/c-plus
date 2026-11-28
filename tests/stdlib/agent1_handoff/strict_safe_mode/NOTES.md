# P1 — Strict-safe mode: minimal evidence for "reject operations without proven safety"

Status: **report only**. No compiler, runtime, header, build, or other repository files were
modified. Every observation below was produced by running the commands in this directory;
raw stdout/stderr/exit-code captures are checked in alongside the sources as
`<case>.strict.stdout`, `<case>.strict.stderr`, `<case>.strict.rc`, and the matching
`.default.*` files. No diagnostic in this report is reconstructed from memory.

## Environment / provenance

- Repo: `c-plus-stdlib`, branch `agent/modern-stdlib`, HEAD `a90b69c`.
- Compiler under test: `./build/c+` → `C+ 0.1.0`, backend host Clang/LLVM
  (`Apple clang version 16.0.0 (clang-1600.0.26.6)`, arm64-apple-darwin27.2.0).
- Command wrapper used: `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH`
  (the documented PATH prefix; the default Homebrew clang has a broken sysroot).
- Commands (verbatim form, `<case>` = each `.cp` file in this directory):

  ```sh
  ./build/c+ check --strict  <case>
  ./build/c+ check --default <case>
  ```

- `--default` is a supported flag (`c+ check --help`):
  `DEFAULT mode: report proven errors, warn if unverified`.

## Precise definition: "reject without proven safety" for Agent 1

A strict-safe mode **rejects an operation at compile time** — non-zero exit from
`c+ check --strict`, carrying a C+ diagnostic tagged `[CPLUS-…]` — whenever the compiler
**cannot construct a static proof of that operation's safety** from facts available in the
translation unit. This is the documented contract:

- `../c-plus-core/compiler/Driver/Driver.h:67-69` — "DEFAULT (false): report proven errors,
  warn on unverified operations. STRICT (true): reject unverified operations rather than warn."
- `../c-plus-core/compiler/Sema/AstSafetyCheck.cpp:2159-2161` — `warnAt()` sets
  `d.level = strict_ ? DiagLevel::Error : DiagLevel::Warning`, i.e. strict *fails closed* by
  promoting an unverified-operation warning to an error; it is not a silent downgrade.

Concretely, an operation counts as **proven safe** only if the compiler can establish, from
tracked metadata (managed-allocation extents, slice/owner state, array bounds, known
capacities, liveness/generation facts) or from constants, **all** of the obligations for its
kind:

1. **Raw pointer arithmetic / typed deref** — the resulting address stays inside the extent
   of the object it targets, and the target is live. Constant-OOB is already a *proven error*
   (both modes reject); an extent the compiler cannot bound is *unverified*.
2. **Allocation used with a checked stdlib API** — the pointer's provenance is a managed
   allocation (not a foreign `malloc` block) and the real capacity is known to the compiler.
3. **Bounded wrapper capacity** — every `dmax`/`smax` argument is either a proven upper bound
   of the destination extent or is derived from tracked allocation metadata / object size;
   a caller-supplied runtime value is *unverified*.
4. **File/IO handle** — the handle is used through the managed wrapper (`stdlib/file.hp`)
   whose close/liveness state is tracked; a raw `FILE*` operation carries no such proof.
5. **`void*` → typed pointer cast** — the cast's target type and extent are justified by the
   provenance of the `void*` source; an unproven cast is *unverified*.
6. **Function-pointer call** — the callee is a known direct target, or the table index is
   provably in range; a volatile/runtime selector is *unverified*.

If any obligation is unmet, strict mode must fail closed. Default mode may preserve the raw-C
spelling and emit a warning. **Counting rule** (per `docs/libc/AGENT1_COMPILER_GAPS.md` §0):
only a diagnostic containing a literal `[CPLUS-` token counts as tagged C+ enforcement; an
untagged rejection is recorded as PARTIAL, never as a strict-safe pass. "Proven error" (both
modes reject, e.g. a literal OOB index) is a distinct category from "unverified operation"
(default warns / strict rejects).

## Measured matrix

`rejected in strict` = `c+ check --strict` exit code != 0.

| # | Operation exercised | strict rc | default rc | rejected in strict | `[CPLUS-` diag | strict-vs-default delta |
|---|---|---:|---:|---|---|---|
| 01 | raw pointer arithmetic on a stdlib object (`string_view::data() + 100`) | 0 | 0 | **no** | none | **none** |
| 02 | unchecked raw `malloc` used with a stdlib API (`cplus_memset_s` on raw block) | 0 | 0 | **no** | none | **none** |
| 03 | unproven capacity passed to a bounded wrapper (`cplus_memcpy_s` with param capacities) | 0 | 0 | **no** | none | **none** |
| 04 | raw `FILE*` operation outside the file wrapper (`fopen`/`fread`/`fclose`) | 0 | 0 | **no** | none | **none** |
| 05 | cast from `void*` to typed pointer (`stdlib_alloc` → `int*`) | 0 | 0 | **no** | none | **none** |
| 06 | function-pointer call with unproven target (`table[volatile cmd]()`) | 0 | 0 | **no** | none | **none** |
| C | control: non-literal `printf` format (documented unverified op) | **1** | 0 | **yes** | `[CPLUS-FFI]` | strict `error`, default `warning` |

### Exact output for cases 01–06 (identical in both modes)

stdout, with `<stem>` = the case filename:

```
check: tests/stdlib/agent1_handoff/strict_safe_mode/<stem>.cp ok
```

stderr: empty (0 bytes). There is **no** `[CPLUS-` diagnostic and no warning in either
`--strict` or `--default` for any of the six operations.

### Exact output for the control (`00_control_strict_delta.cp`)

`check --strict` → rc=1, stdout empty, stderr:

```
tests/stdlib/agent1_handoff/strict_safe_mode/00_control_strict_delta.cp:11:5: error: [CPLUS-FFI] format string in printf is not a string literal (CWE-134; attacker-controlled format)
      printf(fmt, 42);   /* non-literal format: warn (default) / error (strict) */
      ^~~~~~
```

`check --default` → rc=0, stderr is the same three lines with `error:` replaced by `warning:`,
and stdout:

```
check: tests/stdlib/agent1_handoff/strict_safe_mode/00_control_strict_delta.cp ok
```

## Delta assessment

- **For all six P1 operations there is no strict/default delta.** Both modes accept every
  file with the same `check: … ok` line and no diagnostics. Under the definition above, the
  six operations are **unverified** (the proof obligations in §"Precise definition" are not
  discharged), yet the current strict mode does **not** reject them. This is the P1 gap.
- **The flag itself is live.** The control shows a genuine fail-closed delta (`[CPLUS-FFI]`
  error under strict vs. warning under default), so the acceptance of cases 01–06 is a real
  coverage gap, not a no-op flag.
- Relationship to existing catalogs: case 06 is the same shape as
  `docs/libc/DISPATCH_BOUNDS_HANDOFF.md` (dynamic function-pointer index; static constant
  indices are rejected, dynamic ones are UBSan-only). Cases 01/02/03/05 correspond to the
  "raw-C UB / provenance / unknown capacity cannot be made safe by libc alone" gap family in
  `docs/libc/IMPLEMENTATION_PLAN.md` and `docs/libc/ADVERSARIAL_FINDINGS.md`. This report adds
  direct `check --strict` vs `check --default` evidence for all six in one matrix.
- No claim is made about runtime behavior (no `build`/run was executed here); this is strictly
  compile-time (`check`) evidence.

## Reproduce

```sh
cd <repo>
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
for f in tests/stdlib/agent1_handoff/strict_safe_mode/0*.cp; do
  ./build/c+ check --strict  "$f"; echo "strict rc=$?"
  ./build/c+ check --default "$f"; echo "default rc=$?"
done
```
