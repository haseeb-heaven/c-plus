# Agent guardrails (non-negotiable)

Apply these rules at the start of every agent session in this repository.

1. **NO NEW FEATURES OR KEYWORDS.** We are in stabilization and bug-fix mode. Do not invent new syntax, new data structures, or new benchmark scripts.
2. **NO FAKE OR INFLATED BENCHMARKS.** Do not write microbenchmarks that look fast by skipping work. Every benchmark must prevent dead-code elimination (volatile / blackbox sink) and measure real end-to-end execution.
3. **SMALL, ATOMIC COMMITS.** At most one bug fix or one focused improvement per commit, under 300 lines changed. Never dump 20 modified files in a single commit.
4. **NEVER SILENCE WARNINGS OR TESTS.** Do not add `#pragma GCC diagnostic ignored`, disable failing tests, or weaken assertions to make CI pass. Fix the root cause.
5. **C ABI IS SACRED.** Never alter the memory layout (`sizeof` / `alignof`) of standard C structs, unions, or raw pointers (`T*`). Any C+ metadata must live out-of-band (side-table) or inside explicit C+ wrapper types.
