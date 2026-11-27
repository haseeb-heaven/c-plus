#!/usr/bin/env python3
"""Six-way long workload: clang C, C+, Fil-C, Trap-C, Goose and Rust.

Every available implementation runs the same integer workload from
`portable.*` with the same scale, so their three phase checksums must match.
Trap-C has no published toolchain and is recorded as NOT_TESTED, never as a
fabricated number. Fil-C is Linux-only, so it is compiled and run in a
temporary Linux aarch64 container with the repository mounted read-only.

Usage:
    python3 benchmarks/six_way/run.py
    CPLUS_SIX_SCALE=10 python3 benchmarks/six_way/run.py
"""

from __future__ import annotations

import datetime
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = HERE / "results"
OUT.mkdir(parents=True, exist_ok=True)

SCALE = int(os.environ.get("CPLUS_SIX_SCALE", "20"))
RUNS = int(os.environ.get("CPLUS_SIX_RUNS", "1"))
TIMEOUT = int(os.environ.get("CPLUS_SIX_TIMEOUT", "2400"))
CPLUS = Path(os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+")))
CLANG = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")
RUSTC = os.environ.get("CPLUS_RUSTC", "rustc")
GOOSE = os.environ.get("CPLUS_GOOSE", "goose")
FILC_RELEASE = ("https://github.com/pizlonator/fil-c/releases/download/"
                "v0.687/filc-0.687-linux-aarch64.tar.xz")
FILC_CONTAINER = "cplus-six-way-filc"

PHASES = ("stream", "hash", "arena")


def command(args: list[str], env: dict[str, str] | None = None,
            cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, env=env, cwd=cwd, timeout=TIMEOUT)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed: {args[0]} (rc={proc.returncode})\n"
                           + proc.stderr[-1200:])
    return proc


def parse_checksums(text: str) -> dict[str, int]:
    """Read checksums from both single-line (C/Rust) and split-line (Goose)
    forms. Timing numbers that follow a checksum on the same line are ignored.
    """
    result: dict[str, int] = {}
    pending: str | None = None
    totals: list[int] = []
    in_totals = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        fields = line.split()
        if fields[0] == "PHASE" and len(fields) >= 2:
            name = fields[1]
            if len(fields) >= 3 and fields[2].isdigit():
                result[name] = int(fields[2])
                pending = None
            else:
                pending = name
        elif fields[0] == "TOTAL":
            totals.extend(int(x) for x in fields[1:] if x.isdigit())
            pending = None
            in_totals = not totals
        elif in_totals and line.isdigit():
            totals.append(int(line))
        elif pending and line.isdigit():
            result[pending] = int(line)
            pending = None
    if set(result) != set(PHASES) or len(totals) < len(PHASES):
        raise ValueError("workload output missing checksums: " + repr(text[-400:]))
    # Cross-check the TOTAL line against the named phase checksums.
    if totals[:len(PHASES)] != [result[p] for p in PHASES]:
        raise ValueError("TOTAL checksums disagree with phase checksums")
    return result


def prepare_filc() -> None:
    if not shutil.which("docker"):
        raise RuntimeError("docker unavailable; Fil-C cannot be built here")
    command(["docker", "info"], )
    command(["docker", "rm", "-f", FILC_CONTAINER])
    command(["docker", "run", "-d", "--name", FILC_CONTAINER,
             "-v", f"{ROOT}:/repo", "ubuntu:24.04", "sleep", "infinity"])
    command(["docker", "exec", FILC_CONTAINER, "mkdir", "-p", "/work"])
    archive = OUT / "filc.tar.xz"
    if not archive.exists():
        command(["curl", "-sSL", "-o", str(archive), FILC_RELEASE])
    command(["docker", "cp", str(archive), FILC_CONTAINER + ":/work/filc.tar.xz"])
    command(["docker", "exec", FILC_CONTAINER, "bash", "-lc",
             "apt-get update -qq >/dev/null 2>&1 && "
             "apt-get install -y -qq patchelf xz-utils binutils >/dev/null 2>&1 "
             "&& cd /work && tar -xJf filc.tar.xz && "
             "mv filc-0.687-linux-aarch64 filc && cd filc && ./setup.sh >/dev/null 2>&1"])


def one_run(name: str, argv: list[str], env: dict[str, str],
            parse: bool = True) -> dict:
    start = time.perf_counter()
    proc = command(argv, env)
    elapsed = time.perf_counter() - start
    row = {"seconds": elapsed}
    if parse:
        row["checksums"] = parse_checksums(proc.stdout)
    return row


