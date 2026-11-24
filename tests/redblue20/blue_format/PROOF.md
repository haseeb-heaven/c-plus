# BLUE20 — checked format / IO safety proof

Agent: BLUE-TEAM 16/20 · branch `agent/modern-stdlib`
Scope: **only** `tests/redblue20/blue_format/`. No source/header edits, no
`git add` / commit.

## What is proven

The C+ SafeCRT checked format and IO adapters (`stdlib/safe/safe_format.h`,
`stdlib/safe/safe_io.h`, implemented in `stdlib/safe/src/safe_format.c` and
`safe_io.c`, linked from `build/safe/libcplus_safecrt.a`) refuse or contain the
following hostile conditions:

| Property | Category | Asserted post-conditions |
|---|---|---|
| `%n` (incl. length / positional / flag / embedded forms) refused | `pn_printf`, `pn_scanf` | return `-1`, `errno == EINVAL`, `%n` target unchanged, canaries intact |
| non-literal formats refused (CWE-134) | `nonliteral` | return `-1`, `errno == EINVAL`, canaries intact |
| one-byte-too-small destination bounded | `bounded` | exact capacity ⇒ truncated + NUL inside object; `dmax` one over real capacity ⇒ `-1` / `EINVAL`, destination untouched |
| struct-member capacity clamped | `structmem` | `dmax > member size` ⇒ `-1` / `EINVAL`, member and trailing field untouched |
| NULL streams / args refused | `nullstream` | defined failure value per API, `errno == EINVAL`, no crash |
| double-close / use-after-close refused | `doubleclose` | first close `0`, closed marker set, second close `EOF`/`EINVAL`, all I/O on closed stream refused |
| canaries intact | *every case* | leading + trailing guard bytes verified before PASS |
| (control) legitimate formats still accepted | `control` | success returns and exact output; proves the guards are not over-broad |

`safe_format.h` is included **first** so `CPLUS_SAFE_OBJ_SIZE` resolves to
`__builtin_object_size(p, 1)` (nearest enclosing subobject). That is the
definition that makes struct-member clamping effective; `safe_io.h` is included
afterwards and does not override it.

`cplus_runtime_set_violation_trap(0)` is set once before forking, so every
violation returns `-1` / `EINVAL` instead of `_exit(70)` and all post-conditions
are observable in the child.

## Harness

`proof.c`:

- **Fixed seed** `0x424c554520666d74`; per-case parameters from `mix64(SEED ^ i·golden)`.
- **`NCASES = 2100`** (≥ 2000 required), 8 categories round-robin.
- **Fork-per-case**: parent sets parameters, forks; child runs exactly one case,
  guards it with `alarm(2)`, and `_exit`s with a distinct reason code. Parent
  `waitpid`s and classifies exit code / signal.
- Canary-guarded destination buffers (`gbuf_t`) are declared **locally per case
  function** so `__builtin_object_size`'s value is visible at each call site.
- Child stderr is primed after `freopen` so stdio's `isatty` probe cannot clobber
  the `EINVAL` set by the safety violation before the assertion runs.
- FNV-1a digest over `(seed, case count, every case's params and result)` is
  printed as a reproducibility fingerprint.

`run.sh`:

- Rebuilds `build/safe/libcplus_safecrt.a` via `scripts/build_safecrt.sh` with
  the system SDK compiler (`/usr/bin/clang`), then links `proof.c` against that
  archive (`-lm -pthread`).
- Executes the harness under **`timeout 60`**.
- Fails unless the summary is present, `passed == cases`, `failed == 0`, and
  `cases >= 2000`; exits non-zero otherwise.

## Exact results (command: `sh tests/redblue20/blue_format/run.sh`)

```
CAT pn_printf   263 263 0
CAT pn_scanf    263 263 0
CAT nonliteral  263 263 0
CAT bounded     263 263 0
CAT structmem   262 262 0
CAT nullstream  262 262 0
CAT doubleclose 262 262 0
CAT control     262 262 0
SEED 0x424c554520666d74 CASES 2100 DIGEST 0x6ca13d15429800f2
BLUE20 format 2100 2100 0
```

Run exit code: `0`. Re-running produced the identical `DIGEST
0x6ca13d15429800f2`, confirming the fixed-seed determinism. Full per-case trace
is in `out/proof.log`; build logs in `out/safecrt_build.log` and
`out/proof.build.log`.

## Mutation sanity-check (harness is not vacuous)

A throwaway copy of `proof.c` with the printf-family `%n` return assertion
weakened from `rc != -1` to `rc != 0` was compiled against the same archive and
run under the same `timeout 60`:

```
BLUE20 format 2100 1104 996
```

i.e. the harness detects 996 broken cases; the all-PASS result above therefore
reflects the real checked implementations rather than inert assertions. (The
mutant lived only in the system temp directory and was not part of the
deliverables.)

## Deliverables

- `tests/redblue20/blue_format/proof.c`
- `tests/redblue20/blue_format/run.sh`
- `tests/redblue20/blue_format/PROOF.md`
- evidence artifacts under `tests/redblue20/blue_format/out/`
