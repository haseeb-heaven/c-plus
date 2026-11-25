# Expanded CLI apps 2 (C+ checked stdlib) -- deterministic refusals

Four NEW single-file CLI apps under `examples/expanded/apps_2/`. Every app is
driven only by fixed argv (CSV additionally reads fixed fixture files); there is
no clock, no network, no interactive prompt, no user path and no shelling out.
All builds use the repository C+ driver from the repo root in strict,
no-sanitizer mode, with `-I.` so `stdlib/*.hp` resolves. Output is byte-for-byte
reproducible.

## Files

| File | Purpose |
| --- | --- |
| `vault.cp` | In-memory password vault: add / get / list / delete by service name |
| `json_query.cp` | JSON fixture parsed with `stdlib/json.hp`, field-by-dotted-path extraction |
| `csv_tool.cp` | CSV column sum / average with row-width, syntax and overflow refusals |
| `matrix.cp` | Integer matrix add / multiply / determinant with dimension and overflow refusals |
| `run.sh` | Builds and runs all four plus hostile cases; prints the `APPS_2` summary |

## Build commands (exact)

Run from `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`:

```sh
./build/c+ build examples/expanded/apps_2/vault.cp --strict --sanitize=none -I. \
  -o examples/expanded/apps_2/.run/vault
./build/c+ build examples/expanded/apps_2/json_query.cp --strict --sanitize=none -I. \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o examples/expanded/apps_2/.run/json_query
./build/c+ build examples/expanded/apps_2/csv_tool.cp --strict --sanitize=none -I. \
  -o examples/expanded/apps_2/.run/csv_tool
./build/c+ build examples/expanded/apps_2/matrix.cp --strict --sanitize=none -I. \
  -o examples/expanded/apps_2/.run/matrix
```

All four builds exited 0. The JSON compile step prints two harmless clang
warnings ("`-lyyjson`: linker input unused during compilation" and
"`-Lbuild/ext`: argument unused during compilation"); the link still succeeds
and the binary runs.

## Harness command and exact output

```sh
sh examples/expanded/apps_2/run.sh
```

```
== vault ==
build ok   vault
ok         vault_flow
ok         vault_secret
ok         vault_dup
ok         vault_missing
ok         vault_empty_pw
ok         vault_long_name
ok         vault_long_pw
ok         vault_usage
== json_query ==
build ok   json_query
ok         jq_string
ok         jq_int
ok         jq_bool
ok         jq_array
ok         jq_root
ok         jq_malformed
ok         jq_truncated
ok         jq_depth
ok         jq_missing
ok         jq_index
ok         jq_path
ok         jq_usage
== csv_tool ==
build ok   csv_tool
ok         csv_sum
ok         csv_avg
ok         csv_neg_avg
ok         csv_col
ok         csv_ragged
ok         csv_nonnum
ok         csv_overflow
ok         csv_missing
ok         csv_usage
== matrix ==
build ok   matrix
ok         m_add
ok         m_mul
ok         m_det2
ok         m_det4
ok         m_dim_add
ok         m_dim_mul
ok         m_oversized
ok         m_bad_dims
ok         m_bad_data
ok         m_not_square
ok         m_overflow
ok         m_usage
APPS_2 4 4 0 23
```

`APPS_2 4 4 0 23` means: 4 apps declared, 4 apps passed every scenario, 0 apps
failed, and 23 hostile scenarios were correctly refused. For each scenario the
harness requires: exact exit code, empty stderr, and byte-identical stdout
(`cmp`). Every compiler and program invocation is wrapped in `timeout 30`; a
time-out is never a pass. Fixtures are regenerated with fixed bytes each run.

## Per-app commands and exact outputs

### vault.cp

Chained commands are separated by `--` so a whole scenario runs in one process
against one in-memory vault.

```sh
B=examples/expanded/apps_2/.run/vault
timeout 30 "$B" add mail s3cr3t -- add notes opensesame -- get mail -- list \
  -- delete mail -- list
```

