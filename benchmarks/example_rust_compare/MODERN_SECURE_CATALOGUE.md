# Modern GUI + secure apps catalogue (with safe-Rust comparison verdicts)

Scope: the eight examples under `examples/modern_gui/` and
`examples/secure_apps/`, plus the safe-Rust models and comparison harness in
`benchmarks/example_rust_compare/modern_secure/`. This catalogue is an index
and evidence summary; it does not replace the per-group `README.md`,
`NOTES.md` and `RESULTS.md` files.

> **Teaching / non-production.** All eight C+ examples are headless,
> deterministic teaching apps. A "vault", "firewall", "rate limiter" or "game"
> name describes the *model* it demonstrates, not a deployed product. They are
> **runtime refusal evidence** for checked-stdlib usage — not proofs of memory
> safety, GUI correctness, security or freedom from all invalid states. The
> Rust models are likewise teaching artifacts, not production code.

## Headline counts

**8 examples catalogued — 8 MATCH, 0 DIFFERENT, 0 BLOCKED, 0 PENDING**
(82 fixed cases; `compare.sh` exit status 0). Every verdict was produced by a
real build and a `timeout 30` run of both sides; none was fabricated.

| # | Example | Group token | C+ source | Rust model | Verdict | Cases |
|---|---------|-------------|-----------|------------|---------|-------|
| 1 | Dashboard | `GUI_DASHBOARD` | `examples/modern_gui/dashboard/dashboard.cp` | `modern_secure/dashboard.rs` | MATCH | 20 |
| 2 | Settings panel | `GUI_SETTINGS` | `examples/modern_gui/settings/settings_panel.cp` | `modern_secure/settings.rs` | MATCH | 9 |
| 3 | Breakout HUD | `GFX_BREAKOUT` | `examples/modern_gui/games/breakout_ui/breakout_ui.cp` | `modern_secure/breakout_ui.rs` | MATCH | 10 |
| 4 | Invaders HUD | `GFX_INVADERS` | `examples/modern_gui/games/invaders_ui/invaders_ui.cp` | `modern_secure/invaders_ui.rs` | MATCH | 9 |
| 5 | Credential vault | `SECURE_VAULT` | `examples/secure_apps/vault/vault.cp` | `modern_secure/vault.rs` | MATCH | 10 |
| 6 | Input-validation firewall | `SECURE_FIREWALL` | `examples/secure_apps/firewall/firewall.cp` | `modern_secure/firewall.rs` | MATCH | 12 |
| 7 | Token-bucket rate limiter | `SECURE_RATELIMIT` | `examples/secure_apps/ratelimit/ratelimit.cp` | `modern_secure/ratelimit.rs` | MATCH | 10 |
| 8 | Audit log | `SECURE_AUDIT` | `examples/secure_apps/auditlog/auditlog.c` | `modern_secure/auditlog.rs` | MATCH | 2 |

Full per-case captures, stdout hashes and diffs are in
`benchmarks/example_rust_compare/modern_secure/RESULTS.md` and its `.build/`
directory.

## 1. Dashboard — `GUI_DASHBOARD`

- **Core:** three fixed scenarios (`overview`, `operations`, `diagnostics`),
  each with three labels and three named buttons with fixed ON/OFF states.
  One pure description is shared by the `describe` transcript and the headless
  `render` frame.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/modern_gui/dashboard/dashboard.cp --strict --sanitize=none -I. \
    $(pkg-config --cflags --libs sdl3) -Ithird_party/nuklear -Lbuild/ext -lnuklear -o <bin>
  ```
- **Run:** `SDL_VIDEODRIVER=dummy timeout 30 <bin> describe 0` (also `render`,
  `refuse`).
- **Safety refusals:** zero/negative dimensions, dimensions above
  `DASH_MAX_DIM` (4096), scenario id out of range, null GUI context, unknown
  `refuse` case, unknown command — each emits `DASH_REFUSED ... category <n>
  code <n> message ...` and a stable `DASH_RESULT`.
- **Rust comparison:** MATCH, 20 cases. The Rust model reproduces the scenario
  table, refusal codes (category 1/2, code 108/101) and render transcript;
  it opens no SDL window and creates no Nuklear context.

## 2. Settings panel — `GUI_SETTINGS`

- **Core:** six fixed scenarios that validate text, slider and flags before
  committing state; the accepted case renders two real Nuklear frames and
  reads back a 320x240 surface.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/modern_gui/settings/settings_panel.cp --strict --sanitize=none -I. \
    $(pkg-config --cflags sdl3) -Ithird_party/nuklear -Lbuild/ext -lnuklear \
    $(pkg-config --libs sdl3) -o <bin>
  ```
