# Per-method transform safety comparison: C+ checked APIs vs Rust

Scope: the seven transform methods assigned to this agent:
`strtolowercase_s`, `strtouppercase_s`, `strlcat`, `strlcpy`, `strnset_s`,
`strset_s`, `strnterminate_s`.

Only these files were created; no vendor file, compiler/runtime source, or
build script was edited:

```
benchmarks/safety_compare/per_method/transform/transform.c
benchmarks/safety_compare/per_method/transform/transform.rs
benchmarks/safety_compare/per_method/transform/compare.sh
benchmarks/safety_compare/per_method/transform/NOTES.md
benchmarks/safety_compare/per_method/transform/results/.gitignore
```

Generated binaries/logs/token files are written under `results/` and ignored
by that `.gitignore`.

## 1. Symbol verification (nm) — no fabrication

The linked entry points were checked directly in the archive, not inferred
from the presence of a vendor `.c` file. Command:

```sh
nm -g build/safe/libcplus_safecrt.a | \
  grep -E 'cplus_v__?(strtolowercase_s|strtouppercase_s|strlcat|strlcpy|strnset_s|strset_s|strnterminate_s)'
```

Exact output at verification time (all seven methods available, 0 BLOCKED):

```
0000000000000048 T _cplus_v__strtolowercase_s_chk
0000000000000190 T _cplus_v__strtouppercase_s_chk
0000000000000000 T _cplus_v__strnset_s_chk
0000000000000274 T _cplus_v__strnterminate_s_chk
0000000000000124 T _cplus_v__strset_s_chk
0000000000000000 T _cplus_v_strlcat
00000000000000ec T _cplus_v_strlcpy
```

Important ABI detail: the unprefixed public names (e.g. `strtolowercase_s`,
`strnset_s`) are in `#ifdef FOR_DOXYGEN` branches of the pinned upstream
safeclib sources included by `libc/src/safecrt_vendor/{case_group,set_group}.c`,
so those TUs emit only the BOS-aware `*_chk` entry points. `transform.c`
therefore calls the `cplus_v__*_chk` variants with `BOS_UNKNOWN == (size_t)-1`.
`strlcat`/`strlcpy` are emitted directly by `strl_group.c` with no `_chk`
variant. `compare.sh` detects availability with `nm` on the archive and passes
`-DHAVE_<METHOD>=1` only for symbols it actually finds; a missing symbol is
reported as `BLOCKED <method>: symbol '<name>' not linked in <archive>` with
the nm lines as evidence, and that method prints no tokens.

## 2. Toolchains

```
$ /usr/bin/clang --version | sed -n '1p'
Apple clang version 16.0.0 (clang-1600.0.26.6)
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)
```

Host: macOS (darwin). All runs used `LC_ALL=C` and a 30 s timeout.

## 3. Exact commands

From the repository root:

```sh
# C99 harness (only -DHAVE_* for symbols nm found)
/usr/bin/clang -std=c99 \
  -Ilibc/install-include -Ithird_party/safeclib/include \
  -DHAVE_STRTOLOWERCASE_S=1 -DHAVE_STRTOUPPERCASE_S=1 \
  -DHAVE_STRLCAT=1 -DHAVE_STRLCPY=1 -DHAVE_STRNSET_S=1 \
  -DHAVE_STRSET_S=1 -DHAVE_STRNTERMINATE_S=1 \
  benchmarks/safety_compare/per_method/transform/transform.c \
  -o benchmarks/safety_compare/per_method/transform/results/transform_c \
  build/safe/libcplus_safecrt.a

# Safe Rust harness
rustc -O benchmarks/safety_compare/per_method/transform/transform.rs \
  -o benchmarks/safety_compare/per_method/transform/results/transform_rust

# Runs (deterministic, no arguments)
timeout 30 benchmarks/safety_compare/per_method/transform/results/transform_c \
  > benchmarks/safety_compare/per_method/transform/results/c.out
timeout 30 benchmarks/safety_compare/per_method/transform/results/transform_rust \
  > benchmarks/safety_compare/per_method/transform/results/rust.out

# Driver (does all of the above plus detection, diff and counting)
sh benchmarks/safety_compare/per_method/transform/compare.sh
```

