# C+ complex examples

These are larger programs than `demos/`, included to show that C+ compiles
real applications unchanged. Most are **interactive** and are therefore only
built by `./apps/legacy_examples/build_examples.sh`, never run in automated verification.

## Build

```sh
./apps/legacy_examples/build_examples.sh
```

Binaries are written to `apps/legacy_examples/out/`.

## Examples

| file | what it shows |
| --- | --- |
| `games/2048.c` | ncurses game, C17 compatibility |
| `games/snake.c` | POSIX terminal game with a small `_kbhit` shim |
| `games/tetris.c` | larger ncurses game |
| `games/tiles.c` | termios-based sliding-tile game |
| `apps/cgame.c` | interactive C learning game, file-based progress |
| `apps/student.cp` | C+ CRUD app: `class`, `this->`, `auto`, `.len`, `print`, text DB |
| `system/macpulse.cp` | macOS system monitor using Mach/libproc, C+ classes, RAII and `IntVec` |

## Provenance

- `apps/student.cp` and `system/macpulse.cp` were supplied by the C+ project
  author (Haseeb Mir).
- `games/2048.c` is a console 2048 implementation by Maurits van der Schee.
- `games/tetris.c` comes from the `aacanadaa/tetrisplus` source downloaded
  during compatibility testing.
- `games/snake.c`, `games/tiles.c` and `apps/cgame.c` were downloaded as C
  compatibility probes; their original source URLs were not recorded in the
  test scripts.

Third-party sources are included for local demonstration and regression
testing. Verify their upstream licenses before redistributing them. They are
not part of the C+ compiler or standard library.
