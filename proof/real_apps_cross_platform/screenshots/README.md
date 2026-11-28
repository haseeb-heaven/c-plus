# Cross-platform screenshots — captured terminal output

These SVGs are **rendered terminal-output screenshots**. Each one is produced
from a real captured run transcript in `../transcripts/<platform>/<app>.txt` by
`render.sh` (POSIX sh + awk, no Python). They show the program's stdout/stderr
as text in a terminal-window frame.

They are **not** fabricated GUI captures. No window pixels, icons or rendered
3D frames were invented or painted here; the coloured window chrome, the green
`REFUSED` / `OK` token highlighting and the footer are decoration added by the
renderer. The exact captured text is also embedded, XML-escaped, in each SVG's
`<desc>` element so it stays greppable and auditable.

They are also **not a memory-safety proof**. A binary printing a marker, or a
program refusing an invalid input, is evidence for the recorded host, runtime
and inputs only. Safety evidence lives in the project's dedicated suites and
security documentation, not in these images.

## Source of each image

| SVG | Source transcript |
| --- | --- |
| `cli-macos.svg` | `../transcripts/macos/cli.txt` |
| `cli_app-macos.svg` | `../transcripts/macos/cli_app.txt` |
| `games3d-macos.svg` | `../transcripts/macos/games3d.txt` |
| `gui_app-macos.svg` | `../transcripts/macos/gui_app.txt` |
| `math3d-macos.svg` | `../transcripts/macos/math3d.txt` |
| `mesh3d-macos.svg` | `../transcripts/macos/mesh3d.txt` |
| `scene3d-macos.svg` | `../transcripts/macos/scene3d.txt` |
| `widgets-macos.svg` | `../transcripts/macos/widgets.txt` |
| `cli-windows.svg` | `../transcripts/windows/cli.txt` |
| `cli_app-windows.svg` | `../transcripts/windows/cli_app.txt` |
| `games3d-windows.svg` | `../transcripts/windows/games3d.txt` |
| `gui_app-windows.svg` | `../transcripts/windows/gui_app.txt` |
| `math3d-windows.svg` | `../transcripts/windows/math3d.txt` |
| `mesh3d-windows.svg` | `../transcripts/windows/mesh3d.txt` |
| `scene3d-windows.svg` | `../transcripts/windows/scene3d.txt` |
| `widgets-windows.svg` | `../transcripts/windows/widgets.txt` |

A different result on one platform (for example `gui_app-macos.svg` showing an
`OK` describe result while `gui_app-windows.svg` shows a defined refusal) is
not a contradiction: the two transcripts record different invocations. Read the
source transcript linked above for the exact command line, which is also shown
in the image header.

## How they are made

```sh
cd proof/real_apps_cross_platform/screenshots
./render.sh
xmllint --noout *.svg        # optional, if xmllint is installed
```

`render.sh` discovers `*.txt` files under `../transcripts/macos/` and
`../transcripts/windows/`, skips the `#` metadata header lines (or reads only
the region between `BEGIN/END VERBATIM OUTPUT` markers when present), and
writes one `<app>-<platform>.svg` per transcript. It is bounded: at most 64
images, at most 160 displayed lines per image (with an explicit
`[renderer notice — not captured]` line when truncated), and a width cap of
1440 px. A stray carriage return from a CRLF transcript is stripped so the SVG
text stays well-formed; no other content is altered.

The Windows transcripts record real Windows PE runs through Highball/Wine.
Their headers note that Highball/Wine noise was stripped to leave the app's
marker lines; the SVGs show those lines verbatim and carry the runtime line in
the `<desc>` metadata. No claim of native Windows execution is made here.

## Integrity

`SHA256SUMS.txt` lists SHA-256 digests for the SVGs with relative names.
Verify with:

```sh
cd proof/real_apps_cross_platform/screenshots
shasum -a 256 -c SHA256SUMS.txt
```
