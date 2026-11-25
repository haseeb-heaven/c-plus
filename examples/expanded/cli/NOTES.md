# Expanded CLI examples (C+ safe stdlib)

Four single-file CLI apps with deterministic, testable cores. All builds use
the repository's C+ driver in strict, no-sanitizer mode, run from the repo
root so the `stdlib/*.hp` includes resolve with `-I.`.

## Files

| File | Purpose |
| --- | --- |
| `todo.cp` | In-memory task list: add / list / done / remove / save / load |
| `calculator.cp` | Integer expression evaluator with divide-by-zero refusal |
| `text_tools.cp` | Word count / line count / reverse-lines / substring search |
| `file_tools.cp` | File size / copy / compare with bounds-checked reads |
| `run.sh` | Builds and runs all four with fixed inputs; prints the summary line |

No wall-clock calls, no interactive loops, no user paths, no shelling out from
the apps themselves. Every command is driven by fixed argv (one app also
accepts a single fixed line from stdin). Output is byte-for-byte reproducible.

## Build commands (exact)

Run from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`):

```sh
./build/c+ build examples/expanded/cli/todo.cp        --strict --sanitize=none -I. -o examples/expanded/cli/.run/todo
./build/c+ build examples/expanded/cli/calculator.cp  --strict --sanitize=none -I. -o examples/expanded/cli/.run/calculator
./build/c+ build examples/expanded/cli/text_tools.cp  --strict --sanitize=none -I. -o examples/expanded/cli/.run/text_tools
./build/c+ build examples/expanded/cli/file_tools.cp  --strict --sanitize=none -I. -o examples/expanded/cli/.run/file_tools
```

All four builds exited 0 with empty stdout and empty stderr.

## Harness command and exact output

```sh
sh examples/expanded/cli/run.sh
```

```
== todo ==
build ok   todo
ok         todo_flow
ok         todo_invalid
ok         todo_usage
== calculator ==
build ok   calculator
ok         calc_prec
ok         calc_paren
ok         calc_div
ok         calc_trunc
ok         calc_zero
ok         calc_syntax
ok         calc_stdin
== text_tools ==
build ok   text_tools
ok         text_words
ok         text_lines
ok         text_rev
ok         text_find
ok         text_nofind
ok         text_missing
== file_tools ==
build ok   file_tools
ok         ft_size
ok         ft_exact
ok         ft_over
ok         ft_copy
ok         ft_cmp_same
ok         ft_cmp_diff
CLI_EXAMPLES 4 4 0
```

The harness verifies, per scenario: exit code matches, stderr is empty, and
stdout matches the expected bytes exactly (`cmp`). A time-out is never counted
as a pass. Fixtures are regenerated with fixed bytes on every run.

## Per-app commands and exact outputs

### todo.cp

Commands are separated by `--` so a whole scenario runs in one process with
one in-memory vector state:

```sh
B=examples/expanded/cli/.run/todo
F=examples/expanded/cli/.run/todo.db
rm -f "$F"
timeout 30 "$B" add 1 buy_milk -- add 2 call_dad -- done 1 -- list \
  -- save "$F" -- remove 2 -- list -- load "$F"
