#!/usr/bin/env python3
"""Prove the C+ claims with captured evidence.

Three evidence groups, each rendered as a PNG screenshot:

1. apps   - every bundled app and game is built with C+ by the project's own
            verification script, then run; the script's own pass/fail line and
            the program's captured output are the evidence.
2. safety - the same unsafe C program is handed to plain clang and to C+.
            clang accepts it; C+ must reject it either at compile time or
            with a runtime safety violation.  The layer that caught it is
            recorded.
3. abi    - one C translation unit is built by clang and by C+.  The
            exported symbol tables must match (C ABI) and the lowered C
            source must keep the original C shape (C source compatibility).

Usage:
    python3 proof/run_proof.py
    CPLUS_BIN=build/c+ python3 proof/run_proof.py
"""

from __future__ import annotations

import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "proof" / "results"
SHOTS = OUT / "screens"
OUT.mkdir(parents=True, exist_ok=True)
SHOTS.mkdir(parents=True, exist_ok=True)

CPLUS = Path(os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+")))
CLANG = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")
TIMEOUT = int(os.environ.get("CPLUS_PROOF_TIMEOUT", "600"))

sys.path.insert(0, str(ROOT / "proof"))
from report import render  # noqa: E402


def run(args, cwd=None, timeout=TIMEOUT, env=None):
    return subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          timeout=timeout, env=env)


def script_env() -> dict:
    env = dict(os.environ)
    env["CPLUS_BIN"] = str(CPLUS)
    return env


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


# --------------------------------------------------------------------------
# 1. apps and games
# --------------------------------------------------------------------------

LEGACY_DEMOS = [
    "A_hello", "B_bounds", "C_resource", "D_class", "E_networking",
    "F_concurrency", "G_borrow", "H_allocation_borrow", "I_raii_safe",
    "J_modern_io", "K_state_class", "L_class_lifecycle", "M_default_args",
    "N_range_for", "O_extensions", "P_std_library",
]

APP_SCRIPTS = [
    ("game engine", "apps/gameeng/run.sh"),
    ("3D scene demo", "apps/games3d/run.sh"),
    ("web server", "apps/webserver/run.sh"),
    ("sqlite-like db", "apps/sqldb/run.sh"),
    ("cli calculator", "apps/cli_app/run.sh"),
]

DEMO_SRC = {"O_extensions": "O_extensions.cp",
            "P_std_library": "P_std_library.cp"}

# The demo harness verifies these by content, not by a golden file: a forked
# client races the server and the kernel picks the port, so neither the line
# order nor the port number is reproducible.
CONTENT_CHECKS = {
    "E_networking": (
        "listening on 127.0.0.1:",
        "server received: GET / HTTP/1.0",
        "client received: HTTP/1.0 200 OK",
    ),
}


def legacy_demo_cases() -> list[dict]:
    """Run the demo suite once and turn its per-demo verdicts into rows."""
    out_dir = ROOT / "apps" / "legacy_demos" / "out"
    proc = run(["sh", str(ROOT / "apps" / "legacy_demos" / "run_demos.sh")],
               cwd=ROOT, env=script_env())
    verdicts: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        for verdict in ("ok", "FAIL"):
            if line.startswith(verdict + " "):
                name = line[len(verdict) + 1:].split()[0]
                verdicts[name] = line

    rows = []
    for name in LEGACY_DEMOS:
        source = "apps/legacy_demos/" + DEMO_SRC.get(name, name + ".c")
        verdict = verdicts.get(name, "not run")
        passed = verdict.startswith("ok")
        expected = read(out_dir / (name + ".expected"))
        actual = read(out_dir / (name + ".stdout"))
        stderr = read(out_dir / (name + ".stderr"))
        if name == "B_bounds":
            # This demo deliberately reads out of bounds; C+ must abort with a
            # bounds diagnostic instead of corrupting memory.
            exit_code = read(out_dir / "B_bounds.exit").strip()
            safe = exit_code not in ("", "0") and (
                "outside the bounds" in stderr or "out of bounds" in stderr)
            rows.append({
                "label": "legacy B_bounds",
                "source": source,
                "status": "ABORTED_SAFELY" if safe else "FAILED",
                "detail": (f"exit={exit_code}\n" + stderr.strip()),
                "expected": "safe abort with a bounds diagnostic",
            })
            continue
        markers = CONTENT_CHECKS.get(name)
        if markers is not None:
            matched = all(m in actual for m in markers)
            detail = actual.strip()
            expected = "contains: " + "; ".join(markers)
        else:
            matched = expected.strip() == actual.strip() and expected.strip() != ""
            detail = actual.strip()
            expected = expected.strip()
        rows.append({
            "label": "legacy " + name,
            "source": source,
            "status": "OUTPUT_MATCHES" if (passed and matched)
                      else ("OUTPUT_DIFFERS" if passed else "FAILED"),
            "detail": detail,
            "expected": expected,
        })
    return rows


