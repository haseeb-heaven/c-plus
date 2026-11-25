#!/usr/bin/env python3
"""Generate docs/html/cplus_api.html from the checked-in safer/modern headers.

This is a documentation generator, not part of any correctness test path.
`python3 tools/gen_api_html.py --check` regenerates into a temporary file and
diffs it against the committed HTML so stale docs are caught by CI/review.
"""
from __future__ import annotations
import argparse
import difflib
import html
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADERS = [
    "stdlib/web.hp", "stdlib/sqldb.hp", "stdlib/gameeng.hp",
    "stdlib/datastack.hp", "stdlib/mempool.hp", "stdlib/arena.hp",
    "stdlib/ctl_span.hp", "stdlib/nuklear_adapt.hp", "stdlib/goose_mem.hp", "stdlib/no_heap.hp",
    "stdlib/math3d.hp", "stdlib/scene3d.hp", "stdlib/mesh3d.hp",
    "stdlib/widgets.hp", "stdlib/cli.hp",
] + sorted(str(p.relative_to(ROOT)) for p in (ROOT / "stdlib/safe").glob("*.h"))

# Hand-written examples for the methods added in this branch. Everything else
# gets a clearly labelled call skeleton derived from its own parameter list.
EXAMPLES = {
    "web_server_init": 'web_server srv;\nif (web_server_init(&srv, "127.0.0.1", 18099) != 0) return 1;\nweb_server_shutdown(&srv); // stack storage, no heap',
    "web_server_start": 'web_server *srv = web_server_start("127.0.0.1", 18099);\nif (srv) web_server_stop(srv);',
    "web_accept": 'web_conn c;\nif (web_accept(&srv, &c) == 1) { /* dispatch */ web_conn_close(&c); }',
    "web_reply": 'web_reply(&c, 200, "healthy");',
    "sqldb_init": 'sqldb db;\nsqldb_init(&db);',
    "sqldb_insert": 'if (sqldb_insert(&db, 101, 30, "ada") != 0) return 1;',
    "sqldb_select": 'int found = 0;\nconst sql_row *r = sqldb_select(&db, 101, &found);\nif (found) printf("%s\\n", r->name);',
    "sqldb_update": 'sqldb_update(&db, 101, 31, "ada2");',
    "sqldb_delete": 'sqldb_delete(&db, 102);',
    "sqldb_save": 'sqldb_save(&db, "app_dump.db");',
    "sqldb_load": 'sqldb_load(&db, "app_dump.db");',
    "game_world_init": 'game_world w;\ngame_world_init(&w);',
    "game_add_entity": 'int slot = game_add_entity(&w, 5, -3, 10);',
    "game_step": 'game_step(&w, 0.25f);',
    "game_entity_pos": 'int x = 0, y = 0;\nif (game_entity_pos(&w, slot, &x, &y) == 0) printf("(%d,%d)\\n", x, y);',
    "game_kill": 'game_kill(&w, slot);',
    "stdlib_datastack_alloc": 'error e = error();\nvoid *p = stdlib_datastack_alloc(ds, 32, 16, &e);',
    "stdlib_scratch_allocator": 'stdlib_allocator A = stdlib_scratch_allocator();\nvector v = vector_in(&A);',
    "stdlib_slot_pool_init": 'stdlib_slot_pool pool;\nstdlib_slot_pool_init(&pool, sizeof(Entity), 4, &e);',
    "stdlib_slot_ref_get": 'struct Entity *en = (struct Entity *)stdlib_slot_ref_get(&ref, &e);',
    "cplus_diag_get_mode": 'cplus_diag_mode m = cplus_diag_get_mode(); /* opt-in diagnostics */',
}

SKIP = {"if", "for", "while", "switch", "return", "sizeof", "assert",
        "defined", "offsetof", "do", "else"}
DECL = re.compile(r"(?:static\s+inline\s+|static\s+|extern\s+)?([A-Za-z_][\w \t\*]*?)\b"
                  r"([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*(?:;|\{)", re.S)


def preceding_comment(lines: list[str], idx: int) -> str:
    out: list[str] = []
    j = idx - 1
    while j >= 0 and not lines[j].strip():
        j -= 1
    if j >= 0 and lines[j].strip().endswith("*/"):
        while j >= 0:
            s = lines[j].strip()
            if "/*" in s:
                out.append(s.split("/*", 1)[1].replace("*/", "").strip())
                break
            out.append(re.sub(r"^\*+\s?", "", s).replace("*/", "").strip())
            j -= 1
    text = " ".join(x for x in reversed(out) if x)
    return text[:180]


