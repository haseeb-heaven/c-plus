# Rich diagnostics — edge-case results

```
DIAG_TEST 23 23 0
diag: PASS (23 edge cases, exact legacy output preserved)
```

Run: `sh tests/stdlib/diag/run.sh` (bounded 60 s, POSIX sh).

Covered: legacy byte-exactness for all three emitters, rich content (code,
symbolic name, `file:line in function`, detail, explanation, hint), color
policies (`always`/`never`/`auto`, `NO_COLOR`, piped stderr), plain mode,
unknown codes, NULL records, 12 KB detail, config setters/getters, garbage
environment values, and explanations for every documented error code.

Regressions after the change: CRT 86/86, libc 109/109, ABI fingerprint MATCH.