def main() -> int:
    if SCALE < 1 or RUNS < 1:
        print("error: scale and runs must be positive", file=sys.stderr)
        return 2
    base_env = dict(os.environ, CPLUS_SIX_SCALE=str(SCALE))
    results: dict[str, dict] = {}

    clang_exe = OUT / "six_clang"
    command([CLANG, "-std=gnu17", "-O2", "-I" + str(ROOT / "runtime/include"),
             "-include", "cplus/cplus.h", str(HERE / "portable.c"),
             "-o", str(clang_exe), "-lpthread", "-lm"])
    results["clang"] = {"label": "clang -O2 (pure C)",
                        "runs": [one_run("clang", [str(clang_exe)], base_env)
                                 for _ in range(RUNS)]}

    plus_exe = OUT / "six_cplus"
    command([str(CPLUS), "build", "--release", str(HERE / "portable.c"),
             "-o", str(plus_exe)])
    results["cplus"] = {"label": "C+ --release (safer C)",
                        "runs": [one_run("cplus", [str(plus_exe)], base_env)
                                 for _ in range(RUNS)]}

    try:
        prepare_filc()
        script = ("/work/filc/build/bin/clang -O2 -I/repo/runtime/include "
                  "-include cplus/cplus.h /repo/benchmarks/six_way/portable.c "
                  "-o /tmp/six_filc && /tmp/six_filc")
        filc_env = dict(base_env)
        filc_args = ["docker", "exec", "-e", f"CPLUS_SIX_SCALE={SCALE}",
                     FILC_CONTAINER, "bash", "-lc", script]
        results["filc"] = {"label": "Fil-C 0.687 (Linux aarch64 container)",
                           "runs": [one_run("filc", filc_args, filc_env)
                                    for _ in range(RUNS)]}
    except Exception as exc:
        results["filc"] = {"label": "Fil-C", "status": "NOT_TESTED",
                           "reason": str(exc)[:300]}
    finally:
        subprocess.run(["docker", "rm", "-f", FILC_CONTAINER],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    results["trapc"] = {
        "label": "Trap-C", "status": "NOT_TESTED",
        "reason": "No published compiler, source or license exists; "
                  "trapc/itrapc binaries are unavailable.",
    }

    goose_src = OUT / f"portable_scale_{SCALE}.goose"
    goose_src.write_text((HERE / "portable.goose").read_text()
                         .replace("var scale: i64 = 100;",
                                  f"var scale: i64 = {SCALE};"))
    results["goose"] = {"label": "Goose Lang",
                        "runs": [one_run("goose",
                                         [GOOSE, "run", str(goose_src)],
                                         base_env) for _ in range(RUNS)]}

    rust_exe = OUT / "six_rust"
    command([RUSTC, "-O", "-o", str(rust_exe), str(HERE / "portable.rs")])
    results["rust"] = {"label": "Rust -O",
                       "runs": [one_run("rust", [str(rust_exe)], base_env)
                                for _ in range(RUNS)]}

    reference = None
    for name in ("clang", "cplus", "filc", "goose", "rust"):
        entry = results[name]
        if "runs" not in entry:
            continue
        for index, run_row in enumerate(entry["runs"]):
            checks = run_row["checksums"]
            if reference is None:
                reference = checks
            elif checks != reference:
                print(f"error: checksum mismatch for {name} run {index + 1}",
                      file=sys.stderr)
                return 1

    report = {
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scale": SCALE,
        "runs": RUNS,
        "phase_checksums": reference,
        "results": results,
    }
    (OUT / "six_way.json").write_text(json.dumps(report, indent=2) + "\n")

    print(f"six-way workload scale={SCALE} runs={RUNS}")
    print(f"{'toolchain':<28} {'seconds':>10} {'vs clang':>10} status")
    base_time = statistics.median(r["seconds"] for r in results["clang"]["runs"])
    for name in ("clang", "cplus", "filc", "trapc", "goose", "rust"):
        entry = results[name]
        if "runs" not in entry:
            print(f"{entry['label']:<28} {'':>10} {'':>10} "
                  f"NOT_TESTED ({entry['reason'][:40]})")
            continue
        secs = statistics.median(r["seconds"] for r in entry["runs"])
        print(f"{entry['label']:<28} {secs:>10.1f} {secs / base_time:>9.2f}x "
              "PASS")
    print(f"checksums identical across all available toolchains; details: "
          f"{OUT / 'six_way.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
