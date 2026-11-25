#!/usr/bin/env python3
"""Paired safe/unsafe concurrency regression suite.

Unsafe cases must be rejected by `c+ check --strict`; paired safe cases must
be accepted. Exact commands, exit codes, stdout and stderr are recorded. No
skips, no `|| true`, hard timeouts.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/data_race/run_suite.py
"""
import datetime
import json
import os
import platform
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASE_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASE_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

PTH = "#include <stdio.h>\n#include <pthread.h>\n"

UNSAFE = [
    ("unsafe_helper_global",
     PTH + "static int g;\nstatic void inc(void){ g++; }\n"
     "static void *w(void *p){ (void)p; inc(); return 0; }\n"
     "int main(void){ pthread_t a,b; pthread_create(&a,0,w,0); "
     "pthread_create(&b,0,w,0); pthread_join(a,0); pthread_join(b,0); "
     "printf(\"%d\\n\",g); return 0; }\n",
     "shared global 'g'"),
    ("unsafe_missing_unlock",
     PTH + "static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER; static int g;\n"
     "static void *w(void *p){ (void)p; pthread_mutex_lock(&m); g++; return 0; }\n"
     "int main(void){ pthread_t t; pthread_create(&t,0,w,0); pthread_join(t,0); "
     "return 0; }\n",
     "still held on this return path"),
    ("unsafe_early_return_locked",
     PTH + "static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER; static int g;\n"
     "static void *w(void *p){ pthread_mutex_lock(&m); if(p) return 0; g++; "
     "pthread_mutex_unlock(&m); return 0; }\n"
     "int main(void){ pthread_t t; pthread_create(&t,0,w,0); pthread_join(t,0); "
     "return 0; }\n",
     "still held on this return path"),
    ("unsafe_lock_order_inversion",
     PTH + "static pthread_mutex_t a=PTHREAD_MUTEX_INITIALIZER, "
     "b=PTHREAD_MUTEX_INITIALIZER;\n"
     "static void *w1(void*p){ (void)p; pthread_mutex_lock(&a); "
     "pthread_mutex_lock(&b); pthread_mutex_unlock(&b); pthread_mutex_unlock(&a); "
     "return 0; }\n"
     "static void *w2(void*p){ (void)p; pthread_mutex_lock(&b); "
     "pthread_mutex_lock(&a); pthread_mutex_unlock(&a); pthread_mutex_unlock(&b); "
     "return 0; }\n"
     "int main(void){ pthread_t t; pthread_create(&t,0,w1,0); pthread_join(t,0); "
     "pthread_create(&t,0,w2,0); pthread_join(t,0); return 0; }\n",
     "inconsistent lock order"),
]

SAFE = [
    ("safe_helper_global_locked",
     PTH + "static int g; static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;\n"
     "static void inc(void){ pthread_mutex_lock(&m); g++; pthread_mutex_unlock(&m); }\n"
     "static void *w(void *p){ (void)p; inc(); return 0; }\n"
     "int main(void){ pthread_t a,b; pthread_create(&a,0,w,0); "
     "pthread_create(&b,0,w,0); pthread_join(a,0); pthread_join(b,0); "
     "printf(\"%d\\n\",g); return 0; }\n"),
    ("safe_locked_global",
     PTH + "static int g; static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;\n"
     "static void *w(void *p){ (void)p; pthread_mutex_lock(&m); g++; "
     "pthread_mutex_unlock(&m); return 0; }\n"
     "int main(void){ pthread_t a,b; pthread_create(&a,0,w,0); "
     "pthread_create(&b,0,w,0); pthread_join(a,0); pthread_join(b,0); "
     "printf(\"%d\\n\",g); return 0; }\n"),
    ("safe_nested_lock_same_order",
     PTH + "static pthread_mutex_t a=PTHREAD_MUTEX_INITIALIZER, "
     "b=PTHREAD_MUTEX_INITIALIZER;\n"
     "static void *w(void*p){ (void)p; pthread_mutex_lock(&a); "
     "pthread_mutex_lock(&b); pthread_mutex_unlock(&b); pthread_mutex_unlock(&a); "
     "return 0; }\n"
     "int main(void){ pthread_t t; pthread_create(&t,0,w,0); pthread_join(t,0); "
     "return 0; }\n"),
    ("safe_atomic_counter",
     PTH + "#include <stdatomic.h>\nstatic atomic_int c;\n"
     "static void *w(void *p){ (void)p; atomic_fetch_add(&c,1); return 0; }\n"
     "int main(void){ pthread_t a,b; pthread_create(&a,0,w,0); "
     "pthread_create(&b,0,w,0); pthread_join(a,0); pthread_join(b,0); "
     "printf(\"%d\\n\",atomic_load(&c)); return 0; }\n"),
    ("safe_early_return_after_unlock",
     PTH + "static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER; static int g;\n"
     "static void *w(void *p){ pthread_mutex_lock(&m); if(p){ "
     "pthread_mutex_unlock(&m); return 0; } g++; pthread_mutex_unlock(&m); "
     "return 0; }\n"
     "int main(void){ pthread_t t; pthread_create(&t,0,w,0); pthread_join(t,0); "
     "return 0; }\n"),
]


def run(argv, timeout=90):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def main():
    rows, fails = [], []
    for name, src, diag in UNSAFE:
        path = os.path.join(CASE_DIR, name + ".c")
        with open(path, "w") as f:
            f.write(src)
        step = run([BIN, "check", "--strict", "--sanitize=none", path])
        ok = step["rc"] not in (0, None) and diag in step["stderr"]
        rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                     "expected": diag, "steps": [step]})
        if not ok:
            fails.append(name)
        print("%-32s %s" % (name, "PASS" if ok else "FAIL"))
    for name, src in SAFE:
        path = os.path.join(CASE_DIR, name + ".c")
        with open(path, "w") as f:
            f.write(src)
        step = run([BIN, "check", "--strict", "--sanitize=none", path])
        ok = step["rc"] == 0 and not step["timed_out"]
        rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                     "expected": "accept", "steps": [step]})
        if not ok:
            fails.append(name)
        print("%-32s %s" % (name, "PASS" if ok else "FAIL"))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Data-race regression suite\n%s\n%s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-32s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:500]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("data_race: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
