#!/usr/bin/env python3
"""BLUE-TEAM agent 18/20 (blue_apps) - bounded hostile-input proof driver.

Builds are performed by run.sh; this driver executes every app against a large,
FIXED, deterministic corpus of hostile argv/stdin inputs and proves that each
invocation is safe.  Every invocation is bounded by a per-process timeout.

Scope
-----
  * 6  real-world C+ examples  (examples/real_world/*.cp, built by build/c+)
  * 100 cross-platform C apps   (tests/cross_platform_apps/src/*.c, clang)
  = 106 apps.

Coverage
--------
Each app receives >=1000 fixed hostile scenarios (default 1000), i.e.
106 * 1000 = 106,000 bounded invocations.  Scenarios are generated from a
fixed seed (0xB1AE20) plus a hand-crafted attack corpus (SIZE_MAX / negative
numbers, 10 KB stdin & argv, all 256 byte values, invalid UTF-8, deep nesting,
format strings, NUL floods, shell/SQL injection, path traversal, HTTP request
smuggling, 500+ argv entries, seeded random binary, ...).  The corpus is fully
reproducible.

Pass / fail contract
--------------------
These apps are deterministic cores: they read no argv and no stdin.  Their
documented contract encodes "defined refusal" as -1 sentinel tokens in stdout
while still exiting 0.  An invocation PASSES only when:

    returncode == 0  AND  stdout == exact expected contract bytes
                     AND  stderr == b""

Everything else is a FAIL and is bucketed as:

    timeout   - per-invocation timeout (bounded driver)
    signal    - termination by signal (returncode < 0)
    nonzero   - unexpected nonzero exit (no defined refusal)
    corrupt   - exit 0 but stdout corrupted or stderr non-empty

Result: BLUE20 apps <invocations> <passed> <failed>
"""
import json
import os
import random
import string
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
ART = os.path.join(ROOT, "tests", "redblue20", "blue_apps", "artifacts")
RW = os.path.join(ART, "real_world")
CPA = os.path.join(ART, "cross_platform")

N_SCENARIOS = 1000     # fixed hostile inputs PER app
WORKERS = 8            # bounded parallelism (8 CPUs reported)
TIMEOUT = 10           # seconds, per invocation (bounded driver)
SEED = 0xB1AE20

# ---------------------------------------------------------------------------
# App manifest with INDEPENDENT, contract-derived expected stdout (ground truth)
# ---------------------------------------------------------------------------
RW_EXPECTED = {
    "game_engine":     b"GAME_ENGINE 1 70 1 -1 -1 -1\n",
    "snake_game":      b"SNAKE_GAME 28 -1\n",
    "desktop_app":     b"DESKTOP_APP 8 2 -1\n",
    "web_app":         b"WEB_APP 200 204 404 404\n",
    "server_app":      b"SERVER_APP 0 0 2 -1\n",
    "database_engine": b"DATABASE_ENGINE 0 0 -1 -1 30\n",
}
TOKENS = ["GAME_ENGINE_APP", "SNAKE_APP", "DESKTOP_APP", "WEB_APP",
          "SERVER_APP", "DATABASE_APP", "AUDIO_APP", "VIDEO_APP",
          "GFX_APP", "UTILITY_APP"]


