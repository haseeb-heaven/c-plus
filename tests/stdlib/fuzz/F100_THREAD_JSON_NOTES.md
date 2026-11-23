# F100 thread/JSON deterministic fuzz configurations (F-086 .. F-093)

Branch `agent/modern-stdlib`, 2026-11-23. Owner prefix `f100_thread_` /
`f100_json_`. Eight self-checking, fixed-seed fuzz configurations were added
under `tests/stdlib/fuzz/`. No header, runtime, or build-system file was
changed; no pass is claimed without an executed run.

## Files

| Config | Source | Manifest entry | Seed | Ops | Digest |
|--------|--------|----------------|------|-----|--------|
| F-086 | `f100_thread_mutex.cp` | `f100_thread_configs.json` | `0xC0FFEE86` | 1280 locked mutations + 64 lifecycle cycles | `4c35d7a82294a3ab` |
| F-087 | `f100_thread_guard.cp` | `f100_thread_configs.json` | `0xC0FFEE87` | 1280 guard lifecycles + 64 cycles | `f6ba43560d079e75` |
| F-088 | `f100_thread_condvar.cp` | `f100_thread_configs.json` | `0xC0FFEE88` | 1024 generation waits + 256 signal/broadcast + 64 cv cycles | `d391902e5ea53333` |
| F-089 | `f100_json_gen.cp` | `f100_json_configs.json` | `0xC0FFEE89` | 512 fixed-template + 512 generated parses | `65d5e5d4c73f16a9` |
| F-090 | `f100_json_getters.cp` | `f100_json_configs.json` | `0xC0FFEE90` | 1024 (member, getter) combinations | `3d89f31c9e408be1` |
| F-091 | `f100_json_builder.cp` | `f100_json_configs.json` | `0xC0FFEE91` | 1024 builder mutations with round-trip | `5f7a1752dbfc71fc` |
| F-092 | `f100_json_cycles.cp` | `f100_json_configs.json` | `0xC0FFEE92` | 1024 parse/view/write/free cycles | `a2ee9c78125ab398` |
| F-093 | `f100_json_drop.cp` | `f100_json_configs.json` | `0xC0FFEE93` | 1024 post-drop refusal checks | `762e93c1f5170370` |

Drivers: `f100_thread_run.sh`, `f100_json_run.sh`. Each reads its manifest and
builds/runs every config in two modes. JSON flags are stored as a JSON array
(`"-Ithird_party/yyjson"`, `"-Lbuild/ext"`, `"-lyyjson"`) and forwarded as
**separate argv operands** to `c+ build` — never as one shell string.

All harnesses use the 64-bit LCG
`state = state*6364136223846793005 + 1442695040888963407 (mod 2^64)`, taking
`state >> 33`, with a per-config seed. Each folds an FNV-1a digest over
deterministic op values; the digest is printed as `<CFG>_DIGEST <hex>` and
pinned in the manifest. Worker-local digests are stored in distinct array
slots and combined in a fixed order, so thread interleaving cannot change the
digest. Final lines are `<CFG>_... <checks> 0`; a nonzero failure count or a
nonzero exit is a fail.

## Verification performed (no fabricated passes)

Commands run from the repo root on Apple clang/arm64:

```
./tests/stdlib/fuzz/f100_thread_run.sh
./tests/stdlib/fuzz/f100_json_run.sh
```

Each script, for every config:

1. `c+ build --strict --sanitize=none [json flags] <source> -o <exe>`
2. `timeout 30 <exe>` — checks exit 0, pinned final line, pinned digest
3. `c+ build --strict --sanitize=undefined [json flags] <source> -o <exe>`
4. `timeout 30 <exe>` — same checks; digest must equal the strict/no-san digest

Observed result:

```
ok   F-086 mutex deterministic lifecycle with bounded worker threads (1280 ops, digest=4c35d7a82294a3ab)
ok   F-087 guard deterministic lifecycle with ERRORCHECK mutex and bounded workers (1280 ops, digest=f6ba43560d079e75)
ok   F-088 condvar deterministic generation lifecycle with bounded waiters (1024 ops, digest=d391902e5ea53333)
f100 thread configs: 3/3 passed
ok   F-089 deterministic generation of valid and malformed JSON documents (1024 ops, digest=65d5e5d4c73f16a9)
ok   F-090 typed getters including wrong-type refusals (1024 ops, digest=3d89f31c9e408be1)
ok   F-091 seeded builder mutations with write/parse round-trip (1024 ops, digest=5f7a1752dbfc71fc)
ok   F-092 repeated parse / view / write / free cycles (1024 ops, digest=a2ee9c78125ab398)
ok   F-093 post-drop refusal without UAF (1024 ops, digest=762e93c1f5170370)
f100 json configs: 5/5 passed
```

