# Differential C corpus and results

Deterministic C fixtures used by
`tools/compiler_differential/run_differential.py`.

## Layout

- `manifest.json` — master seed, per-case seed, category, source path and
  SHA-256 of every fixture. The runner verifies both the manifest hashes and a
  fresh regeneration from `corpus.py` before running anything.
- `cases/*.c` — 25 plain C fixtures (arithmetic, structs, pointers, arrays,
  callbacks, stdio). All output is deterministic; no addresses are printed and
  qsort keys are distinct.
- `results/summary.txt` — human-readable counts, exact reference compiler
  versions, and every mismatch by case name.
- `results/summary.json` — machine-readable record of all comparisons,
  compilers, versions, commands and outputs.
- `results/logs/<case>__<compiler>.txt` — compile/run logs for every pair.
- `results/mismatches/<case>__<compiler>/` — one bundle per mismatch with
  `seed.txt`, `source.c`, `commands.txt`, `comparison.json`, individual logs,
  both executables and `README.txt` reproduction instructions.

Regenerate the corpus with:

```sh
python3 tools/compiler_differential/generate_corpus.py
```
