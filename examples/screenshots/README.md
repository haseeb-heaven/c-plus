# Captured runtime demonstrations

Verbatim terminal transcripts and rendered SVG images from real runs of the
checked-library demos. They demonstrate **refusals under the tested inputs
only** — not full memory/lifetime/thread safety.

## Contents

- `captures/*.txt` — six transcripts: `unsafe_vs_safe_memcpy`,
  `bounds_query`, `lifetime_refusal`, `sync_double_destroy`,
  `database_safety_refusals`, `stdc_hostile_refusals`.
- `images/*.svg` — four rendered terminal screenshots (the first four above).
- `images/captures/*.txt` — local transcript copies so SVG rendering works
  without the sibling directory.

## Regenerate

```sh
./examples/screenshots/capture.sh          # rebuild demos, rewrite transcripts
cd examples/screenshots/images && ./render.sh   # rewrite SVGs
```

All build output goes to `captures/tmp/` (gitignored). Each transcript records
its own exact build/run command and toolchain header.

## Honest scope

Refusals are emitted by real binaries under `/usr/bin/timeout 30` on
macOS/arm64 with Apple clang 16. Different hosts, toolchains or uninstrumented
callers are not covered.
