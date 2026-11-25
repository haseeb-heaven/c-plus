# Task 6 / Phase 4 — filesystem & path security notes

Target: `stdlib/fs.hp` (POSIX `stat`/`unlink`/`rename`/`mkdir`/`mkdtemp` wrappers).
Suite: `tests/security/stdlib/fs_security.cp`.
Branch: `agent/modern-stdlib`. Host: Darwin 27.2.0 arm64, euid 501 (non-root),
`PATH_MAX=1024`, `NAME_MAX=255`. Compiler: `build/c+` (C+ 0.1.0).

## What this suite is (and is not)

It is a **deterministic characterization / security audit** of `fs.hp`, not a
claim that `fs.hp` validates or sanitizes paths. `fs.hp` is intentionally a
thin, stateless wrapper: it has **no** path canonicalization, **no** confinement
root, **no** `lstat`/`O_NOFOLLOW`/`openat`/`fstatat` primitive, and **no** atomic
check-then-use operation. Each check records that reality so callers know what
they must enforce themselves.

**No defect was reproduced, so `stdlib/fs.hp` was NOT modified** (see
"Defect decision" below). Its sha256 at investigation time was
`d15847aed8d2cc750c1b7940fdc0bdf6995c171fa1493856796b81a8c9f82adf`.

## Exact commands and results

All builds/runs bounded with `timeout -k 5 30` (strict-first per instructions).

1. Strict / no-sanitizer build:
   `timeout -k 5 30 ./build/c+ build tests/security/stdlib/fs_security.cp --strict --sanitize=none -o /tmp/cplus_fs_security_strict`
   → `STRICT_BUILD_RC=0`.
2. Strict run:
   `timeout -k 5 30 /tmp/cplus_fs_security_strict </dev/null >…stdout 2>…stderr`
   → `STRICT_RUN_RC=0`; final line `FS_SECURITY 68 0`; `68` `FSQ_` lines, all `1`;
   stderr `0` bytes; no underflow/UB diagnostic.
3. Root SKIP branch (deterministic test hook, default 0 so no behavior change):
   `./build/c+ build … --strict --sanitize=none -o …_force_root -- -DFSQ_FORCE_ROOT=1`
   → build rc 0; run rc 0; prints
   `SKIP_REASON FSQ_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic`,
   `0` `FSQ_PERM` checks counted, final line `FS_SECURITY 64 0` (68 − 4 root-only
   permission checks), stderr `0` bytes. On this host (euid 501) the real,
   non-root permission branch executes and passes (4 `FSQ_PERM_*` checks = 1).
4. UBSan build (attempted second):
   `timeout -k 5 30 ./build/c+ build tests/security/stdlib/fs_security.cp --strict --sanitize=undefined -o /tmp/cplus_fs_security_ubsan`
   → `UBSAN_BUILD_RC=1`, **BLOCKED** (not a test failure):
   `c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`
   followed by `error: failed to compile instrumented C+ runtime`. This is the
   known external runtime/header mismatch also documented by the sibling suite
   (`tests/stdlib/system_safety/run.sh`). Per file-ownership rules I did **not**
   edit the compiler/runtime, so no UBSan runtime result is claimable. The
   strict/no-sanitizer result above is the verified baseline.

## Findings by area (all PASS; characterization, not "hardening claims")

1. **Path traversal via `..`** — `FSQ_TRAV_*` (8 checks): a path such as
   `<sandbox>/work/../safe/secret.txt` resolves through the OS; `fs_exists`,
   `fs_is_file`, `fs_size` all follow it. The confined `work/` dir has no secret
   of its own, proving `fs.hp` provides **no confinement**. The caller must
   enforce any root boundary.
2. **Symlinked-directory traversal** — a path with **no `..` text**
   (`work/escape_link/secret.txt`) still reaches the secret because the link
   points outside. This is the concrete reason a naive string `..` filter is
   **not** a defense; `fs.hp` performs no such filter and does not resolve links.
