# Web server, SQL-like DB, game engine: modern libs + real apps (2026-11-24)

Additive modern libs added to this branch; verified by real apps (not demos).

| Component | lib | app | verifier |
|---|---|---|---|
| HTTP/1.1 server | `stdlib/web.hp` | `apps/webserver/` | `APPS` / `TEST_WEB` |
| SQL-like DB | `stdlib/sqldb.hp` | `apps/sqldb/` | `APPS` / `TEST_SQLDB` |
| Game engine core | `stdlib/gameeng.hp` | `apps/gameeng/` | `APPS` / `TEST_GAMEENG` |

Non-claims:
- POSIX sockets only (`web.hp`); server is single-threaded, serves a scripted
  request count, `usleep` in close as a settle hack.
- `sqldb` is a simple in-memory table with a text persistence format.
- `gameeng` is a headless world with deterministic `vec3`-free movement.
- No code copied; pure C+ extensions (`static inline`), no compiler changes.

Run: `sh apps/run_all.sh` and `sh tests/edge1000/run_edge1000.sh`.
