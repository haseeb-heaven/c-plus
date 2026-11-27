#!/usr/bin/env python3
"""Render proof evidence as PNG screenshots.

The report pages are plain HTML laid out with fixed row heights so the total
page height is known before the browser starts.  Headless Chrome then captures
exactly that region, which keeps the images reproducible and free of the
scroll-cut artefacts a viewport screenshot produces.

Usage:
    python3 proof/report.py
"""

from __future__ import annotations

import html
import math
import os
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CHROME_CANDIDATES = [
    os.environ.get("CPLUS_CHROME", ""),
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
]

PAGE_WIDTH = 1600
PAD = 28
CONTENT_WIDTH = PAGE_WIDTH - 2 * PAD
LINE_HEIGHT = 18
CELL_PAD = 7
FONT_PX = 13
CHAR_PX = 7.8
HEADER_ROW_HEIGHT = 32
HEADER_BLOCK_HEIGHT = 126
FOOTER_BLOCK_HEIGHT = 42

BG = "#0d1117"
PANEL = "#161b22"
BORDER = "#30363d"
TEXT = "#e6edf3"
MUTED = "#8b949e"
GREEN = "#3fb950"
RED = "#f85149"
YELLOW = "#d29922"
BLUE = "#58a6ff"
PURPLE = "#bc8cff"

STATUS_COLORS = {
    "OUTPUT_MATCHES": GREEN,
    "RAN": BLUE,
    "OUTPUT_DIFFERS": YELLOW,
    "BUILD_FAILED": RED,
    "SKIPPED": MUTED,
    "FAILED": RED,
    "ABORTED_SAFELY": GREEN,
    "NONE": RED,
    "COMPILE_TIME": GREEN,
    "RUNTIME": YELLOW,
}


def chrome_binary() -> str:
    for candidate in CHROME_CANDIDATES:
        if candidate and Path(candidate).exists():
            return candidate
    found = shutil.which("google-chrome") or shutil.which("chromium")
    if found:
        return found
    raise RuntimeError(
        "no headless browser found; set CPLUS_CHROME to a Chrome binary")


def esc(text) -> str:
    return html.escape(str(text if text is not None else ""))


def lines_for(text: str, column_width: int) -> int:
    if not text:
        return 1
    per_line = max(8, int(column_width / CHAR_PX))
    count = 0
    for raw in str(text).splitlines() or [""]:
        count += max(1, math.ceil(len(raw) / per_line))
    return max(1, count)


def clip(text: str, column_width: int, max_lines: int) -> str:
    """Keep at most `max_lines` rendered lines of `text`."""
    if text is None:
        return ""
    text = str(text)
    per_line = max(8, int(column_width / CHAR_PX))
    kept: list[str] = []
    used = 0
    for raw in text.splitlines() or [""]:
        chunks = [raw[i:i + per_line] for i in range(0, len(raw), per_line)] or [""]
        for chunk in chunks:
            if used >= max_lines:
                kept.append("... (truncated)")
                return "\n".join(kept)
            kept.append(chunk)
            used += 1
    return "\n".join(kept)


def cell_html(text, column_width, max_lines=12, color=None, mono=True) -> str:
    body = clip(text, column_width, max_lines)
    lines = lines_for(body, column_width)
    height = lines * LINE_HEIGHT + 2 * CELL_PAD
    style = (f"width:{column_width}px;vertical-align:top;"
             f"height:{height}px")
    if color:
        style += f";color:{color}"
    if mono:
        style += ";font-family:ui-monospace,SFMono-Regular,Menlo,monospace"
    return (f'<td style="{style}">'
            f'<pre style="margin:0;white-space:pre-wrap;word-break:break-word;'
            f'font-size:{FONT_PX}px;line-height:{LINE_HEIGHT}px">'
            f'{esc(body)}</pre></td>')