`timeout 30` never fired and no internal SIGALRM watchdog fired in any passing
run. F-088 was repeated 5 times; the digest was identical each time.

## Hang / race discipline

- Every thread config arms `alarm(8)` (F-088: `alarm(10)`) with a handler that
  calls `_Exit(99)`, so a lost unlock or missed wakeup terminates
  deterministically; `timeout 30` is the second bound.
- All shared state is mutated only under the mutex. The F-088 worker startup
  handshake proves all waiters are blocked before main broadcasts; a
  generation counter plus predicate loop makes wakeups lossless.
- F-087 re-initialises its mutex as `PTHREAD_MUTEX_ERRORCHECK` through the
  test-only `native()` cast, turning any hidden double-unlock into a fatal,
  observable exit instead of memory corruption.
- No raw game/global offsets, no background-thread native calls, no real
  unguarded races are generated.

## SKIPs (explicit, not counted as passes)

1. **JSON allocator/OOM injection (all JSON configs).** `json.hp` passes a NULL
   `yyjson_alc` to `yyjson_read_opts` / `yyjson_mut_doc_new`, and yyjson's
   allocator is per-call with no global hook. `stdlib_alloc_fail_at/after`
   cannot reach yyjson allocations, so no honest allocation-failure case can be
   generated. No OOM pass is fabricated.
2. **F-088 `drop_with_waiter`.** `pthread_cond_destroy` with blocked waiters is
   EBUSY/UB and `condvar::drop` frees storage without synchronisation. Printed
   as `SKIP ...`.
3. **F-088 `wait_without_mutex`.** POSIX `pthread_cond_wait` requires the mutex
   to be held; violating it is caller UB, not a C+ contract.
4. **F-093 precaptured view after owner drop.** A `json_view` captured before
   `json::drop()` is a dangling borrow by the wrapper's documented contract
   ("valid only while its owner lives"); using it afterwards is caller UB and
   the wrapper does not promise refusal. Post-drop *refusal* is tested only
   through the documented path (new views obtained from the dropped owner).
5. **ASan.** AddressSanitizer is broken on this host (see `docs/LIMITATIONS.md`,
   "No use-after-free detection"). UAF absence is therefore argued from the
   API-refusal assertions under UBSan, not from an ASan run.

## Defects / hazards found during construction (repro included)

These are compiler/wrapper findings, not new failures in the shipped configs.
No header or compiler file was modified (outside this task's file ownership).

1. **Builder keys are borrowed, not copied (wrapper hazard, F-091).**
   `json_builder::add_int/add_double/add_bool/add_string` forward the caller's
   `key` pointer to `yyjson_mut_obj_add_*`, whose header states "The key string
   is not copied, you should keep the string unmodified for the lifetime of
   this JSON document." Repro (before the harness fix): a reused stack
   `char key[16]` filled with `snprintf(key, ..., "k%d", i)` inside the mutation
   loop produced `{"k7":false,"k7":false,"k7":799.625,...}` — all eight keys
   aliased `k7`; 985/1024 round-trip getter checks failed. Fix in the harness:
   use program-lifetime key literals (`f091_keys[]`). Suggested upstream fix:
   use `yyjson_mut_obj_add_*_cpy` for keys or document the borrow requirement.
2. **C+ does not construct class-typed local arrays (compiler, F-086).**
   `thread t[4]` lowers to a raw C array with no constructor calls, leaving
   `joinable` as stack garbage. Repro: the first F-086 revision returned
   `STDLIB_FAIL` / code `107` from `thread::start` for indices 0 and 3, and
   only 2 of 4 workers ran (counter 1539 vs shadow 2563). Fix: four named
   owning `thread` locals reached through a pointer table.
3. **An explicit `.drop()` discharges the RAII obligation (compiler, F-087).**
   In a helper containing `guard g = guard(m); ... g.drop();`, the lowering
   emitted no scope-exit `guard_drop`, because an explicit drop is treated as
   satisfying the ownership marker. Repro: with the ERRORCHECK mutex the
   process exited 107 ("pthread_mutex_lock failed") because a guard was never
   released. Fix: the guard helper never calls explicit `drop()`; it relies on
   the implicit destructor (which the lowering does emit in that case).
4. **Chained calls on a returned view are not lowered (compiler, F-089).**
   `root.at(i).valid()` failed to compile with "no member named 'valid' in
   'struct json_view'". Fix: assign `json_view item = root.at(i);` to a local
   first. Same pattern as the existing `c->m->lock()` guidance.
