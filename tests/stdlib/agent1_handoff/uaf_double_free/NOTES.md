# P0 handoff — UAF / double-free compiler-effort reproducers (Agent 1)

Owner: handoff agent 1 of 12. Files in this directory are the only files
touched by this task. No `compiler/**`, `runtime/**`, or `.hp` header was
modified; no git add/commit was performed.

## Purpose and responsibility boundary

These three `.cp` files are **minimal, independent compiler-effort reproducers**
for the P0 item "Complete UAF and double-free analysis". They intentionally use
the **normal stdlib class API** (`string::drop()`, `string::at()`,
`string::c_str()`) and the checked runtime API (`cplus_check_live()`) so that:

* the invalid operation is expressed through the API / compiler analysis,
  never by a raw dereference of intentionally stale memory in the test, and
* each program remains memory-safe if the compiler fails to reject it.

**These are compiler responsibilities, not stdlib bugs.** The stdlib already
behaves correctly: `string::drop()` releases the buffer exactly once and clears
the owner, the second `drop()` is a safe no-op, `at()` on a dead owner returns
`'\0'`, and `cplus_check_live()` on a dangling managed pointer aborts with a
deterministic runtime diagnostic (observed exit 105, see below). What is
missing is **compile-time detection / tagged C+ diagnostics**: the current
`build/c+` does not relate a `class string`'s internal `chars` pointer (or a
captured alias of it) to the `drop()` that frees it, so it emits no
`[CPLUS-` diagnostic for any of the three cases. Agent 1's fix belongs in the
compiler's ownership/lifetime analysis, not in `stdlib/string.hp`.

Environment used for every command below:

```
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
cwd = /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
compiler = ./build/c+   (repo-built binary, in-tree)
```

Classification rule (same as `tests/stdlib/compile_time/run_compile_time.sh`):
only stderr/stdout containing a `[CPLUS-` token counts as **tagged REJECT**;
a rejection without `[CPLUS-` is **untagged REJECT**; exit 0 is **ACCEPT**.

---

## 1. `uaf_after_drop.cp`

Source operation: `string s = string("abc"); s.drop(); print(s.at(0));`

Exact command:

```
./build/c+ check --strict tests/stdlib/agent1_handoff/uaf_double_free/uaf_after_drop.cp
```

Exact stdout:

```
check: tests/stdlib/agent1_handoff/uaf_double_free/uaf_after_drop.cp ok
```

Exact stderr: *(empty)*

Exit code: `0`

Expected outcome: **REJECT** with a tagged C+ diagnostic.

Observed classification: **ACCEPT** (compiler capability gap — no `[CPLUS-`
token; `check` reports `ok`).

Diagnostic Agent 1 must emit (at the `print(s.at(0))` use site, after the
`drop()`), following the existing compiler wording:

```
error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: <use of dropped owner 's'>
```

Rationale: `string::drop()` frees the buffer owned by `s`; any later read
through `s` is a use of that freed buffer. The test is safe at runtime because
`at()` guards the read, so the compiler must supply the safety proof.

Supplementary evidence (not part of the required command):
`./build/c+ build --strict --sanitize=none ... -o <exe>` also succeeds (exit 0),
and running that binary exits 0 (prints a single NUL byte via `at()`). No
memory corruption occurs — reinforcing that this is a missing compile-time
check, not a stdlib defect.

---

## 2. `double_drop.cp`

Source operation: `string s = string("abc"); s.drop(); s.drop();`

Exact command:

```
./build/c+ check --strict tests/stdlib/agent1_handoff/uaf_double_free/double_drop.cp
```

Exact stdout:

```
check: tests/stdlib/agent1_handoff/uaf_double_free/double_drop.cp ok
```

Exact stderr: *(empty)*

Exit code: `0`

Expected outcome: **REJECT** with a tagged C+ diagnostic.

Observed classification: **ACCEPT** (compiler capability gap — no `[CPLUS-`
token; `check` reports `ok`).

Diagnostic Agent 1 must emit (at the **second** `s.drop()`):

```
error: [CPLUS-OWNER] double free of 's'
```

