#!/usr/bin/env python3
"""Prove cross-compiler ABI compatibility with a REAL downloaded C app.

`hash_crc32.c` (TheAlgorithms/C, MIT/GPL corpus) is split verbatim into a
helper TU defining `uint32_t crc32(const char *)` and a caller TU. Objects are
built by c+ (LEGACY, plain C ABI) and by clang, then linked in BOTH
directions with clang's linker. All four binaries must print `Tests passed`.

This closes the gap in abi_safety/abi_real_world, where every object came from
one compiler and cross-object ABI was never exercised.
"""
import json, os, subprocess, sys, datetime, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CPLUS = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
CLANG = os.environ.get("REF_CC", "/usr/bin/clang")
OUT = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(OUT, "results")
os.makedirs(RESULTS, exist_ok=True)

# Verbatim function bodies from tests/compiler/real_world_c/cache/hash_crc32.c
HELPER = r'''#include <stdint.h>
#include <stddef.h>
uint32_t crc32(const char* s)
{
    uint32_t crc = 0xffffffff;
    size_t i = 0;
    while (s[i] != '\0')
    {
        uint8_t byte = s[i];
        crc = crc ^ byte;
        for (uint8_t j = 8; j > 0; --j)
        {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
        i++;
    }
    return crc ^ 0xffffffff;
}
'''
CALLER = r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
uint32_t crc32(const char *);
void test_crc32(void)
{
    assert(crc32("Hello World") == 1243066710);
    assert(crc32("Hello World!") == 472456355);
    assert(crc32("Hello world") == 2346098258);
    assert(crc32("Hello world!") == 461707669);
    printf("Tests passed\n");
}
int main(void) { test_crc32(); return 0; }
'''


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60, **kw)


def main():
    rows, fails = [], []
    d = tempfile.mkdtemp(prefix="abi_mixed_", dir=RESULTS)
    helper = os.path.join(d, "helper.c")
    caller = os.path.join(d, "caller.c")
    open(helper, "w").write(HELPER)
    open(caller, "w").write(CALLER)
    base = [CLANG, "-std=gnu89", "-O2"]
    cplus = [CPLUS, "build", "--legacy-c", "--sanitize=none"]
    steps = {
        "helper.clang.o": base + ["-c", helper, "-o", os.path.join(d, "helper.clang.o")],
        "helper.cplus.o": cplus + ["-c", helper, "-o", os.path.join(d, "helper.cplus.o")],
        "caller.clang.o": base + ["-c", caller, "-o", os.path.join(d, "caller.clang.o")],
        "caller.cplus.o": cplus + ["-c", caller, "-o", os.path.join(d, "caller.cplus.o")],
    }
    for name, cmd in steps.items():
        p = run(cmd)
        rows.append({"step": name, "rc": p.returncode, "stderr": p.stderr.decode(errors="replace")[:300]})
        if p.returncode:
            fails.append("object build failed: " + name)
    if not fails:
        paths = {k: os.path.join(d, k) for k in steps}
        links = {
            "all_clang": [CLANG, paths["caller.clang.o"], paths["helper.clang.o"], "-o", os.path.join(d, "all_clang")],
            "all_cplus": [CLANG, paths["caller.cplus.o"], paths["helper.cplus.o"], "-o", os.path.join(d, "all_cplus")],
            "cplus_caller_clang_helper": [CLANG, paths["caller.cplus.o"], paths["helper.clang.o"], "-o", os.path.join(d, "cplus_caller_clang_helper")],
            "clang_caller_cplus_helper": [CLANG, paths["caller.clang.o"], paths["helper.cplus.o"], "-o", os.path.join(d, "clang_caller_cplus_helper")],
        }
        for name, cmd in links.items():
            p = run(cmd)
            ok = p.returncode == 0
            out = run([os.path.join(d, name)]).stdout.decode().strip() if ok else ""
            rows.append({"step": name, "rc": p.returncode, "stdout": out,
                         "stderr": p.stderr.decode(errors="replace")[:300]})
            if not ok or out != "Tests passed":
                fails.append("%s: rc=%d out=%r" % (name, p.returncode, out))
        # nm proof that _crc32 is defined in helper and undefined in caller
        for obj in ("helper.clang.o", "helper.cplus.o", "caller.clang.o", "caller.cplus.o"):
            p = run(["/usr/bin/nm", "-g", paths[obj]])
            rows.append({"step": "nm:" + obj, "rc": p.returncode,
                         "symbols": [ln for ln in p.stdout.decode(errors="replace").splitlines()
                                     if "crc32" in ln]})
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "cplus": CPLUS, "clang": CLANG,
               "source": "tests/compiler/real_world_c/cache/hash_crc32.c (verbatim functions)",
               "results": rows, "failures": fails}
    with open(os.path.join(RESULTS, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(RESULTS, "summary.txt"), "w") as f:
        f.write("Cross-compiler ABI proof: c+ <-> clang objects from real C app\n")
        for r in rows:
            f.write("%-30s rc=%s %s\n" % (r["step"], r["rc"],
                    r.get("stdout") or r.get("symbols") or r.get("stderr", "")[:120]))
        f.write("\nfailures: %d\n" % len(fails))
        for x in fails:
            f.write("  " + x + "\n")
    for r in rows:
        print("%-30s rc=%s %s" % (r["step"], r["rc"], r.get("stdout") or ""))
    print("mixed ABI: %d steps, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
