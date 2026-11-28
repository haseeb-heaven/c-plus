# `extra_fs_edges.cp` — remaining UNTESTED `stdlib/fs.hp` paths

Branch `agent/modern-stdlib`. Point-in-time evidence from this run only; no
header was modified. This closes the reachable UNTESTED items in
`tests/stdlib/agent2_audit/module_audits/fs.hp.md` §7 (items 4, 6, 11-adjacent
metadata, 12, and the trailing-slash / dot-component path classes).

## Files created

- `tests/stdlib/agent2/extra_fs_edges.cp` — self-checking test.
- `tests/stdlib/agent2/extra_fs_edges_NOTES.md` — this document.

No other repository file was created, modified, staged or committed. Build
artifacts and logs were written to an `opencode` temp directory outside the
repository.

## What is covered (tag → UNTESTED item)

| Area | Tags | Closes / notes |
|---|---|---|
| Symlink → file | `FS_SYM_FILE_*` | §7.4 / §7.6: `stat` follows the link (`exists`/`is_file`/`size` see the target); `unlink` removes the link, target survives. `fs.hp` has no `lstat` wrapper — `lstat()` in the test proves link removal. |
| Symlink → directory | `FS_SYM_DIR_*` | §7.4 / §7.6: link resolves as a dir; `fs_remove` unlinks the link (POSIX) and leaves the dir intact; `fs_size(dir)` is a defined non-negative value. |
| Dangling symlink | `FS_DANGLING_*` | §7.4: `stat`-based queries are all false; `fs_size` is -1 / IO(109); `fs_remove` still unlinks the link. |
| Metadata | `FS_META_*` | §7.11-adjacent: `fs_size` is the only metadata helper in `fs.hp` (no mtime/atime wrapper exists); byte size asserted on a known 5-byte file; successful call resets a previously failed error record. |
| Overlong path | `FS_LONG_*` | §7.12: `PATH_MAX + 127` bytes → defined IO failure for exists/size/remove/mkdir; length asserted `> PATH_MAX`. |
| Trailing slash | `FS_SLASH_*` | new: dir + `/` resolves; regular file + `/` does not (`ENOTDIR`); missing dir + `/` is absent. |
| `.` / `..` components | `FS_DOT_*`, `FS_DOTDOT_*` | new: `dir/./child` and `dir/sub/../child` resolve to the same file; `dir/sub/..` resolves to the dir. |
| Repeated remove | `FS_REPEATED_REMOVE_MISSING` | §7.6: 4× `fs_remove` of a never-created path → always FAIL / IO(109), no crash. |
| mkdtemp reseed | `FS_MKDTEMP_*` | §7.1-style: failed call consumes `XXXXXX` (documented non-defect, repro below), second call without reseed fails template validation, reseeding the same buffer with a valid parent succeeds. |
| Permissions | `FS_PERM_*` / `SKIP FS_PERMISSIONS` | §8 skip policy: executed for real as non-root; explicit SKIP when `euid == 0`. |
| Cleanup proof | `FS_EDGE_BASELINE`, `FS_EDGE_NO_LEFTOVERS`, `FS_EDGE_FD_NO_LEAK` | PID-unique `/tmp` prefix and `/dev/fd` counts identical before/after. |

## Build and run commands (exact)

STRICT `--sanitize=none` was run **first**, then default, then UBSan. Every
build and every run was wrapped in `timeout 30` from the repository root:

```sh
build/c+ build tests/stdlib/agent2/extra_fs_edges.cp --strict --sanitize=none -o "$TD/extra_fs_edges_strict"
timeout 30 "$TD/extra_fs_edges_strict" >"$TD/strict.stdout" 2>"$TD/strict.stderr"

build/c+ build tests/stdlib/agent2/extra_fs_edges.cp --default -o "$TD/extra_fs_edges_default"
timeout 30 "$TD/extra_fs_edges_default" >"$TD/default.stdout" 2>"$TD/default.stderr"

build/c+ build tests/stdlib/agent2/extra_fs_edges.cp --sanitize=undefined -o "$TD/extra_fs_edges_ubsan"
timeout 30 "$TD/extra_fs_edges_ubsan" >"$TD/ubsan.stdout" 2>"$TD/ubsan.stderr"
```

## Per-mode results

| Mode | Build rc | Run rc | stdout last line | stderr | stdout identical to strict |
|---|---|---|---|---|---|
| `--strict --sanitize=none` | 0 | 0 | `EXTRA_FS_EDGES 55 0` | empty | (reference) |
| `--default` | 0 | 0 | `EXTRA_FS_EDGES 55 0` | empty | yes |
| `--sanitize=undefined` | 0 | 0 | `EXTRA_FS_EDGES 55 0` | empty | yes |

All 55 checks are `1` in every mode; 0 failures. No timeouts, no fatal exits,
no UBSan diagnostics. `diff` of the three stdout files is empty. Note: ASan is
unavailable on this host (per `docs/LIMITATIONS.md`), so no memory-safety
claim is made — these are behavioural/UBSan results only.

## Exact stdout (identical in all three modes)

