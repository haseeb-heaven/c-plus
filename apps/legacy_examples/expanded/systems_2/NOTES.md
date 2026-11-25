# systems_2 — deterministic systems/simulation teaching models

Four small, self-contained **teaching models** built on the checked C+
standard library:

| file | model | collections used |
| --- | --- | --- |
| `lru_cache.cp` | fixed-capacity LRU cache (string keys/values, eviction order) | `strmap`, `vector`, `string`, `error` |
| `scheduler.cp` | round-robin + priority task scheduler (bounded queue) | `deque` x2, `vector`, `string`, `error` |
| `event_bus.cp` | in-memory subscribe / publish / unsubscribe bus | `strmap`, `vector`, `string`, `error` |
| `ring_buffer.cp` | fixed-capacity byte ring buffer (wraparound, bounded reads) | `vector`, `string`, `error` |

## Non-claims (read this first)

> **These are deterministic teaching models, not production systems and not
> safety proofs.** No wall clock, no real threads, no network, no interactive
> loop, no user paths and no hidden state drift: every run is driven by a
> fixed argv transcript. The `REFUSE` lines are **runtime evidence that the
> listed hostile inputs were rejected with a specific category and exit code**
> for these particular inputs. They are **not** a formal proof of memory
> safety, liveness, fairness, absence of bugs, or any property beyond the
> observed executions. `expect` lines are in-program content checks that make
> each program exit 1 on a wrong internal value; they are self-checks, not
> theorem proving. Do not treat `SYSTEMS_2 ... 0 failed` as a correctness
> certificate.

The names describe the *model* demonstrated, not a claim that these are real
databases, operating-system schedulers, message brokers or device drivers.

## Build (exact commands, from the repository root)

```sh
./build/c+ build examples/expanded/systems_2/lru_cache.cp   --strict --sanitize=none -I. -o examples/expanded/systems_2/results/lru_cache
./build/c+ build examples/expanded/systems_2/scheduler.cp   --strict --sanitize=none -I. -o examples/expanded/systems_2/results/scheduler
./build/c+ build examples/expanded/systems_2/event_bus.cp   --strict --sanitize=none -I. -o examples/expanded/systems_2/results/event_bus
./build/c+ build examples/expanded/systems_2/ring_buffer.cp --strict --sanitize=none -I. -o examples/expanded/systems_2/results/ring_buffer
```

All four build cleanly in `--strict` mode with **no diagnostics** (verified
individually and through `run.sh`). The build is also required to produce an
executable; the harness treats a zero compiler exit code without a binary as
a build failure.

## Run everything (exact command and exact output)

```sh
sh examples/expanded/systems_2/run.sh
```

Verbatim output of the final run:

```text
== lru_cache ==
build ok   lru_cache
ok         lru_flow (refused=0)
ok         lru_hostile (refused=8)
== scheduler ==
build ok   scheduler
ok         sch_flow (refused=0)
ok         sch_hostile (refused=10)
== event_bus ==
build ok   event_bus
ok         bus_flow (refused=0)
ok         bus_hostile (refused=8)
== ring_buffer ==
build ok   ring_buffer
ok         rb_flow (refused=0)
ok         rb_hostile (refused=9)
SYSTEMS_2 4 4 0 35
```

`SYSTEMS_2 <apps> <passed> <failed> <refused>` means: 4 apps attempted, 4
passed, 0 failed, 35 hostile inputs correctly refused. The harness exits 0.

### What a scenario checks

For each of the 8 scenarios the harness runs the fixed argv sequence twice
under `timeout -k 5 30` with stdin from `/dev/null`, and requires:

* exit code 0 on both runs;
* empty stderr on both runs;
* stdout byte-identical to `expected/<label>.expected` on both runs;
* stdout of run 1 byte-identical to stdout of run 2 (determinism).

The expected files are checked in under `expected/` (the repository's
`.gitignore` ignores `examples/expanded/**/results/`, so goldens cannot live
there).

## Per-app transcripts (byte-exact)

### lru_cache

Fixed argv (flow):
```sh
./examples/expanded/systems_2/results/lru_cache \
  cap 3 -- put alpha 1 -- put beta 2 -- put gamma 3 -- get alpha \
  -- put delta 4 -- dump -- expect alpha 1 -- expect gamma 3 -- expect delta 4
```
Output (`expected/lru_flow.expected`):
```text
LRU cap 3
LRU PUT alpha 1 update=0
LRU PUT beta 2 update=0
LRU PUT gamma 3 update=0
LRU GET alpha 1
LRU EVICT beta
LRU PUT delta 4 update=0
LRU dump n= 3
LRU entry delta 4
LRU entry alpha 1
LRU entry gamma 3
LRU evictions 1
LRU evicted beta
LRU expect alpha OK 1
LRU expect gamma OK 3
LRU expect delta OK 4
LRU_SUMMARY 10 10 0 0
```
Reading: `get alpha` promotes alpha, so `put delta` evicts **beta** (the least
recently used). `dump` lists most-recent first: delta, alpha, gamma.

