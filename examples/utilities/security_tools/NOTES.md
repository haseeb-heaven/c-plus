# Security utilities — runtime refusal demonstrations (NOT production security tools)

> **Explicit scope statement:** these five utilities are **demonstrations** of
> additive checked-stdlib APIs refusing hostile input at run time. They are
> **not** production checksum, password, copy, parser or bounds-checking
> security tools. The `OK` / `REFUSED` lines are observations of runtime
> constraint checks, not proofs of memory safety, lifetime safety, or
> cryptographic strength (FNV-1a and CRC-32 are deliberately non-cryptographic).
> The checked layers do not establish pointer provenance or full ownership and
> can be bypassed by callers that do not use them. `pwdcheck` uses only fixed
> dummy strings and never reads, stores, or logs a real secret.

## Files

| File | Purpose |
| --- | --- |
| `checksum.cp` | bounded CRC32/FNV-1a over one fixed argv string; refuses oversized/empty input |
| `pwdcheck.cp` | fixed password rules over bounded input; refuses oversized/weak input; never echoes the candidate |
| `safe_copy.cp` | `cplus_memcpy_s` one-over (406), null src/dst (400) refusals with an intact canary; unguarded "before" case runs in a forked child |
| `safe_token.cp` | `cplus_strtok_s` + `cplus_strnlen_s` + `cplus_strncpy_s`; refuses oversized input, long tokens, too many tokens |
| `bounds_check.cp` | CLI over `stdlib/memory.hp`: exact fit / one-over / interior pointer / stale generation |
| `run.sh` | builds all five, runs every case under `timeout 30`, prints `UTIL_SECURITY <tools> <passed> <failed> <refused>` |

Protocol: deterministic `<TOOL> <case> OK|REFUSED ...` lines. Exit **0** for a
successful utility run, **2** when an expected refusal was observed, **1** for
an unexpected state (e.g. accepted one-over, corrupted canary, unknown case).
`run.sh` treats exit 2 as the expected outcome of a refusal case.

## Exact build commands (from repo root)

```sh
# 1-4 need the safeclib-backed checked string/memory archive.
./build/c+ build examples/utilities/security_tools/checksum.cp \
  --strict --sanitize=none -I. -o examples/utilities/security_tools/checksum \
  -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a

./build/c+ build examples/utilities/security_tools/pwdcheck.cp \
  --strict --sanitize=none -I. -o examples/utilities/security_tools/pwdcheck \
  -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a

./build/c+ build examples/utilities/security_tools/safe_copy.cp \
  --strict --sanitize=none -I. -o examples/utilities/security_tools/safe_copy \
  -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a

./build/c+ build examples/utilities/security_tools/safe_token.cp \
  --strict --sanitize=none -I. -o examples/utilities/security_tools/safe_token \
  -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a

# 5 uses pure header helpers, no extra archive.
./build/c+ build examples/utilities/security_tools/bounds_check.cp \
  --strict --sanitize=none -I. -o examples/utilities/security_tools/bounds_check
```

The harmless `clang: warning: ... 'linker' input unused` lines come from a
non-link pass of `c+`; `run.sh` explicitly checks that each binary exists.

## Exact observed outputs (from `examples/utilities/security_tools/run.sh`)

The vendored safeclib ignore handler emits lines such as
`IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max` on a refusal;
`run.sh` strips those lines, so the recorded transcript contains only protocol
lines and `UTIL_CASE` verdicts.

