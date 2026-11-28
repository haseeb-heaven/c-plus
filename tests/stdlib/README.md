# stdlib test suites: API-coverage policy

`tests/stdlib/run_coverage.sh` builds `tests/stdlib/coverage.cp` (a native C+
port of the former Python static scanner) and
`tests/stdlib/coverage_closure.cp`, then prints a per-header table. This page
records what the gate means and what is deliberately **not** claimed.

## What the number is

The percentage is **static API-call-site reachability**: for each public name
parsed from `stdlib/*.hp`, the tool looks for `name(` (or, for `drop`, the
owning type name) in the scanned test/example sources. It is not execution,
branch, or safety coverage. See `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md` for
the full list of false positives/negatives.

`coverage_closure.cp` makes the call sites explicit and checks their documented
return values at run time. Every call in it is real and deterministic; comments
or dead branches never count as coverage.

## CORE gate: >= 80%, no exceptions

Headers marked `CORE` in the report keep the original hard gate:

- every CORE header must reach **at least 80.0%** or `run_coverage.sh` exits 1;
- the threshold is never lowered and no CORE API is silently removed from the
  denominator;
- a missing CORE API is fixed by adding a real invocation to
  `coverage_closure.cp` (or to a scanned test), not by weakening the tool.

This includes the pure helpers that used to be unreported: `net.hp`, `web.hp`,
`scoped_mem.hp`, `ctl_span.hp`, `gameeng.hp` and `sqldb.hp`.

## INFORMATIONAL: frozen app-facing headers

Three headers are app-facing wrappers over optional UI dependencies. They are
listed in the table but marked `INFORMATIONAL` and are excluded from the CORE
gate:

| Header | Why it is informational |
| --- | --- |
| `gfx_sdl3.hp` | Needs SDL3 windowed rendering. The headless closure supplies no SDL3 link flags and has no display, so no frame can be created. |
| `nuklear_adapt.hp` | Needs the vendored Nuklear header (`third_party/nuklear`) and a live `nk_context`; neither is on this closure's include path. |
| `nuklear_cpp.hp` | Needs a live Nuklear context and the exceptions4c `TRY`/`CATCH` block around `nk_init`; not reproducible in the headless closure. |

Their uncalled APIs are printed under `NOT_COVERED informational app-facing
headers` with the exact reason above. **Do not fabricate a call** to satisfy the
scanner: a fake call would count in the text search while proving nothing.
These headers are exercised, where possible, by their own dedicated suites
(`tests/stdlib/nuklear_adapt/`, `tests/stdlib/nuklear_cpp/`,
`tests/crt/unit/test_gfx_sdl3_lifecycle.c` and the app proofs), not by this
dependency-free closure. The same rule applies to any future header that needs
SDL3, Nuklear, libuv, miniaudio or yaml: if the dependency is not part of this
build, report `NOT_COVERED` with the reason instead of inventing a call.

## NOT_COVERED: platform-conditional APIs

`net_sigpipe_scope_enter`/`net_sigpipe_scope_leave` and
`web_sigpipe_scope_enter`/`web_sigpipe_scope_leave` exist only under
`#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)`. This host defines
`SO_NOSIGPIPE`, so those functions are not compiled at all. Calling them from
the closure would not compile; the tool reports them explicitly as
`NOT_COVERED platform-conditional APIs` with that reason. Their CORE headers
still clear 80% on the strength of real, compiled calls (`net_suppress_sigpipe`,
`net_send_flags`, `web_timeout_ms`, `web_send_all`, and so on).

## Verification

```sh
# Full stdlib suite (builds third_party ext libraries first); must show no
# new real failures.
sh tests/stdlib/run_stdlib_tests.sh

# Gate under test: must exit 0 with all CORE rows >= 80%.
sh tests/stdlib/run_coverage.sh

# Compiler/runtime suite; must stay unchanged.
sh tests/run_tests.sh
```

A green run means "every CORE API has a real, checked call site and every CORE
header is >= 80%", **not** "the optional UI APIs were exercised".