Fixed argv (hostile, 8 refusals):
```sh
./examples/expanded/systems_2/results/lru_cache \
  cap 0 -- cap 99 -- put k v -- put aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa v \
  -- put k bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb -- put '' v \
  -- get missing -- bogus
```
Output (`expected/lru_hostile.expected`):
```text
LRU REFUSE cap cat= 2 code= 101 msg= capacity out of range
LRU REFUSE cap cat= 2 code= 101 msg= capacity out of range
LRU REFUSE put cat= 2 code= 101 msg= zero capacity
LRU REFUSE put cat= 2 code= 101 msg= key
LRU REFUSE put cat= 2 code= 101 msg= value
LRU REFUSE put cat= 2 code= 101 msg= key
LRU REFUSE get cat= 1 code= 106 msg= missing key
LRU REFUSE bogus cat= 1 code= 108 msg= unknown command
LRU_SUMMARY 8 0 8 0
```

### scheduler

Fixed argv (flow):
```sh
./examples/expanded/systems_2/results/scheduler \
  capacity 3 -- arrive net 0 2 -- arrive disk 1 1 -- arrive ui 0 1 \
  -- expect net 2 -- expect disk 1 -- run 4 -- dump
```
Output (`expected/sch_flow.expected`):
```text
SCH capacity 3
SCH arrive net prio= 0 burst= 2
SCH arrive disk prio= 1 burst= 1
SCH arrive ui prio= 0 burst= 1
SCH expect net OK 2
SCH expect disk OK 1
SCH run disk prio= 1 remain= 0
SCH done disk
SCH run net prio= 0 remain= 1
SCH run ui prio= 0 remain= 0
SCH done ui
SCH run net prio= 0 remain= 0
SCH done net
SCH queue high n= 0
SCH queue norm n= 0
SCH completed 3
SCH did disk
SCH did ui
SCH did net
SCH_SUMMARY 8 8 0 0
```
Reading: high-priority `disk` runs first (priority), then normal tasks round
robin; `net` needs two ticks and is re-enqueued between them.

Fixed argv (hostile, 10 refusals) — zero/oversized capacity, empty-queue tick,
duplicate name, queue overflow, invalid priority, burst out of range,
oversized name, invalid run count, unknown command. Output
(`expected/sch_hostile.expected`):
```text
SCH REFUSE capacity cat= 2 code= 101 msg= capacity out of range
SCH REFUSE tick cat= 2 code= 101 msg= empty queue
SCH capacity 2
SCH arrive a prio= 0 burst= 1
SCH REFUSE arrive cat= 1 code= 106 msg= duplicate name
SCH arrive b prio= 0 burst= 1
SCH REFUSE arrive cat= 2 code= 107 msg= queue overflow
SCH REFUSE capacity cat= 2 code= 101 msg= capacity out of range
SCH REFUSE arrive cat= 1 code= 108 msg= invalid priority
SCH REFUSE arrive cat= 2 code= 101 msg= burst out of range
SCH REFUSE arrive cat= 2 code= 101 msg= name
SCH REFUSE run cat= 2 code= 101 msg= run count out of range
SCH REFUSE bogus cat= 1 code= 108 msg= unknown command
SCH_SUMMARY 13 3 10 0
```

### event_bus

Fixed argv (flow):
```sh
./examples/expanded/systems_2/results/event_bus \
  topic room -- sub room alice -- sub room bob -- pub room hello \
  -- expect room alice -- unsub room bob -- pub room bye -- dump
```
Output (`expected/bus_flow.expected`):
```text
EB topic room
EB sub room alice
EB sub room bob
EB pub room deliver= 2 payload= hello
EB deliver room -> alice hello
EB deliver room -> bob hello
EB expect room alice OK subscribed
EB unsub room bob
EB pub room deliver= 1 payload= bye
EB deliver room -> alice bye
EB dump topics= 1
EB topic room subs= 1
EB has room alice
EB deliveries n= 2
EB delivery 0 2
EB delivery 1 1
EB_SUMMARY 8 8 0 0
```
Reading: publish fans out in subscription order; unsubscribe reduces the next
delivery count from 2 to 1.