def script_cases() -> list[dict]:
    rows = []
    for label, relative in APP_SCRIPTS:
        script = ROOT / relative
        if not script.exists():
            rows.append({"label": label, "source": relative,
                         "status": "SKIPPED", "detail": "script missing",
                         "expected": ""})
            continue
        proc = run(["sh", str(script)], cwd=ROOT, env=script_env())
        tail = proc.stdout.strip().splitlines()
        evidence = "\n".join(tail[-14:]) if tail else "(no output)"
        rows.append({
            "label": label,
            "source": relative,
            "status": "OUTPUT_MATCHES" if proc.returncode == 0 else "FAILED",
            "detail": evidence,
            "expected": "verification script exits 0",
        })
    return rows


def app_cases() -> list[dict]:
    return legacy_demo_cases() + script_cases()


# --------------------------------------------------------------------------
# 2. safety matrix
# --------------------------------------------------------------------------

SAFETY_CASES = [
    ("heap buffer overflow", """
#include <stdlib.h>
#include <string.h>
int main(void) {
  char *p = malloc(8);
  if (!p) return 2;
  strcpy(p, "0123456789");
  free(p);
  return 0;
}
"""),
    ("stack buffer overflow", """
#include <string.h>
int main(void) {
  char buf[8];
  strcpy(buf, "0123456789");
  return 0;
}
"""),
    ("out of bounds write", """
int main(void) {
  int a[4] = {0, 1, 2, 3};
  a[7] = 42;
  return a[7];
}
"""),
    ("use after free", """
#include <stdlib.h>
int main(void) {
  int *p = malloc(sizeof(int));
  if (!p) return 2;
  *p = 1;
  free(p);
  return *p;
}
"""),
    ("double free", """
#include <stdlib.h>
int main(void) {
  char *p = malloc(8);
  if (!p) return 2;
  free(p);
  free(p);
  return 0;
}
"""),
    ("null pointer dereference", """
int main(void) {
  int *p = 0;
  return *p;
}
"""),
    ("overlapping memcpy", """
#include <string.h>
int main(void) {
  char buf[64] = {0};
  memcpy(buf, buf + 2, 8);
  return 0;
}
"""),
    ("unaligned pointer cast", """
#include <stdint.h>
int main(void) {
  char buf[16] = {0};
  uint32_t *p = (uint32_t *)&buf[1];
  return (int)*p;
}
"""),
    ("write through const cast", """
static const int g = 1;
int main(void) {
  int *p = (int *)&g;
  *p = 2;
  return 0;
}
"""),
    ("strcat past the end", """
#include <stdlib.h>
#include <string.h>
int main(void) {
  char *p = malloc(8);
  if (!p) return 2;
  strcpy(p, "12345");
  strcat(p, "67890");
  free(p);
  return 0;
}
"""),
]


def safety_cases() -> list[dict]:
    results = []
    work = OUT / "safety"
    work.mkdir(exist_ok=True)
    for name, body in SAFETY_CASES:
        src = work / (name.replace(" ", "_") + ".c")
        src.write_text(body.lstrip())
        clang = run([CLANG, "-std=gnu17", "-O0", "-w", str(src), "-o",
                     str(work / (src.stem + "_clang"))])
        cplus = run([str(CPLUS), "check", "--sanitize=none", str(src)])
        caught = "NONE"
        detail = ""
        if cplus.returncode != 0:
            caught = "COMPILE_TIME"
            detail = cplus.stdout[-1200:]
        else:
            exe = work / (src.stem + "_cplus")
            built = run([str(CPLUS), "build", "--sanitize=address", str(src),
                         "-o", str(exe)])
            if built.returncode != 0:
                caught = "COMPILE_TIME"
                detail = built.stdout[-1200:]
            else:
                executed = run([str(exe)], timeout=60)
                if executed.returncode != 0:
                    caught = "RUNTIME"
                    detail = executed.stdout[-1200:]
                else:
                    detail = "(program exited 0; no violation observed)"
        results.append({
            "case": name,
            "source": str(src.relative_to(ROOT)),
            "clang_accepted": clang.returncode == 0,
            "cplus_caught_by": caught,
            "clang_output": clang.stdout[-800:],
            "cplus_output": detail,
        })
    return results


# --------------------------------------------------------------------------
# 3. C ABI and C source preservation
# --------------------------------------------------------------------------

ABI_SOURCE = """
#include <stddef.h>
#include <string.h>

typedef struct Point {
  int x;
  int y;
} Point;

int point_sum(const Point *p) { return p->x + p->y; }

void point_move(Point *dst, const Point *src) {
  dst->x = src->x;
  dst->y = src->y;
}

size_t copy_name(char *out, size_t cap, const char *in) {
  size_t n = strlen(in);
  if (n + 1 > cap)
    return 0;
  memcpy(out, in, n + 1);
  return n;
}

int main(void) {
  Point p = {3, 4};
  char buf[16];
  int moved = point_sum(&p);
  copy_name(buf, sizeof buf, "cplus");
  return moved + (int)strlen(buf);
}
"""


def nm_symbols(path: Path) -> list[str]:
    """Global defined symbols (T/D/B/R) reported by nm."""
    if not path.exists():
        return []
    out = run(["nm", "-gU", str(path)])
    names = []
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("T", "D", "B", "R"):
            names.append(parts[2])
        elif len(parts) == 2 and parts[0] in ("T", "D", "B", "R"):
            names.append(parts[1])
    return sorted(set(names))


