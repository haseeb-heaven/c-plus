# HP fuzz harness notes

This document records the design, the exact verification commands and the
operating discipline for `tests/stdlib/fuzz/fuzz_hp.cp` — a deterministic fuzz
harness for the modern C+ standard-library `.hp` APIs on the
`agent/modern-stdlib` branch.

## 1. Scope and ownership

| Item | Path |
| --- | --- |
| Harness (standalone program) | `tests/stdlib/fuzz/fuzz_hp.cp` |
| These notes | `docs/stdlib/HP_FUZZ_NOTES.md` |

The harness is intentionally self-contained. It does not modify any `.hp`
header, any compiler code, or any build script. It complements (does not
replace) the libFuzzer targets already under `tests/stdlib/fuzz/`: those accept
arbitrary bytes and therefore *must* tolerate contract-violating inputs; this
harness generates **only valid operations** so that any invariant violation is
attributable to the library, not to the caller.

Headers exercised (all five, as required):

- `stdlib/error.hp` — `error` value and its `clear()`/`ok()`/`category()`/
  `code()` methods, plus the checked allocator used by the containers'
  growth paths;
- `stdlib/string.hp` — `string`, `string_view`, append/reserve/substr/field/
  at/find/equals/view/drop;
- `stdlib/vector.hp` — `vector`, `slice`, push/insert/erase/pop/at/view/drop;
- `stdlib/collections.hp` — `intmap`, `intset`, `strmap`, `deque`, `list`,
  `stack`, `queue` (put/get/contains/remove/insert/push/pop/checked reads/
  clear/grow/drop);
- `stdlib/io.hp` — `read_int`, `read_long`, `read_double`, `read_line`,
  `stdlib_io_token` driven against an empty stream (documented EOF path).

## 2. Determinism

- **Fixed-seed LCG.** `rng_state` starts at `0xC0FFEEULL` and advances with
  `state = state * 6364136223846793005ULL + 1442695040888963407ULL`, taking
  the high 32 bits (`state >> 33`, matching the recurrence used by
  `deep_collections_collisions.cp`). No wall clock, no `rand()`, no seed from
  the environment.
- **Fixed operation count.** `CASES` is `1200` (> the required 1000). Each
  iteration draws three bounded words, selects one of `NOPS = 18` operations
  (two per container family), and derives a key, a value and an index by
  reducing the words into small ranges.
- **Bounded lengths/capacities.** String content is capped at `MAXSTR = 64`
  bytes (clear-and-restart before an append that would exceed it); all other
  containers are capped at `MAXSEQ = 48` elements; keys and values come from a
  32-entry domain; reserve/grow sizes are reduced to `< 48`. No generated
  value can approach `STDLIB_ALLOC_LIMIT` or an integer overflow boundary.
- **stdin independence.** `io_check()` calls
  `freopen("/dev/null", "r", stdin)` before using the `io.hp` readers, so
  EOF is returned regardless of how the caller redirects standard input.
  Verified: five runs with different piped stdin all produced the same digest.
- **Digest.** A 64-bit FNV-1a accumulator folds in, for every operation, the
  operation index, kind, key, value, selected result codes and the post-op
  extents of all nine containers. It is printed once as
  `hp_fuzz_digest <16-hex-digits>` and must be byte-identical across runs and
  across build modes.

## 3. Invariant model

After every operation `check_invariants()` compares each live container with
an independent fixed-size shadow model:

- `string`: `valid()`, `size()`, `capacity() >= size()`, `strlen(c_str())`,
  byte-for-byte content via `memcmp`, UTF-8 validity, and `to_view().equals()`;
- `vector`: `size()`, `capacity() >= size()`, every element through
  `at_checked()`, and every element through a freshly taken `slice`
  (including `slice::stale()`);
- `intmap` / `intset` / `strmap`: `valid()`, `size()`, `contains()` and `get()`
  for all 32 keys in the domain;
- `deque` / `list`: `size()` and every element through `at_checked()`;
- `stack`: `size()` and `top_checked()`;
- `queue`: `size()` and `front_checked()`.

Any mismatch prints `hp_fuzz: INVARIANT <label>` and exits with code 1. On
success the final line is exactly `hp_fuzz: 1200 cases ok` followed by exit
code 0. All containers are released exactly once at the end (`drop()` is
idempotent).

## 4. Validity policy — no UB inputs, no bug claims

The harness deliberately **does not** generate undefined-behaviour inputs:

- no NULL pointers to APIs that require non-NULL buffers;
- no indices beyond a container's extent (checked reads past the end are
  avoided; where a checked read is exercised at `size()`, the API's documented
  guarded result is folded into the digest but never dereferenced);
- no overflowing sizes, negative capacities or `SIZE_MAX` requests;
- no use-after-free, double-free, type confusion, or stale slice reads;
- no raw memory offsets or hand-rolled allocator calls.

The only "failure" outcomes generated are documented recoverable ones:
removing/getting an absent key (`-1`), `field()` past the last field (`-1`),
`at_checked()` just past the end (`-1`), and EOF (`STDLIB_EOF`, with the error
value still OK) from the `io.hp` readers. These are asserted against the shadow
model, so they are coverage, not expected crashes.

