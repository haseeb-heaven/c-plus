# Issue #217 regression

`stdlib/view_lifetime.hp::stdlib_view_register()` used to treat only a
never-used (`ptr == NULL`) slot as reusable. Retirement keeps the pointer as a
tombstone so open-addressed probe chains stay intact, so after 4096 distinct
addresses had been registered and retired every slot had a non-NULL key and all
later registrations permanently returned `0`. That made `string::to_view()`,
`vector::view()` and `stdlib_arena_take_checked()` fail forever with dead or
stale views ("lifetime registry exhausted").

This is the same root cause as issue #100. The fix is present in
`stdlib/view_lifetime.hp`: the register probe records the first reusable slot
(`!s->live || s->ptr == NULL`) but keeps probing to `s->ptr == ptr` or
`s->ptr == NULL`, so duplicate keys are still updated in place, probe chains
stay intact and tombstones are reclaimed. Lookups only stop at a never-used
slot.

`test_issue217.cp` checks:

* 5000 distinct addresses are churned, then all 4096 slots are verified to
  hold non-NULL tombstones with no live entry (the exact exhaustion state);
* 64 fresh addresses still register, validate, report the right token and
  byte extent, and never revive an old token;
* re-registering a retired address gets a fresh token that supersedes the
  stale one;
* end-to-end: `string_view` is live, `slice` is valid/not stale, and
  `stdlib_arena_take_checked()` / `release_checked()` work after the churn.

Run with the already-built C+ compiler; no project build is performed:

```sh
sh tests/stdlib/issue217/run.sh
```

Pass condition: two byte-identical runs, empty stderr, no `FAIL ` line and a
final `ISSUE217 <checks> 0` line with `checks > 0`. Generated artifacts are
written only to `tests/stdlib/issue217/results/`.