```
VAULT add mail
VAULT add notes
VAULT get mail s3cr3t
VAULT list 2
VAULT entry 0 mail
VAULT entry 1 notes
VAULT delete mail
VAULT list 1
VAULT entry 0 notes
```

Exit 0. `list` and `add` print only the service name; the password `s3cr3t`
appears only on the explicit `get`.

Hostile input (exit 1; each refusal also sets a typed `error *`):

```sh
LONG=$(awk 'BEGIN{while(n++<257) printf "a"}')
timeout 30 "$B" add a 1 -- add a 2 -- get nope -- add empty '' \
  -- add "$LONG" pw -- add svc "$LONG"
```

```
VAULT add a
VAULT error duplicate
VAULT error missing-key
VAULT error empty-password
VAULT error name-too-long
VAULT error password-too-long
```

No arguments gives `VAULT error usage` with exit 2.

### json_query.cp

```sh
B=examples/expanded/apps_2/.run/json_query
F='{"user":{"name":"ada","age":36,"active":true},"items":[{"tag":"x"},{"tag":"y"}],"n":7}'
timeout 30 "$B" "$F" user.name
timeout 30 "$B" "$F" items.1.tag
```

```
JSON value user.name ada
JSON value items.1.tag y
```

Both exit 0. Integers (`user.age` -> `36`) and booleans (`user.active` ->
`true`) work the same way.

Hostile input (exit 1 for each):

```sh
DEEP=$(awk 'BEGIN{for(i=0;i<9;i++)printf "["; printf "0"; for(i=0;i<9;i++)printf "]"}')
timeout 30 "$B" '{oops' x
timeout 30 "$B" "$DEEP" 0
timeout 30 "$B" "$F" user.missing
timeout 30 "$B" "$F" items.9.tag
```

```
JSON error parse
JSON error depth
JSON error missing-key
JSON error bad-index
```

`{"a":1` (truncated) also gives `JSON error parse`; `n.x` gives
`JSON error bad-path`; wrong argv count gives `JSON error usage` with exit 2.

### csv_tool.cp

```sh
D=examples/expanded/apps_2/.run
printf '10,20,30\n4,5,6\n7,8,9\n' > "$D/good.csv"
B="$D/csv_tool"
timeout 30 "$B" "$D/good.csv" 0 sum
timeout 30 "$B" "$D/good.csv" 2 avg
```

```
CSV sum 0 21
CSV rows 3
CSV avg 2 15
CSV rows 3
```

Both exit 0. A negative fixture (`-3,0,0` / `-6,0,0`) gives
`CSV avg 0 -4` / `CSV rows 2`.

Hostile input (exit 1 for each):

```sh
printf '10,20,30\n4,5\n7,8,9\n' > "$D/ragged.csv"
printf '10,abc,30\n' > "$D/nonnum.csv"
printf '9223372036854775807,0,0\n9223372036854775807,0,0\n' > "$D/overflow.csv"
timeout 30 "$B" "$D/good.csv" 3 sum
timeout 30 "$B" "$D/ragged.csv" 0 sum
timeout 30 "$B" "$D/nonnum.csv" 1 sum
timeout 30 "$B" "$D/overflow.csv" 0 sum
```

```
CSV error column-out-of-range
CSV error malformed-row
CSV error not-numeric
CSV error overflow
```

A missing file gives `CSV error cannot-open` (exit 1); wrong argv count gives
`CSV error usage` (exit 2).

### matrix.cp

```sh
B=examples/expanded/apps_2/.run/matrix
timeout 30 "$B" add 2 2 1,2,3,4 2 2 5,6,7,8
timeout 30 "$B" determinant 2 2 1,2,3,4
```

```
MAT add 2 2
MAT 0 0 6
MAT 0 1 8
MAT 1 0 10
MAT 1 1 12
MAT determinant 2 -2
```

