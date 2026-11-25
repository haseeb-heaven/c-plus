#!/usr/bin/env python3
"""Same-program safety and benchmark comparison across C+/clang/gcc/rustc and
optionally Fil-C / TrapC (supply FILC_BIN / TRAPC_BIN).

Toolchains that are not installed are RECORDED as TOOLCHAIN_UNAVAILABLE, never
silently skipped and never counted as passing. C compilers consume the .c
sources; rustc consumes the equivalent .rs source. Outcomes are classified as
compile_reject / runtime_trap / clean_run with exact rc and diagnostic.
"""
import json, os, shutil, subprocess, sys, datetime, statistics, tempfile, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OUT = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(OUT, "results")
os.makedirs(RES, exist_ok=True)
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
C = {
    "cplus_strict": [BIN, "build", "--strict", "--sanitize=none"],
    "cplus_default": [BIN, "build", "--default", "--sanitize=none"],
    "clang": ["/usr/bin/clang", "-std=gnu17", "-O2"],
    "gcc": ["/opt/homebrew/bin/gcc-14", "-std=gnu17", "-O2"],
}
RUST = {"rustc_debug": ["rustc", "--edition", "2021", "-C", "opt-level=0"],
        "rustc_release": ["rustc", "--edition", "2021", "-O"]}
for key, var, default in (("fil-c", "FILC_BIN", None), ("trapc", "TRAPC_BIN", None)):
    p = os.environ.get(var) or shutil.which(key)
    if p:
        C[key] = [p]

SCEN = {
    "const_oob": {
        "c": "int main(void){int a[4]={0};return a[7];}\n",
        "rs": "fn main(){ let a=[0i32;4]; println!(\"{}\", a[7]); }\n",
    },
    "overflow": {
        "c": "static int add1(int x){return x+1;}\nint main(void){return add1(2147483647);}\n",
        "rs": "fn add1(x:i32)->i32{x+1}\nfn main(){println!(\"{}\",add1(i32::MAX));}\n",
    },
    "null_deref": {
        "c": "static void d(int*p){*p=1;}\nint main(void){d(0);return 0;}\n",
        "rs": "fn main(){let p: *mut i32 = std::ptr::null_mut(); unsafe{*p=1;} }\n",
    },
    "clean": {
        "c": "int main(void){int s=0;for(int i=0;i<100;i++)s+=i;return s==4950?0:1;}\n",
        "rs": "fn main(){let s: i32=(0..100).sum(); assert_eq!(s,4950);}\n",
    },
}
BENCH = {
    "compute": {
        "c": "int main(void){volatile long x=1;long s=0;for(long i=0;i<200000000L;i++)s+=x;return s==200000000L?0:1;}\n",
        "rs": "fn main(){let mut s=0i64;for i in 0..200000000i64{s+=std::hint::black_box(i)*0+1;} assert_eq!(std::hint::black_box(s),200000000);}\n",
    },
}


def run(cmd, timeout=60, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, **kw)


def classify(rc, text):
    if rc is None:
        return "no_run"
    low = text.lower()
    if rc < 0 or (rc != 0 and rc != 2) or rc in (101, 102, 103, 104, 105, 110, 111, 134):
        return "runtime_trap"
    if "index out of bounds" in low or "overflow" in low or "segmentation" in low:
        return "runtime_trap"
    return "clean_run"


