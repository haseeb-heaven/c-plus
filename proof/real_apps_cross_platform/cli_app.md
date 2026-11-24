# cli_app — calculator CLI demo

**Source:** `examples/cli_app/cli_calc.cp` (header `stdlib/cli.hp`).
**APIs used:** the full `cli.hp` surface — command and option registration,
`argv` parsing, status-based refusals, id-based dispatch and generated help.
This is the deterministic `add A B` path (bounded decimal operands, range
checked in `long` before printing); no clock, prompts, environment reads or
file I/O.

| platform | binary | transcript | screenshot | exit | exact invocation | exact marker line |
| --- | --- | --- | --- | ---: | --- | --- |
| macOS arm64 | `binaries/macos/cli_app` | `transcripts/macos/cli_app.txt` | `screenshots/cli_app-macos.svg` | 0 | `./cli_app add 2 3` | `CLI add 2 3 5` |
| Windows PE (Highball/Wine) | `binaries/windows/cli_app.exe` | `transcripts/windows/cli_app.txt` | `screenshots/cli_app-windows.svg` | 0 | `highball run -v ./cli_app.exe add 2 3` | `CLI add 2 3 5` |

This app is part of the re-run subset in `run.sh`: both invocations are
executed live and the exact stdout line is checked, not merely read from a
saved transcript.  Windows output also carries Highball's launcher line
(engine `x64-crossover26.3-r27`, renderer `dxvk`), which is runtime noise and
not part of the app marker.
