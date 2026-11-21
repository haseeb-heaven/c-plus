# exceptions4c provenance

- Upstream: https://github.com/guillermocalvo/exceptions4c
- Commit: 9f7879589936a53a037963502a25d8e07738a66b
- License: Apache-2.0 (see LICENSE/NOTICE; Copyright 2025 Guillermo Calvo)
- Vendored by: Agent 2 (PR #17, agent/modern-stdlib)
- Scope: `src/exceptions4c.{c,h}` copied verbatim. The single C+ exception
  engine; `stdlib/safe/safe_except.{h,c}` only re-exports TRY/CATCH/CATCH_ALL/
  FINALLY/THROW and adds a thread-local context supplier + RETHROW + a
  structured-error bridge. No separate engine is implemented.

## Public API used
- TRY / CATCH(type) / CATCH_ALL / FINALLY / THROW(type, fmt, ...) (upstream)
- e4c_get_exception, e4c_set_context_supplier (upstream)

## Local adaptations (glue only, in stdlib/safe)
- `cplus_except_init()` installs a `_Thread_local struct e4c_context` supplier so
  each thread owns its exception context.
- `RETHROW()` re-throws the currently caught exception via upstream `e4c_throw`.
- `CPLUS_CHECK` / `CPLUS_THROW_IF` bridge structured errors (errno_t/status) to
  exceptions. Libraries keep returning structured errors by default.