Consequently: a crash, hang, sanitizer report or invariant mismatch from this
harness is evidence of a real library defect. Conversely, inputs that violate
a documented precondition are **not** fuzzed here and must not be used to claim
a bug. Deliberately fatal safety checks belong in dedicated forked-child
tests (see §6), not in this process.

## 5. Build and run (exact commands)

All commands run from the repository root. `build/c+` is the compiler built by
the project; the three required modes are below. Each binary is run under an
external `timeout` so a hang is reported rather than masking a pass.

```sh
mkdir -p build/hp-fuzz

# 1. Default mode
./build/c+ build tests/stdlib/fuzz/fuzz_hp.cp \
    -o build/hp-fuzz/fuzz_hp_default
timeout 30s ./build/hp-fuzz/fuzz_hp_default

# 2. Strict mode, sanitizers off
./build/c+ build tests/stdlib/fuzz/fuzz_hp.cp \
    --strict --sanitize=none \
    -o build/hp-fuzz/fuzz_hp_strict_none
timeout 30s ./build/hp-fuzz/fuzz_hp_strict_none

# 3. UBSan (strict + undefined-behaviour sanitizer)
./build/c+ build tests/stdlib/fuzz/fuzz_hp.cp \
    --strict --sanitize=undefined \
    -o build/hp-fuzz/fuzz_hp_ubsan
timeout 30s ./build/hp-fuzz/fuzz_hp_ubsan
```

Optional type-check / warning checks:

```sh
./build/c+ check tests/stdlib/fuzz/fuzz_hp.cp
./build/c+ build tests/stdlib/fuzz/fuzz_hp.cp \
    --strict --sanitize=none -Werror -o build/hp-fuzz/fuzz_hp_werror
```

Expected stdout of every successful run (no leading or trailing noise):

```
hp_fuzz_digest 4237bdfb74e154a4
hp_fuzz: 1200 cases ok
```

A correct run produces no stderr. A nonzero exit, an `INVARIANT` line, a
different digest, or any sanitizer text on stderr is a failure.

## 6. Subprocess and timeout guidance for fatal cases

This harness never induces a fatal runtime failure, so its own process always
reaches the success line. When extending the suite with deliberately fatal
scenarios (e.g. a double free, a type confusion, or a bounds violation that
the runtime must catch), use the following discipline so the parent process
stays alive and a hang cannot stall the suite:

1. **Never run a fatal case in the harness process.** Fork a child, or launch
   a separate subprocess, perform the deliberate violation there, and let it
   exit with the expected `CPLUS_EXIT_*` code. The parent records the child's
   status and continues. Example shape (POSIX shell):

   ```sh
   timeout 10s ./build/hp-fuzz/fatal_child >child.out 2>child.err
   rc=$?
   # rc == 124 means timeout (hang); rc == 0 means the expected fatal did not
   # fire; any other rc is the runtime's fatal exit code and must be compared
   # against the documented CPLUS_EXIT_* value.
   ```

2. **Always wrap with an external timeout** (`timeout 10s ...`). Reaching the
   final line is the termination proof; a `124` exit is a hang, not a pass.
   Use `timeout -k 2s 10s` if a child can ignore `SIGTERM`.

3. **Separate streams.** Capture stdout, stderr and exit code independently.
   Sanitizer diagnostics (UBSan/ASan) appear on stderr; a fatal-case child may
   print there legitimately, but the normal harness must have an empty stderr.

4. **Do not hide failures with shell pipelines.** Use `set -e`/`set -u` where
   appropriate and `set -o pipefail` when piping, or avoid pipes and redirect
   to files as above. Check the exit code explicitly instead of relying on
   `tail`/`grep` status.

5. **Keep fatal cases out of the deterministic digest.** The digest is defined
   over valid operations only; folding results from a deliberately fatal child
   would make it depend on process boundaries and break reproducibility.

6. **Never claim a bug from a precondition violation.** If a child triggers a
   fatal because the harness passed an out-of-contract argument, that is a
   harness defect. Confirm the input is documented as valid before recording
   a library finding.

## 7. Verification results (this branch)

| Check | Result |
| --- | --- |
| DEFAULT build + run | pass — `hp_fuzz: 1200 cases ok`, digest `4237bdfb74e154a4`, exit 0 |
| `--strict --sanitize=none` build + run | pass — identical digest, exit 0 |
| `--strict --sanitize=undefined` build + run | pass — identical digest, exit 0, stderr 0 bytes |
| `c+ check` | `check: tests/stdlib/fuzz/fuzz_hp.cp ok` |
| strict + `-Werror` build | pass, no warnings |
| Determinism (5 runs, varied stdin) | identical digest on every run |
| Cases executed | 1200 valid operations per run |

**Warnings / limitations**

- AddressSanitizer is not available with the currently selected C++ backend
  (a compile-and-run probe rejects `-fsanitize=address`), so this harness was
  verified in default, strict/none and UBSan modes only. The three required
  modes all pass; this is an environment limitation, not a harness failure.
- The harness covers the documented success/recoverable paths of the `.hp`
  APIs. Deliberately fatal paths are intentionally excluded (§4, §6).
- It is a standalone program, not a libFuzzer target; "fuzz" here means
  deterministic grammar-driven generation over a fixed seed, which is why the
  digest is reproducible and suitable as a regression oracle.
