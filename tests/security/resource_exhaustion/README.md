# `resource_exhaustion` — bounded resource-exhaustion resilience fixtures

Task 6 Phase 3. Eight self-checking C+ harnesses that exercise allocation
pressure, descriptor/socket exhaustion, oversized collections/strings, excessive
JSON nesting, thread-creation failure and malformed-input hangs — **without ever
stressing the host**. Every harness is self-checking and every build/run is
bounded by `timeout -k 5 30` (30 s hard limit, 5 s SIGKILL escalation).

## Files

| File | Purpose |
| --- | --- |
| `run.sh` | bounded runner; strict/no-sanitizer first, then UBSan; table + JSON summary |
| `support/re_harness.hp` | `<tag> <1\|0>` checks, `SKIP_REASON`, final `RESOURCE_EXHAUSTION <checks> <failures>` |
| `support/re_watchdog.hp` | fork + SIGALRM backstop + SIGKILL escalation; exact wait status returned |
| `support/re_limits.hp` | soft-only RLIMIT helpers, bounded `fcntl` fd count |
| `re_mem_pressure.cp` | `stdlib_alloc_fail_*` pressure, latching loop in a watched child |
| `re_fd_exhaust.cp` | EMFILE in a child with lowered `RLIMIT_NOFILE` |
| `re_huge_collections.cp` | oversized vector/deque/intmap/string refused at pinned `STDLIB_ALLOC_LIMIT=4096` |
| `re_json_nesting.cp` | 1024-level balanced / 4096-level unterminated JSON, both watched |
| `re_string_growth.cp` | bounded append loop stopped by the limit; injected growth rollback |
| `re_thread_failure.cp` | `RLIMIT_NPROC` injection attempt with bounded loop; SKIP when not injectable |
| `re_socket_exhaust.cp` | AF_UNIX socketpair exhaustion + one controlled loopback TCP exchange |
| `re_malformed_guard.cp` | watchdog positive control + malformed JSON/UTF-8/number probes |
| `NOTES.md` | exact commands, results, SKIPs, limitations and findings |

## Run

```sh
sh tests/security/resource_exhaustion/run.sh                 # strict-none, then ubsan
sh tests/security/resource_exhaustion/run.sh --only re_fd_exhaust
sh tests/security/resource_exhaustion/run.sh --mode strict-none
sh tests/security/resource_exhaustion/run.sh --mode ubsan --json-stdout
```

Environment: `CPLUS_BIN` (default `<root>/build/c+`), `RE_OUT` (default
`<this dir>/.reout`), `RE_BUILD_TIMEOUT`, `RE_RUN_TIMEOUT`, `RE_KILL_AFTER`.

Exit status: `0` = at least one PASS and no FAIL/BUILD_FAILURE/TIMEOUT/BLOCKED;
`1` = a required case failed; `2` = setup/usage error. A declared in-test SKIP
(`SKIP_REASON …`) never counts as a pass; it is recorded in stdout, the JSON
summary and `NOTES.md`.

## Modes and exact build commands

Modes execute in a fixed order: `strict-none` first, `ubsan` second.

| Mode | Build command |
| --- | --- |
| `strict-none` | `timeout -k 5 30 build/c+ build <src> --strict --sanitize=none <extra> -o <bin>` |
| `ubsan` | `timeout -k 5 30 build/c+ build <src> --strict --sanitize=undefined -DCPLUS_EXIT_ARITHMETIC=110 <extra> -o <bin>` |

`<extra>` is `-Ithird_party/yyjson -Lbuild/ext -lyyjson` for JSON/yyjson
harnesses (three separate operands). The `-DCPLUS_EXIT_ARITHMETIC=110` shim is
**build-only**; it supplies a constant to the external instrumented runtime that
this branch's local header lacks (see `NOTES.md` §4). It is not a source edit
and does not suppress sanitizer diagnostics. Each binary is run as
`timeout -k 5 30 <bin> </dev/null` with separate stdout/stderr capture.

## Safety / strict-safe patterns

- **No host exhaustion.** Memory pressure uses only `stdlib_alloc_fail_at`,
  `stdlib_alloc_fail_after` and `stdlib_alloc_fail_reset` at 8–64 B, plus
  arithmetic-refused over-limit requests. `STDLIB_ALLOC_LIMIT` is pinned to
  4096 B in the size-related fixtures. No `malloc`-until-failure loop exists.
- **Injection is scoped.** Every RLIMIT change is soft-only, performed in a
  child, restored on all paths; caller-owned peers are all closed; every
  `stdlib_alloc_fail_*` arming is paired with a reset.
- **Bounded loops.** All loops have literal caps (12 allocations, 32 opens,
  24 socketpairs, 16 thread starts, 24 appends). Forked children carry a 2–3 s
  self-alarm *and* a 5–6 s parent watchdog.
- **Strict compliance.** No pointer-parameter subscripts (pointer arithmetic or
  `__builtin_memcpy`), no variable division/modulo, unique RAII local names,
  explicit `NULL` comparisons, `(void)!fflush(stdout)` before fork, and child
  results communicated only through `_exit()` codes so buffered output cannot
  be duplicated.
- **No vacuous passes.** Child exit codes are asserted by the parent; telemetry
  arithmetic and `stdlib_alloc_seq()` deltas prove "no allocation attempted";
  a watchdog positive-control child proves the guard itself is live.

## Known limitations

1. **No explicit yyjson depth limit.** `YYJSON_READER_DEPTH_LIMIT` is `0`
   (disabled) in the vendored header and `libyyjson.a`. The nesting fixture
   therefore imposes its own 1024/4096 caps and tests hang/crash resilience,
   not a parser depth bound.
2. **`pthread_create` failure is not injectable on this host** — `RLIMIT_NPROC`
   lowering did not cause a failure within 16 attempts; the case emits an
   explicit SKIP. The deterministic `thread::start` argument/lifecycle error
   paths are still verified.
3. **UBSan requires the build-only shim** due to an external compiler/runtime
   header mismatch; plain `c+ … --sanitize=undefined` builds are BLOCKED before
   any test code compiles (documented in `docs/security/verification-status.md`
   §3.1 and `docs/security/stdlib-threat-model.md` §7.4).