Fixed argv (hostile, 8 refusals) — duplicate topic, duplicate subscription,
oversized payload, unknown topic, unknown subscriber, oversized topic,
oversized subscriber, unknown command. Output
(`expected/bus_hostile.expected`):
```text
EB topic room
EB REFUSE topic cat= 1 code= 106 msg= duplicate topic
EB sub room alice
EB REFUSE sub cat= 1 code= 106 msg= duplicate subscription
EB REFUSE pub cat= 2 code= 101 msg= payload
EB REFUSE pub cat= 1 code= 106 msg= unknown topic
EB REFUSE unsub cat= 1 code= 106 msg= unknown subscriber
EB REFUSE sub cat= 2 code= 101 msg= topic
EB REFUSE sub cat= 2 code= 101 msg= subscriber
EB REFUSE bogus cat= 1 code= 108 msg= unknown command
EB_SUMMARY 10 2 8 0
```

### ring_buffer

Fixed argv (flow):
```sh
./examples/expanded/systems_2/results/ring_buffer \
  cap 4 -- push 1 2 3 4 -- pop 2 -- push 5 6 -- dump -- expect 4 3 4 5 6 -- peek 2
```
Output (`expected/rb_flow.expected`):
```text
RB cap 4
RB PUSH n= 4 used= 0
RB push_bytes used= 4 head= 0
RB POP n= 2 used= 4
RB popped 0 1
RB popped 1 2
RB pop_bytes used= 2 head= 2
RB PUSH n= 2 used= 2
RB push_bytes used= 4 head= 2
RB dump used= 4 cap= 4 head= 2
RB byte 0 3
RB byte 1 4
RB byte 2 5
RB byte 3 6
RB oplog n= 3
RB op 0 4
RB op 1 -2
RB op 2 2
RB expect OK n= 4
RB PEEK n= 2 used= 4
RB peeked 0 3
RB peeked 1 4
RB_SUMMARY 7 7 0 0
```
Reading: after popping two bytes the head is 2; pushing 5,6 advances the tail
past the physical end and wraps it to index 0. `dump` linearises 3,4,5,6 and
`expect 4 3 4 5 6` confirms wraparound correctness.

Fixed argv (hostile, 9 refusals) — zero/oversized capacity, zero-capacity
push, overflow, underflow, non-positive read count, byte out of range, batch
too large, unknown command. Output (`expected/rb_hostile.expected`):
```text
RB REFUSE cap cat= 2 code= 101 msg= capacity out of range
RB REFUSE push cat= 2 code= 101 msg= zero capacity
RB cap 4
RB PUSH n= 4 used= 0
RB push_bytes used= 4 head= 0
RB REFUSE push cat= 2 code= 107 msg= overflow
RB REFUSE pop cat= 2 code= 101 msg= underflow
RB REFUSE peek cat= 2 code= 101 msg= count out of range
RB REFUSE push cat= 2 code= 101 msg= byte out of range
RB REFUSE cap cat= 2 code= 101 msg= capacity out of range
RB REFUSE push cat= 2 code= 101 msg= batch too large
RB REFUSE bogus cat= 1 code= 108 msg= unknown command
RB_SUMMARY 11 2 9 0
```

## Refusal taxonomy observed

Categories (`cat=`) and codes (`code=`) are the stdlib's
`STDLIB_CATEGORY_*` / `CPLUS_EXIT_*` values printed by the `error*` API.

| cat | code | meaning in these apps |
| --- | --- | --- |
| 2 (RANGE) | 101 (BOUNDS) | zero/oversized capacity, oversized key/value/name/topic/payload/subscriber, out-of-range byte, empty/non-positive read count, batch too large, empty-queue tick/run |
| 2 (RANGE) | 107 (RESOURCE) | scheduler queue overflow; ring-buffer overflow |
| 1 (INVALID) | 106 (OWNERSHIP) | missing key, unknown topic/subscriber, duplicate topic/subscription/name |
| 1 (INVALID) | 108 (UNSUPPORTED) | unknown command, invalid priority, arity/bad args |

All refusals are reported before any state is mutated (validation is
failure-atomic). The programs continue after a refusal and still exit 0,
because refusal is the expected outcome of a hostile case; only a failed
`expect` content check makes a program exit 1.

## Determinism and failure modes

* No clock, threads, randomness or environmental input is read anywhere.
* Each scenario is executed twice and compared byte-for-byte.
* A wrong internal value makes `expect` print `FAIL` and exit 1, so a golden
  file cannot mask a content bug.
* A build error, missing binary, wrong exit code, any stderr output, a golden
  mismatch, or a nondeterministic second run marks that app `failed`.

## Files

```
examples/expanded/systems_2/
  lru_cache.cp  scheduler.cp  event_bus.cp  ring_buffer.cp
  run.sh            build + run all scenarios, print SYSTEMS_2 verdict
  NOTES.md          this file
  expected/*.expected   checked-in byte-exact golden outputs (tracked)
  results/          generated binaries/logs/outputs (gitignored)
```
