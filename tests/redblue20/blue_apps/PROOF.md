# BLUE-TEAM 18/20 — `blue_apps` proof: real-world examples & cross-platform apps

Branch: `agent/modern-stdlib`
Result: **ALL PASS** — `BLUE20 apps 106000 106000 0`

## Objective

Build **every** app in `examples/real_world/` and `tests/cross_platform_apps/src/`,
then drive each with **>=1000 fixed hostile inputs** through a bounded driver and
prove: exit 0 (clean) or defined refusal, **no signal exits**, **no output
corruption**. Every outcome must be a pass.

## Scope (106 apps)

| Group | Count | Source | Compiler |
|---|---|---|---|
| Real-world C+ examples | 6 | `examples/real_world/{game_engine,snake_game,desktop_app,web_app,server_app,database_engine}.cp` | project `./build/c+ build … --default --sanitize=none -I.` |
| Cross-platform C apps | 100 | `tests/cross_platform_apps/src/app_0.c` … `app_99.c` | `/usr/bin/clang -O2 -Wall -Wextra -pedantic` |

All 106 binaries were rebuilt fresh from the checked-in sources into
`tests/redblue20/blue_apps/artifacts/`. **Build failures: 0.** All 106
`*.build.log` files are empty (no warnings, no errors): `count_nonne=0`.

## Exact commands

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/redblue20/blue_apps/run.sh
```

`run.sh` (ownership-safe) does only this:
1. `cd` to the repo root derived from its own location; `PATH` prefixed with
   `$ROOT/build`; `SDKROOT` unset.
2. Builds the 6 real-world examples with `timeout 90 ./build/c+ build … -o artifacts/real_world/$n`.
3. Builds the 100 cross-platform apps with `timeout 60 /usr/bin/clang … -o artifacts/cross_platform/app_$i`.
4. Aborts with `BUILD_FAILURES>0` if any build fails, else `exec python3 fuzz.py "$ROOT"`.

No source/header is generated, edited, added, or committed; all writes are
under `tests/redblue20/blue_apps/` only.

## Bounded driver (`fuzz.py`)

* **Fixed hostile corpus:** exactly `1000` scenarios per app, generated from
  fixed seed `0xB1AE20` (`11644448`) plus 20 hand-crafted attack classes. Fully
  deterministic and reproducible. Total invocations: `106 apps x 1000 = 106000`.
* **Bound:** every invocation has a `10 s` per-process timeout; the driver runs
  `8` concurrent workers (8 CPUs). Wall-clock elapsed: `308.84 s`.
* **Attack classes exercised:** empty input; 10 KB stdin; 10 KB argv; SIZE_MAX
  (`18446744073709551615`); negative/overflow integers (`-1`, `-2`, `INT_MIN`,
  `LLONG_MIN`, huge decimals); shell/SQL/injection strings (`DROP TABLE`,
  `rm -rf /`, `;sudo`, `$(id)`, backticks, `|&;<>`); deep bracket/paren nesting
  (10k); format strings (`%s%n%d%p`, `%99999999s`); all 256 byte values;
  invalid UTF-8 / bad BOM; 500 argv entries; NUL floods on stdin (argv cannot
  hold NUL — stripped defensively); CR/LF/control-character floods; path
  traversal (`../../etc/passwd`, `/dev/zero`, `/proc/self/mem`); HTTP request
  smuggling (`Content-Length: -1`); valid multi-byte Unicode; seeded random
  binary blobs (0–4096 bytes) and seeded random printable strings.

### Pass / fail contract (conservative)

The apps are deterministic cores that read neither argv nor stdin, so hostile
input must be ignored cleanly. Their documented contract encodes **defined
refusal** as sentinel tokens in stdout (a `-1` for 105 apps; HTTP `404` status
for `web_app`) while still exiting 0. An invocation PASSES only when:

```
returncode == 0  AND  stdout == exact expected contract bytes  AND  stderr == b""
```

Anything else is a FAIL, bucketed as `timeout` (124/137), `signal` (rc < 0),
`nonzero` (unexpected nonzero exit / no defined refusal), or `corrupt`
(exit 0 but wrong stdout or non-empty stderr). Expected stdout strings are
hard-coded from the documented contract (independent of the binaries); each
app additionally gets a baseline empty-input run checked against that contract
before the fuzz loop (`BASELINE_MISMATCH`/`MISSING` would abort with rc 2).

## Exact results (stdout of the command above)

```
BUILD_FAILURES=0
BLUE20 apps 106000 106000 0
apps=106 scenarios_per_app=1000 workers=8 timeout_s=10 seed=11644448 elapsed_s=308.84
counts={"clean": 106000, "corrupt": 0, "nonzero": 0, "signal": 0, "timeout": 0}
detail_counts={"clean:exit0_exact_output": 106000}
refusal_verified_apps=106/106
findings=0
```

Interpretation:

* **106000 invocations**, **106000 passed**, **0 failed**.
* `signal = 0` → no signal exits (SIGSEGV/SIGABRT/SIGBUS/SIGILL/etc.).
* `timeout = 0` → no hangs; every process terminated within the 10 s bound.
* `corrupt = 0` → stdout byte-for-byte identical to contract on every run;
  stderr empty on every run.
* `nonzero = 0` → no unexpected exit codes; defined refusal confirmed on all
  106 apps (`refusal_verified_apps=106/106`).
* `findings = 0`; process exit code of `run.sh` = **0** (all pass).

Full machine-readable evidence: `tests/redblue20/blue_apps/results.json`
(per-app `passed: 1000 / failed: 0` for all 106 apps, empty `findings`).

## Ownership / honesty

* Only files created: `tests/redblue20/blue_apps/{run.sh,fuzz.py,PROOF.md,results.json}`
  and build artifacts beneath `tests/redblue20/blue_apps/artifacts/`.
  `git status --porcelain -- tests/redblue20/blue_apps` shows only `?? tests/redblue20/blue_apps/`.
* No `git add`, no `git commit`, no edits to source, headers, build files, or
  any other agent's directory. (Other modified/untracked paths in the shared
  worktree belong to other agents and were not touched.)
* These apps consume no external input; the proof demonstrates that the full
  hostile corpus is ignored cleanly and that execution is deterministic,
  signal-free, and output-correct. It is evidence for these fixed inputs under
  these build flags — not a claim of arbitrary-input parsing safety or of a
  live-network/game runtime.