Rationale: a class-owned resource must be released exactly once; the second
`drop()` releases the same owner again. This mirrors the existing diagnostic
emitted for raw managed buffers (`[CPLUS-OWNER] double free of 'buf'`), which
the compiler already produces for `cplus_release(buf); cplus_release(buf);`.

Supplementary evidence: `./build/c+ build --strict --sanitize=none ...` succeeds
(exit 0); the built binary exits 0 because the second stdlib `drop()` is a no-op
when `chars == NULL`. No double free happens at runtime — again a compiler gap.

---

## 3. `stale_alias.cp`

Source operation:

```
string s = string("abc");
const char *p = s.c_str();      /* captured while owner alive */
s.drop();                       /* frees the buffer p aliases */
cplus_check_live((void *)p);    /* checked use of the stale alias */
```

Exact command:

```
./build/c+ check --strict tests/stdlib/agent1_handoff/uaf_double_free/stale_alias.cp
```

Exact stdout:

```
check: tests/stdlib/agent1_handoff/uaf_double_free/stale_alias.cp ok
```

Exact stderr: *(empty)*

Exit code: `0`

Expected outcome: **REJECT** with a tagged C+ diagnostic.

Observed classification: **ACCEPT** (compiler capability gap — no `[CPLUS-`
token; `check` reports `ok`).

Diagnostic Agent 1 must emit (at the `cplus_check_live((void *)p)` call):

```
error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: liveness check on released pointer 'p'
```

Rationale: `p` aliases the owner's buffer captured before `drop()` and is
dangling afterwards. `cplus_check_live()` is used deliberately, as the checked
API that *proves* a managed pointer is live, so the test never raw-dereferences
stale memory. The compiler must relate the captured alias to the resource
released by `s.drop()`.

Supplementary evidence: `./build/c+ build --strict --sanitize=none ...` succeeds
(exit 0). At run time the checked API catches the bug safely: the binary exits
`105` with stderr
`C+ runtime error: dangling pointer: pointer is no longer a live C+ allocation`.
So the stdlib/runtime is doing its job; only the compile-time analysis is
missing.

---

## Control experiment (proves the compiler already has the machinery)

Identical checked-use pattern on a *directly* managed pointer (not a pointer
stored inside / captured from a stdlib `class`) is rejected with a tagged
diagnostic. Source used (probe, kept out of the final reproducer files):

```c
#include "cplus/cplus.h"
int main(void) {
    char *p = (char *)cplus_alloc(4);
    cplus_release(p);
    cplus_check_live(p);
    return 0;
}
```

Command:

```
./build/c+ check --strict <probe>.cp
```

Exact stdout: *(empty)*

Exact stderr:

```
<probe>.cp:5:5: error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: liveness check on released pointer 'p'
      cplus_check_live(p);
      ^~~~~~~~~~~~~~~~
help: suggested correction: do not dereference after release; copy the data or transfer ownership with cplus_move()
```

Exit code: `1` → **tagged REJECT**. This confirms the gap is specifically the
tracking of stdlib class-owned resources and aliases captured from them, not a
missing diagnostic vocabulary.

## Summary

| file | expect | observed (check --strict) | exit | required diagnostic |
|---|---|---|---|---|
| `uaf_after_drop.cp` | REJECT (tagged) | ACCEPT | 0 | `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free:` on use of dropped `s` |
| `double_drop.cp` | REJECT (tagged) | ACCEPT | 0 | `[CPLUS-OWNER] double free of 's'` on 2nd `s.drop()` |
| `stale_alias.cp` | REJECT (tagged) | ACCEPT | 0 | `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: liveness check on released pointer 'p'` |

All three are currently capability gaps (ACCEPT) in `./build/c+ check --strict`.
Closing them is Agent 1's compiler work; the examples in
`tests/stdlib/compile_time/string_use_after_drop.cp` and
`string_double_drop.cp` show the same gap was previously worked around by
bypassing the class API and calling `cplus_release()` on the raw buffer — these
reproducers deliberately do **not** use that workaround, so they exercise the
real API path that must be fixed.
