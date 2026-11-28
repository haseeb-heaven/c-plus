# Precompiled standard-C library links unchanged with and without the safety layer

**Scope:** static `.a` only, macOS **arm64**.
**Run:** `sh tests/abi/stdc_precompiled/run.sh` (portable 60-second watchdog in the script).
**Environment:** Apple clang 16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`; `arch=arm64 os=Darwin`; `PATH=/usr/bin:/bin`.
**Date of recorded run (UTC):** 2026-11-23T05:57:02Z.
**Result: ALL CHECKS PASSED (`failures=0`).**

`$ROOT` below = `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.
All generated files live under `tests/abi/stdc_precompiled/artifacts/`.

## 1. Build the C library ONCE and record SHA-256

```sh
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -c "$ROOT/tests/abi/stdc_precompiled/legacy.c" \
  -o "$ROOT/tests/abi/stdc_precompiled/artifacts/stdc_legacy.o"
/usr/bin/ar rcs "$ROOT/tests/abi/stdc_precompiled/artifacts/libstdc_legacy.a" \
  "$ROOT/tests/abi/stdc_precompiled/artifacts/stdc_legacy.o"
/usr/bin/shasum -a 256 stdc_legacy.o libstdc_legacy.a   # -> artifacts/lib.sha256
```

`legacy.c` is plain ISO C: no C+ header, no safety layer. Its ordinary `memcpy`
/ `memcmp` calls bind to host libc in this object and are fixed at this point.
The compile command appears **exactly once** in `run.sh`.

| File | SHA-256 |
|---|---|
| `stdc_legacy.o` | `57ec4966a559090df3370f2137e49d2a0d0692875aedb1cd24761f3b02e0c1a1` |
| `libstdc_legacy.a` (baseline) | `ddca3e14c2a164459541194f8bcc5ebd0213ed9e449cd988592f30b20eaeb651` |

**Reproducibility note.** The compiled object `stdc_legacy.o` is bit-reproducible:
its SHA-256 was `57ec4966…` in every run. The macOS `ar rcs` container embeds a
member timestamp and has no `-D` deterministic mode, so the `.a` hash differs
between *fresh rebuilds* (observed `e6f00c99…`, `ed8909ab…`, `ddca3e14…` across
three runs). This is a container-metadata detail only. The invariant this test
proves is that the **saved archive (and object) are bit-for-bit unchanged before
and after all three links within a run** — i.e. linking callers with or without
the safety layer never rebuilds or rewrites the precompiled library.

Prebuilt support archives used by the safe caller (read-only, not modified):

| Archive | SHA-256 |
|---|---|
| `build/safe/libcplus_safecrt.a` | `ac66b0246dbac3337f439e4b91b0e50f901aac7f07bba0ecb1581c637187ef2e` |
| `build/libc/libcplus_hardened.a` | `86f6a7223f19d10a3e8b1d9b9902731ba05fc061e7ce1463c08a3047281c5dda` |

## 2. Build two callers from ONE source against the SAME saved archive

**(A) plain — no `safe_c.h`, links only the saved `.a`:**

```sh
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I"$ROOT/tests/abi/stdc_precompiled" \
  "$ROOT/tests/abi/stdc_precompiled/caller.c" \
  "$ROOT/tests/abi/stdc_precompiled/artifacts/libstdc_legacy.a" \
  -o "$ROOT/tests/abi/stdc_precompiled/artifacts/app_plain"
```

**(B) safe — `-include stdlib/safe/safe_c.h`, SAME archive + both C+ archives:**

```sh
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I"$ROOT/tests/abi/stdc_precompiled" -I"$ROOT" \
  -I"$ROOT/stdlib/safe" -I"$ROOT/libc" -I"$ROOT/libc/install-include" \
  -I"$ROOT/third_party/safeclib/include" \
  -I"$ROOT/third_party/intel-safestringlib/include" \
  -include stdlib/safe/safe_c.h \
  "$ROOT/tests/abi/stdc_precompiled/caller.c" \
  "$ROOT/tests/abi/stdc_precompiled/artifacts/libstdc_legacy.a" \
  "$ROOT/build/safe/libcplus_safecrt.a" \
  "$ROOT/build/libc/libcplus_hardened.a" \
  -lm -pthread -o "$ROOT/tests/abi/stdc_precompiled/artifacts/app_safe"
```

**Negative caller — same force-include, same archive:**

```sh
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -pedantic \
  -I"$ROOT/tests/abi/stdc_precompiled" -I"$ROOT" \
  -I"$ROOT/stdlib/safe" -I"$ROOT/libc" -I"$ROOT/libc/install-include" \
  -I"$ROOT/third_party/safeclib/include" \
  -I"$ROOT/third_party/intel-safestringlib/include" \
  -include stdlib/safe/safe_c.h \
  "$ROOT/tests/abi/stdc_precompiled/negative.c" \
  "$ROOT/tests/abi/stdc_precompiled/artifacts/libstdc_legacy.a" \
  "$ROOT/build/safe/libcplus_safecrt.a" \
  "$ROOT/build/libc/libcplus_hardened.a" \
  -lm -pthread -o "$ROOT/tests/abi/stdc_precompiled/artifacts/negative_safe"
```

