#!/usr/bin/env python3
"""C+ vs Rust runtime benchmark with exact stdout and RSS evidence.

No sanitizers, no `|| true`, no silent skips.  Each binary gets one warm-up
then RUNS timed runs under /usr/bin/time -l; median real time is reported with
the RSS of that same run.
"""
import json, os, re, statistics, subprocess, sys, time, datetime, platform

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CPLUS = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
RUSTC = os.environ.get("RUSTC", "rustc")
RUNS = int(os.environ.get("BENCH_RUNS", "5"))
TIMEOUT = 300
OUT = os.path.join(os.path.dirname(__file__), "results")
os.makedirs(OUT, exist_ok=True)

PROGRAMS = ["bounds_sum", "integer_compute", "pointer_chase", "function_call",
            "alloc_churn", "raii_loop", "method_dispatch"]

CONFIGS = {
    "cplus_safe_release": lambda src, exe: [CPLUS, "build", src, "--release", "--sanitize=none", "-o", exe],
    "cplus_legacy_release": lambda src, exe: [CPLUS, "build", src, "--release", "--legacy-c", "--sanitize=none", "-o", exe],
    "cplus_debug": lambda src, exe: [CPLUS, "build", src, "--debug", "--sanitize=none", "-o", exe],
    "rust_O": lambda src, exe: [RUSTC, "--edition", "2021", "-O", "-C", "debuginfo=0", src, "-o", exe],
    "rust_opt0": lambda src, exe: [RUSTC, "--edition", "2021", "-C", "opt-level=0", "-C", "debuginfo=0", src, "-o", exe],
}


def run_timed(binary):
    subprocess.run([binary], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=TIMEOUT, check=True)
    samples = []
    for _ in range(RUNS):
        proc = subprocess.run(["/usr/bin/time", "-l", binary],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=TIMEOUT)
        if proc.returncode != 0:
            raise RuntimeError("run rc=%d: %s" % (proc.returncode, proc.stderr[:200]))
        txt = proc.stderr.decode("utf-8", "replace")
        m = re.search(r"(\d+\.\d+)\s+real\s+(\d+\.\d+)\s+user\s+(\d+\.\d+)\s+sys", txt)
        rss = re.search(r"(\d+)\s+maximum resident set size", txt)
        if not m or not rss:
            raise RuntimeError("could not parse time output: %s" % txt[:200])
        samples.append((float(m.group(1)), float(m.group(2)), float(m.group(3)), int(rss.group(1))))
    samples.sort(key=lambda x: x[0])
    median = samples[len(samples) // 2]
    return {"real": median[0], "user": median[1], "sys": median[2], "rss": median[3],
            "samples": samples}


def main():
    cplus_v = subprocess.run([CPLUS, "--version"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT).stdout.decode().splitlines()[0]
    rust_v = subprocess.run([RUSTC, "--version"], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT).stdout.decode().strip()
    rows, failures = [], []
    for name in PROGRAMS:
        csrc = os.path.join(ROOT, "benchmarks", name + ".c")
        rsrc = os.path.join(ROOT, "benchmarks", "rust_equivalent", name + ".rs")
        if not os.path.exists(rsrc):
            failures.append({"case": name, "reason": "missing Rust equivalent"})
            continue
        outputs = {}
        for cfg, builder in CONFIGS.items():
            exe = os.path.join(OUT, "%s__%s" % (name, cfg))
            cmd = builder(csrc if cfg.startswith("cplus") else rsrc, exe)
            build = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   timeout=TIMEOUT)
            if build.returncode != 0:
                failures.append({"case": name, "config": cfg,
                                 "reason": "build failed: " + build.stderr.decode()[:300]})
                continue
            out = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 timeout=TIMEOUT, check=True).stdout.decode().strip()
            outputs[cfg] = out
            rows.append({"case": name, "config": cfg, "stdout": out,
                         **run_timed(exe)})
        if len(set(outputs.values())) > 1:
            failures.append({"case": name, "reason": "stdout mismatch: %s" % outputs})
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "cplus": cplus_v, "rustc": rust_v,
               "runs": RUNS, "method": "1 warmup; %d timed; median real + RSS of that run" % RUNS,
               "configs": list(CONFIGS), "results": rows, "failures": failures}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("C+ vs Rust benchmark\n%s\n%s\n%s\nruns=%d method=%s\n\n" %
                (payload["host"], cplus_v, rust_v, RUNS, payload["method"]))
        f.write("%-18s %-22s %9s %12s %9s\n" % ("program", "config", "real_s", "rss_bytes", "stdout"))
        for r in rows:
            f.write("%-18s %-22s %9.3f %12d %s\n" % (r["case"], r["config"], r["real"], r["rss"], r["stdout"]))
        f.write("\nfailures: %d\n" % len(failures))
        for x in failures:
            f.write("  %s\n" % x)
    print("benchmarks: %d measurements, %d failures" % (len(rows), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