3. **Symlinks: file / dir / dangling** — `FSQ_SYM*`, `FSQ_DANGLING*` (16 checks):
   `stat()` follows links (link-to-file → `fs_is_file==1`, link-to-dir →
   `fs_is_dir==1`, dangling → all queries report absent and `fs_size` returns a
   defined `IO`/`CPLUS_EXIT_IO` error). `fs_remove()` uses `unlink()` and thus
   removes the **link**, leaving the target intact.
4. **TOCTOU** — `FSQ_TOCTOU_*` (5 checks): deterministic check→swap→use. A
   stat-based size check returns `3`; after the symlink is swapped the same call
   returns `5`. **String checks do NOT prevent TOCTOU.** No lexical validation,
   canonicalization, or check could make this atomic; `fs.hp` exposes no
   `openat`/`O_NOFOLLOW`/`fstatat` pair, so the check/use gap is inherent. Callers
   needing atomicity must use descriptor-relative, `O_NOFOLLOW`-style OS
   primitives themselves. (This is documented, not asserted as fixed.)
5. **Temporary paths** — `FSQ_TMP_*` (6 checks): `fs_mkdtemp` requires a mutable
   template ending in `XXXXXX`; it delegates exclusive creation to `mkdtemp(3)`
   (no clobber, random suffix). Two identical templates yield **distinct**
   paths. A template without `XXXXXX` → `INVALID`; a missing parent → `IO`; a
   short/empty buffer → `INVALID`. **PID uniqueness is supplied by the caller's
   prefix**, not by `fs.hp`; the test uses a PID-unique `/tmp` prefix.
6. **Unsafe replacement** — `FSQ_RENAME_*` (7 checks): `fs_rename` is `rename(2)`;
   it **silently overwrites** an existing destination (`OLD`→`NEW`), moves to a
   fresh name, and only fails when the target is a directory (`IO`). There is
   **no no-clobber** option (`link()` / `renameat2(RENAME_NOREPLACE)`) in
   `fs.hp`. This is the documented API contract, preserved, not "fixed".
7. **Permissions** — `geteuid()==0` prints an explicit, non-passing `SKIP_REASON`
   line (root bypasses DAC → nondeterministic). Non-root: a `0000` directory
   makes `fs_is_file` report absent and `fopen` fail; cleanup restores mode and
   removes fixtures. Verified both branches (steps 2 and 3).
8. **Directory traversal via existing APIs** — `FSQ_ITER_*` (4 checks): `fs.hp`
   has no iteration API; callers use `opendir`/`readdir`/`closedir`. `readdir`
   returns `.` and `..`, entries are found, and `fs_is_dir`/`fs_is_file`
   classify them. Callers must avoid blindly following entry symlinks.
9. **Malformed paths** — 16 checks: `NULL` → `INVALID` (queries are false, ops
   fail); empty string → `IO`; embedded NUL truncates at the first NUL (defined
   POSIX syscall behavior, proven with an explicit-length copy); `PATH_MAX+`
   and `NAME_MAX+1` paths → defined `IO` failures, never an overflow; malformed
   `mkdtemp` templates → `INVALID`.
10. **Cleanup / leak proof** — `FSQ_BASELINE`, `FSQ_NO_LEFTOVERS`,
    `FSQ_FD_NO_LEAK`: PID-unique `/tmp` prefix count returns to 0 and `/dev/fd`
    count is unchanged, so no filesystem or descriptor leak is left.

## Defect decision

No memory-safety error, wrong error category/code, missing reset, or crash was
reproduced under strict/no-sanitizer. The security-relevant behaviors above
(traversal, symlink following, TOCTOU, rename clobber) are the intended,
documented semantics of a thin POSIX wrapper; "fixing" them would change the
public API/behavior. Therefore **`stdlib/fs.hp` was left unchanged**. Strict-safe
patterns only were used: fixed stack buffers, `snprintf`, explicit-length
`__builtin_memcpy`, `FILE*` I/O, and bounded libc calls — no raw pointer math.

## Test protocol

Each executed check prints `<TAG> <1|0>`; SKIPs print `SKIP_REASON <TAG> …`;
the final line is `FS_SECURITY <checks> <failures>`; exit 0 iff failures == 0.
No header or runtime file was modified. All fixtures live under a PID-unique
`/tmp` prefix and are removed before exit.
