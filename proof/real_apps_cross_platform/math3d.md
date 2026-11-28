# math3d — finite-checked 3D math

**Source:** `tests/stdlib/math3d/test_math3d.cp` (header `stdlib/math3d.hp`).
**APIs used:** `vec3` add/sub/scale/dot/cross/length/normalize, `mat4`
identity/multiply/translate/scale/rotate/perspective/look_at, `quat`
multiply/normalize/from-axis/round-trip, plus finite-input refusals for
NaN/Inf and zero-length-axis / zero-aspect / degenerate near-far guards.

| platform | binary | transcript | screenshot | exit | exact final marker line |
| --- | --- | --- | --- | ---: | --- |
| macOS arm64 | `binaries/macos/math3d` | `transcripts/macos/math3d.txt` | `screenshots/math3d-macos.svg` | 0 | `MATH3D 36 0` |
| Windows PE (Highball/Wine) | `binaries/windows/math3d.exe` | `transcripts/windows/math3d.txt` | `screenshots/math3d-windows.svg` | 0 | `MATH3D 36 0` |

Both runs are deterministic and bounded (macOS `timeout 30`; Windows
`highball run -v` with the three MinGW DLLs next to the `.exe`).  The marker
means 36 checks, 0 failures; earlier lines such as `t_m_perspective 1` and
`t_refuse_q_nonfinite 1` are all `1`.  This is execution evidence for these
fixed inputs only, not a general numeric-safety proof.
