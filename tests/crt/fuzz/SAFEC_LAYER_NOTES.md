# safec_layer_* — deterministic fuzz coverage for the checked ordinary-name C layer

Branch `agent/modern-stdlib`, 2026-11-23. All files are confined to
`tests/crt/fuzz/` with the `safec_layer_` prefix.

## What is covered

The harnesses call the **ordinary ISO C spellings** (`memcpy`, `memmove`,
`memset`, `memcmp`, `strcpy`, `strncpy`, `strcat`, `strncat`, `malloc`,
`calloc`, `realloc`, `free`) as remapped by `stdlib/safe/safe_c.h` onto the
safeclib-backed auto wrappers in `build/safe/libcplus_safecrt.a` plus the
hardened libc in `build/libc/libcplus_hardened.a`.

* Fixed LCG seed `0x5AFEC1DE` (`1664525 / 1013904223`), 200 iterations per
  pass; schedule forces length `0`, exact-fit `512`, one-over `513`, then
  bounded random lengths in `[0, 512]`.
* 16-byte canaries before and after every guarded stack payload; canaries are
  re-verified after every call. No harness operation ever writes outside a
  declared object — the one-over lengths are only passed to the checked
  wrapper, whose refusal (`errno == ERANGE`, canaries intact) is asserted.
* `memmove` additionally exercises genuinely overlapping ranges in both
  directions (literal offsets `0/16` and `32/48`, so strict-mode capacity
  inference works) against a byte-exact expected buffer.
* Allocation wrappers: exact-size tracked blocks, metadata (`byte_size`,
  `elem_size`, `elem_count`, `is_live`), interior and one-past-end remaining
  capacity, one-over refusal, double-free refusal, calloc zeroing,
  realloc grow/shrink and injected-failure preservation. Generation counters
  are not hashed because they are process-global.
* The whole operation stream is replayed with SafeCRT logging **OFF** and
  **TRACE** (5). The FNV-1a 64 digest and operation count must be identical.
  Violation trap is disabled so refusals are observable, a silent constraint
  handler is installed, and stderr (logger + violation diagnostics) is
  redirected to `/dev/null` during both passes. `SC_KEEP_STDERR=1` keeps it
  visible for debugging.
* Every harness is built twice: plain `-O1` and `-fsanitize=undefined
  -fno-sanitize-recover=all`. A real raw-UB bug was found this way during
  development (a `n == 513` loop indexing a 512-byte snapshot in the memcmp
  case) and fixed; the final UBSan runs are clean.

## Exact commands

Driver: `sh tests/crt/fuzz/safec_layer_run.sh` (run from the repository root).
It resolves `ROOT` from its own path, uses `CC=${CPLUS_CLANG:-/usr/bin/clang}`
and a `mktemp -d "${TMPDIR:-/tmp}/safec_layer_fuzz.XXXXXX"` output directory
removed via trap; binaries are never written into the repository.

For each harness (`name` in `safec_layer_memory`, `safec_layer_string`,
`safec_layer_alloc`):

```sh
shasum -a 256 tests/crt/fuzz/$name.c tests/crt/fuzz/safec_layer_common.h
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -I. -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/crt/fuzz/$name.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o "$OUTDIR/$name"
timeout 30 "$OUTDIR/$name"
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -I. -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -fsanitize=undefined -fno-sanitize-recover=all \
  tests/crt/fuzz/$name.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o "$OUTDIR/${name}_ubsan"
timeout 30 "$OUTDIR/${name}_ubsan"
```

Prebuilt static libraries were used as-is: `build/safe/libcplus_safecrt.a`
and `build/libc/libcplus_hardened.a`.

## Results (final run, both builds)

```
safec_layer_memory: digest=989b321ba4a2cbed
safec_layer_memory: 890 operations, 2 logging passes, parity ok
safec_layer_memory: digest=989b321ba4a2cbed    (UBSan, identical)
safec_layer_memory: 890 operations, 2 logging passes, parity ok
safec_layer_string: digest=8cf2fd283aae2de3
safec_layer_string: 800 operations, 2 logging passes, parity ok
safec_layer_string: digest=8cf2fd283aae2de3    (UBSan, identical)
safec_layer_string: 800 operations, 2 logging passes, parity ok
safec_layer_alloc: digest=5695543741e2fe92
safec_layer_alloc: 317 operations, 2 logging passes, parity ok
safec_layer_alloc: digest=5695543741e2fe92     (UBSan, identical)
safec_layer_alloc: 317 operations, 2 logging passes, parity ok
safec_layer_run: ALL HARNESS PASS
```

Final `sh tests/crt/fuzz/safec_layer_run.sh` exit code: **0**.
No assertion failures and no UBSan reports in the final run. The earlier
`Abort trap: 6` was a harness bug (the unbounded memcmp snapshot loop), not
an intentional refused-case abort; it was fixed in `safec_layer_memory.c`.
Compiler output contains only expected `-Wunused-function` warnings for
helpers in `safec_layer_common.h` that a given translation unit does not use.

## Source digests (SHA-256)

```
5189b07da2a6d567fedff452f1b3457f6a366af8a8726559b67a331c9450ea97  tests/crt/fuzz/safec_layer_memory.c
b828b50ba26104f8687a2e6425e66d67559d63b4c424a410f655242eab9fd7cc  tests/crt/fuzz/safec_layer_string.c
0ead6311dc3ef0a7893502db96014222342117d9383e04b84453a9560292e69a  tests/crt/fuzz/safec_layer_alloc.c
5d44d30a12f52df0fd3e0980916655f9c683c56284d10182532bb9fb641147cc  tests/crt/fuzz/safec_layer_common.h
b9071166f0c526bf6e3a317fd7c2011d4231c0c227dcc8b8b0797f2b1653f1e5  tests/crt/fuzz/safec_layer_run.sh
```

## Files

`tests/crt/fuzz/safec_layer_common.h`, `safec_layer_memory.c`,
`safec_layer_string.c`, `safec_layer_alloc.c`, `safec_layer_run.sh`, and this
`SAFEC_LAYER_NOTES.md`. No source, build, or git changes outside
`tests/crt/fuzz/`.
