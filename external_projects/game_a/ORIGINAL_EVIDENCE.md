# game_a — Original upstream C game: provenance & ABI evidence

Status: **OK** — permissive license verified, original source preserved byte-for-byte, compiled and ran locally with recorded exact commands/exits. No fabricated results.

---

## 1. Provenance (exact)

| Field | Value |
|---|---|
| Project | Corg-Labs/snake — "► Classic Snake game in the terminal, in C." |
| Repository (HTML) | https://github.com/Corg-Labs/snake |
| Clone URL | https://github.com/Corg-Labs/snake.git |
| GitHub API (repo metadata) | https://api.github.com/repos/Corg-Labs/snake |
| Default branch | `main` |
| Pinned commit SHA | `b6bc0404635842526c2b4b31b0389c60a87d3bc1` |
| Commit verification | `git ls-remote` returned `b6bc040... HEAD` and `b6bc040... refs/heads/main` (HEAD == main tip) |
| Commit author / date | Anurag Singh <216409111+Oval17@users.noreply.github.com>, 2026-06-04T19:35:16Z |
| Commit message | `docs: add Tutorial section to README (#2)` |
| Commit tree SHA | `8b73512174318ee1753867b2e6f7f557ef0476d1` |
| Commit parent | `5d446b4fc0675db64db8774ebfc7af78f46dfef8` |
| Commit signature | GitHub API reports `verification.verified = true` (valid PGP signature) |
| Commit API URL | https://api.github.com/repos/Corg-Labs/snake/commits/b6bc0404635842526c2b4b31b0389c60a87d3bc1 |
| Download date/time (UTC) | **2026-11-23T06:46:46Z** |

Exact per-file download URLs (raw, pinned to the commit SHA — not a floating branch):

- `https://raw.githubusercontent.com/Corg-Labs/snake/b6bc0404635842526c2b4b31b0389c60a87d3bc1/snake.c`
- `https://raw.githubusercontent.com/Corg-Labs/snake/b6bc0404635842526c2b4b31b0389c60a87d3bc1/License`
- `https://raw.githubusercontent.com/Corg-Labs/snake/b6bc0404635842526c2b4b31b0389c60a87d3bc1/Readme.md`

Downloaded with `curl -fsSL -o <file> <pinned-raw-url>` into `external_projects/game_a/original/`.

### License verification (permissive — no GPL/AGPL)

- GitHub API repo metadata: `"license": {"key": "mit", "spdx_id": "MIT", ...}`.
- License text fetched from the pinned raw URL:
  `https://raw.githubusercontent.com/Corg-Labs/snake/b6bc0404635842526c2b4b31b0389c60a87d3bc1/License`
- Verified text begins `MIT License` / `Copyright (c) 2026 Corg-Labs` and contains the standard MIT grant + "AS IS" warranty disclaimer. It is **MIT**, which is a verified permissive license. There is no GPL/AGPL/copyleft text anywhere in the repository files.
- The saved copy is `external_projects/game_a/original/License` (hash below).

---

## 2. Original files — hashes (byte-for-byte preserved)

All three files were saved verbatim under `external_projects/game_a/original/`. SHA-256 was computed from the downloaded bytes; the git blob SHA-1 was computed with `git hash-object` and **matches the blob SHA-1 reported by the GitHub contents API for this commit**, proving the local bytes are byte-for-byte identical to the upstream objects.

| File | Size (bytes) | SHA-256 (local bytes) | git blob SHA-1 (local) | GitHub API blob SHA-1 | Match |
|---|---|---|---|---|---|
| `snake.c` | 2312 | `13de0d13e1ee1e621c48d7f91ac283126a04c3980b0c22c4fafbf81d25bc5774` | `7148c45271b7b68e7c0b0c5dc25c0b4639725eca` | `7148c45271b7b68e7c0b0c5dc25c0b4639725eca` | YES |
| `License` | 1066 | `4bee40b5b896b3546630630dbd4bdb9b65087e06a665db843840b11adfd29308` | `0d36d21c250aaa7df0ea759a75e8ad2aa7c8f6d6` | `0d36d21c250aaa7df0ea759a75e8ad2aa7c8f6d6` | YES |
| `Readme.md` | 5271 | `6b5a90307802b0aeddb32f0cdcd8558f7232326ade9fdc681aafd28560baec9a` | `84e3799389255914a9109dcfca7d324401391972` | `84e3799389255914a9109dcfca7d324401391972` | YES |

The above SHA-256 values were re-checked **after** the build and run (see §6) and were unchanged.

---

## 3. What the original is

A single-file terminal Snake game. Source: `original/snake.c`. It uses only the C standard library and POSIX/libc interfaces via `stdio.h`, `stdlib.h`, `string.h`, `time.h`, `unistd.h`, `termios.h`, `fcntl.h` — **no third-party libraries**. It renders with ANSI escape codes and reads WASD/arrow keys in raw terminal mode; `q` quits. Board `W=40`, `H=20`; snake starts length 4 at center; food `*`; score += 10 per food; tick `usleep(90000)`.

Note: this is a standalone **executable** (`int main(void)`), not a library. No header files, no exported API beyond `main`.

---

## 4. Build results (actually executed)

Working directory for all commands: `external_projects/game_a`

Exact command (plain C99, no extra link flags needed):

```
/usr/bin/clang -std=c99 original/snake.c -o build/snake
```

Result:

