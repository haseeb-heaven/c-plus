#!/usr/bin/env python3
"""Native optimizer / safety-mode equivalence suite for C+.

The suite compiles a set of small, well-defined C+ programs with the *native*
compiler binary (``$CPLUS_BIN``, default ``$PWD/build/c+``) under six build
configurations and asserts:

  * debug vs release            (--debug / --release)
  * -O0 vs -O2 via C+ flags     (-O0 / -O2 passed through to the backend)
  * native guards on vs off     (--default --sanitize=none / --legacy-c)

Every configuration passes ``--sanitize=none`` so no backend sanitizer is
involved: the only safety instrumentation is the C+ native guard layer.

For every (case, configuration) pair the runner:

  1. builds with ``--emit-c`` and records the exact command and full output;
  2. runs the produced binary and requires the *exact* declared stdout and
     exit code (no ``|| true``, no bare-nonzero acceptance);
  3. inspects the emitted C: native guard tokens must be present in all five
     Safe-mode configs and absent in the Legacy-C config; ``always`` tokens
     (RAII drop calls) must be present in every config.

A case passes only when all six configs agree byte-for-byte on stdout and exit
code and all emitted-C assertions hold. Timeouts and compiler/binary crashes
are hard failures.

Results are written next to this script under ``results/``:

  results/summary.txt                 counts, exact commands, all differences
  results/summary.json                full machine-readable record
  results/logs/<case>.<cfg>.build.txt build command, rc and full output
  results/logs/<case>.<cfg>.run.txt   run command, rc and full output
  results/emitted/<case>.<cfg>.c      lowered C inspected for guard tokens
  cases/<case>.c                      fixture source (rewritten each run)

Exit status is 0 only when every case matches every expectation.
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import datetime
import json
import os
import pathlib
import shlex
import subprocess
import sys
from collections import Counter

HERE = pathlib.Path(__file__).resolve().parent
CASES_DIR = HERE / "cases"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"
EMITTED = RESULTS / "emitted"
BINS = RESULTS / "bin"

BIN = os.environ.get("CPLUS_BIN", str(pathlib.Path.cwd() / "build" / "c+"))
TIMEOUT = int(os.environ.get("OPT_TIMEOUT", "60"))
WORKERS = int(os.environ.get("OPT_WORKERS", "4"))
RUN_TIMEOUT = 30

# Exact CLI flags per configuration.  --sanitize=none is present everywhere so
# the comparison isolates optimization mode and native-guard presence.
CONFIGS = {
    "debug": ["--debug", "--sanitize=none"],
    "release": ["--release", "--sanitize=none"],
    "o0": ["-O0", "--sanitize=none"],
    "o2": ["-O2", "--sanitize=none"],
    "guards_on": ["--default", "--sanitize=none"],
    "guards_off": ["--legacy-c", "--sanitize=none"],
}
CONFIG_ORDER = list(CONFIGS)
LEGACY_CONFIG = "guards_off"
SAFE_CONFIGS = tuple(c for c in CONFIG_ORDER if c != LEGACY_CONFIG)

CONFIG_PURPOSE = {
    "debug": "-O0 plus debug info (compiler default)",
    "release": "-O2 (native guards must stay on)",
    "o0": "-O0 passed through as a C+ flag",
    "o2": "-O2 passed through as a C+ flag",
    "guards_on": "DEFAULT mode, native guards present",
    "guards_off": "Legacy-C mode, native guards disabled",
}

# Native guard tokens emitted in lowered C by the Safe-mode rewriter/lowering.
ARITH = ("__cplus_checked_",)          # checked arithmetic prelude + call sites
BOUNDS = ("__cplus_bounds_check",)     # dynamic index into a fixed-size array
PTR = ("cplus_check_ptr_formation",)   # pointer formation from a known extent
ALLOC = (                              # allocation registration / release
    "cplus_register_allocation",
    "cplus_free_registered",
    "cplus_realloc_registered",
)


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    family: str
    source: str
    stdout: str
    exit_code: int
    native_guards: tuple[str, ...] = ()
    always_tokens: tuple[str, ...] = ()
    note: str = ""


CASES: list[Case] = [
    # ------------------------------------------------------------------
    # Arithmetic (checked add / sub / mul / div / rem on untrusted input)
    # ------------------------------------------------------------------
    Case(
        "arith_addmul", "arithmetic", """
