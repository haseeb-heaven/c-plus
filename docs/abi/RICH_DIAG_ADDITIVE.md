# ABI note — optional rich diagnostics

The rich-diagnostics layer (`stdlib/safe/safe_diag.h`) is **additive only**.

Verified against the previous fingerprint before refreshing the baseline:

- `build/libc/libcplus_hardened.a` — **unchanged**. No symbol added, removed or
  modified. This is the ordinary C ABI used by precompiled C consumers.
- `build/safe/libcplus_safecrt.a` — 17 **new** symbols (`cplus_diag_*`,
  `cplus_safe_log_at`, `cplus_runtime_violation_at`,
  `cplus_runtime_report_at`); **zero** removals and **zero** changes to any
  existing symbol. `strcpy_s`, `printf_s`, `memcpy_s` and every other buffer
  API keep their names, signatures and layouts; no buffer representation or
  calling convention changed.
- All three historical diagnostic output formats remain byte-identical in the
  default `LEGACY` mode (`tests/stdlib/diag/run.sh`, 23 cases).

The optional symbols are new entry points only; callers that do not use rich
diagnostics are unaffected. The ABI fingerprint baseline was refreshed to
record these additions; `sh tools/abi_compare/run.sh compare
tests/abi/fingerprints/baseline.txt` reports an empty diff.