`multiply 2 3 1,2,3,4,5,6 3 2 7,8,9,10,11,12` prints:

```
MAT multiply 2 2
MAT 0 0 58
MAT 0 1 64
MAT 1 0 139
MAT 1 1 154
```

Hostile input (exit 1 for each):

```sh
timeout 30 "$B" add 2 2 1,2,3,4 2 3 1,2,3,4,5,6
timeout 30 "$B" add 5 2 1,2,3,4,5,6,7,8,9,10 5 2 1,2,3,4,5,6,7,8,9,10
timeout 30 "$B" add 2 2 1,2,3 2 2 5,6,7,8
timeout 30 "$B" determinant 2 3 1,2,3,4,5,6
```

```
MAT error dim-mismatch
MAT error oversized-dims
MAT error bad-data
MAT error not-square
```

A 4x4 matrix whose entries are all `1000000000` gives `MAT error overflow`
(exit 1); no arguments gives `MAT error usage` (exit 2).

## Checked-API and `--strict` findings

* `argv` is an FFI pointer: direct `argv[i]` is rejected under `--strict`. Each
  app copies the slot with `__builtin_memcpy(&value, p + idx, sizeof(value))`.
* `vault` copies names/passwords with `string::substr` after an explicit
  `string::size()` cap check, because `substr` truncates silently. Fixed 257-byte
  tables hold at most 256 payload bytes.
* `json_query` uses `json_parse_budget(text, len, 8, &err)` so nesting beyond 8
  containers is refused before yyjson allocates. `err.code()` distinguishes
  `CPLUS_EXIT_BOUNDS` ("depth") from the malformed-JSON `INVALID` path ("parse").
  Array indices are validated with a digit-only scan plus `strtol` (`end`
  initialised) before `json_view::at`.
* `csv_tool` reads through `file::read_line` with an `error *` and extracts
  fields with `string::field`; an exact field-length helper prevents silent
  truncation. Sum overflow is checked before `+=` with `LLONG_MAX/MIN` bounds;
  average division is preceded by an explicit `denom == 0` guard, which is what
  the strict arithmetic checker requires to prove a non-zero divisor.
* `matrix` uses `__builtin_mul_overflow` / `__builtin_add_overflow` /
  `__builtin_sub_overflow` with a `MAT_SAFE` magnitude cap. A hand-rolled
  division-based overflow check was rejected by `--strict` because the checker
  could not prove the variable divisor non-zero after the value was written
  through an out-parameter; the compiler builtins avoid a divisor entirely.
  Writes through the `long long *vals` parameter use pointer arithmetic
  (`*(vals + k) = value`) rather than a subscript, matching the strict FFI rule.
* `goto` is rejected by this C+ build ("bypasses C+ cleanup; destructors may not
  run"), so `csv_tool` and `matrix` use structured early returns and helper
  functions instead.
* `strtol` out-parameters are initialised to the input pointer before every
  call, and every parse checks `end == text + len`, `*end == '\0'` and
  `errno != ERANGE`.

## Teaching-model refusal statement (not a safety proof)

These apps are teaching examples. Every refusal above is an **observed,
deterministic behaviour of this checkout's toolchain** under the exact commands
shown -- it is **not** a formal proof, a verified safety guarantee, or evidence
that the general C+ language or these programs are memory-safe, overflow-free or
secure in other builds, on other inputs, or under other compilers. The bounds,
error codes and "refused" counts are produced by explicit checks in the example
source and confirmed by exact stdout/exit-code comparisons in `run.sh`; no
theorem, proof assistant, model checking or exhaustive input exploration was
performed. A passing harness run means only that the listed scenarios behaved as
listed here. Treat this as a demonstration of the *pattern* of refusing
untrusted input with checked stdlib APIs and typed errors, not as a claim that
the pattern has been proved correct.

No passes were assumed: every line in this document was obtained by executing
the commands on this host.
