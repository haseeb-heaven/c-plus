# HTML API reference

`cplus_api.html` is generated from the checked-in headers by
`tools/gen_api_html.py`. It covers 248 methods across the safer (`stdlib/safe`)
and modern (`stdlib/*.hp`) libraries, including `web.hp`, `sqldb.hp`,
`gameeng.hp`, data stacks, slot pools, arena/pool allocators, math3d/scene3d/
mesh3d, widgets and CLI.

Regenerate after any header change:

```sh
python3 tools/gen_api_html.py
sh tests/docs/run_doc_check.sh
```

The checker regenerates into a temporary file and diffs against the committed
HTML; it fails on stale docs. Do not hand-edit the generated file. Examples for
the methods added on this branch are hand-written in `tools/gen_api_html.py`;
all other entries are labelled *call skeleton* and are derived from the method's
own parameter list, not compiled programs. See
`docs/stdlib/MODERN_SAFER_EVIDENCE.md` for measured evidence and non-claims.