Both builds produced empty stderr. Neither run produced stderr or a nonzero
exit. The default safeclib constraint handler writes
`IGNORE CONSTRAINT HANDLER: ...` lines to stdout, so `transform.c` installs a
silent handler via `cplus_sc_set_str_constraint_handler_s_wrapper`; the
driver additionally filters any such line into separate `*.tokens` files used
only for the comparison (raw output is kept as evidence in `results/`).

## 4. Cases and tokens

Each method prints exactly six deterministic `<method> <case> <token>` lines:

| case | meaning | token (C / Rust) |
|---|---|---|
| `valid` | normal transform succeeds and produces the expected content | `OK` |
| `null` | NULL/absent destination | `REFUSED` |
| `zero_bound` | dmax/dsize zero | `REFUSED` |
| `over_bound` | dmax one past `RSIZE_MAX_STR` (4096), or dsize one past capacity | `REFUSED` |
| `trunc` / `term` | bounded transform / forced NUL termination | `TRUNC` / `TERMINATED` |
| `canary` | sentinel byte beyond the written region survives | `CANARY_INTACT` |

`6 cases x 7 methods = 42` lines. Example from `results/c.tokens`:

```
strtolowercase_s valid OK
strtolowercase_s null REFUSED
strtolowercase_s zero_bound REFUSED
strtolowercase_s over_bound REFUSED
strtolowercase_s trunc TRUNC
strtolowercase_s canary CANARY_INTACT
...
strnterminate_s term TERMINATED
strnterminate_s canary CANARY_INTACT
```

Final driver output:

```
$ sh benchmarks/safety_compare/per_method/transform/compare.sh
METHOD_TRANSFORM 42 42 0 0
$ echo $?
0
```

Counts: 42 cases, 42 matched, 0 mismatched, 0 blocked. The full diff is in
`results/tokens.diff` (empty on this run).

## 5. BLOCKED-path verification (not fabricated)

To prove the driver does not invent tokens when a symbol is absent, it was
re-run against a minimal archive containing only `sc_strcmp_s.o`, which has
none of the seven symbols. Output (abridged):

```
BLOCKED strtolowercase_s: symbol 'cplus_v__strtolowercase_s_chk' not linked in .../results/no_syms.a
... (one line per method) ...
BLOCKED: harness could not be built/run; evidence:
  C build rc=1 (.../results/c.build.err)
METHOD_TRANSFORM 0 0 0 7
```

Exit status was 0: BLOCKED is not a mismatch and not a pass. The driver also
handles a missing archive (`CPLUS_ARCHIVE` override) with the same convention.

## 6. Explicit non-equivalence note

This is a **safety-outcome comparison, not a semantic identity proof**.

* The C side calls verbatim vendored C code that returns `errno_t`/counts and
  invokes a constraint handler. The Rust side has no `errno_t` and no NULL:
  `Option`, explicit bound checks, and safe indexing `get_mut`/`truncate`
  model the refusal instead of reproducing it. Matching `REFUSED` tokens mean
  *both refuse the operation*, not that they fail through the same mechanism.
* Rust prevents the invalid states statically (borrow checking, slice bounds,
  owned `String`/`Vec`), whereas the C checked APIs detect them at runtime and
  return an error code. A token match demonstrates equivalent observable
  safety, not equivalent implementation or identical failure semantics.
* `strlcat`/`strlcpy` are raw BSD APIs with no `RSIZE_MAX_STR`/object-size
  check. For their `over_bound` case the harness enforces the documented
  precondition `dsize <= capacity` before calling (and the Rust side performs
  the same check), so `REFUSED` is a checked-call-site refusal, not a return
  value from the C function. This is intentional and is the one case where the
  token is not derived from a call into the archive.
* `to_lowercase`/`to_uppercase` are Unicode-aware; the C functions are ASCII
  only. The harness deliberately uses ASCII inputs so the selected cases are
  comparable. The Rust harness contains no unchecked blocks.
