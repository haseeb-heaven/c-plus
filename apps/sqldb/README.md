# sqldb — real SQL-like database app (verified)

This is a real, verified C+ application (not a demo). Runner: `sh apps/sqldb/run.sh`.

Verified verdict: `APPS 3 3 0` — aggregate of the `webserver`, `sqldb` and
`gameeng` runners (3 apps, 3 passed, 0 failed) from `sh apps/run_all.sh`.
Its library evidence is `stdlib/sqldb.hp`.

## Screenshot (generated from an actual runner transcript)

![sqldb](../../proof/all_apps_games_screenshots/screenshots/sqldb.svg)

Transcript + verifier: [`proof/all_apps_games_screenshots/sqldb.md`](../../proof/all_apps_games_screenshots/sqldb.md).