- **Run:** `SDL_VIDEODRIVER=dummy timeout 30 <bin> apply` (or any scenario
  name, or several names in one process).
- **Safety refusals:** oversized text (40 > 31), out-of-range slider (101),
  invalid flags mask, malformed UTF-8, null text and null GUI context. Each
  refusal leaves the state at its `UNSET` sentinel and reports a typed
  `category`/`code`.
- **Rust comparison:** MATCH, 9 cases. The Rust model emits the six fixed
  transcripts; it does not validate live UTF-8 or render real widgets.

## 3. Breakout HUD — `GFX_BREAKOUT`

- **Core:** fixed-seed xorshift ball physics over a bounded brick grid, with a
  real SDL3 software-rendered playfield and a Nuklear HUD whose four lines are
  echoed to stdout on every frame.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/modern_gui/games/breakout_ui/breakout_ui.cp \
    --strict --sanitize=none -I. $(pkg-config --cflags --libs sdl3) \
    -Ithird_party/nuklear -Lpath/to/libnuklear -lnuklear -o <bin>
  ```
  (The group `run.sh` builds a local `libnuklear.a` when the shared archive is
  absent. The harness passes the archive as an explicit linker operand after
  `--`, which is the form that links cleanly here.)
- **Run:** `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 <bin> RLSR`.
- **Safety refusals:** `--probe-null` guards a null surface and null Nuklear
  context; bad `--grid` dimensions (`4<=W<=32`, `1<=H<=8`, `W*H<=256`),
  oversized command token (>64 chars), too many ticks (>128), unknown
  command/option. Clean exit 2; guard diagnostics go to stderr.
- **Rust comparison:** MATCH, 10 cases. The Rust model reproduces the RNG,
  collision/score/lives state machine and frame/HUD transcript, including the
  stable `painted=1` pixel probe; no real surface is created.

## 4. Invaders HUD — `GFX_INVADERS`

- **Core:** fixed-seed ship column, fixed 3x5 enemy formation, bounded bullet
  pool (1..16), lives as a deterministic function of shots lost, and a HUD
  whose SCORE/LIVES/WAVE lines plus a two-pixel probe are echoed to stdout.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/modern_gui/games/invaders_ui/invaders_ui.cp \
    $(pkg-config --cflags --libs sdl3) -I. -Ithird_party/nuklear \
    -Lbuild/ext -lnuklear -o <bin>
  ```
- **Run:** `SDL_VIDEODRIVER=dummy timeout 30 <bin> LRF` (or `--bullets=2 FFFF`,
  or `-` with one stdin line).
- **Safety refusals:** `--null-surface`, `--null-context` fault injection,
  invalid `--bullets` capacity (1..16), oversized token, bad command, too many
  ticks, stdin argument/empty/oversize. Clean exit 2 with
  `REFUSED reason=... code=... category=... message=...` on stderr.
- **Rust comparison:** MATCH, 9 cases. The Rust model reproduces bullets,
  waves, lives, the 10x16 render grid and the `hud_nonbg=1` transcript; no SDL
  surface or Nuklear context is created.

## 5. Credential vault — `SECURE_VAULT`

- **Core:** in-memory fixed-capacity credential table (max 8 services, 16-byte
  names, 32-byte secrets), a bounded 16-record audit buffer, a 64-byte canary
  checked at exit, and chained commands separated by `--` in one process.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/secure_apps/vault/vault.cp --strict --sanitize=none -I. -o <bin>
  ```
- **Run:** `timeout 30 <bin> add mail s3cr3t -- get mail -- list -- audit`.
- **Safety refusals:** `duplicate-service`, `empty-name`, `empty-secret`,
  `oversized-name`, `oversized-secret`, `missing-service`,
  `index-out-of-range`, `vault-full`, `secret-logging-forbidden` (`debug`),
  `audit-overflow` (17th append refused, buffer unchanged), `bad-args`,
  `unknown-command`, `usage`. Output: `VAULT refuse <reason> code <n> cat
  <name>`. Secrets are emitted only by `get`.
- **Rust comparison:** MATCH, 10 cases. The Rust model uses a `Vec`-backed
  table, reproduces audit record formatting, refusal counts and canary-ok
  summary; it handles no real credentials and has no byte canary.

## 6. Input-validation firewall — `SECURE_FIREWALL`

- **Core:** ordered checks over one argv token for four policies (`token`,
  `pathseg`, `sqlsafe`, `text`), plus a CONTRAST line showing the consequence
  an unchecked `strcpy` would have for the same input.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/secure_apps/firewall/firewall.cp --strict --sanitize=none -I. -o <bin>
  ```
- **Run:** `timeout 30 <bin> token alpha` (exit 0) or `timeout 30 <bin> token
  admin` (exit 2).