```
FS_EDGE_BASELINE 1
FS_SYM_FILE_FIXTURE 1
FS_SYM_FILE_SETUP 1
FS_SYM_FILE_EXISTS 1
FS_SYM_FILE_ISFILE 1
FS_SYM_FILE_ISDIR 1
FS_SYM_FILE_SIZE 1
FS_SYM_FILE_REMOVE_LINK 1
FS_SYM_FILE_TARGET_INTACT 1
FS_SYM_FILE_CLEAN 1
FS_SYM_DIR_MKDIR 1
FS_SYM_DIR_SETUP 1
FS_SYM_DIR_EXISTS 1
FS_SYM_DIR_ISDIR 1
FS_SYM_DIR_ISFILE 1
FS_SYM_DIR_SIZE 1
FS_SYM_DIR_REMOVE_LINK 1
FS_SYM_DIR_CLEAN 1
FS_DANGLING_SETUP 1
FS_DANGLING_EXISTS 1
FS_DANGLING_ISFILE 1
FS_DANGLING_ISDIR 1
FS_DANGLING_SIZE 1
FS_DANGLING_REMOVE 1
FS_META_FIXTURE 1
FS_META_SIZE 1
FS_META_ERR_RESET 1
FS_META_CLEAN 1
FS_LONG_LENGTH 1
FS_LONG_EXISTS 1
FS_LONG_SIZE 1
FS_LONG_REMOVE 1
FS_LONG_MKDIR 1
FS_SLASH_MKDIR 1
FS_SLASH_DIR 1
FS_SLASH_FILE_FIXTURE 1
FS_SLASH_FILE_NOTDIR 1
FS_SLASH_MISSING 1
FS_SLASH_CLEAN 1
FS_DOT_SETUP 1
FS_DOT_COMPONENT 1
FS_DOTDOT_COMPONENT 1
FS_DOTDOT_DIR 1
FS_DOT_CLEAN 1
FS_REPEATED_REMOVE_MISSING 1
FS_MKDTEMP_FAIL_FIRST 1
FS_MKDTEMP_CONSUMED_NO_RESEED 1
FS_MKDTEMP_PARENT_MKDIR 1
FS_MKDTEMP_RESEEDED_OK 1
FS_MKDTEMP_CLEAN 1
FS_PERM_SETUP 1
FS_PERM_CREATE_DENIED 1
FS_PERM_CLEANUP 1
FS_EDGE_NO_LEFTOVERS 1
FS_EDGE_FD_NO_LEAK 1
EXTRA_FS_EDGES 55 0
```

stderr is empty in every mode.

## SKIPs

- **None triggered in these runs.** The test process ran with `euid = 501`,
  so the permission scenario executed for real and emitted `FS_PERM_SETUP 1`,
  `FS_PERM_CREATE_DENIED 1`, `FS_PERM_CLEANUP 1`.
- The explicit skip path is present and was reviewed. If `geteuid() == 0`, the
  program prints exactly:
  ```
  SKIP FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic
  ```
  and counts no pass/failure for that block (root bypasses DAC, so a
  create-denied assertion is nondeterministic). The final line would then read
  `EXTRA_FS_EDGES 52 0` (the three `FS_PERM_*` checks are not executed). This
  is intentional and is not a forced pass.

## Defects

**No defect was reproduced in `stdlib/fs.hp`.** All defined-failure paths
returned the documented categories (`IO`/109 for syscall failures,
`INVALID`/108 for bad templates) and no check produced a fatal code. The
test does not modify the header to force a pass.

### Documented non-defect: failed `fs_mkdtemp` consumes `XXXXXX`

A failed `fs_mkdtemp` leaves the caller's template mutated: libc `mkdtemp`
overwrites the six `X` characters with random characters *before* `mkdir`
fails. Reusing the buffer without reseeding therefore fails template
validation (`INVALID`), which looks like a library bug but is inherited from
libc and is already documented in `DEEP_FS_TASK5_CONTRACT.md:172-193`.

Minimal repro against this host (no header edits):

```c
/* cc repro.c -o repro && ./repro  */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
int main(void) {
    char t[128];
    snprintf(t, sizeof t, "/tmp/cplus_repro_no_parent_%d/XXXXXX", (int)getpid());
    printf("before: %s (has XXXXXX=%d)\n", t, strstr(t,"XXXXXX") != NULL);
    errno = 0;
    printf("mkdtemp=%s errno=%d\n", mkdtemp(t) ? "ok" : "NULL", errno);
    printf("after : %s (has XXXXXX=%d)\n", t, strstr(t,"XXXXXX") != NULL);
    return 0;
}
```

Observed on this host:

```
before: /tmp/cplus_repro_no_parent_507xx/XXXXXX (has XXXXXX=1)
mkdtemp=NULL errno=2
after : /tmp/cplus_repro_no_parent_507xx/EHGiGk (has XXXXXX=0)
```

Consequence for callers: rebuild the `XXXXXX` suffix before retrying. The test
pins this behaviour in `FS_MKDTEMP_CONSUMED_NO_RESEED` and proves recovery with
`FS_MKDTEMP_RESEEDED_OK`.

### Characterisation (not a defect): `stat` follows symlinks

`fs_exists`/`fs_is_file`/`fs_is_dir`/`fs_size` all use `stat()`, which follows
symlinks; there is no `lstat`-based wrapper in `fs.hp`. Therefore a link to a
file reports `is_file == 1`, a link to a dir reports `is_dir == 1`, and a
dangling link reports all queries false. This matches the header's documented
implementation and is asserted rather than treated as a failure.

## Cleanup

All fixtures use the PID-unique prefix `cplus_fse_<pid>_` under `/tmp`. Files
are removed with `fs_remove`, directories with `rmdir()`. `FS_EDGE_BASELINE`,
`FS_EDGE_NO_LEFTOVERS` and `FS_EDGE_FD_NO_LEAK` assert the `/tmp` prefix count
and `/dev/fd` count are unchanged. After all three runs, external inspection
found no artifacts:

```
$ ls -d /tmp/cplus_fse_*
(none)
```