def skeleton(name: str, args: str) -> str:
    params = []
    for raw in args.split(","):
        raw = raw.strip()
        if not raw or raw == "void":
            continue
        pieces = raw.split()
        pname = pieces[-1].lstrip("*")
        if not pname.isidentifier():
            pname = "arg"
        t = " ".join(pieces[:-1])
        if "*" in raw or "allocator" in t or "stack" in t or "pool" in t:
            params.append(f"({pname} != 0 ? {pname} : 0)")
        elif "char" in t and "*" in raw:
            params.append('"text"')
        elif "float" in t or "double" in t:
            params.append("0.0f")
        elif "size_t" in t:
            params.append("0u")
        else:
            params.append("0")
    return f"{name}({', '.join(params)});  /* call skeleton: see source for contracts */"


def methods(rel: str) -> list[dict]:
    lines = (ROOT / rel).read_text(errors="replace").splitlines()
    found: list[dict] = []
    seen: set[str] = set()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", "//", "/*", "*", "typedef", "struct", "enum")):
            continue
        m = DECL.search(line)
        if not m or m.group(2) in SKIP or m.group(2) in seen:
            continue
        name = m.group(2)
        seen.add(name)
        sig = " ".join(line.split())
        if sig.endswith("{"):
            sig = sig[:-1].rstrip()
        found.append({
            "name": name,
            "sig": html.escape(sig),
            "file": rel,
            "line": i + 1,
            "desc": html.escape(preceding_comment(lines, i) or "See source header for contract."),
            "example": html.escape(EXAMPLES.get(name, skeleton(name, m.group(3)))),
        })
    return found


def render() -> str:
    groups = [(rel, methods(rel)) for rel in HEADERS if (ROOT / rel).exists()]
    total = sum(len(v) for _, v in groups)
    p: list[str] = []
    p.append("<!doctype html>\n<html lang='en'><head><meta charset='utf-8'>")
    p.append("<meta name='viewport' content='width=device-width,initial-scale=1'>")
    p.append("<title>C+ safer &amp; modern library API</title><style>")
    p.append("body{font:15px/1.5 -apple-system,Segoe UI,sans-serif;margin:2rem auto;max-width:1000px;color:#222}"
             "h1{border-bottom:3px solid #385} code,pre{font-family:ui-monospace,Menlo,monospace}"
             "pre{background:#f4f6f8;padding:.8rem;overflow:auto;border-radius:6px}"
             "section{border-top:1px solid #ddd;padding-top:1rem} .meta{color:#567;font-size:.85rem}"
             "nav{columns:2;font-size:.9rem} a{color:#246}</style></head><body>")
    p.append("<h1>C+ safer &amp; modern library API</h1>")
    p.append(f"<p class='meta'>Generated by <code>tools/gen_api_html.py</code> from checked-in headers. "
             f"{total} methods in {len(groups)} headers. Do not edit by hand; run "
             "<code>python3 tools/gen_api_html.py</code> to refresh.</p>")
    p.append("<p>These are additive APIs. Examples labelled <em>call skeleton</em> are generated from "
             "parameter lists and are not compiled programs; examples for methods added on this branch "
             "are hand-written. See <code>docs/stdlib/MODERN_SAFER_EVIDENCE.md</code> for measured "
             "safer/faster evidence and non-claims.</p><nav>")
    for rel, ms in groups:
        p.append(f"<a href='#{rel.replace('/', '-')}'>{html.escape(rel)} ({len(ms)})</a><br>")
    p.append("</nav>")
    for rel, ms in groups:
        anchor = rel.replace("/", "-")
        p.append(f"<h2 id='{anchor}'>{html.escape(rel)}</h2>")
        for m in ms:
            p.append(f"<section><h3 id='{html.escape(m['name'])}'>{html.escape(m['name'])}</h3>")
            p.append(f"<p class='meta'>{html.escape(m['file'])}:{m['line']} — {m['desc']}</p>")
            p.append(f"<pre><code>{m['sig']}</code></pre>")
            p.append(f"<pre><code>{m['example']}</code></pre></section>")
    p.append("</body></html>\n")
    return "\n".join(p)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/html/cplus_api.html")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    out = ROOT / a.out
    content = render()
    if a.check:
        with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False) as f:
            f.write(content)
            tmp = Path(f.name)
        old = out.read_text() if out.exists() else ""
        diff = list(difflib.unified_diff(old.splitlines(), content.splitlines(),
                                         fromfile=str(out), tofile="generated"))
        tmp.unlink()
        if diff:
            print("\n".join(diff[:60]))
            print("doc check: STALE — regenerate with python3 tools/gen_api_html.py")
            return 1
        print("doc check: up to date")
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(content)
    print(f"wrote {a.out}: {content.count('<section>')} methods")
    return 0


if __name__ == "__main__":
    sys.exit(main())
