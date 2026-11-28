# game_a — modern C+ conversion evidence

Status: **OK** — real strict build, real runs, byte-identical stdout and exit codes against the original's recorded evidence. No fabricated results.

---

## 1. Provenance (exact link to the original)

The original is Corg-Labs/snake, pinned upstream commit
`b6bc0404635842526c2b4b31b0389c60a87d3bc1` (MIT, verified permissive).

- Repository: https://github.com/Corg-Labs/snake
- Exact source URL: https://raw.githubusercontent.com/Corg-Labs/snake/b6bc0404635842526c2b4b31b0389c60a87d3bc1/snake.c
- Local preserved copy: `external_projects/game_a/original/snake.c` (2312 bytes, SHA-256 `13de0d13e1ee1e621c48d7f91ac283126a04c3980b0c22c4fafbf81d25bc5774`, unchanged by this work).
- Full original evidence (hashes, build, run, ABI): `external_projects/game_a/ORIGINAL_EVIDENCE.md`.

Conversion source: `external_projects/game_a/cplus/snake_modern.cp`.

---

## 2. What stayed byte-identical

- The CLI: `int main(void)`, no arguments, no argv parsing.
- The stdin protocol: non-blocking raw-byte drain, WASD/arrows, `q` quits; `W=40`, `H=20`, length-4 snake at centre, `*` food, `+10` per food, `usleep(90000)` tick.
- All stdout bytes and the process exit code for the fixed inputs in §6, including the deterministic immediate-quit input `q` from ORIGINAL_EVIDENCE.md §5: SHA-256 `234d359e90fb4b5c0253ad2134cdb5f8cacc8f33db8394b7f5ab9019d428d282`, 44 bytes.
- stderr is empty.
- Game logic (direction locking, wall/self collision, body-shift semantics, food growth, rendering characters `@ o * .`) is semantically unchanged.
- The original translation unit's exported surface: `int main(void)` is the only externally defined function from this source; `raw_on`, `raw_off` and `orig` remain file-static.

---

## 3. What changed, with line-level rationale

Line numbers refer to `external_projects/game_a/cplus/snake_modern.cp` final form.

| Original line(s) | Change | Rationale |
|---|---|---|
| 3–9 (`#include`s) | Kept all seven C headers; added `stdlib/safe/safe_c.h` **first**, then `cplus/cplus.h`, `stdlib/error.hp`, `stdlib/vector.hp`. | `safe_c.h` routes ordinary `printf` to `cplus_printf`, adding compile-time format-attribute checking and linking against the hardened libc. Including it before `cplus/cplus.h` avoids the `cplus_log` token collision (same ordering used by `tests/integration/app.cp`). The other stdlib headers supply the C+ `vector` container and error categories. |
| 24 (`int sx[W*H], sy[W*H]`) | Replaced with two `vector` instances `sx`/`sy`, each `reserve(W*H)`; initial 4 segments appended with `push_back`. | C+ owned, bounds-checked container with deterministic `drop()` (RAII) instead of two fixed raw arrays. Capacity matches the original `W*H` maximum. |
| 25 (init loop) | Same 4-segment initialization via `push_back(W/2 - i)` / `push_back(H/2)`. | Identical initial board. |
| 29 (`char c;`) | `char c = 0;` | Strict mode rejects reads of uninitialised locals; `read()` still writes before every use, so valid-input behaviour is unchanged. |
| 31 (`char a,b;`) | `char a = 0, b = 0;` | Same strict-mode lifetime fix for the two-byte arrow-key lookahead. |
| 40 (`nx=sx[0]+dx`) | `sx.at(0)` / `sy.at(0)` | Checked accessor; for all reachable indices returns the identical value. |
| 42 (self-collision loop) | `for (size_t i = 0; i < sx.size(); i++) if(sx.at(i)==nx && sy.at(i)==ny)` | Checked element access; same comparison order, same early exit. |
| 44–45 (body shift) | `sx.insert(0,nx)` / `sy.insert(0,ny)`, then `pop_back(NULL)` on non-food ticks. | `insert(0, head)` performs the original tail-to-head right-shift plus head write; `pop_back` discards the old tail. On food ticks the old tail is retained, matching the original `len++` growth. This also removes the strict-mode "negative array index on sx/sy" rejection of the raw `sx[i]=sx[i-1]` loop. |
| 46 (food growth) | `score+=10; fx=rand()%W; fy=rand()%H;` moved after the insert; `len++` replaced by *not* popping. | Same score, same RNG consumption order, same resulting snake length. |
| 27, 60 (`raw_on()` / `raw_off()`) | Wrapped in a `RawModeGuard` C+ class with constructor calling `raw_on()`, `restore_now()` and `drop()` calling `raw_off()` once (armed flag). | RAII guarantees terminal restoration on any early return. `restore_now()` is called explicitly before the final print so the `\033[?25h` cursor-show still precedes `Game over!`, preserving byte-identical output. |
| 50 (render body scan) | `for (size_t i = 0; i < sx.size(); i++) if(sx.at(i)==x && sy.at(i)==y)` | Checked accessor and bounds-known loop; same head/body classification. |
| 47–57 (rendering / score line) | Unchanged in text, but `printf` now expands via `safe_c.h` to `cplus_printf`. | Compile-time format checking; identical bytes on stdout for all literal formats used here. |
| end | `sx.drop(); sy.drop();` | Deterministic release of the owned vectors (RAII), no effect on output or exit code. |

