# Issue #216 regression

`stdlib/widgets.hp` reported a too-small serialized size whenever a widget had
negative `id`, `x` or `y`, because `stdlib_widgets_dec_digits()` counted only
the magnitude and not the leading `-`. `stdlib_window_serialize()` then wrote
the label, newline and final NUL past the exact-size output buffer.

`test_issue216.cp` checks:

* `stdlib_widgets_dec_digits()` byte counts for positive, negative and
  `LLONG_MIN` / `LLONG_MAX` values;
* the exact issue reproducer (`id=-1, kind=1, x=-10, y=-20, label="hello"`) with
  an exact-size buffer and a tail canary that detects any write past the
  advertised size;
* a four-widget case (12 missing sign bytes before the fix) with byte-exact
  serialized text, tail canary and a short-buffer refusal that leaves the
  destination untouched.

Run with the already-built C+ compiler; no project build is performed:

```sh
sh tests/stdlib/issue216/run.sh
```

Pass condition: two byte-identical runs, empty stderr, no `FAIL ` line and a
final `ISSUE216 <checks> 0` line with `checks > 0`. Generated artifacts are
written only to `tests/stdlib/issue216/results/`.
