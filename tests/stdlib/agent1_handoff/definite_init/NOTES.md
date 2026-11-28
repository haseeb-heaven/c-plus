# P1 — Definite initialization on every path (minimal reproducers)
Agent 1 handoff, branch `agent/modern-stdlib`, macOS. All cases live in `tests/stdlib/agent1_handoff/definite_init/`. No compiler, runtime, header, or other repository file was modified.
## Method
Every case was run exactly as:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/<case>.cp
```
from the repository root. `manifest.json` in this directory is generated from the same runs and stores stdout, stderr, exit code and classification verbatim. Each case declares `// EXPECT: REJECT` and expects a tagged `[CPLUS-LIFETIME] variable '...' is uninitialized (read before it is initialized)` diagnostic.
## Result summary
- 10 cases: **5 tagged REJECT**, **5 silent ACCEPT**, 0 untagged REJECT.
Where the compiler already emits the tagged diagnostic (positive controls / working scope): direct scalar reads, a raw `error *` owner assigned on one branch, a scalar out-parameter with a non-writing path, a loop with a zero-iteration path, and an early return that leaves a guarded raw pointer (plus its guard flag) uninitialized.
Where it stays silent: by-value class owners (`string`), borrowed `slice`, `string_view`, and by-value struct fields — including the guarded-resource aggregate case. In those cases `check --strict` exits 0 with no `[CPLUS-` diagnostic. Strict unsanitized builds also succeed; running them produces garbage exit statuses (20, 1, 64) and a segfault (139) for the aggregate guard case, consistent with an actual uninitialized read. These are compiler-coverage gaps, not safe zero-initialization.
## Per-case results (verbatim)
### 01_direct_scalar_control.cp
- Category: positive-control/direct scalar
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **REJECT**, exit code **1**
- Classification: **POSITIVE_CONTROL_TAGGED_REJECT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **yes**
- Note: Baseline positive control: the C+ checker already emits a tagged [CPLUS-LIFETIME] uninitialized diagnostic for a direct scalar read.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/01_direct_scalar_control.cp
```
stdout (verbatim):
```text
<empty>
```
stderr (verbatim):
```text
tests/stdlib/agent1_handoff/definite_init/01_direct_scalar_control.cp:9:12: error: [CPLUS-LIFETIME] variable 'count' is uninitialized (read before it is initialized)
      return count;
             ^~~~~
```
### 02_error_ptr_one_branch.cp
- Category: raw pointer / owner on one branch
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **REJECT**, exit code **1**
- Classification: **TAGGED_REJECT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **yes**
- Note: Tagged diagnostic produced. Definite-init for a raw pointer local is enforced across the branch.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/02_error_ptr_one_branch.cp
```
stdout (verbatim):
```text
<empty>
```
stderr (verbatim):
```text
tests/stdlib/agent1_handoff/definite_init/02_error_ptr_one_branch.cp:14:12: error: [CPLUS-LIFETIME] variable 'slot' is uninitialized (read before it is initialized)
      return slot->failed; /* uninitialized on the false path */
             ^~~~
```
### 03_scalar_outparam.cp
- Category: scalar out-parameter with a silent writer path
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **REJECT**, exit code **1**
- Classification: **TAGGED_REJECT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **yes**
- Note: Tagged diagnostic produced. The checker sees that the callee may not write *out.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/03_scalar_outparam.cp
```
stdout (verbatim):
```text
<empty>
```
stderr (verbatim):
```text
tests/stdlib/agent1_handoff/definite_init/03_scalar_outparam.cp:17:12: error: [CPLUS-LIFETIME] variable 'result' is uninitialized (read before it is initialized)
      return result; /* indeterminate when g_condition == 0 */
             ^~~~~~
