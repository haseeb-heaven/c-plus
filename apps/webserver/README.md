# webserver — real curl-verified web server app (verified)

This is a real, verified C+ application (not a demo). Runner: `sh apps/webserver/run.sh`.

Verified verdict: `APPS 3 3 0` — aggregate of the `webserver`, `sqldb` and
`gameeng` runners (3 apps, 3 passed, 0 failed) from `sh apps/run_all.sh`.
Its library evidence is `stdlib/web.hp`.

## Screenshot (generated from an actual runner transcript)

![webserver](../../proof/all_apps_games_screenshots/cli_transcripts/webserver.svg)

Transcript + verifier: [`proof/all_apps_games_screenshots/webserver.md`](../../proof/all_apps_games_screenshots/README.md).