- **Safety refusals:** empty input, >256 bytes, NUL markers (`%00`, `\0`,
  `\x00`, `%u0000`), C0/DEL control bytes, path traversal, SQL-injection
  markers, policy allow-list/pattern failures, unknown policy. Output includes
  `CONTRAST ...`, `REFUSED <rule> <code>`, `DETAIL ...`, and `FIREWALL
  verdict=...`.
- **Rust comparison:** MATCH, 12 cases. The Rust model reimplements the
  ordered checks and contrast arithmetic in safe `str`/byte operations; it
  does not call `stdlib/string.hp` or `stdlib/error.hp`.

## 7. Token-bucket rate limiter — `SECURE_RATELIMIT`

- **Core:** capacity/refill/request-count validation, monotonic ticks, cost
  bounds, and an integer token bucket that refills before each ALLOW/DENY
  decision. Refusal code is also the process exit status.
- **Build (documented):**
  ```sh
  ./build/c+ build examples/secure_apps/ratelimit/ratelimit.cp --strict --sanitize=none -I. -o <bin>
  ```
- **Run:** `timeout 30 <bin> 3 1 5 0 1 1 1 2 1 3 1 4 1`.
- **Safety refusals:** `RL_ERR_ARGS` (1), `RL_ERR_PARSE` (2),
  `RL_ERR_CAPACITY` (3), `RL_ERR_REFILL` (4), `RL_ERR_COUNT` (5),
  `RL_ERR_TICK` (6, negative/non-monotonic), `RL_ERR_COST` (7),
  `RL_ERR_OVERFLOW` (8). All print `REFUSED <code> <NAME> <detail>`.
- **Rust comparison:** MATCH, 10 cases. The Rust model uses `i128`
  intermediates for overflow detection instead of C+ checked builtins.

## 8. Audit log — `SECURE_AUDIT`

- **Core:** fixed synthetic audit events (code/kind/pointer/size) appended
  through the additive `cplus_violation_log_*` API, then a read-back of three
  fixed lines and a `RESULT <scenarios> <passed> <failed> <refused>` line.
- **Build (documented):**
  ```sh
  /usr/bin/clang -std=c99 -pthread -Ilibc/install-include \
    examples/secure_apps/auditlog/auditlog.c \
    build/libc/libcplus_hardened.a -o <bin>
  ```
- **Run:** `timeout 30 <bin> <log-path-in-a-temp-dir>`; the harness deletes
  the log before each run so the append/read-back stays deterministic.
- **Safety refusals:** seven total — `event_before_open` and
  `event_after_close` (real `EBADF`), `open_null_path`, `open_empty_path`,
  `event_null_kind`, `event_empty_kind` (real `EINVAL`), and
  `event_oversized_kind`, which is an **application-policy** refusal because
  the backend silently truncates `kind` at 96 bytes (`VIO_KIND_CAP`) and would
  otherwise corrupt an audit record.
- **Rust comparison:** MATCH, 2 cases. The Rust model emits the fixed
  transcript without opening, appending to or reading any file.

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/modern_secure/compare.sh
```

One `MODERN_COMPARE <name> <verdict> <reason>` line is printed per example.
`MATCH` = stdout bytes and exit status equal for all fixed cases. `DIFFERENT`
= at least one case differed (nonzero script exit). `BLOCKED` = build failure
or 30 s timeout (inconclusive). `PENDING` = source or Rust model not yet
created; the message includes the exact refresh command `sh <this-dir>/compare.sh`.
No MATCH is claimed for a missing source or model.

## Explicit non-equivalence and non-claims

- A MATCH attests **only** stdout-byte and exit-status agreement for the fixed
  cases. It is not proof of semantic equivalence, memory safety, absence of
  undefined behavior, or security.
- The Rust models deliberately omit the real GUI/graphics/Nuklear/SDL layers,
  the checked C+ stdlib calls, the byte canary and the file-backed audit log.
  Their hardcoded `painted=1` / `hud_nonbg=1` observations are valid for the
  documented headless (`SDL_VIDEODRIVER=dummy`, software-rendered) host only.
- Results were produced on macOS arm64 with SDL3 via `pkg-config`, vendored
  Nuklear, `build/c+`, `rustc 1.100.0` and `timeout 30`. No other platform,
  SDL version, video driver or renderer backend is claimed to match.
- A clean build or emulator-style check is **not** live-display or
  live-application success. Nothing here is production software.

See `examples/modern_gui/README.md`, `examples/secure_apps/README.md`, their
per-group `NOTES.md`, and
`benchmarks/example_rust_compare/modern_secure/RESULTS.md` for the underlying
evidence.