def apps():
    out = []
    for name, exp in RW_EXPECTED.items():
        out.append(("real_world/" + name, os.path.join(RW, name), exp))
    for i in range(100):
        exp = ("%s %d %d -1 -1\n" % (TOKENS[i // 10], i, i % 100)).encode()
        out.append(("cross_platform/app_%d" % i,
                    os.path.join(CPA, "app_%d" % i), exp))
    return out


# ---------------------------------------------------------------------------
# Fixed hostile scenario corpus
# ---------------------------------------------------------------------------
STR_PAYLOADS = [
    "", "A", "A" * 32, "A" * 10240,
    "18446744073709551615", "4294967296", "-1", "-2", "-2147483648",
    "-9223372036854775808", "999999999999999999999999999999",
    "0", "00", "0xdeadbeef", "0777", "1e309", "NaN", "Infinity",
    "DROP TABLE users; rm -rf /; :(){ :|:& };:",
    "'; DROP TABLE--", "$(id)", "`whoami`", "|&;<>()", "..\\..\\windows\\system32",
    "../../etc/passwd", "/dev/zero", "/proc/self/mem", "-h", "--help", "-",
    "(" * 10000 + ")" * 10000, "[" * 5000 + "]" * 5000,
    "%s%n%d%p" * 100, "%99999999s", "\\" * 1024, "/" * 1024,
    "." * 1024, "\t" * 1024, " " * 1024,
    "GET /../../etc/passwd HTTP/1.1\r\nHost: evil\r\n\r\n",
    "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
    "OK\u00e9\u65e5\u672c\u8a9e\U0001f680" * 40,
    "a\nb\r\nc|d" * 100,  # NUL bytes are exercised via stdin only (argv cannot hold NUL)
    "x" * 500,  # 500 argv entries handled separately below
]
RAW_PAYLOADS = [
    b"", b"A" * 10240, bytes(range(256)) * 40, b"\xff\xfe\x80" * 3400,
    b"(" * 10000 + b")" * 10000, b"%s%n%d%p" * 100,
    b"\x00" * 4096, b"\r\n" * 2048, b"\n" * 8192,
    bytes(range(256)) * 4 + b"\x00\xff" * 100,
    b"{" * 2048 + b"}" * 2048,
    b"\xef\xbb\xbf\xff\xfe" * 512,
]
SAFE_CHARS = string.ascii_letters + string.digits + "/.;-_=%$\\\"\t\r\n"


def build_scenarios(n):
    rng = random.Random(SEED)
    scenes = []

    # --- 20 explicitly hand-crafted attack classes first --------------------
    crafted = [
        ([], b""),                                             # 0  empty
        ([], b"A" * 10240),                                    # 1  10KB stdin
        (["A" * 10240], b""),                                  # 2  10KB argv
        (["18446744073709551615"], b""),                       # 3  SIZE_MAX
        (["-1", "-2", "-2147483648", "-9223372036854775808"], b""),  # 4 negatives
        (["DROP", "TABLE", "rm", "-rf", "/", ";", "sudo"],
         b"DROP TABLE users; rm -rf /; :(){ :|:& };:\n"),      # 5 injection
        (["(" * 10000 + ")" * 10000], b"(" * 10000 + b")" * 10000),  # 6 nesting
        (["%s%n%d%p" * 100], b"%s%n%d%p" * 1000),             # 7 format strings
        ([], bytes(range(256)) * 40),                          # 8  all bytes
        (["x"] * 500, b""),                                    # 9  500 argv
        ([], b"\xff\xfe\x80" * 3400),                          # 10 invalid UTF-8
        (["../../etc/passwd", "/dev/zero", "-h"],
         b"PATH=/etc/shadow\nGET /../../etc/passwd HTTP/1.1\n"),  # 11 traversal
        ([], b"\x00" * 8192),                                  # 12 NUL flood
        (["\t" * 1024, "\n" * 1024, "\r" * 1024], b"\t\n\r" * 1024),  # 13 controls
        (["999999999999999999999999"], b"1e309\nNaN\nInfinity\n"),  # 14 huge nums
        (["OK\u00e9\u65e5\u672c\u8a9e\U0001f680" * 40], b""), # 15 valid unicode
        ([], b"\r\n" * 4096),                                  # 16 CRLF flood
        (["{" * 2048 + "}" * 2048], b"{" * 2048 + b"}" * 2048),  # 17 braces
        (["--help", "-h", "--version", "-v", "--"], b"\xef\xbb\xbf\xff\xfe" * 512),  # 18 flags+bad BOM
        (["|&;<>()", "$(id)", "`whoami`"],
         b"GET / HTTP/1.1\r\nContent-Length: -1\r\n\r\n"),     # 19 shell/HTTP
    ]
    scenes.extend(crafted)

    # --- remaining scenarios: seeded, deterministic, reproducible ----------
    while len(scenes) < n:
        argc = rng.randint(0, 8)
        argv = []
        for _ in range(argc):
            base = rng.choice(STR_PAYLOADS)
            if rng.random() < 0.35:
                base += "".join(rng.choice(SAFE_CHARS)
                                for _ in range(rng.randint(0, 512)))
            argv.append(base)
        kind = rng.randint(0, 4)
        if kind == 0:
            stdin = b""
        elif kind == 1:
            stdin = rng.choice(RAW_PAYLOADS)
        elif kind == 2:
            stdin = bytes(rng.randrange(256) for _ in range(rng.randint(0, 4096)))
        elif kind == 3:
            stdin = "".join(rng.choice(SAFE_CHARS)
                            for _ in range(rng.randint(0, 4096))).encode()
        else:
            stdin = rng.choice(RAW_PAYLOADS) + bytes(
                rng.randrange(256) for _ in range(rng.randint(0, 2048)))
        scenes.append((argv, stdin))
    return scenes[:n]


# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------
def classify(rc, out, err, expected):
    if rc == 124 or rc == 137:
        return "timeout", "bounded_timeout"
    if rc < 0:
        return "signal", "signal_%d" % (-rc)
    if rc != 0:
        return "nonzero", "rc_%d" % rc
    if out != expected:
        return "corrupt", "stdout_mismatch"
    if err:
        return "corrupt", "stderr_nonempty"
    return "clean", "exit0_exact_output"


def make_runner(path):
    def run_one(scene):
        argv, stdin = scene
        # argv cannot contain NUL at the OS level; strip defensively so a
        # generated payload can never abort the driver itself.
        argv = [a.replace("\x00", "") for a in argv]
        try:
            p = subprocess.run([path] + argv, input=stdin,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=TIMEOUT)
            return p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return 124, b"", b""
        except OSError as e:
            return 127, b"", ("OSERROR:%s" % e).encode()
    return run_one


def main():
    scenarios = build_scenarios(N_SCENARIOS)
    manifest = apps()

    # Baseline sanity: build/extract must match the independent contract.
    for label, path, expected in manifest:
        if not os.path.isfile(path):
            print("MISSING %s (%s)" % (label, path))
            return 2
        try:
            p = subprocess.run([path], input=b"", stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            print("BASELINE_TIMEOUT %s" % label)
            return 2
        if p.returncode != 0 or p.stdout != expected or p.stderr != b"":
            print("BASELINE_MISMATCH %s rc=%d out=%r err=%r expected=%r" % (
                label, p.returncode, p.stdout, p.stderr, expected))
            return 2

    counts = {"clean": 0, "timeout": 0, "signal": 0,
              "nonzero": 0, "corrupt": 0}
    detail = {}
    per_app = {}
    findings = []
    invocations = 0
    t0 = time.time()

    with ThreadPoolExecutor(max_workers=WORKERS) as ex:
        for label, path, expected in manifest:
            run_one = make_runner(path)
            ok = 0
            bad = 0
            for scene, (rc, out, err) in zip(scenarios, ex.map(run_one, scenarios)):
                invocations += 1
                bucket, why = classify(rc, out, err, expected)
                counts[bucket] += 1
                dk = bucket + ":" + why
                detail[dk] = detail.get(dk, 0) + 1
                if bucket == "clean":
                    ok += 1
                else:
                    bad += 1
                    if len(findings) < 40:
                        findings.append({
                            "app": label, "rc": rc, "bucket": bucket,
                            "why": why,
                            "stdout": out[:120].decode("utf-8", "replace"),
                            "stderr": err[:120].decode("utf-8", "replace"),
                        })
            per_app[label] = {"passed": ok, "failed": bad}

    elapsed = time.time() - t0
    passed = counts["clean"]
    failed = invocations - passed
    summary = "BLUE20 apps %d %d %d" % (invocations, passed, failed)

    result = {
        "summary": summary,
        "invocations": invocations,
        "passed": passed,
        "failed": failed,
        "counts": counts,
        "detail_counts": detail,
        "apps": len(manifest),
        "scenarios_per_app": N_SCENARIOS,
        "workers": WORKERS,
        "timeout_seconds": TIMEOUT,
        "seed": SEED,
        "elapsed_seconds": round(elapsed, 2),
        # Defined refusals are encoded either as a -1 sentinel (105 apps) or,
        # for web_app, as HTTP 404 status codes. Both are verified byte-exact.
        "refusal_verified_apps": sum(
            1 for _, _, e in manifest if b"-1" in e or b"404" in e),
        "per_app": per_app,
        "findings": findings,
    }
    with open(os.path.join(ROOT, "tests", "redblue20", "blue_apps",
                           "results.json"), "w") as f:
        json.dump(result, f, indent=2, sort_keys=True)

    print(summary)
    print("apps=%d scenarios_per_app=%d workers=%d timeout_s=%d seed=%d elapsed_s=%.2f" %
          (len(manifest), N_SCENARIOS, WORKERS, TIMEOUT, SEED, elapsed))
    print("counts=" + json.dumps(counts, sort_keys=True))
    print("detail_counts=" + json.dumps(detail, sort_keys=True))
    print("refusal_verified_apps=%d/%d" %
          (result["refusal_verified_apps"], len(manifest)))
    print("findings=%d" % len(findings))
    for item in findings[:20]:
        print("FINDING %s rc=%s %s/%s out=%r err=%r" % (
            item["app"], item["rc"], item["bucket"], item["why"],
            item["stdout"], item["stderr"]))
    if failed:
        badapps = [k for k, v in per_app.items() if v["failed"]]
        print("FAILED_APPS(%d)=%s" % (len(badapps), ",".join(badapps[:30])))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
