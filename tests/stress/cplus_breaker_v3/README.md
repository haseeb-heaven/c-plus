# C+ Breaker V3 — 25 adversarial cases

Targets modern C+ (`this->`, class RAII, `.hp`, `auto`, `.len`, `defer`, `countof`, managed ownership) and real C compatibility.

Run from an extracted `cplus_breaker_v3` folder located inside `scratch`:

```bash
CPLUS_BIN=../../build/c+ bash run.sh
```

If the location differs, set `CPLUS_BIN` to an absolute path. The runner stores outputs in `.results/`. Positive tests compare exact stdout, negative tests require compiler rejection, runtime tests assert C+ exit codes when documented. `25_freed_len_trap` deliberately accepts *any nonzero exit* because the exact runtime exit code is a version-dependent policy; inspect its stderr to establish that it trapped for the right reason.

A failure does **not automatically prove** the compiler is wrong: check whether an extension is documented as supported, review diagnostics and inspect generated C with `--emit-c`. Especially useful cases: nested `defer` order, ownership return/move, class destruction, and `this->` method calls.