```
### 04_byval_owner_one_branch.cp
- Category: class-owned object on one branch
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **ACCEPT**, exit code **0**
- Classification: **GAP_SILENT_ACCEPT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **no**
- Note: SILENT: check rc=0, no [CPLUS- diagnostic. Definite-init is not enforced for by-value class-typed owners.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/04_byval_owner_one_branch.cp
```
stdout (verbatim):
```text
check: tests/stdlib/agent1_handoff/definite_init/04_byval_owner_one_branch.cp ok
```
stderr (verbatim):
```text
<empty>
```
### 05_slice_one_branch.cp
- Category: slice declared but not initialized on a path
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **ACCEPT**, exit code **0**
- Classification: **GAP_SILENT_ACCEPT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **no**
- Note: SILENT: check rc=0, no [CPLUS- diagnostic. A strict unsanitized build also succeeds; its exit status depends on the uninitialized read (observed 20), confirming a genuine false negative.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/05_slice_one_branch.cp
```
stdout (verbatim):
```text
check: tests/stdlib/agent1_handoff/definite_init/05_slice_one_branch.cp ok
```
stderr (verbatim):
```text
<empty>
```
### 06_stringview_one_branch.cp
- Category: string_view declared but not initialized on a path
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **ACCEPT**, exit code **0**
- Classification: **GAP_SILENT_ACCEPT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **no**
- Note: SILENT: check rc=0, no [CPLUS- diagnostic. Strict unsanitized build succeeds; observed runtime exit 1 from the uninitialized read.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/06_stringview_one_branch.cp
```
stdout (verbatim):
```text
check: tests/stdlib/agent1_handoff/definite_init/06_stringview_one_branch.cp ok
```
stderr (verbatim):
```text
<empty>
```
### 07_loop_zero_iterations.cp
- Category: loop with zero iterations then read
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **REJECT**, exit code **1**
- Classification: **TAGGED_REJECT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **yes**
- Note: Tagged diagnostic produced. The checker is path-sensitive for the zero-iteration path.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/07_loop_zero_iterations.cp
```
stdout (verbatim):
```text
<empty>
```
stderr (verbatim):
```text
tests/stdlib/agent1_handoff/definite_init/07_loop_zero_iterations.cp:11:12: error: [CPLUS-LIFETIME] variable 'last' is uninitialized (read before it is initialized)
      return last; /* indeterminate when the loop runs zero times */
             ^~~~
```
### 08_early_return_guard_ptr.cp
- Category: early return leaves a guarded raw resource uninitialized
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **REJECT**, exit code **1**
- Classification: **TAGGED_REJECT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **yes**
- Note: Tagged diagnostics produced for BOTH the guard flag and the guarded resource pointer.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/08_early_return_guard_ptr.cp
```
stdout (verbatim):
```text
<empty>
```
stderr (verbatim):
```text
tests/stdlib/agent1_handoff/definite_init/08_early_return_guard_ptr.cp:18:9: error: [CPLUS-LIFETIME] variable 'ready' is uninitialized (read before it is initialized)
      if (ready)                   /* guard flag itself is uninitialized here */
          ^~~~~
tests/stdlib/agent1_handoff/definite_init/08_early_return_guard_ptr.cp:19:16: error: [CPLUS-LIFETIME] variable 'slot' is uninitialized (read before it is initialized)
          return slot->failed;     /* guarded resource also uninitialized */
                 ^~~~
```
### 09_struct_field_one_branch.cp
- Category: out-value stored in a struct field on one branch
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **ACCEPT**, exit code **0**
- Classification: **GAP_SILENT_ACCEPT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **no**
- Note: SILENT: check rc=0, no [CPLUS- diagnostic. Definite-init does not look through by-value aggregates; the strict unsanitized build succeeds with runtime exit 64.
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/09_struct_field_one_branch.cp
```
stdout (verbatim):
```text
check: tests/stdlib/agent1_handoff/definite_init/09_struct_field_one_branch.cp ok
```
stderr (verbatim):
```text
<empty>
```
### 10_early_return_guard_struct.cp
- Category: early return leaves a guarded resource aggregate uninitialized
- Expected: REJECT / `[CPLUS-LIFETIME] uninitialized`
- Observed: **ACCEPT**, exit code **0**
- Classification: **GAP_SILENT_ACCEPT**
- Tagged `[CPLUS-LIFETIME] uninitialized`: **no**
- Note: SILENT: check rc=0, no [CPLUS- diagnostic. Strict unsanitized build succeeds; the false path dereferences an uninitialized pointer and segfaults (exit 139).
Command:
```sh
./build/c+ check --strict tests/stdlib/agent1_handoff/definite_init/10_early_return_guard_struct.cp
```
stdout (verbatim):
```text
check: tests/stdlib/agent1_handoff/definite_init/10_early_return_guard_struct.cp ok
```
stderr (verbatim):
```text
<empty>
```
