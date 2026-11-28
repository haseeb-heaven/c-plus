#!/usr/bin/env python3
"""Compare C+ and Rust builds on paired benchmark programs.

For every ``pairs/<name>.cp`` / ``pairs/<name>.rs`` pair the runner:

* compiles C+ twice   -- ``--release --sanitize=none`` and
                         ``--debug --sanitize=none``
* compiles Rust twice -- ``rustc -O`` and ``rustc -C opt-level=0`` (the
                         equivalent of ``-O0``; ``rustc`` has no ``-O0`` flag)
* runs every binary with a fixed iteration count baked into the sources and a
  hard per-run timeout, taking the best wall time of N samples and the peak
  RSS reported by ``/usr/bin/time -l``
* asserts all four builds print byte-for-byte identical stdout
* writes ``results/summary.txt`` and ``results/summary.json`` in this directory

The C+ compiler comes from the ``CPLUS_BIN`` environment variable; ``rustc`` is
resolved from PATH.  No existing source, build output, or game asset is touched.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
PAIRS_DIR = HERE / "pairs"
RESULTS_DIR = HERE / "results"

# Tunable via CLI flags; these defaults keep a full run under a few minutes.
DEFAULT_SAMPLES = 5
DEFAULT_WARMUPS = 1
DEFAULT_TIMEOUT = 30.0

TIME_BINARY = "/usr/bin/time"
if not Path(TIME_BINARY).is_file():
    TIME_BINARY = shutil.which("time") or TIME_BINARY

RSS_RE = re.compile(r"^\s*(\d+)\s+maximum resident set size\s*$", re.MULTILINE)


# --------------------------------------------------------------------------- #
# Build variants
# --------------------------------------------------------------------------- #

class Variant:
    def __init__(self, key: str, language: str, label: str, command: list[str]):
        self.key = key
        self.language = language
        self.label = label
        self.command = command


def cplus_variants(cplus_bin: str, source: Path, output: Path) -> list[Variant]:
    def cmd(mode: str) -> list[str]:
        # ``build`` subcommand + explicit flags: --release/-O2 or --debug/-O0.
        return [
            cplus_bin, "build", str(source),
            mode, "--sanitize=none", "-o", str(output),
        ]

    return [
        Variant("cplus_release", "C+", "c+ --release --sanitize=none",
                cmd("--release")),
        Variant("cplus_debug", "C+", "c+ --debug --sanitize=none",
                cmd("--debug")),
    ]


def rust_variants(rustc: str, source: Path, output: Path) -> list[Variant]:
    return [
        Variant("rust_opt", "Rust", "rustc -O",
                [rustc, "-O", str(source), "-o", str(output)]),
        Variant("rust_noopt", "Rust", "rustc -C opt-level=0",
                [rustc, "-C", "opt-level=0", str(source), "-o", str(output)]),
    ]


# --------------------------------------------------------------------------- #
# Compilation / execution helpers
# --------------------------------------------------------------------------- #

def run_command(command: list[str], timeout: float, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )


def compile_variant(variant: Variant, timeout: float, cwd: Path) -> dict[str, Any]:
    started = time.perf_counter()
    try:
        proc = run_command(variant.command, timeout, cwd)
    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "error": f"compile timed out after {timeout:g}s",
            "command": shlex.join(variant.command),
        }
    except OSError as exc:
        return {
            "ok": False,
            "error": f"compile failed to start: {exc}",
            "command": shlex.join(variant.command),
        }
    elapsed = time.perf_counter() - started
    if proc.returncode != 0:
        return {
            "ok": False,
            "error": f"compile exited {proc.returncode}",
            "command": shlex.join(variant.command),
            "compile_seconds": round(elapsed, 6),
            "stderr": proc.stderr.strip()[-2000:],
        }
    return {
        "ok": True,
        "command": shlex.join(variant.command),
        "compile_seconds": round(elapsed, 6),
    }


def time_one(binary: Path, timeout: float, cwd: Path) -> dict[str, Any]:
    """Run one binary under /usr/bin/time -l; return stdout, wall time, RSS."""
    command = [TIME_BINARY, "-l", str(binary)]
    started = time.perf_counter()
    try:
        proc = run_command(command, timeout, cwd)
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": f"run timed out after {timeout:g}s"}
    except OSError as exc:
        return {"ok": False, "error": f"run failed to start: {exc}"}
    wall = time.perf_counter() - started

    match = RSS_RE.search(proc.stderr)
    if proc.returncode != 0:
        return {
            "ok": False,
            "error": f"run exited {proc.returncode}",
            "wall_seconds": round(wall, 6),
            "stderr": proc.stderr.strip()[-2000:],
        }
    if not match:
        return {
            "ok": False,
            "error": "could not parse peak RSS from /usr/bin/time -l",
            "wall_seconds": round(wall, 6),
            "stderr": proc.stderr.strip()[-2000:],
        }
    return {
        "ok": True,
        # Strip only a single trailing newline so trailing blank lines remain
        # observable in the cross-build stdout comparison.
        "stdout": proc.stdout[:-1] if proc.stdout.endswith("\n") else proc.stdout,
        "wall_seconds": wall,
        "peak_rss_bytes": int(match.group(1)),
    }


def measure(binary: Path, samples: int, warmups: int, timeout: float,
            cwd: Path) -> dict[str, Any]:
    stdout_values: list[str] = []
    runs: list[dict[str, Any]] = []

    for _ in range(warmups):
        warm = time_one(binary, timeout, cwd)
        if not warm["ok"]:
            return warm

    for _ in range(samples):
        result = time_one(binary, timeout, cwd)
        if not result["ok"]:
            return result
        stdout_values.append(result["stdout"])
        runs.append({
            "wall_seconds": round(result["wall_seconds"], 6),
            "peak_rss_bytes": result["peak_rss_bytes"],
        })

    unique_stdout = set(stdout_values)
    return {
        "ok": True,
        "stdout": stdout_values[0],
        "stdout_stable": len(unique_stdout) == 1,
        "best_wall_seconds": round(min(r["wall_seconds"] for r in runs), 6),
        "peak_rss_bytes": max(r["peak_rss_bytes"] for r in runs),
        "runs": runs,
    }


def ratio(numerator: float | None, denominator: float | None) -> float | None:
    if numerator is None or denominator in (None, 0):
        return None
    return round(numerator / denominator, 4)


# --------------------------------------------------------------------------- #
# Reporting
# --------------------------------------------------------------------------- #

def fmt(value: float | None, digits: int = 4) -> str:
    return "n/a" if value is None else f"{value:.{digits}f}"


def fmt_ratio(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}x"


def render_text(report: dict[str, Any]) -> str:
    config = report["config"]
    lines: list[str] = []
    add = lines.append

    add("C+ vs Rust paired benchmark summary")
    add("=" * 72)
    add(f"generated (UTC): {report['generated_utc']}")
    add(f"cplus binary:    {config['cplus_bin']}")
    add(f"rustc:           {config['rustc']}")
    add(f"samples/run:     {config['samples']} (warmups: {config['warmups']})")
    add(f"timeout/run:     {config['timeout_seconds']:g}s")
    add("")
    add(f"pairs discovered: {report['counts']['pairs']}")
    add(f"builds attempted: {report['counts']['builds_attempted']}")
    add(f"builds succeeded: {report['counts']['builds_succeeded']}")
    add(f"runs measured:    {report['counts']['runs_measured']}")
    add(f"mismatches:       {report['counts']['mismatches']}")
    add(f"failures:         {report['counts']['failures']}")
    add("")

    for program in report["programs"]:
        add("-" * 72)
        add(f"program: {program['name']}")
        add(f"  stdout: {program['stdout']!r}")
        add(f"  stdout identical across all measured builds: "
            f"{program['stdout_match']}")
        add("")
        add(f"  {'build':<22} {'best wall (s)':>14} {'peak RSS (KiB)':>15}")
        for result in program["results"]:
            if result["ok"]:
                add(f"  {result['label']:<22} "
                    f"{fmt(result['best_wall_seconds']):>14} "
                    f"{result['peak_rss_bytes'] / 1024:>15.1f}")
            else:
                add(f"  {result['label']:<22} {'FAILED':>14}  "
                    f"{result.get('error', 'unknown error')}")

        ratios = program.get("ratios", {})
        add("")
        add("  time ratios (best wall):")
        add(f"    c+ release / rust -O        : {fmt_ratio(ratios.get('time_cplus_release_over_rust_opt'))}")
        add(f"    c+ debug  / rust -O         : {fmt_ratio(ratios.get('time_cplus_debug_over_rust_opt'))}")
        add(f"    c+ release / c+ debug       : {fmt_ratio(ratios.get('time_cplus_release_over_cplus_debug'))}")
        add(f"    rust -O   / rust no-opt     : {fmt_ratio(ratios.get('time_rust_opt_over_rust_noopt'))}")
        add("  peak RSS ratios:")
        add(f"    c+ release / rust -O        : {fmt_ratio(ratios.get('rss_cplus_release_over_rust_opt'))}")
        add(f"    c+ debug  / rust -O         : {fmt_ratio(ratios.get('rss_cplus_debug_over_rust_opt'))}")
        add(f"    c+ release / c+ debug       : {fmt_ratio(ratios.get('rss_cplus_release_over_cplus_debug'))}")
        add(f"    rust -O   / rust no-opt     : {fmt_ratio(ratios.get('rss_rust_opt_over_rust_noopt'))}")

        if program["failures"]:
            add("  failures:")
            for failure in program["failures"]:
                add(f"    - {failure['label']}: {failure['error']}")
        if program["mismatches"]:
            add("  stdout mismatches:")
            for label, mismatch in program["mismatches"].items():
                add(f"    - {label}: {mismatch!r}")
        add("")

    add("-" * 72)
    add("ratios > 1 mean the numerator (first build) used more of the metric.")
    add("Wall time is the best of N high-resolution samples; peak RSS comes from")
    add("`/usr/bin/time -l` on the same sample set. Rust `-O0` is expressed as")
    add("`-C opt-level=0`, the flag rustc accepts for no optimization.")
    return "\n".join(lines) + "\n"


def write_results(report: dict[str, Any]) -> tuple[Path, Path]:
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    json_path = RESULTS_DIR / "summary.json"
    txt_path = RESULTS_DIR / "summary.txt"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=False) + "\n",
                         encoding="utf-8")
    txt_path.write_text(render_text(report), encoding="utf-8")
    return txt_path, json_path


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def discover_pairs() -> list[tuple[str, Path, Path]]:
    pairs: list[tuple[str, Path, Path]] = []
    for cplus_source in sorted(PAIRS_DIR.glob("*.cp")):
        rust_source = cplus_source.with_suffix(".rs")
        if rust_source.is_file():
            pairs.append((cplus_source.stem, cplus_source, rust_source))
    return pairs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cplus-bin", default=os.environ.get("CPLUS_BIN"),
                        help="C+ compiler binary (default: $CPLUS_BIN)")
    parser.add_argument("--rustc", default=shutil.which("rustc") or "rustc",
                        help="Rust compiler from PATH (default: rustc)")
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES,
                        help=f"timed runs per build (default: {DEFAULT_SAMPLES})")
    parser.add_argument("--warmups", type=int, default=DEFAULT_WARMUPS,
                        help=f"untimed runs per build (default: {DEFAULT_WARMUPS})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help=f"hard timeout per compile/run in seconds "
                             f"(default: {DEFAULT_TIMEOUT:g})")
    parser.add_argument("--filter", default=None,
                        help="only run pairs whose name contains this string")
    args = parser.parse_args(argv)

    if not args.cplus_bin:
        parser.error("CPLUS_BIN is unset and --cplus-bin was not provided")
    if not Path(args.cplus_bin).is_file():
        parser.error(f"C+ binary not found: {args.cplus_bin}")
    if shutil.which(args.rustc) is None and not Path(args.rustc).is_file():
        parser.error(f"rustc not found: {args.rustc}")
    if args.samples < 1:
        parser.error("--samples must be at least 1")
    if args.warmups < 0:
        parser.error("--warmups cannot be negative")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    if not PAIRS_DIR.is_dir():
        parser.error(f"no pairs directory at {PAIRS_DIR}")

    pairs = discover_pairs()
    if args.filter:
        pairs = [pair for pair in pairs if args.filter in pair[0]]
    if not pairs:
        parser.error("no paired programs found")

    report: dict[str, Any] = {
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "config": {
            "cplus_bin": str(Path(args.cplus_bin).resolve()),
            "rustc": args.rustc,
            "samples": args.samples,
            "warmups": args.warmups,
            "timeout_seconds": args.timeout,
            "time_binary": TIME_BINARY,
        },
        "counts": {
            "pairs": len(pairs),
            "builds_attempted": 0,
            "builds_succeeded": 0,
            "runs_measured": 0,
            "mismatches": 0,
            "failures": 0,
        },
        "programs": [],
    }

    with tempfile.TemporaryDirectory(prefix="cplus_vs_rust_") as temp_name:
        workdir = Path(temp_name)
        for name, cplus_source, rust_source in pairs:
            program: dict[str, Any] = {
                "name": name,
                "sources": {
                    "cplus": str(cplus_source.relative_to(HERE)),
                    "rust": str(rust_source.relative_to(HERE)),
                },
                "results": [],
                "ratios": {},
                "stdout": None,
                "stdout_match": False,
                "failures": [],
                "mismatches": {},
            }
            case_dir = workdir / name
            case_dir.mkdir()

            # Four variants: c+ --release, c+ --debug, rustc -O, rustc no-opt.
            variants = [
                cplus_variants(args.cplus_bin, cplus_source,
                               case_dir / "cplus_release")[0],
                cplus_variants(args.cplus_bin, cplus_source,
                               case_dir / "cplus_debug")[1],
                rust_variants(args.rustc, rust_source,
                              case_dir / "rust_opt")[0],
                rust_variants(args.rustc, rust_source,
                              case_dir / "rust_noopt")[1],
            ]

            print(f"[{name}] compiling 4 builds ...", flush=True)
            compiled: dict[str, Path] = {}
            for variant in variants:
                report["counts"]["builds_attempted"] += 1
                build = compile_variant(variant, args.timeout, workdir)
                if not build["ok"]:
                    report["counts"]["failures"] += 1
                    program["failures"].append({
                        "label": variant.label,
                        "key": variant.key,
                        "error": build["error"],
                        "stderr": build.get("stderr", ""),
                    })
                    program["results"].append({
                        "key": variant.key,
                        "label": variant.label,
                        "language": variant.language,
                        "ok": False,
                        "error": build["error"],
                    })
                    continue
                report["counts"]["builds_succeeded"] += 1
                binary = case_dir / variant.key
                compiled[variant.key] = binary
                program["results"].append({
                    "key": variant.key,
                    "label": variant.label,
                    "language": variant.language,
                    "ok": True,
                    "compile_seconds": build["compile_seconds"],
                })

            measurement_by_key: dict[str, dict[str, Any]] = {}
            for variant in variants:
                if variant.key not in compiled:
                    continue
                binary = compiled[variant.key]
                print(f"[{name}] measuring {variant.label} ...", flush=True)
                measured = measure(binary, args.samples, args.warmups,
                                   args.timeout, workdir)
                measurement_by_key[variant.key] = measured

                result = next(r for r in program["results"]
                              if r["key"] == variant.key)
                if not measured["ok"]:
                    report["counts"]["failures"] += 1
                    result["ok"] = False
                    result["error"] = measured["error"]
                    program["failures"].append({
                        "label": variant.label,
                        "key": variant.key,
                        "error": measured["error"],
                        "stderr": measured.get("stderr", ""),
                    })
                    continue

                report["counts"]["runs_measured"] += 1
                result.update({
                    "ok": True,
                    "stdout": measured["stdout"],
                    "stdout_stable": measured["stdout_stable"],
                    "best_wall_seconds": measured["best_wall_seconds"],
                    "peak_rss_bytes": measured["peak_rss_bytes"],
                    "runs": measured["runs"],
                })
                if not measured["stdout_stable"]:
                    report["counts"]["mismatches"] += 1
                    program["mismatches"][f"{variant.key} (within build)"] = \
                        measured["stdout"]

            # Assert identical stdout across all builds that ran successfully.
            outputs = {
                key: value["stdout"]
                for key, value in measurement_by_key.items()
                if value["ok"]
            }
            if outputs:
                canonical_key = next(iter(outputs))
                canonical = outputs[canonical_key]
                program["stdout"] = canonical
                program["stdout_match"] = all(
                    value == canonical for value in outputs.values()
                )
                for key, value in outputs.items():
                    if value != canonical:
                        report["counts"]["mismatches"] += 1
                        program["mismatches"][key] = value
            if not program["stdout_match"] and not program["mismatches"]:
                # Fewer than two builds succeeded; nothing to compare against.
                program["mismatches"]["<insufficient builds>"] = None

            def wall(key: str) -> float | None:
                value = measurement_by_key.get(key)
                return value["best_wall_seconds"] if value and value["ok"] else None

            def peak(key: str) -> int | None:
                value = measurement_by_key.get(key)
                return value["peak_rss_bytes"] if value and value["ok"] else None

            program["ratios"] = {
                "time_cplus_release_over_rust_opt": ratio(
                    wall("cplus_release"), wall("rust_opt")),
                "time_cplus_debug_over_rust_opt": ratio(
                    wall("cplus_debug"), wall("rust_opt")),
                "time_cplus_release_over_cplus_debug": ratio(
                    wall("cplus_release"), wall("cplus_debug")),
                "time_rust_opt_over_rust_noopt": ratio(
                    wall("rust_opt"), wall("rust_noopt")),
                "rss_cplus_release_over_rust_opt": ratio(
                    peak("cplus_release"), peak("rust_opt")),
                "rss_cplus_debug_over_rust_opt": ratio(
                    peak("cplus_debug"), peak("rust_opt")),
                "rss_cplus_release_over_cplus_debug": ratio(
                    peak("cplus_release"), peak("cplus_debug")),
                "rss_rust_opt_over_rust_noopt": ratio(
                    peak("rust_opt"), peak("rust_noopt")),
            }
            report["programs"].append(program)

    txt_path, json_path = write_results(report)

    # Validate the JSON we just wrote is well-formed and has the expected keys.
    with json_path.open(encoding="utf-8") as handle:
        loaded = json.load(handle)
    assert loaded["counts"]["pairs"] == len(pairs)
    assert len(loaded["programs"]) == len(pairs)

    print()
    print(f"wrote {txt_path}")
    print(f"wrote {json_path}")
    print()
    print(render_text(report))

    return 1 if report["counts"]["failures"] or report["counts"]["mismatches"] else 0


if __name__ == "__main__":
    sys.exit(main())