def main():
    rows, benches, fails = [], [], []
    # Explicitly record absent toolchains instead of silently omitting them.
    for cid, var in (("fil-c", "FILC_BIN"), ("trapc", "TRAPC_BIN")):
        if cid not in C:
            rows.append({"scenario": "toolchain_availability", "toolchain": cid,
                         "lang": "C", "outcome": "TOOLCHAIN_UNAVAILABLE",
                         "compiler_output": "set %s=/path/to/compiler to enable" % var,
                         "command": [var]})
    # Safety scenarios
    for name, pair in SCEN.items():
        for cid, cmd in C.items():
            src = os.path.join(RES, name + "_" + cid + ".c")
            exe = os.path.join(RES, name + "_" + cid + ".bin")
            open(src, "w").write(pair["c"])
            b = run(cmd + [src, "-o", exe])
            rec = {"scenario": name, "toolchain": cid, "lang": "C",
                   "command": cmd + [src, "-o", exe],
                   "compile_rc": b.returncode,
                   "compiler_output": (b.stdout + b.stderr).decode(errors="replace")[:600]}
            if b.returncode:
                rec["outcome"] = "compile_reject"
            else:
                r = run([exe])
                rec["run_rc"] = r.returncode
                rec["runtime_output"] = (r.stdout + r.stderr).decode(errors="replace")[:600]
                rec["outcome"] = classify(r.returncode, rec["runtime_output"])
            rows.append(rec)
        for rid, cmd in RUST.items():
            src = os.path.join(RES, name + "_" + rid + ".rs")
            exe = os.path.join(RES, name + "_" + rid + ".bin")
            open(src, "w").write(pair["rs"])
            b = run(cmd + [src, "-o", exe])
            rec = {"scenario": name, "toolchain": rid, "lang": "Rust",
                   "command": cmd + [src, "-o", exe], "compile_rc": b.returncode,
                   "compiler_output": (b.stdout + b.stderr).decode(errors="replace")[:600]}
            if b.returncode:
                rec["outcome"] = "compile_reject"
            else:
                r = run([exe])
                rec["run_rc"] = r.returncode
                rec["runtime_output"] = (r.stdout + r.stderr).decode(errors="replace")[:600]
                rec["outcome"] = classify(r.returncode, rec["runtime_output"])
            rows.append(rec)
        for r in rows:
            print("%-12s %-14s %-16s rc=%-4s %s" %
                  (r["scenario"], r["toolchain"], r["outcome"],
                   r.get("run_rc", r.get("compile_rc", "-")), r["compiler_output"].splitlines()[0][:60] if r["compiler_output"] else ""))
    # Benchmarks: median of 3 wall times via /usr/bin/time -l on macOS
    for name, pair in BENCH.items():
        for cid, cmd in list(C.items()) + list(RUST.items()):
            ext = ".rs" if cid.startswith("rustc") else ".c"
            src = os.path.join(RES, name + "_" + cid + ext)
            exe = os.path.join(RES, name + "_" + cid + ".bench")
            open(src, "w").write(pair["rs"] if ext == ".rs" else pair["c"])
            b = run(cmd + [src, "-o", exe])
            if b.returncode:
                benches.append({"bench": name, "toolchain": cid, "status": "BUILD_FAIL",
                                "output": b.stderr.decode()[:300]})
                continue
            times, rss = [], []
            for _ in range(3):
                p = run(["/usr/bin/time", "-l", exe, ">/dev/null", "2>&1"], timeout=120)
                raw = (p.stdout + p.stderr).decode(errors="replace")
                mt = re.search(r"(\d+\.\d+)\s+real", raw)
                mr = re.search(r"(\d+)\s+maximum resident set size", raw)
                if mt:
                    times.append(float(mt.group(1)))
                if mr:
                    rss.append(int(mr.group(1)))
            benches.append({"bench": name, "toolchain": cid, "status": "OK",
                            "runs": 3, "median_s": statistics.median(times) if times else None,
                            "median_rss_bytes": statistics.median(rss) if rss else None})
            print("%-10s %-14s %s s  rss=%s" %
                  (name, cid, benches[-1]["median_s"], benches[-1]["median_rss_bytes"]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "cplus": BIN, "note": "fil-c/trapc recorded only when binaries are supplied via FILC_BIN/TRAPC_BIN",
               "scenarios": rows, "benchmarks": benches, "failures": fails}
    with open(os.path.join(RES, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(RES, "summary.txt"), "w") as f:
        f.write("Same-program cross-compiler safety + benchmark comparison\n%s\n\n" % payload["generated"])
        for r in rows:
            f.write("%-12s %-14s %-16s compile=%s run=%s\n" %
                    (r["scenario"], r["toolchain"], r["outcome"],
                     r.get("compile_rc", "-"), r.get("run_rc", "-")))
        f.write("\nBENCHMARKS\n")
        for b in benches:
            f.write("%-10s %-14s %-10s median=%s rss=%s\n" %
                    (b["bench"], b["toolchain"], b["status"],
                     b.get("median_s"), b.get("median_rss_bytes")))
    print("cross-compiler: %d scenario checks, %d benchmarks" % (len(rows), len(benches)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
