# macOS-native proof — real apps cross-platform (part: macOS)

Scope: **macOS arm64 native execution only.** These are the 8 prebuilt macOS
binaries from `tests/modern_libs_cross/out/bin/*_mac`, copied into
`proof/real_apps_cross_platform/binaries/macos/` with clean names
(`cli_mac` -> `cli`, `cli_app_mac` -> `cli_app`; all others drop the `_mac`
suffix). No Windows/PE result is claimed here.

Host: `uname -m` = `arm64`. `file` verified every copied binary as
`Mach-O 64-bit executable arm64`. SHA-256 digests (relative filenames) are
recorded in `binaries/macos/SHA256SUMS.txt`; they match the source `*_mac`
files byte-for-byte.

Each binary was run under `timeout 30` from `binaries/macos/` with
`PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin` and `SDKROOT` /
`CPLUS_CLANG` removed via `env -u`. `SDL_VIDEODRIVER=dummy` was set for
`games3d` and `gui_app`. `cli_app` was invoked with `add 2 3` and `gui_app`
with `describe 0` (the canonical deterministic-success invocations used in
`tests/modern_libs_cross/run.sh`). Exact commands, exit codes, stdout and
stderr are in `<name>.txt` beside this file. All stderr streams were empty.

| name | exit | marker final line | SHA-256 prefix |
| --- | ---: | --- | --- |
| math3d | 0 | `MATH3D 36 0` | `bd78e68c2813` |
| scene3d | 0 | `SCENE3D 79 0` | `e685d3faf8f0` |
| mesh3d | 0 | `drop 1 1` | `b4e1c289b421` |
| widgets | 0 | `WIDGETS 144 0` | `85252332a646` |
| cli | 0 | `CLI_TEST 27 27 0` | `1e68b21a8a4d` |
| games3d | 0 | `SCENE_DONE status=OK nodes=7 meshes=3` | `0e949fd5b622` |
| gui_app | 0 | `GUI_RESULT describe:0,0,0 OK` | `cf11d0a2cdff` |
| cli_app | 0 | `CLI add 2 3 5` | `b73c9ec2093f` |

Verdict: **8 binaries run, 8 pass, 0 fail** (pass = exit 0, empty stderr,
expected final marker line). This is execution proof for these exact
deterministic inputs on macOS arm64 only, not a general correctness or
memory-safety claim, and not evidence of Windows runtime behavior.
