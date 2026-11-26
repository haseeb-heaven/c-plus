# gameeng — real headless game engine app (verified)

This is a real, verified C+ application (not a demo). Runner: `sh apps/gameeng/run.sh`.

Verified verdict: `APPS 3 3 0` — aggregate of the `webserver`, `sqldb` and
`gameeng` runners (3 apps, 3 passed, 0 failed) from `sh apps/run_all.sh`.
Its library evidence is `stdlib/gameeng.hp`.

## Screenshot (generated from an actual runner transcript)

![gameeng](../../proof/all_apps_games_screenshots/cli_transcripts/gameeng.svg)

Transcript + verifier: [`proof/all_apps_games_screenshots/gameeng.md`](../../proof/all_apps_games_screenshots/README.md).