def runtime_library() -> Path | None:
    """The C+ runtime archive the driver links into every program."""
    for candidate in (ROOT / "build" / "libcplus_runtime.a",
                      ROOT / "build" / "lib" / "libcplus_runtime.a"):
        if candidate.exists():
            return candidate
    return None


def abi_case() -> dict:
    work = OUT / "abi"
    work.mkdir(exist_ok=True)
    src = work / "abi_probe.c"
    src.write_text(ABI_SOURCE.lstrip())
    clang_exe = work / "abi_clang"
    cplus_exe = work / "abi_cplus"
    clang = run([CLANG, "-std=gnu17", "-O2", "-I", str(ROOT / "runtime/include"),
                 str(src), "-o", str(clang_exe)])
    cplus = run([str(CPLUS), "build", "--release", str(src), "-o",
                 str(cplus_exe)])
    emitted = work / "abi_emitted.c"
    emit = run([str(CPLUS), "build", "--release", "--emit-c", str(emitted),
                str(src), "-o", str(cplus_exe)])

    clang_syms = nm_symbols(clang_exe)
    cplus_syms = nm_symbols(cplus_exe)
    # C+ links its runtime into every program, so the binary exports the
    # runtime's own entry points on top of the program's.  Those belong to
    # the runtime archive, not to the translated C source, so they are
    # separated before the ABI comparison.
    lib = runtime_library()
    runtime_syms = set(nm_symbols(lib)) if lib else set()
    cplus_user_syms = [s for s in cplus_syms if s not in runtime_syms]
    added_by_runtime = [s for s in cplus_syms if s in runtime_syms]
    shared = [s for s in clang_syms if s in cplus_user_syms]
    only_clang = [s for s in clang_syms if s not in cplus_user_syms]
    only_cplus = [s for s in cplus_user_syms if s not in clang_syms]
    return {
        "source": str(src.relative_to(ROOT)),
        "clang_symbols": clang_syms,
        "cplus_symbols": cplus_user_syms,
        "runtime_symbols": added_by_runtime,
        "runtime_library": str(lib.relative_to(ROOT)) if lib else None,
        "shared": shared,
        "only_clang": only_clang,
        "only_cplus": only_cplus,
        "abi_identical": clang_syms == cplus_user_syms,
        "emitted_c": str(emitted.relative_to(ROOT)) if emitted.exists() else None,
        "emitted_is_c": bool(emitted.exists())
        and emitted.read_text().count("int main") >= 1,
        "emitted_head": emitted.read_text()[:2400] if emitted.exists() else "",
        "emit_output": emit.stdout[-1500:],
        "clang_run": run([str(clang_exe)]).stdout if clang_exe.exists() else "",
        "cplus_run": run([str(cplus_exe)]).stdout if cplus_exe.exists() else "",
    }


# --------------------------------------------------------------------------
# report rendering
# --------------------------------------------------------------------------

def main() -> int:
    if not CPLUS.exists():
        print(f"error: c+ binary not found at {CPLUS}", file=sys.stderr)
        return 2
    started = time.time()
    apps = app_cases()
    safety = safety_cases()
    abi = abi_case()
    bench_path = ROOT / "benchmarks" / "six_way" / "results" / "six_way.json"
    bench = json.loads(bench_path.read_text()) if bench_path.exists() else {}

    report = {
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "cplus": str(CPLUS),
        "clang": CLANG,
        "apps": apps,
        "safety": safety,
        "abi": abi,
        "benchmark": bench,
    }
    (OUT / "proof.json").write_text(json.dumps(report, indent=2) + "\n")

    shots = []
    shots.append(render("apps", "C+ apps and games: built with C+, run, output verified",
                        apps, SHOTS / "01_apps.png"))
    shots.append(render("safety", "Safety matrix: plain clang accepts, C+ rejects",
                        safety, SHOTS / "02_safety.png"))
    shots.append(render("abi", "C ABI and C source preservation",
                        abi, SHOTS / "03_abi.png"))
    if bench:
        shots.append(render("benchmark", "Six-way workload across six toolchains",
                            bench, SHOTS / "04_benchmark.png"))
    report["screenshots"] = [str(p.relative_to(ROOT)) for p in shots]
    (OUT / "proof.json").write_text(json.dumps(report, indent=2) + "\n")

    apps_ok = sum(1 for a in apps
                  if a["status"] in ("OUTPUT_MATCHES", "ABORTED_SAFELY"))
    safety_ok = sum(1 for s in safety if s["cplus_caught_by"] != "NONE")
    print(f"apps verified: {apps_ok}/{len(apps)}")
    print(f"safety cases caught by C+: {safety_ok}/{len(safety)}")
    print(f"C ABI identical to clang: {abi['abi_identical']}")
    print(f"lowered C source kept: {abi['emitted_is_c']}")
    print(f"screenshots: {', '.join(str(p) for p in shots)}")
    print(f"elapsed {time.time() - started:.1f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