```text
CHECKSUM selfcheck OK fnv=0x4f9f2cab crc=0xcbf43926
CHECKSUM valid OK len=3 fnv=0x1a47e90b crc=0x352441c2 text=abc
CHECKSUM done
UTIL_CASE checksum PASS rc=0 pattern=CHECKSUM valid OK
CHECKSUM selfcheck OK fnv=0x4f9f2cab crc=0xcbf43926
CHECKSUM oversize REFUSED code=101 cat=5 reason=input-too-long
UTIL_CASE checksum PASS rc=2 pattern=CHECKSUM oversize REFUSED code=101
CHECKSUM selfcheck OK fnv=0x4f9f2cab crc=0xcbf43926
CHECKSUM empty REFUSED code=108 cat=1 reason=empty-input
UTIL_CASE checksum PASS rc=2 pattern=CHECKSUM empty REFUSED code=108
PWCHECK selfcheck OK fixed-dummy-rules-ok
PWCHECK strong OK len=8 score=4 upper=1 lower=1 digit=1 symbol=1
PWCHECK done
UTIL_CASE pwdcheck PASS rc=0 pattern=PWCHECK strong OK
PWCHECK selfcheck OK fixed-dummy-rules-ok
PWCHECK weak REFUSED code=108 cat=1 reason=policy-rules-not-met
UTIL_CASE pwdcheck PASS rc=2 pattern=PWCHECK weak REFUSED code=108
PWCHECK selfcheck OK fixed-dummy-rules-ok
PWCHECK oversize REFUSED code=101 cat=5 reason=input-too-long
UTIL_CASE pwdcheck PASS rc=2 pattern=PWCHECK oversize REFUSED code=101
SAFECOPY exact OK rc=0 canary=0xa5 buf7=8
UTIL_CASE safe_copy PASS rc=0 pattern=SAFECOPY exact OK rc=0
SAFECOPY over REFUSED code=406 reason=slen-exceeds-dmax canary=0xa5 intact=1
UTIL_CASE safe_copy PASS rc=2 pattern=SAFECOPY over REFUSED code=406
SAFECOPY nullsrc REFUSED code=400 reason=null-source canary=0xa5 intact=1
UTIL_CASE safe_copy PASS rc=2 pattern=SAFECOPY nullsrc REFUSED code=400
SAFECOPY null REFUSED code=400 reason=null-destination canary=0xa5 intact=1
UTIL_CASE safe_copy PASS rc=2 pattern=SAFECOPY null REFUSED code=400
SAFECOPY raw OK canary=0x2a corrupted=1 (unguarded subprocess)
SAFECOPY raw_child OK child_exit=9 parent_canary=0xa5
UTIL_CASE safe_copy PASS rc=0 pattern=SAFECOPY raw_child OK child_exit=9
SAFETOKEN token 0 len=5 text=alpha
SAFETOKEN token 1 len=4 text=beta
SAFETOKEN token 2 len=5 text=gamma
SAFETOKEN token 3 len=5 text=delta
SAFETOKEN valid OK n=4 remaining=43
SAFETOKEN done
UTIL_CASE safe_token PASS rc=0 pattern=SAFETOKEN valid OK n=4
SAFETOKEN oversize REFUSED code=101 cat=5 reason=input-too-long
UTIL_CASE safe_token PASS rc=2 pattern=SAFETOKEN oversize REFUSED code=101
SAFETOKEN long_token REFUSED code=101 cat=5 reason=token-too-long
UTIL_CASE safe_token PASS rc=2 pattern=SAFETOKEN long_token REFUSED code=101
SAFETOKEN many_tokens REFUSED code=101 cat=5 reason=too-many-tokens
UTIL_CASE safe_token PASS rc=2 pattern=SAFETOKEN many_tokens REFUSED code=101
BOUNDS exact OK cap=16 span_full=1 query_full=1
UTIL_CASE bounds_check PASS rc=0 pattern=BOUNDS exact OK
BOUNDS one_over REFUSED code=101 cat=5 span_1over=0 query=-1
UTIL_CASE bounds_check PASS rc=2 pattern=BOUNDS one_over REFUSED code=101 cat=5
BOUNDS interior OK interior_has_metadata=0 contains_24=1 contains_25=0
UTIL_CASE bounds_check PASS rc=0 pattern=BOUNDS interior OK
BOUNDS stale REFUSED code=103 cat=7 moved=1 old_key_current=0 old_query=-1 fresh_query=1
UTIL_CASE bounds_check PASS rc=2 pattern=BOUNDS stale REFUSED code=103 cat=7
UTIL_SECURITY 5 5 0 12
```

