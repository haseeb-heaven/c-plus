# Known scope-escape limits (honest security boundary, 2026-11-25)

Two adversarial reproducers were supplied against Agent 2 code. Both are
**confirmed limitations**, not fixed vulnerabilities. We do not claim they are
absent.

## 1. `ctl_span` has no borrow/lifetime token

`ctl_span` is intentionally a non-owning `{data, len, elem_size}` view. A span
initialized from a stack buffer and returned from the declaring frame leaves
`data` dangling; `ctl_span_at()` cannot detect it. This is the same contract as
CTL's plain-C span: callers must keep the backing storage alive for the whole
span lifetime.

Mitigation: keep spans inside the frame that owns their data; for values that
must outlive a frame use `stdlib_handle_pool` / `stdlib_pool_ref` (generation
checked, stale refs refused) or copy into a checked container. Do not return a
span from a function whose backing object is local.

## 2. Raw pointers from a scoped bump stack can escape and be hijacked

`stdlib_scoped_mem.hp` (the renamed scoped-memory facade) restores the thread
data-stack top with one store when a scope drops. A raw pointer saved past that
drop is stale: a later scope reuses the same address, and writes through the
stale alias silently corrupt the new object. No compiler analysis or runtime
side table currently tags these bump addresses.

Mitigation: never store raw bump pointers outside their scope. Use
`stdlib_pool_ref` handles for values that must survive; those carry a
generation token and stale uses are refused with exit codes 103/104. The
opt-in no-heap policy (`stdlib/no_heap.hp`) removes accidental heap use but
does **not** add escape checking.

## Scope boundary

The third supplied reproducer (array-element alias UAF bypass in the compiler's
static analyzer) is Agent 1 scope and was not touched per instruction.

No claim: these limitations mean the libraries are not "100% safe". They are
documented so callers can write safe code within the supported contracts.
