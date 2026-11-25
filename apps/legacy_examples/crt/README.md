# Checked CRT examples

One standalone example per public checked API. Build:

```sh
sh scripts/build_safecrt.sh
CC=${CPLUS_CLANG:-clang}
for f in apps/legacy_examples/crt/*.c; do
  [ "$(basename "$f")" = "README.md" ] && continue
  "$CC" -Istdlib/safe -Ithird_party/intel-safestringlib/include "$f" build/safe/libcplus_safecrt.a -o "build/safe/$(basename "$f" .c)"
done
```