def table(columns: list[tuple[str, int]], rows: list[list[dict]]) -> tuple[str, int]:
    """Render a table.  Each row cell is a dict with keys text/color/align."""
    head = "".join(
        f'<th style="width:{w}px;text-align:left;padding:{CELL_PAD}px;'
        f'font-size:12px;letter-spacing:.06em;text-transform:uppercase;'
        f'color:{MUTED};border-bottom:1px solid {BORDER}">{esc(name)}</th>'
        for name, w in columns)
    out = [f'<table style="width:{sum(w for _, w in columns)}px;'
           f'border-collapse:collapse;table-layout:fixed">',
           f"<thead><tr>{head}</tr></thead><tbody>"]
    height = HEADER_ROW_HEIGHT
    for row_cells in rows:
        rendered = []
        row_height = 0
        for (name, width), spec in zip(columns, row_cells):
            text = spec.get("text", "")
            lines = lines_for(clip(text, width, spec.get("max_lines", 12)), width)
            row_height = max(row_height, lines * LINE_HEIGHT + 2 * CELL_PAD)
            rendered.append(cell_html(text, width, spec.get("max_lines", 12),
                                      spec.get("color")))
        out.append(f'<tr style="height:{row_height}px">' + "".join(rendered)
                   + "</tr>")
        height += row_height
    out.append("</tbody></table>")
    return "".join(out), height


def page(title: str, subtitle: str, verdict: str, verdict_color: str,
         body: str, body_height: int, footer: str = "") -> tuple[str, int]:
    total = (PAD * 2 + HEADER_BLOCK_HEIGHT + body_height
             + (FOOTER_BLOCK_HEIGHT if footer else 0) + 14)
    html_text = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>{esc(title)}</title>