#include <stdio.h>
int main(int argc, char **argv){
  int a, b, c;
  (void)argv;
  a = argc + 10;
  b = a * 3;
  c = b - a;
  printf("arith=%d,%d,%d\\n", a, b, c);
  return 0;
}
""", "arith=11,33,22\n", 0, ARITH,
        note="argc-derived operands force checked arithmetic guards"),

    Case(
        "arith_divrem", "arithmetic", """
#include <stdio.h>
int main(int argc, char **argv){
  int d, q, r;
  (void)argv;
  d = argc + 6;
  q = 1000 / d;
  r = 1000 % d;
  printf("div=%d,%d\\n", q, r);
  return 0;
}
""", "div=142,6\n", 0, ARITH,
        note="non-zero divisor derived from argc; checked div/rem guards"),

    # ------------------------------------------------------------------
    # Bounds
    # ------------------------------------------------------------------
    Case(
        "bounds_stack", "bounds", """
#include <stdio.h>
int main(int argc, char **argv){
  int a[8];
  int i, idx;
  (void)argv;
  for(i=0;i<8;i++) a[i]=i+1;
  idx = argc + 2;
  printf("bs=%d,%d\\n", idx, a[idx]);
  return 0;
}
""", "bs=3,4\n", 0, ARITH + BOUNDS,
        note="dynamic index into a fixed-size stack array"),

    Case(
        "bounds_heap", "bounds", """
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv){
  int n, idx, i;
  int *h;
  (void)argv;
  n = argc + 4;
  h = malloc(8*sizeof(int));
  if(!h) return 2;
  for(i=0;i<n;i++) h[i]=i*i;
  idx = n - 2;
  printf("bh=%d,%d\\n", idx, h[idx]);
  free(h);
  return 0;
}
""", "bh=3,9\n", 0, ARITH + ALLOC,
        note="heap subscripts use allocation registration rather than a "
             "static __cplus_bounds_check"),

    # ------------------------------------------------------------------
    # Pointer formation
    # ------------------------------------------------------------------
    Case(
        "ptr_form_stack", "pointer_formation", """
#include <stdio.h>
int main(int argc, char **argv){
  int a[6] = {10,20,30,40,50,60};
  int off;
  int *p, *q;
  (void)argv;
  off = argc + 1;
  p = a + off;
  q = p - 1;
  printf("ps=%d,%d\\n", *p, *q);
  return 0;
}
""", "ps=30,20\n", 0, ARITH + PTR,
        note="pointer formation from a statically known array extent"),

    Case(
        "ptr_form_heap", "pointer_formation", """
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv){
  int i, off;
  int *h;
  (void)argv;
  h = malloc(8*sizeof(int));
  if(!h) return 2;
  for(i=0;i<8;i++) h[i]=i+1;
  off = argc + 2;
  { int *p = h + off; printf("ph=%d\\n", *p); }
  free(h);
  return 0;
}
""", "ph=4\n", 0, ARITH + ALLOC,
        note="heap pointer arithmetic; relies on allocation registration, "
             "not a static formation guard"),

    # ------------------------------------------------------------------
    # RAII cleanup (drop calls are language lowering, present in both modes)
    # ------------------------------------------------------------------
    Case(
        "raii_scope", "raii_cleanup", """
#include <stdio.h>
resource struct Tag { int id; };
void Tag_drop(Tag *self){ if(self->id!=0){ printf("drop=%d\\n", self->id); self->id=0; } }
Tag Tag_make(int id){ Tag t; t.id=id; return t; }
int main(void){
  { Tag t = Tag_make(7); (void)t; }
  printf("rs=done\\n");
  return 0;
}
""", "drop=7\nrs=done\n", 0, (), ("Tag_drop",),
        note="scope-exit cleanup must survive every optimization level"),

    Case(
        "raii_early_return", "raii_cleanup", """