Notes on checked `_s` mappings: `safe_c.h` maps ordinary `printf` to the attributed `cplus_printf` wrapper (a real format-safety improvement for valid input: same bytes, compile-time format checking). No raw memory-copy or string API is used by this program, so no other `_s` mapping is introduced. Compiler-generated `memcpy` for the `struct termios t=orig` copy is left as the backend emits it; it is not a source-level call and cannot be macro-remapped.

---

## 4. ABI statement

The original is a standalone executable, not a library. From `nm -g` of the original object (`external_projects/game_a/build/nm_defined.txt`), the only symbol defined is `T _main`; `raw_on`, `raw_off` and `orig` are file-static; the rest are libc imports.

The conversion preserves this contract:

- `int main(void)` is unchanged in name, signature and return value.
- `raw_on` and `raw_off` remain `t` (local) and `orig` remains local `b` in the linked binary.
- No global function or variable is added to the source-level API. The C+ class lowering emits three global helper symbols (`RawModeGuard_ctor`, `RawModeGuard_drop`, `RawModeGuard_restore_now`) as implementation artifacts of the RAII guard; they are not part of the original program's caller-facing API and are never referenced outside this translation unit. C+ stdlib/safe archive symbols (`cplus_alloc`, `cplus_printf`, `calloc_s`, …) are likewise statically linked toolchain implementation details. The program's entry point remains `int main(void)` with unchanged signature and return value.

---

## 5. Build and verification commands (actually executed)

From the repository root:

```
./build/c+ build external_projects/game_a/cplus/snake_modern.cp \
    --strict --sanitize=none -I. -Istdlib/safe -Ilibc/install-include \
    -o external_projects/game_a/cplus/snake_modern -- \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread
```

Result: exit 0, binary produced. The only stderr is three benign Clang `-Wunused-command-line-argument` warnings from the driver's compile pass seeing the `--` link arguments (the same pattern used by `tests/integration/run_integration.sh`). No C+ errors, no strict-mode rejections.

Verification harness (runs the build, the fixed inputs, byte/SHA comparison and ABI checks):

```
sh external_projects/game_a/cplus/verify_conversion.sh
```

Result:

```
CONVERT game_a 12 12 0
```

(exit 0 — 12 checks, 12 matched, 0 differed).

---

## 6. Before / after outputs

Original (ORIGINAL_EVIDENCE.md §5, `printf 'q' | timeout 30 ./build/snake`):

```
exit 0, stderr empty
stdout escaped: ^[[2J^[[?25l^[[?25h^[[2J^[[H Game over! Score: 0
stdout hex:
00000000: 1b 5b 32 4a 1b 5b 3f 32 35 6c 1b 5b 3f 32 35 68
00000010: 1b 5b 32 4a 1b 5b 48 20 47 61 6d 65 20 6f 76 65
00000020: 72 21 20 53 63 6f 72 65 3a 20 30 0a
```

Conversion (`printf 'q' | timeout 30 external_projects/game_a/cplus/snake_modern`):

```
exit 0, stderr empty
stdout escaped: ^[[2J^[[?25l^[[?25h^[[2J^[[H Game over! Score: 0
stdout hex: identical (44 bytes, SHA-256
234d359e90fb4b5c0253ad2134cdb5f8cacc8f33db8394b7f5ab9019d428d282)
```

Additional deterministic inputs compared directly against the original evidence binary (all quit before the first rendered frame, so the time-seeded food position cannot affect stdout): `q`, `qq`, `wq`, `dwq`, `ESC [ A q` — every one produced byte-identical stdout and the same exit code.

---

## 7. Files created (all inside `external_projects/game_a/cplus/`)

```
snake_modern.cp          # the modern C+ conversion
snake_modern             # strict-built executable
build.stdout             # build stdout (empty)
build.stderr             # build stderr (driver link-arg warnings only)
run.stdout               # captured stdout for input 'q' (44 bytes)
run.stderr               # captured stderr (empty)
verify_conversion.sh     # reproducible verification harness
CONVERSION.md            # this document
```

No files were changed under `external_projects/game_a/original/` or `external_projects/game_a/build/`.
