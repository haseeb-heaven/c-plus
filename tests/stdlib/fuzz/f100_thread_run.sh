#!/bin/sh
# F-086..F-088: build and run the f100 thread deterministic fuzz configurations.
#   1. --strict --sanitize=none
#   2. --strict --sanitize=undefined
# Every run is bounded by `timeout 30`; every binary self-arms a shorter SIGALRM
# watchdog.  Digests must match across both modes and the manifest.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
MANIFEST="$ROOT/tests/stdlib/fuzz/f100_thread_configs.json"
OUT=${TMPDIR:-/tmp}/f100_fuzz_thread
mkdir -p "$OUT"

python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'
import json, pathlib, subprocess, sys

root, cplus, manifest, out = (pathlib.Path(x) for x in sys.argv[1:5])
data = json.loads(manifest.read_text())
modes = [("strict-none", "--sanitize=none"), ("ubsan", "--sanitize=undefined")]
fail = 0

for cfg in data["configs"]:
    src = root / cfg["source"]
    per_mode = {}
    ok = True
    for label, san in modes:
        exe = out / (cfg["id"] + "_" + label)
        build = [str(cplus), "build", "--strict", san] + list(cfg["build_flags"]) + \
                [str(src), "-o", str(exe)]
        try:
            b = subprocess.run(build, cwd=str(root), capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            print("FAIL %s build %s: timeout" % (cfg["id"], label)); ok = False; continue
        if b.returncode != 0:
            print("FAIL %s build %s (rc=%d): %s" % (cfg["id"], label, b.returncode,
                                                   (b.stdout + b.stderr)[-400:]))
            ok = False; continue
        try:
            r = subprocess.run(["timeout", "30", str(exe)], cwd=str(root),
                               capture_output=True, text=True, timeout=35)
        except subprocess.TimeoutExpired:
            print("FAIL %s run %s: timeout 30 exceeded" % (cfg["id"], label))
            ok = False; continue
        lines = (r.stdout + r.stderr).splitlines()
        digest = next((x.split()[-1] for x in lines if "_DIGEST " in x), None)
        per_mode[label] = digest
        final_ok = any(x == cfg["expected_final_line"] for x in lines)
        if r.returncode != 0:
            print("FAIL %s run %s: exit %d\n%s" % (cfg["id"], label, r.returncode,
                                                   "\n".join(lines[-12:])))
            ok = False; continue
        if not final_ok:
            print("FAIL %s run %s: missing final line %r" % (cfg["id"], label,
                                                             cfg["expected_final_line"]))
            ok = False; continue
        if digest != cfg["expected_digest"]:
            print("FAIL %s run %s: digest %s != %s" % (cfg["id"], label, digest,
                                                       cfg["expected_digest"]))
            ok = False; continue
    if ok and len(set(per_mode.values())) != 1:
        print("FAIL %s: digests differ across modes %r" % (cfg["id"], per_mode))
        ok = False
    if ok:
        print("ok   %s %s (%s ops, digest=%s)" % (cfg["id"], cfg["topic"],
                                                  cfg["ops"], per_mode["strict-none"]))
    else:
        fail += 1

print("f100 thread configs: %d/%d passed" % (len(data["configs"]) - fail, len(data["configs"])))
sys.exit(1 if fail else 0)
PY
exit $?