#include <stdio.h>
resource struct Tag { int id; };
void Tag_drop(Tag *self){ if(self->id!=0){ printf("drop=%d\\n", self->id); self->id=0; } }
Tag Tag_make(int id){ Tag t; t.id=id; return t; }
static int early(int id){ Tag t = Tag_make(id); if(id==2) return -1; return id; }
int main(void){ printf("re=%d\\n", early(2)); return 0; }
""", "drop=2\nre=-1\n", 0, (), ("Tag_drop",),
        note="early-return cleanup (__cplus_ret) in all configs"),

    # ------------------------------------------------------------------
    # Allocation
    # ------------------------------------------------------------------
    Case(
        "alloc_basic", "allocation", """
#include <stdio.h>
#include <stdlib.h>
int main(void){
  int i;
  int *p;
  p = malloc(4*sizeof(int));
  if(!p) return 2;
  for(i=0;i<4;i++) p[i]=i+1;
  printf("ab=%d,%d\\n", p[0], p[3]);
  free(p);
  printf("ab=clean\\n");
  return 0;
}
""", "ab=1,4\nab=clean\n", 0, ARITH + ALLOC,
        note="malloc/free registration and release guards"),

    Case(
        "alloc_zero_realloc", "allocation", """
#include <stdio.h>
#include <stdlib.h>
int main(void){
  int *p, *q;
  p = calloc(4, sizeof(int));
  if(!p) return 2;
  p[0]=5;
  q = realloc(p, 8*sizeof(int));
  if(!q) return 3;
  q[7]=9;
  printf("az=%d,%d\\n", q[0], q[7]);
  free(q);
  return 0;
}
""", "az=5,9\n", 0, ALLOC,
        note="calloc/realloc/free with realloc registration guard"),

    # ------------------------------------------------------------------
    # Combined case: all five families in one program
    # ------------------------------------------------------------------
    Case(
        "mixed_all", "mixed", """