`tools=5`, `passed=5`, `failed=0`, `refused=12` observed refusal lines;
`run.sh` exit code `0`.

## What each refusal means

- `CHECKSUM oversize`: `cplus_strnlen_s(text, 33)` returns 33 for a 48-byte
  argument; the tool refuses with `CPLUS_EXIT_BOUNDS` (101), category `BOUNDS`
  (5), before any digest work. Known-answer self-checks pass first:
  FNV-1a("hello") = `0x4f9f2cab`, CRC32("123456789") = `0xcbf43926`.
- `PWCHECK weak/oversize`: fixed rules (8..32 printable bytes, upper/lower/
  digit/symbol) are evaluated on a bounded local copy; the candidate is never
  printed. Oversize is code 101, weak is code 108 (`CPLUS_EXIT_UNSUPPORTED`).
- `SAFECOPY over`: `cplus_memcpy_s(dst, 8, src9, 9)` returns safeclib
  `ESNOSPC` 406 and the trailing `volatile` canary stays `0xa5`. The `raw`
  case shows the unguarded 9-byte write corrupting a child's canary to `0x2a`
  (child exit 9) while the parent canary remains `0xa5`.
- `SAFETOKEN long_token/many_tokens/oversize`: token length and token count
  are bounded independently; each violation is refused with code 101. No
  partial token transcript is printed for a refused input.
- `BOUNDS one_over/stale`: `stdlib_mem_query` returns `STDLIB_FAIL` (-1) with
  category `BOUNDS`/code 101 for `[0,17)` on a 16-byte block, and category
  `LIFECYCLE` (7)/code 103 (`CPLUS_EXIT_USE_AFTER_FREE`) when a generation key
  captured before `stdlib_mem_transfer` is reused. A fresh key still succeeds.

## Negative controls (exit codes are real, not hardcoded)

Direct invocations that do **not** satisfy the harness expectations still
return nonzero, so `run.sh` cannot report a fabricated pass:

```text
$ ./examples/utilities/security_tools/safe_copy nope
SAFECOPY unknown_case REFUSED code=108 reason=unknown-case
rc=1
$ ./examples/utilities/security_tools/checksum
CHECKSUM selfcheck OK fnv=0x4f9f2cab crc=0xcbf43926
CHECKSUM usage REFUSED code=108 cat=1 reason=missing-input
rc=2
$ ./examples/utilities/security_tools/pwdcheck ''
PWCHECK selfcheck OK fixed-dummy-rules-ok
PWCHECK too_short REFUSED code=101 cat=2 reason=input-too-short
rc=2
```

If a checked call were to accept a hostile case (for example the one-over
returning 0 or a changed canary), the corresponding utility prints a refusal
with the actual observed code and still exits nonzero; `run.sh` then reports
`UTIL_CASE ... FAIL` and a nonzero tool count.

## Authoring notes / constraints

- All builds use `--strict --sanitize=none` as required. Strict mode requires
  out-parameters passed by address to be initialized at declaration
  (`const void *base = 0; size_t size = 0; char *ctx = 0; rsize_t remaining = 0`).
- `CPLUS_SAFE_NO_UNPREFIXED_MACROS` prevents the checked headers from
  redefining ordinary `memcpy`/`strcpy` and lets the `raw` case perform a real
  unchecked byte copy via `__builtin_memcpy`.
- `cplus_strnlen_s(text, MAX + 1)` is the bounded length probe: a value greater
  than `MAX` means the string is not terminated within the allowed window.
- `--` forwards the two static archives to the C backend of `c+`; the C+
  driver can exit 0 even on a linker failure, so `run.sh` checks `-x` on each
  binary.
- `run.sh` resolves the repo root from its own path and works from any cwd.
