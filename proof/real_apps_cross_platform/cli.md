# cli — bounded command/option registry

**Source:** `tests/stdlib/cli/test_cli.cp` (header `stdlib/cli.hp`).
**APIs used:** `cli` init, command and option registration, duplicate name,
long name and long help bounds, `cli_parse`, id-based dispatch and dispatch
refusal, deterministic `cli_help`, unknown option/command, no command, too
many args, bare dash, flag value, missing value, oversized and null tokens,
negative argc, and `cli_error` string rendering.  Successful parse output is
shown verbatim between `HELP_BEGIN` / `HELP_END` markers.

| platform | binary | transcript | screenshot | exit | exact final marker line |
| --- | --- | --- | --- | ---: | --- |
| macOS arm64 | `binaries/macos/cli` | `transcripts/macos/cli.txt` | `screenshots/cli-macos.svg` | 0 | `CLI_TEST 27 27 0` |
| Windows PE (Highball/Wine) | `binaries/windows/cli.exe` | `transcripts/windows/cli.txt` | `screenshots/cli-windows.svg` | 0 | `CLI_TEST 27 27 0` |

`CLI_TEST 27 27 0` means 27 total, 27 passed, 0 failed; the `CHECK ... PASS`
lines and help text match between the two captured transcripts.
