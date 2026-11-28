# CLI app — `binaries/cli_calc`

Source: `apps/cli_app/cli_calc.cp` (real verified app, not a demo)
Library used: `stdlib/cli.hp`

## What it proves

A command registry with bounded option/argument parsing and id-based dispatch.
Valid commands compute correct results; unknown commands, oversized tokens,
missing values and arithmetic overflow are refused with stable codes.

## Evidence

- Binary: `binaries/cli_calc` (Mach-O arm64, SHA-256 in `SHA256SUMS.txt`)
- Build: `transcripts/cli_app.build.log`
- Run: `transcripts/cli_app.run.txt`

Captured lines:

```
CLI add 2 3 5                  EXIT=0
CLI mul 6 7 42                 EXIT=0
CLI refuse unknown command 7   EXIT=2
```

The owning app runner (`apps/cli_app/run.sh`) runs 17 scenarios: 17 passed /
0 failed, including a hostile 10 KB argument (`CLI refuse name or token too
long 3`) and a wide-product overflow refusal (`CLI overflow`). Verified
verdict: `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok`.
Screenshot: [cli_app.svg](../all_apps_games_screenshots/screenshots/cli_app.svg).

## Reproduce

```sh
sh apps/cli_app/run.sh
proof/games_apps/binaries/cli_calc add 2 3
sh proof/games_apps/run.sh
```