- **Exit code: 0**
- stderr: empty (`build/compile.stderr`, 0 bytes)
- `-lncurses`: **not needed** (no curses dependency)
- `-lm`: **not needed**

Produced binary:

```
-rwxr-xr-x  build/snake
build/snake: Mach-O 64-bit executable arm64
```

Object-file compile (for symbol inspection), also actually run:

```
/usr/bin/clang -std=c99 -c original/snake.c -o build/snake.o
```

Result: **Exit code: 0**, stderr empty (`build/compile_obj.stderr`, 0 bytes).

Compiler: `Apple clang version 16.0.0 (clang-1600.0.26.6)` (`/usr/bin/clang`). No warnings were emitted.

---

## 5. Run results (deterministic, actually executed)

Exact command (deterministic input: a single `q` byte makes the game quit on the first loop iteration; `timeout 30` guards against a hang):

```
printf 'q' | timeout 30 ./build/snake > build/run.stdout 2> build/run.stderr
```

Result:

- **Exit code: 0** (no timeout, no crash)
- stderr: empty (`build/run.stderr`, 0 bytes)
- stdout: 44 bytes (`build/run.stdout`), SHA-256 `234d359e90fb4b5c0253ad2134cdb5f8cacc8f33db8394b7f5ab9019d428d282`

stdout, escaped (`cat -v`):

```
^[[2J^[[?25l^[[?25h^[[2J^[[H Game over! Score: 0
```

stdout, hex:

```
00000000: 1b 5b 32 4a 1b 5b 3f 32 35 6c 1b 5b 3f 32 35 68  .[2J.[?25l.[?25h
00000010: 1b 5b 32 4a 1b 5b 48 20 47 61 6d 65 20 6f 76 65  .[2J.[H Game ove
00000020: 72 21 20 53 63 6f 72 65 3a 20 30 0a              r! Score: 0.
```

Interpretation: the ANSI sequences clear the screen/hide then show the cursor, and the program exits with `Game over! Score: 0` — the expected deterministic result for an immediate `q` quit. This is a genuine execution, not a fabricated pass. (Because stdin is a pipe rather than a TTY, the `tcgetattr`/`tcsetattr` raw-mode calls fail harmlessly; the control flow and output are unaffected.)

---

## 6. Post-build integrity re-check

Re-running `shasum -a 256 original/*` after compiling and running produced exactly the SHA-256 values in §2. The original files were never modified.

---

## 7. ABI evidence (`nm`, `otool`)

No wrapper, shim, or patched copy was produced. The ABI is **whatever the original source exports** when compiled as above. The only global symbol *defined* by the original translation unit is `main`; everything else is an imported libc symbol or an internal (non-external) symbol.

`nm -g build/snake.o` — global symbols (verbatim):

```
                 U ___chkstk_darwin
                 U ___stack_chk_fail
                 U ___stack_chk_guard
                 U ___stdoutp
                 U _fcntl
                 U _fflush
0000000000000000 T _main
                 U _memcpy
                 U _printf
                 U _putchar
                 U _rand
                 U _read
                 U _srand
                 U _tcgetattr
                 U _tcsetattr
                 U _time
                 U _usleep
```

`nm -u build/snake.o` — undefined/imported symbols (ABI dependencies on libc):

```
___chkstk_darwin
___stack_chk_fail
___stack_chk_guard
___stdoutp
_fcntl
_fflush
_memcpy
_printf
_putchar
_rand
_read
_srand
_tcgetattr
_tcsetattr
_time
_usleep
```

`nm -m build/snake.o` (Mach-O types) confirms `_main` is the only `external` defined symbol (`(__TEXT,__text) external _main`, `T`); `raw_on`, `raw_off`, and `orig` are `non-external` (file-static), and the string literals are local (`l_.str*`).

`nm -g build/snake` (linked executable) defines `__mh_execute_header` and `main` and imports the same libc symbols — no additional exports.

`otool -L build/snake`:

```
build/snake:
	/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)
```

Only the system C library is dynamically linked. There is no third-party ABI surface.

---

## 8. Explicit statements

- **Original source UNCHANGED.** The bytes under `external_projects/game_a/original/` are byte-for-byte identical to upstream commit `b6bc0404635842526c2b4b31b0389c60a87d3bc1`; SHA-256 and git blob SHA-1 are recorded in §2 and were re-verified after all build/run activity (§6). No edits were made to any original file.
- **ABI is whatever the original exports.** Only `_main` is exported by the original translation unit (plus the Mach-O `__mh_execute_header` in the linked executable); all other symbols are libc imports. No extra API, shim, or ABI-altering change was introduced.
- **No fabricated pass.** Every command in §4, §5, and §7 was executed on this machine with the exact exit codes and outputs recorded; raw logs are preserved in `external_projects/game_a/build/`.

---

## 9. Files created (all inside `external_projects/game_a/`)

```
original/snake.c          # verbatim upstream source
original/License          # verbatim upstream MIT license
original/Readme.md        # verbatim upstream README
build/snake               # compiled executable (evidence)
build/snake.o             # compiled object (symbol evidence)
build/compile.stderr      # empty
build/compile_obj.stderr  # empty
build/run.stdout          # 44 bytes recorded stdout
build/run.stderr          # empty
build/nm_defined.txt      # nm -g of object
build/nm_undefined.txt    # nm -u of object
build/nm_mach-o.txt       # nm -m of object
build/nm_executable.txt   # nm -g of executable
build/otool_L.txt         # linked dylibs
ORIGINAL_EVIDENCE.md      # this document
```
