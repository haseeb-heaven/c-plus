# CLI app — `binaries/cli_calc`

Source: `examples/cli_app/cli_calc.cp`
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

The owning suite (`examples/cli_app/run.sh`) runs 17 scenarios: 17 passed /
0 failed, including a hostile 10 KB argument (`CLI refuse name or token too
long 3`) and a wide-product overflow refusal (`CLI overflow`).

## Reproduce

```sh
sh examples/cli_app/run.sh
proof/games_apps/binaries/cli_calc add 2 3
sh proof/games_apps/run.sh
```