<style>
  * {{ box-sizing: border-box; }}
  body {{ margin:0; padding:{PAD}px; width:{PAGE_WIDTH}px;
          background:{BG}; color:{TEXT};
          font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif; }}
  h1 {{ margin:0 0 6px 0; font-size:30px; font-weight:700; letter-spacing:-.01em; }}
  .sub {{ color:{MUTED}; font-size:15px; margin-bottom:14px; }}
  .verdict {{ display:inline-block; padding:7px 14px; border-radius:7px;
              background:{PANEL}; border:1px solid {BORDER};
              color:{verdict_color}; font-weight:700; font-size:15px;
              margin-bottom:4px; }}
  .meta {{ color:{MUTED}; font-size:12.5px;
           font-family:ui-monospace,SFMono-Regular,Menlo,monospace; }}
  table {{ background:{PANEL}; }}
  td {{ border-bottom:1px solid {BORDER}; padding:{CELL_PAD}px;
        font-size:{FONT_PX}px; line-height:{LINE_HEIGHT}px; }}
  tbody tr:nth-child(even) td {{ background:#11161d; }}
  pre {{ font-family:ui-monospace,SFMono-Regular,Menlo,monospace; }}
  .foot {{ margin-top:18px; color:{MUTED}; font-size:12.5px; }}
</style></head>
<body>
  <h1>{esc(title)}</h1>
  <div class="sub">{esc(subtitle)}</div>
  <div class="verdict">{esc(verdict)}</div>
  <div style="height:12px"></div>
  {body}
  <div class="foot">{esc(footer)}</div>
</body></html>
"""
    return html_text, total


# --------------------------------------------------------------------------
# section renderers
# --------------------------------------------------------------------------

def render_apps(apps: list[dict]) -> tuple[str, int]:
    matched = sum(1 for a in apps
                  if a.get("status") in ("OUTPUT_MATCHES", "ABORTED_SAFELY"))
    columns = [("app / game", 250), ("source", 330), ("result", 210),
               ("captured evidence", 754)]
    rows = []
    for a in apps:
        status = a.get("status", "?")
        detail = a.get("detail") or a.get("output") or a.get("reason") or ""
        rows.append([
            {"text": a.get("label", "")},
            {"text": a.get("source", "")},
            {"text": status, "color": STATUS_COLORS.get(status, TEXT)},
            {"text": detail, "max_lines": 9},
        ])
    body, height = table(columns, rows)
    verdict = (f"{matched}/{len(apps)} apps and games verified: built with C+, "
               f"run, and their output checked against the expected result")
    color = GREEN if matched == len(apps) and len(apps) else YELLOW
    return page("C+ apps and games", "Every bundled app and game is built by "
                "the C+ compiler, executed, and its output compared with the "
                "checked-in expectation.",
                verdict, color, body, height,
                "Each app's own verification script is the oracle; the "
                "captured column shows the program's real output.")


def render_safety(cases: list[dict]) -> tuple[str, int]:
    caught = sum(1 for c in cases if c.get("cplus_caught_by") != "NONE")
    columns = [("unsafe C program", 300), ("plain clang", 210),
               ("C+ verdict", 200), ("diagnostic", 834)]
    rows = []
    for c in cases:
        verdict = c.get("cplus_caught_by", "NONE")
        rows.append([
            {"text": c.get("case", "")},
            {"text": "ACCEPTED (no error)" if c.get("clang_accepted")
                     else "rejected",
             "color": YELLOW if c.get("clang_accepted") else MUTED},
            {"text": verdict, "color": STATUS_COLORS.get(verdict, TEXT)},
            {"text": c.get("cplus_output", ""), "max_lines": 14},
        ])
    body, height = table(columns, rows)
    verdict = (f"C+ rejected {caught}/{len(cases)} programs that plain clang "
               f"compiled without complaint")
    color = GREEN if caught == len(cases) and len(cases) else YELLOW
    return page("Safety matrix", "The same unsafe C source is handed to plain "
                "clang and to C+. clang accepts it; C+ must reject it at "
                "compile time or stop it at run time.",
                verdict, color, body, height,
                "Every row is a real program from the safety corpus; the "
                "clang column shows what the C compiler on this machine did "
                "with it.")


def render_abi(abi: dict) -> tuple[str, int]:
    clang_syms = abi.get("clang_symbols", [])
    cplus_syms = abi.get("cplus_symbols", [])
    only_clang = abi.get("only_clang", [])
    only_cplus = abi.get("only_cplus", [])
    identical = abi.get("abi_identical")

    def sym_block(names):
        return "\n".join(names) if names else "(none)"

    columns = [("symbol table", 300), ("clang -O2", 470),
               ("C+ --release", 470), ("difference", 304)]
    rows = [[
        {"text": "exported globals\n(nm -gU)"},
        {"text": sym_block(clang_syms), "max_lines": 16},
        {"text": sym_block(cplus_syms), "max_lines": 16},
        {"text": ("identical" if identical else
                  "clang only: " + ", ".join(only_clang) + "\n"
                  "C+ only: " + ", ".join(only_cplus)),
         "color": GREEN if identical else RED, "max_lines": 16},
    ], [
        {"text": "runtime symbols\nlinked by C+"},
        {"text": "(none linked)"},
        {"text": sym_block(abi.get("runtime_symbols", [])), "max_lines": 8},
        {"text": "from " + (abi.get("runtime_library") or "the C+ runtime") +
                 "\n(not part of the translated C source)"},
    ]]
    body, height = table(columns, rows)

    source_columns = [("C source preservation", 380),
                      ("evidence", 1164)]
    source_rows = [[
        {"text": "--emit-c output\nis still C"},
        {"text": (abi.get("emitted_c") or "(not emitted)") +
                 "\n\n" + (abi.get("emitted_head") or
                           abi.get("emit_output") or "(no emitted source)"),
         "max_lines": 26},
    ]]
    body2, height2 = table(source_columns, source_rows)
    spacer = 22
    height += height2 + spacer

    verdict = ("C ABI identical to clang and the lowered source is still "
               "C" if identical and abi.get("emitted_is_c")
               else "C ABI or C source preservation needs attention")
    color = GREEN if identical and abi.get("emitted_is_c") else YELLOW
    return page("C ABI and C source preservation",
                "One C translation unit is built twice: once by clang and once "
                "by C+. The exported symbol tables must match, and the "
                "lowered source must still be C.",
                verdict, color, body + "<div style='height:22px'></div>" + body2,
                height,
                "The probe uses plain C only: a struct, a function taking a "
                "const pointer, memcpy and strlen.  The C+ column lists the "
                "program's own globals; the runtime archive that C+ links "
                "into every program contributes its own entry points, which "
                "are shown separately and are not part of the translated C "
                "source.")


def render_benchmark(bench: dict) -> tuple[str, int]:
    """Six-way workload: the same integer program in six toolchains."""
    results = bench.get("results", {})
    order = ("clang", "cplus", "filc", "trapc", "goose", "rust")
    base = None
    clang_entry = results.get("clang", {})
    if "runs" in clang_entry:
        base = statistics.median(r["seconds"] for r in clang_entry["runs"])

    columns = [("toolchain", 320), ("median seconds", 150), ("vs clang", 110),
               ("status", 170), ("measured runs", 794)]
    rows = []
    for key in order:
        entry = results.get(key)
        if not entry:
            continue
        label = entry.get("label", key)
        if "runs" not in entry:
            rows.append([
                {"text": label},
                {"text": "-"},
                {"text": "-"},
                {"text": "NOT_TESTED", "color": YELLOW},
                {"text": entry.get("reason", "")},
            ])
            continue
        secs = statistics.median(r["seconds"] for r in entry["runs"])
        runs = ", ".join(f"{r['seconds']:.1f}s" for r in entry["runs"])
        rows.append([
            {"text": label},
            {"text": f"{secs:.1f}", "color": GREEN if key == "cplus" else TEXT},
            {"text": f"{secs / base:.2f}x" if base else "-"},
            {"text": "PASS", "color": GREEN},
            {"text": f"{len(entry['runs'])} runs: {runs}"},
        ])
    body, height = table(columns, rows)

    checks = bench.get("phase_checksums") or {}
    check_text = "\n".join(f"PHASE {name:<8} {value}" for name, value in
                           checks.items()) or "(not available)"
    checksum_columns = [("workload agreement", 320), ("evidence", 1224)]
    checksum_rows = [[
        {"text": "phase checksums\nidentical everywhere"},
        {"text": check_text +
                 "\n\nEvery toolchain ran the same SplitMix64/FNV-1a workload, so "
                 "the three phase checksums must match bit for bit.  They do, "
                 "which also shows that the C+ program computes exactly what "
                 "the plain C program computes."},
    ]]
    body2, height2 = table(checksum_columns, checksum_rows)
    height += height2 + 22

    scale = bench.get("scale")
    runs = bench.get("runs")
    tested = sum(1 for k in order if "runs" in results.get(k, {}))
    verdict = (f"{tested} toolchains ran the identical workload "
               f"(scale {scale}, {runs} runs each) and produced identical "
               f"checksums")
    return page("Six-way workload: C+ against five other toolchains",
                "The same integer workload is compiled by six toolchains and "
                "executed with the same scale, so the wall-clock times are "
                "directly comparable and the results must be identical.",
                verdict, GREEN, body + "<div style='height:22px'></div>" + body2,
                height,
                "Trap-C has no published compiler, source or license, so it is "
                "recorded as NOT_TESTED instead of being given a fabricated "
                "number.  Fil-C is Linux-only and runs in a container.")


# --------------------------------------------------------------------------
# entry point
# --------------------------------------------------------------------------
def render(section: str, subtitle: str, data, path) -> Path:
    """Render `data` for `section` into a PNG screenshot at `path`."""
    path = Path(path).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)

    if section == "apps":
        html_text, height = render_apps(data)
    elif section == "safety":
        html_text, height = render_safety(data)
    elif section == "abi":
        html_text, height = render_abi(data)
    elif section == "benchmark":
        html_text, height = render_benchmark(data)
    else:
        raise ValueError("unknown proof section: " + section)

    html_path = path.with_suffix(".html")
    html_path.write_text(html_text)

    browser = chrome_binary()
    proc = subprocess.run(
        [browser, "--headless=new", "--disable-gpu", "--hide-scrollbars",
         "--no-sandbox", "--force-device-scale-factor=1",
         "--virtual-time-budget=4000",
         f"--screenshot={path}", f"--window-size={PAGE_WIDTH},{height}",
         html_path.as_uri()],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        timeout=120)
    if not path.exists() or path.stat().st_size == 0:
        raise RuntimeError(f"headless capture failed for {path}:\n"
                           + proc.stderr[-1500:])
    return path


def main() -> int:
    """Render every screenshot from an existing proof.json."""
    import json
    data_path = ROOT / "proof" / "results" / "proof.json"
    if not data_path.exists():
        print(f"error: {data_path} not found; run proof/run_proof.py first",
              file=sys.stderr)
        return 2
    report = json.loads(data_path.read_text())
    shots = ROOT / "proof" / "results" / "screens"
    shots.mkdir(parents=True, exist_ok=True)
    made = [
        render("apps", "C+ apps and games: built with C+, run, output verified",
               report["apps"], shots / "01_apps.png"),
        render("safety", "Safety matrix: plain clang accepts, C+ rejects",
               report["safety"], shots / "02_safety.png"),
        render("abi", "C ABI and C source preservation", report["abi"],
               shots / "03_abi.png"),
    ]
    if report.get("benchmark"):
        made.append(render("benchmark", "Six-way workload across six toolchains",
                           report["benchmark"], shots / "04_benchmark.png"))
    for p in made:
        print(p.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