All three builds returned `RC=0`.

## 3. Archive SHA-256 re-hashed before and after the links

| Point | SHA-256 of `libstdc_legacy.a` | SHA-256 of `stdc_legacy.o` |
|---|---|---|
| Baseline (after `ar rcs`) | `ddca3e14c2a164459541194f8bcc5ebd0213ed9e449cd988592f30b20eaeb651` | `57ec4966a559090df3370f2137e49d2a0d0692875aedb1cd24761f3b02e0c1a1` |
| PRE-LINK | `ddca3e14c2a164459541194f8bcc5ebd0213ed9e449cd988592f30b20eaeb651` | `57ec4966a559090df3370f2137e49d2a0d0692875aedb1cd24761f3b02e0c1a1` |
| POST-LINK (after all 3 links) | `ddca3e14c2a164459541194f8bcc5ebd0213ed9e449cd988592f30b20eaeb651` | `57ec4966a559090df3370f2137e49d2a0d0692875aedb1cd24761f3b02e0c1a1` |

`PASS archive unchanged before links`, `PASS object unchanged before links`,
`PASS archive unchanged after all links`, `PASS object unchanged after all links`.

## 4. Run with identical valid inputs; compare stdout

Both valid callers received the identical argv `20 22` (`IN_A=20 IN_B=22`).

```sh
./artifacts/app_plain 20 22
./artifacts/app_safe  20 22
```

| Program | Exit code |
|---|---|
| `app_plain` | `0` |
| `app_safe` | `0` |
| `negative_safe` | `70` (intended refusal) |

`app_plain.stdout` and `app_safe.stdout` are **byte-identical**:

- `cmp -s` returned 0 → `PASS stdout byte-identical`.
- `app_plain` stdout SHA-256 = `2919b20d948858a07cf17c107af894cf47b9a463dd19e822b5c815ab98ae7d64`
- `app_safe`  stdout SHA-256 = `2919b20d948858a07cf17c107af894cf47b9a463dd19e822b5c815ab98ae7d64`

Identical stdout (both builds):

```
caller: version=11
caller: mul=440
caller: sum=36
caller: copy_equal=1
caller: label=stdc-legacy-ok
caller: END
```

Both callers' stderr was empty. The `strcpy` / `strcat` in `caller.c` were
macro-mapped to the checked wrappers in build (B) yet produced the same string.

## 5. Negative caller: 64-byte copy into `char[8]` is refused

`negative.c`, built with the same `-include stdlib/safe/safe_c.h`, first calls the
**unchanged** saved library for valid data, performs a valid in-bounds copy, then
calls `memcpy(small, src, 64)` where `small` is `char[8]`.

stdout (proves the unchanged library was still called successfully):

```
negative: lib_version=11 lib_mul=42 lib_sum=294
negative: valid_copy=ok small0=O
```

stderr (the refusal):

```
[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 64 > capacity 8, ESNOSPC 406)
```

Exit code: **`70`** (`_exit(70)` from `cplus_runtime_violation`, the default
fail-stop trap). The trailing `negative: UNEXPECTED overflow was not refused`
line was **not** printed. Checks passed: exit 70, ESNOSPC refusal diagnostic,
`ESNOSPC 406` named, saved library still called, no `UNEXPECTED` line.

## 6. Conclusion

- The checked layer is a **caller-side, compile-time macro mapping** installed by
  `-include stdlib/safe/safe_c.h`. It rewrites ordinary C names in the *caller's*
  translation unit only (e.g. `memcpy` → `cplus_memcpy_s_auto`, `strcpy` →
  `cplus_strcpy_s_auto`); it defines no new symbol and changes no struct layout
  or exported C symbol in the precompiled library.
- The precompiled library `libstdc_legacy.a` was built **once** with
  `/usr/bin/clang -O2 -std=c11 -c` and archived. Its SHA-256 was identical before
  and after all three links, so **no rebuild of the precompiled C library is
  needed** when a consumer opts into the safety layer.
- Both the plain and safe callers link that *same saved archive*, accept identical
  valid inputs, exit `0`, and emit byte-identical stdout — the **ABI and observable
  behaviour for valid data are unchanged**.
- The negative caller demonstrates the value the layer adds entirely on the
  caller side: an out-of-bounds 64-byte copy into `char[8]` is refused with an
  `ESNOSPC 406` diagnostic and exit `70`, while the unchanged library continues
  to be called normally for valid data.

**Limits:** static `.a` only; macOS arm64; safety coverage is runtime constraint
checking of the mapped ordinary names, not a proof of pointer provenance,
lifetime, or alias safety.