```

Output (`$F` expands to `examples/expanded/cli/.run/todo.db` from the repo root):

```
TODO add 1 buy_milk
TODO add 2 call_dad
TODO done 1
TODO item 1 buy_milk 1
TODO item 2 call_dad 0
TODO list 2
TODO save 2 examples/expanded/cli/.run/todo.db
TODO remove 2
TODO item 1 buy_milk 1
TODO list 1
TODO load 1 examples/expanded/cli/.run/todo.db
TODO item 1 buy_milk 1
TODO item 2 call_dad 0
TODO list 2
```

Exit 0. The saved file contains exactly:

```
1|1|buy_milk
2|0|call_dad
```

Invalid input (exit 1):

```sh
timeout 30 "$B" add 1 x -- add 1 y -- done 99 -- bogus
```
```
TODO add 1 x
TODO error duplicate
TODO error not-found
TODO error unknown-command
```

No arguments (exit 2):

```
TODO error usage
```

### calculator.cp

```sh
B=examples/expanded/cli/.run/calculator
timeout 30 "$B" "1+2*3"
```
```
CALC 1+2*3 = 7
CALC ops 2
```

```sh
timeout 30 "$B" "(1+2)*3"
```
```
CALC (1+2)*3 = 9
CALC ops 2
```

```sh
timeout 30 "$B" "7/(2+2)"
```
```
CALC 7/(2+2) = 1
CALC ops 2
```

```sh
timeout 30 "$B" "10/3"
```
```
CALC 10/3 = 3
CALC ops 1
```

Divide-by-zero refusal (exit 1):

```sh
timeout 30 "$B" "1/0"
```
```
CALC ERROR divide-by-zero
```

Malformed expression (exit 1):

```sh
timeout 30 "$B" "1+"
```
```
CALC ERROR syntax
```

Stdin form (exit 0):

```sh
printf '4*(3+1)\n' | timeout 30 "$B"
```
```
CALC 4*(3+1) = 16
CALC ops 2
```

### text_tools.cp

Fixture:

```sh
printf 'alpha beta gamma\ndelta alpha\nomega\n' > examples/expanded/cli/.run/text.in
B=examples/expanded/cli/.run/text_tools
F=examples/expanded/cli/.run/text.in
```

```sh
timeout 30 "$B" "$F" words
```
```
TEXT words 6
```

```sh
timeout 30 "$B" "$F" lines
```
```
TEXT lines 3 3
```

```sh
timeout 30 "$B" "$F" rev
```
```
TEXT rev 2 omega
TEXT rev 1 delta alpha
TEXT rev 0 alpha beta gamma
```

```sh
timeout 30 "$B" "$F" find alpha
```
```
TEXT find 0 0 alpha beta gamma
TEXT find 1 6 delta alpha
TEXT find hits 2
```

```sh
timeout 30 "$B" "$F" find zzz
```
```
TEXT find hits 0
```

Missing file (exit 1):

```sh
timeout 30 "$B" examples/expanded/cli/.run/does-not-exist words
```
```
TEXT error cannot-open
```

### file_tools.cp

Fixtures:

```sh
D=examples/expanded/cli/.run
printf 'hello file tools\n' > "$D/src.txt"
printf 'different\n'       > "$D/other.txt"
head -c 1024 /dev/zero    > "$D/exact.bin"   # exact fit for FT_CAP = 1024
head -c 1025 /dev/zero    > "$D/over.bin"    # one byte over the buffer
B="$D/file_tools"
```

```sh
timeout 30 "$B" size "$D/src.txt"
```
```
FILETOOLS size 17
```

```sh
timeout 30 "$B" size "$D/exact.bin"
```
```
FILETOOLS size 1024
```

One-over refusal (exit 1, bounds error via `error *`):

```sh
timeout 30 "$B" size "$D/over.bin"
```
```
FILETOOLS error one-over
```

```sh
timeout 30 "$B" copy "$D/src.txt" "$D/dst.txt"
```
```
FILETOOLS copy 17
```

```sh
timeout 30 "$B" cmp "$D/src.txt" "$D/dst.txt"
```
```
FILETOOLS cmp 1
```

```sh
timeout 30 "$B" cmp "$D/src.txt" "$D/other.txt"
```
```
FILETOOLS cmp 0
```

## Checked-API notes / strict-mode findings

* `argv` is an FFI pointer: direct `argv[i]` is rejected by `--strict`
  (`CPLUS-FFI: unverifiable extent`). Each app copies the slot with
  `__builtin_memcpy(&value, p + idx, sizeof(value))` first.
* `--strict` also rejects variable-divisor `/`. The calculator never uses
  `/` on a computed value: `calc_udiv()` is a shift-and-subtract unsigned
  division (32-bit magnitudes suffice because operands are bounded to 1e9),
  and the zero divisor is reported through `error_fail(err,
  CPLUS_EXIT_UNSUPPORTED, "divide-by-zero")`.
* A 64-bit variable shift count is likewise rejected by the strict checker
  (`shift count cannot be proven within the left operand's width`); the
  division helper uses 32-bit shifts in a `for (int sh = 31; sh >= 0; --sh)`
  loop, which the checker accepts.
* C library out-parameters such as `strtol`'s `end` must be initialised before
  the call under `--strict`; the parsers initialise `end` to the input pointer.
* `vector` reads use `at_checked()`; failures are propagated, not silently
  converted to zero values. `string` copies use `substr()` / `field()` and
  check the return code and `valid()`.
* `file` reads use `file::read_line` / `file::read` with an `error *`;
  `file_tools` reads one byte at a time so it can detect the exact-fit vs
  one-over boundary without unchecked pointer arithmetic, and closes through
  the checked `close()` API.

No passes were assumed: every line above was observed by running the commands
on this host.