#include <stdio.h>
#include <stdlib.h>
resource struct Guard { int id; };
void Guard_drop(Guard *self){ if(self->id!=0){ printf("gd=%d\\n", self->id); self->id=0; } }
Guard Guard_make(int id){ Guard g; g.id=id; return g; }
int main(int argc, char **argv){
  int idx, v, x;
  int a[4] = {2,4,6,8};
  int *p, *h, *hp;
  (void)argv;
  idx = argc + 1;
  v = a[idx - 1];
  x = v * 3;
  p = a + (idx - 1);
  h = malloc(4*sizeof(int));
  if(!h) return 2;
  h[2] = x;
  hp = h + 2;
  { Guard g = Guard_make(42); (void)g; }
  printf("mix=%d,%d,%d,%d\\n", v, x, *p, *hp);
  free(h);
  return 0;
}
""", "gd=42\nmix=4,12,4,12\n", 0, ARITH + BOUNDS + ALLOC, ("Guard_drop",),
        note="arithmetic + bounds + pointer formation + allocation + RAII"),
]


def run_cmd(argv, timeout=TIMEOUT):
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr,
                "timed_out": False}
    except subprocess.TimeoutExpired as exc:
        out, err = exc.stdout or b"", exc.stderr or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return {"rc": None, "stdout": out, "stderr": err, "timed_out": True}
    except OSError as exc:
        return {"rc": None, "stdout": "", "stderr": "OS ERROR: %s" % exc,
                "timed_out": False}


def build_and_emit(case, config, src_path):
    emit_path = EMITTED / ("%s.%s.c" % (case.name, config))
    bin_path = BINS / ("%s.%s.bin" % (case.name, config))
    if bin_path.exists():
        bin_path.unlink()
    argv = [BIN, "build", str(src_path), *CONFIGS[config],
            "--emit-c", str(emit_path), "-o", str(bin_path)]
    res = run_cmd(argv)
    (LOGS / ("%s.%s.build.txt" % (case.name, config))).write_text(
        "command: %s\nreturncode: %s\ntimed_out: %s\n\n"
        "--- stdout ---\n%s\n--- stderr ---\n%s\n"
        % (shlex.join(argv), res["rc"], res["timed_out"],
           res["stdout"], res["stderr"]))
    emitted_text = emit_path.read_text() if emit_path.exists() else ""
    return {
        "config": config,
        "flags": CONFIGS[config],
        "argv": argv,
        "command": shlex.join(argv),
        "rc": res["rc"],
        "timed_out": res["timed_out"],
        "stdout": res["stdout"],
        "stderr": res["stderr"],
        "binary": str(bin_path),
        "binary_exists": bin_path.exists(),
        "emitted_c": str(emit_path),
        "emitted_text": emitted_text,
    }


def run_binary(case, config, bin_path):
    argv = [str(bin_path)]
    res = run_cmd(argv, timeout=RUN_TIMEOUT)
    (LOGS / ("%s.%s.run.txt" % (case.name, config))).write_text(
        "command: %s\nreturncode: %s\ntimed_out: %s\n\n"
        "--- stdout ---\n%s\n--- stderr ---\n%s\n"
        % (shlex.join(argv), res["rc"], res["timed_out"],
           res["stdout"], res["stderr"]))
    return {
        "config": config,
        "argv": argv,
        "command": shlex.join(argv),
        "rc": res["rc"],
        "timed_out": res["timed_out"],
        "stdout": res["stdout"],
        "stderr": res["stderr"],
    }


def evaluate_case(case):
    src_path = CASES_DIR / ("%s.c" % case.name)
    src_path.write_text(case.source.lstrip("\n"))

    configs = {}
    reasons = []
    for config in CONFIG_ORDER:
        build = build_and_emit(case, config, src_path)
        run = None
        cfg_reasons = []

        if build["timed_out"]:
            cfg_reasons.append("build TIMEOUT after %ds" % TIMEOUT)
        if build["rc"] != 0:
            cfg_reasons.append("build rc %s != 0" % build["rc"])
        if not build["binary_exists"]:
            cfg_reasons.append("binary not produced")

        if build["rc"] == 0 and build["binary_exists"]:
            run = run_binary(case, config, build["binary"])
            if run["timed_out"]:
                cfg_reasons.append("run TIMEOUT after %ds" % RUN_TIMEOUT)
            if run["rc"] != case.exit_code:
                cfg_reasons.append("exit %s != expected %s"
                                   % (run["rc"], case.exit_code))
            if run["stdout"] != case.stdout:
                cfg_reasons.append("stdout %r != expected %r"
                                   % (run["stdout"], case.stdout))

        # Emitted-C native guard assertions.
        text = build["emitted_text"]
        guard_pass = 0
        guard_total = 0
        if not text:
            cfg_reasons.append("emitted C missing or empty")
        else:
            if config == LEGACY_CONFIG:
                for tok in case.native_guards:
                    guard_total += 1
                    if tok in text:
                        cfg_reasons.append(
                            "legacy emitted C unexpectedly contains guard %r" % tok)
                    else:
                        guard_pass += 1
            else:
                for tok in case.native_guards:
                    guard_total += 1
                    if tok not in text:
                        cfg_reasons.append(
                            "%s emitted C missing guard %r" % (config, tok))
                    else:
                        guard_pass += 1
            for tok in case.always_tokens:
                guard_total += 1
                if tok not in text:
                    cfg_reasons.append(
                        "%s emitted C missing required token %r" % (config, tok))
                else:
                    guard_pass += 1

        configs[config] = {
            "build": build,
            "run": run,
            "guard_checks_passed": guard_pass,
            "guard_checks_total": guard_total,
            "reasons": cfg_reasons,
            "passed": not cfg_reasons,
        }
        if cfg_reasons:
            reasons.append({"config": config, "reasons": cfg_reasons})

    observed = []
    for c in CONFIG_ORDER:
        r = configs[c]["run"]
        observed.append((r["stdout"], r["rc"]) if r else None)
    identical = (len(observed) == len(CONFIG_ORDER)
                 and all(o == observed[0] for o in observed)
                 and observed[0] is not None)
    if not identical:
        reasons.append({"config": "equivalence",
                        "reasons": ["stdout/exit differ across configurations"]})

    return {
        "name": case.name,
        "family": case.family,
        "note": case.note,
        "source": case.source.lstrip("\n"),
        "expected_stdout": case.stdout,
        "expected_exit": case.exit_code,
        "native_guards": list(case.native_guards),
        "always_tokens": list(case.always_tokens),
        "configs": configs,
        "stdout_exit_identical": identical,
        "passed": not reasons,
        "reasons": reasons,
    }


def version_string():
    res = run_cmd([BIN, "--version"], timeout=15)
    return (res["stdout"] + res["stderr"]).strip().splitlines()[:1]


def write_summary(records):
    cases = len(records)
    passed = sum(1 for r in records if r["passed"])
    builds = sum(len(r["configs"]) for r in records)
    runs = sum(1 for r in records for c in CONFIG_ORDER if r["configs"][c]["run"])
    guard_checks = sum(r["configs"][c]["guard_checks_total"]
                       for r in records for c in CONFIG_ORDER)
    guard_pass = sum(r["configs"][c]["guard_checks_passed"]
                     for r in records for c in CONFIG_ORDER)
    identical = sum(1 for r in records if r["stdout_exit_identical"])

    failures = []
    for r in records:
        for item in r["reasons"]:
            cfg = item["config"]
            if cfg in r["configs"]:
                c = r["configs"][cfg]
                failures.append({
                    "case": r["name"], "family": r["family"], "config": cfg,
                    "reasons": item["reasons"],
                    "build_command": c["build"]["command"],
                    "run_command": c["run"]["command"] if c["run"] else None,
                    "build_rc": c["build"]["rc"],
                    "run_rc": c["run"]["rc"] if c["run"] else None,
                    "run_stdout": c["run"]["stdout"] if c["run"] else None,
                    "expected_stdout": r["expected_stdout"],
                })
            else:
                failures.append({
                    "case": r["name"], "family": r["family"], "config": cfg,
                    "reasons": item["reasons"],
                })

    by_family = {}
    for fam in sorted({r["family"] for r in records}):
        fr = [r for r in records if r["family"] == fam]
        by_family[fam] = {
            "cases": len(fr),
            "passed": sum(1 for r in fr if r["passed"]),
            "failed": sum(1 for r in fr if not r["passed"]),
        }

    payload = {
        "suite": "optimizer_mode_equivalence",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary": BIN,
        "binary_version": version_string(),
        "timeout_seconds": TIMEOUT,
        "run_timeout_seconds": RUN_TIMEOUT,
        "workers": WORKERS,
        "configs": [
            {"name": c, "flags": CONFIGS[c], "purpose": CONFIG_PURPOSE[c],
             "command_template": "c+ build <src> %s --emit-c <out.c> -o <bin>"
                                 % " ".join(CONFIGS[c])}
            for c in CONFIG_ORDER
        ],
        "totals": {
            "cases": cases,
            "cases_passed": passed,
            "cases_failed": cases - passed,
            "configs_per_case": len(CONFIG_ORDER),
            "builds": builds,
            "runs": runs,
            "behavior_checks": runs * 2,
            "guard_checks": guard_checks,
            "guard_checks_passed": guard_pass,
            "guard_checks_failed": guard_checks - guard_pass,
            "stdout_exit_identical_cases": identical,
            "difference_entries": len(failures),
        },
        "by_family": by_family,
        "differences": failures,
        "cases": records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(payload, indent=2) + "\n")

    t = payload["totals"]
    lines = []
    add = lines.append
    add("C+ optimizer / safety-mode equivalence suite")
    add("=" * 68)
    add("binary       : %s" % BIN)
    add("version      : %s" % (payload["binary_version"] or ["(unknown)"])[0])
    add("generated    : %s" % payload["generated_at"])
    add("timeout      : %ss build / %ss run per subprocess"
        % (TIMEOUT, RUN_TIMEOUT))
    add("workers      : %d" % WORKERS)
    add("")
    add("BUILD CONFIGURATIONS (exact flags; all include --sanitize=none)")
    add("-" * 68)
    for c in CONFIG_ORDER:
        add("  %-10s %-34s %s"
            % (c, " ".join(CONFIGS[c]), CONFIG_PURPOSE[c]))
        add("             %s" % payload["configs"][CONFIG_ORDER.index(c)]["command_template"])
    add("")
    add("SUMMARY")
    add("-" * 68)
    add("cases                       : %d" % t["cases"])
    add("cases passed                : %d" % t["cases_passed"])
    add("cases failed                : %d" % t["cases_failed"])
    add("configs per case            : %d" % t["configs_per_case"])
    add("builds                      : %d" % t["builds"])
    add("runs                        : %d" % t["runs"])
    add("behavior checks (stdout+exit): %d" % t["behavior_checks"])
    add("guard checks                : %d (%d passed, %d failed)"
        % (t["guard_checks"], t["guard_checks_passed"],
           t["guard_checks_failed"]))
    add("identical stdout/exit cases : %d / %d"
        % (t["stdout_exit_identical_cases"], t["cases"]))
    add("difference entries          : %d" % t["difference_entries"])
    add("")
    add("BY FAMILY")
    add("-" * 68)
    for fam, info in by_family.items():
        add("  %-20s cases=%2d passed=%2d failed=%2d"
            % (fam, info["cases"], info["passed"], info["failed"]))
    add("")
    add("DIFFERENCES / FAILURES (exact case + config + commands)")
    add("-" * 68)
    if not failures:
        add("  none - all six configurations produce identical stdout/exit")
    else:
        for f in failures:
            add("  case=%s family=%s config=%s"
                % (f["case"], f["family"], f["config"]))
            for reason in f["reasons"]:
                add("      - %s" % reason)
            if f.get("build_command"):
                add("      build: %s" % f["build_command"])
            if f.get("run_command"):
                add("      run  : %s" % f["run_command"])
            if f.get("run_stdout") is not None:
                add("      got stdout : %r" % f["run_stdout"])
                add("      want stdout: %r" % f["expected_stdout"])
    add("")
    add("Full records: results/summary.json ; logs: results/logs/")
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")
    return payload


def main():
    if not os.path.exists(BIN):
        print("error: CPLUS_BIN not found: %s" % BIN, file=sys.stderr)
        return 2

    for d in (CASES_DIR, LOGS, EMITTED, BINS):
        d.mkdir(parents=True, exist_ok=True)

    names = [c.name for c in CASES]
    if len(names) != len(set(names)):
        raise SystemExit("duplicate case name in manifest")

    print("optimizer_mode_equivalence: %d cases x %d configs, binary=%s"
          % (len(CASES), len(CONFIG_ORDER), BIN))
    for cfg in CONFIG_ORDER:
        print("  %-10s c+ build <src> %s --emit-c <out.c> -o <bin>"
              % (cfg, " ".join(CONFIGS[cfg])))

    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for rec in pool.map(evaluate_case, CASES):
            records.append(rec)

    payload = write_summary(records)
    t = payload["totals"]
    print("cases        : %d passed / %d failed"
          % (t["cases_passed"], t["cases_failed"]))
    print("builds       : %d   runs: %d" % (t["builds"], t["runs"]))
    print("guard checks : %d passed / %d failed"
          % (t["guard_checks_passed"], t["guard_checks_failed"]))
    print("identical    : %d / %d cases" % (t["stdout_exit_identical_cases"],
                                           t["cases"]))
    if payload["differences"]:
        print("differences (%d entries):" % t["difference_entries"])
        seen = set()
        for d in payload["differences"]:
            key = (d["case"], d["config"])
            if key in seen:
                continue
            seen.add(key)
            print("  - %s [%s/%s] %s"
                  % (d["case"], d["config"], d["family"], d["reasons"][0]))
    print("summary: %s" % (RESULTS / "summary.txt"))
    return 1 if payload["differences"] else 0


if __name__ == "__main__":
    sys.exit(main())
