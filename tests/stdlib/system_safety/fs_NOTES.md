# Task 3 Phase 3 — filesystem safety verification notes

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Compiler:** existing `build/c+` binary (not rebuilt)
**Limits:** every build and every run wrapped in `timeout -k 5 30`
**Ownership:** created only `tests/stdlib/system_safety/fs_*.cp` and this file.
`stdlib/fs.hp` was **not** modified (no library defect was reproduced). No
`git add`/`git commit` was run by this task. Temporary build artifacts were
placed in `tests/stdlib/system_safety/.fsrun/` and removed at the end.

## Files created

| File | Checks | Coverage |
| --- | --- | --- |
| `fs_basic_safety.cp` | 36 | missing file/dir, NULL path on every API, `err == NULL`, empty string, embedded NUL (truncation + explicit length), relative vs absolute via `chdir`/`getcwd`, NAME_MAX and NAME_MAX+1 component boundaries, `opendir`/`readdir` iteration, `fs_mkdtemp` temp file/dir cleanup, fd + `/tmp` leak proof |
| `fs_longpath_safety.cp` | 29 | path > PATH_MAX on queries/size/remove/mkdir/rename/both rename directions, overlong mkdtemp template, invalid templates (NULL, size 0, size 6, no `XXXXXX`, size == strlen caller rule), valid nested path just under PATH_MAX with 3 long components, cleanup |
| `fs_symlink_safety.cp` | 29 | symlink→file (`stat` follows, `lstat`/`readlink` see link, `fs_remove` unlinks only, target intact), symlink→dir, dangling symlink (queries 0, `fs_size` IO), `.` and `..` traversal components (documented OS normalization only), `fs_remove` on a directory fails, cleanup |
| `fs_ops_safety.cp` | 27 | rename over existing file replaces bytes, rename with missing source / missing destination parent leaves target intact, 8× repeated `fs_remove` of a missing path, directory iteration count, mkdtemp temp cleanup, **bounded 4-thread file-creation race** (`thread.hp`, join all, verify all, cleanup) |
| `fs_perm_safety.cp` | 12 | `chmod(dir,0000)`: `fopen`, nested `fs_mkdir`, `fs_mkdtemp`, `fs_remove` all denied with IO; dir still visible; access restored after `chmod 0755`; explicit SKIP when `geteuid()==0` |

**Total: 5 files, 133 checks, 0 failures, 5/5 runs rc=0 in each of 2 modes.**

## Output protocol

Each check prints `<TAG> <1|0>` (`1` = pass). Final line is
`FS_SAFETY <checks> <failures>`; exit code is 0 only when failures == 0.
A run is accepted only when build rc=0, run rc=0, stderr is empty, the final
line reports zero failures, and strict-none and UBSan stdout are byte-identical
(determinism). SKIP lines are not counted as passes or failures.

## Verification 1 — strict / no sanitizer (run FIRST)

Exact command shape (executed from the repo root for each of the five files):

```sh
mkdir -p tests/stdlib/system_safety/.fsrun
timeout -k 5 30 build/c+ build tests/stdlib/system_safety/fs_basic_safety.cp \
  --strict --sanitize=none -o tests/stdlib/system_safety/.fsrun/fs_basic_safety_strict_none \
  > .../fs_basic_safety_strict_none.build.log 2>&1
timeout -k 5 30 tests/stdlib/system_safety/.fsrun/fs_basic_safety_strict_none \
  </dev/null > .../<name>.out 2> .../<name>.err
```

Same shape for `fs_longpath_safety`, `fs_symlink_safety`, `fs_ops_safety`,
`fs_perm_safety`.

| Test | Build rc | Run rc | stderr | Final line |
| --- | --- | --- | --- | --- |
| `fs_basic_safety` | 0 | 0 | empty | `FS_SAFETY 36 0` |
| `fs_longpath_safety` | 0 | 0 | empty | `FS_SAFETY 29 0` |
| `fs_symlink_safety` | 0 | 0 | empty | `FS_SAFETY 29 0` |
| `fs_ops_safety` | 0 | 0 | empty | `FS_SAFETY 27 0` |
| `fs_perm_safety` | 0 | 0 | empty | `FS_SAFETY 12 0` |

No variable division/modulo and no pointer-parameter subscripts are used;
explicit-length copies use `__builtin_memcpy`, all loops are bounded, and every
local (including each RAII `thread` handle) has a unique name.

## Verification 2 — UBSan (run AFTER strict/no-sanitizer)

### Environment blocker (external, not fixed here)

The plain UBSan build fails before reaching any test code:

```
$ timeout -k 5 30 build/c+ build tests/stdlib/system_safety/fs_basic_safety.cp \
    --strict --sanitize=undefined -o /tmp/fs_ubsan_default_probe
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
RC=1
```

Cause: the external `c-plus-core` runtime source references
`CPLUS_EXIT_ARITHMETIC` (defined in its own `cplus.h`), while this branch's
runtime header (`runtime/include/cplus/cplus.h`) only defines exit codes
101–109. This is a toolchain/header mismatch outside this task's file ownership
and outside this repository's runtime sources. **No compiler or runtime file was
edited.**

### Workaround used for verification (backend define only, no file edits)

```sh
timeout -k 5 30 build/c+ build tests/stdlib/system_safety/<name>.cp \
  --strict --sanitize=undefined -o .../<name>_ubsan \
  -- -DCPLUS_EXIT_ARITHMETIC=110
timeout -k 5 30 .../<name>_ubsan </dev/null > .../<name>.out 2> .../<name>.err
```

| Test | Build rc | Run rc | stderr | Final line | stdout == strict |
| --- | --- | --- | --- | --- | --- |
| `fs_basic_safety` | 0 | 0 | empty | `FS_SAFETY 36 0` | yes |
| `fs_longpath_safety` | 0 | 0 | empty | `FS_SAFETY 29 0` | yes |
| `fs_symlink_safety` | 0 | 0 | empty | `FS_SAFETY 29 0` | yes |
| `fs_ops_safety` | 0 | 0 | empty | `FS_SAFETY 27 0` | yes |
| `fs_perm_safety` | 0 | 0 | empty | `FS_SAFETY 12 0` | yes |

## SKIP policy and reasons

`fs_perm_safety.cp` checks `geteuid()`. On this host **euid = 501 (non-root)**,
so no SKIP was emitted and all 12 permission checks executed. For a root run the
program prints (and does not count):

```
SKIP FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic
SKIP_REASON FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic
```

followed by `FS_SAFETY <checks> <failures>` with only the executed checks;
root bypasses DAC, so a denied-access assertion would be nondeterministic.
The reason is also emitted in the `SKIP_REASON` form consumed by the shared
`tests/stdlib/system_safety/run.sh` metadata collector.

## Cleanup evidence

Every fixture uses a PID-unique prefix: `cplus_fsb_`, `cplus_fsl_`,
`cplus_fss_`, `cplus_fso_`, `cplus_fsp_` (five different prefixes so parallel
agents cannot collide). Each file asserts:

- baseline `/dev/fd` count and `/tmp` PID-prefix count are captured at start;
- final counts are identical (`*_NO_LEFTOVERS`, `*_FD_NO_LEAK` all report 1);
- every created file is removed with `fs_remove`, every directory with `rmdir`;
- `fs_exists(path) == 0` is asserted after every removal, including failed
  operations (no artifact on IO/INVALID failures).

Final inspection after all runs:

```
$ for p in cplus_fsb_ cplus_fsl_ cplus_fss_ cplus_fso_ cplus_fsp_; do
    find /tmp -maxdepth 1 -name "${p}*" | wc -l; done
0 0 0 0 0
```

The permission fixture always restores `chmod 0755` before `rmdir`, including
the setup-failure branch.

## Defects

**Library defects reproduced: 0.** `stdlib/fs.hp` was not changed. Every
observed result matched the documented contract: `stat` follows symlinks,
`unlink` does not remove directories, `fs_mkdtemp` consumes the `XXXXXX` suffix
on failure, and `size == strlen(template)` is rejected by the `len >= size`
guard. These are documented non-defects, not fixes.

**Test-side defects found and fixed while establishing the contract: 2.**

1. `fs_basic_safety.cp` used `fs_mkdir(fsb_missing, NULL)` where the parent
   directory existed; the call correctly succeeded, leaving a directory that
   broke the final `rmdir` and two checks. Replaced with `fs_mkdir("", NULL)`,
   which is a defined IO failure. (Found under strict/no-sanitizer; fixed on the
   test side, no header change.)
2. `fs_ops_safety.cp` inverted rename semantics: it expected the *old destination*
   contents (`"NEW"`) after `rename(A,B)` although A held `"OLDLONGCONTENT"`.
   Corrected to expect A's 14 bytes in B; the three affected checks then passed.
   (Found under strict/no-sanitizer; fixed on the test side, no header change.)

A third, pre-build authoring typo (an undefined placeholder identifier in
`fs_basic_safety.cp`) was removed before the first build; it never produced a
test result.

## Limitations

UBSan coverage was obtained only with the `-DCPLUS_EXIT_ARITHMETIC=110`
backend define described above; the default UBSan build remains blocked by the
external core-runtime/header mismatch. ASan is not available on this host, so
no memory-safety claim is made from these results — they establish defined
return values, error categories, filesystem-artifact state and absence of
leaks/UB under the instrumented subset.
