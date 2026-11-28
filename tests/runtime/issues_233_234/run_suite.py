#!/usr/bin/env python3
"""Regressions for runtime issues #233 and #234.

Each case compiles a tiny C program that includes the runtime implementation
directly, so no project build is required.  The source under test can be
selected with CPLUS_RUNTIME_SRC (default: runtime/cplus_runtime.c at the repo
root), which makes it easy to run the same suite against an unfixed baseline.

Usage:
    python3 tests/runtime/issues_233_234/run_suite.py
    CPLUS_RUNTIME_SRC=/path/to/baseline/cplus_runtime.c \\
    CPLUS_HEADER_DIR=/path/to/baseline/include \\
        python3 tests/runtime/issues_233_234/run_suite.py
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = HERE / "results"
OUT.mkdir(parents=True, exist_ok=True)
CC = os.environ.get("CC", "/usr/bin/clang")
RUNTIME_SRC = Path(os.environ.get(
    "CPLUS_RUNTIME_SRC", str(ROOT / "runtime" / "cplus_runtime.c"))).resolve()
HEADER = Path(os.environ.get(
    "CPLUS_HEADER_DIR", str(ROOT / "runtime" / "include"))).resolve()

SDK_ARGS: list[str] = []
if sys.platform == "darwin":
    try:
        sdk = subprocess.check_output(["xcrun", "--show-sdk-path"],
                                      text=True).strip()
        if sdk:
            SDK_ARGS = ["-isysroot", sdk]
    except (OSError, subprocess.CalledProcessError):
        pass

BASE = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <dlfcn.h>
#include "cplus/cplus.h"
"""

RUNTIME_INCLUDE = '\n#include "%s"\n' % RUNTIME_SRC


def prog(body: str, prefix: str = "") -> str:
    return prefix + BASE + RUNTIME_INCLUDE + body


CASES: list[dict[str, object]] = [
    # ------------------------------------------------------------------ #233
    dict(name="233_scan_char_ptr_writes_to_buffer",
         source=prog("""
int main(void) {
  char *buf = (char *)cplus_alloc(16);
  char *original = buf;
  scan(buf);
  /* The old macro passed &buf, so the token overwrote the pointer itself. */
  if (buf != original) { puts("POINTER_OVERWRITTEN"); return 2; }
  if (strcmp(buf, "abc") != 0) { printf("buf=%s\\n", buf); return 3; }
  if (cplus_alloc_bytes(buf) != 16u) return 4;
  cplus_release(buf);
  puts("OK");
  return 0;
}
"""),
         stdin="abc\n", out=b"OK\n", rc=0, err="", env={}, flags=[],
         compile_error=None),
    dict(name="233_scan_char_ptr_uses_allocated_size",
         source=prog("""
int main(void) {
  char *buf = (char *)cplus_alloc(8);
  scan(buf); /* 10 bytes needed; the old hardcoded 256u accepted it */
  puts("UNEXPECTED: oversized token was written into an 8-byte buffer");
  return 0;
}
"""),
         stdin="abcdefghi\n", out=b"", rc=101, err="not enough space",
         env={}, flags=[], compile_error=None),
    dict(name="233_scan_char_array_still_bounded",
         source=prog("""
int main(void) {
  char name[4] = {0};
  scan(name);
  if (strcmp(name, "abc") != 0) { printf("name=%s\\n", name); return 2; }
  puts("OK");
  return 0;
}
"""),
         stdin="abc\n", out=b"OK\n", rc=0, err="", env={}, flags=[],
         compile_error=None),
    dict(name="233_scan_char_array_too_small_fails",
         source=prog("""
int main(void) {
  char name[3] = {0};
  scan(name);
  puts("UNEXPECTED: oversized token was written into a 3-byte array");
  return 0;
}
"""),
         stdin="abc\n", out=b"", rc=101, err="not enough space",
         env={}, flags=[], compile_error=None),
    dict(name="233_scan_unsigned_char_accepts_full_range",
         source=prog("""
int main(void) {
  unsigned char uc = 0;
  scan(uc); /* (uc)+0 promotes to int; the old macro used signed char range */
  printf("uc=%u\\n", (unsigned)uc);
  if (uc != 200u) return 2;
  puts("OK");
  return 0;
}
"""),
         stdin="200\n", out=b"uc=200\nOK\n", rc=0, err="", env={}, flags=[],
         compile_error=None),
    dict(name="233_scan_unsigned_char_rejects_overflow",
         source=prog("""
int main(void) {
  unsigned char uc = 0;
  scan(uc);
  puts("UNEXPECTED: 300 wrapped into an unsigned char");
  return 0;
}
"""),
         stdin="300\n", out=b"", rc=111, err="too large", env={}, flags=[],
         compile_error=None),
    dict(name="233_scan_unsigned_short_accepts_full_range",
         source=prog("""
int main(void) {
  unsigned short us = 0;
  scan(us);
  printf("us=%u\\n", (unsigned)us);
  if (us != 40000u) return 2;
  puts("OK");
  return 0;
}
"""),
         stdin="40000\n", out=b"us=40000\nOK\n", rc=0, err="", env={},
         flags=[], compile_error=None),
    dict(name="233_scan_unsigned_short_rejects_overflow",
         source=prog("""
int main(void) {
  unsigned short us = 0;
  scan(us);
  puts("UNEXPECTED: 70000 wrapped into an unsigned short");
  return 0;
}
"""),
         stdin="70000\n", out=b"", rc=111, err="too large", env={}, flags=[],
         compile_error=None),
    dict(name="233_scan_evaluates_lvalue_once",
         source=prog("""
int main(void) {
  char *slots[2];
  slots[0] = (char *)cplus_alloc(8);
  slots[1] = (char *)cplus_alloc(8);
  int i = 0;
  scan(slots[i++]);
  printf("i=%d\\n", i);
  if (i != 1) return 2;
  if (strcmp(slots[0], "xy") != 0) return 3;
  puts("OK");
  return 0;
}
"""),
         stdin="xy\n", out=b"i=1\nOK\n", rc=0, err="", env={}, flags=[],
         compile_error=None),
    dict(name="233_const_char_ptr_rejected_at_compile_time",
         source=prog("""
int main(void) {
  const char *p = "literal";
  scan(p);
  (void)p;
  return 0;
}
"""),
         stdin="", out=b"", rc=0, err="", env={}, flags=[],
         compile_error="writable char buffer"),
    # ------------------------------------------------------------------ #234
    dict(name="234_shift64_signed_overflow_traps",
         source=prog("""
int main(void) {
  volatile long long r = __cplus_checked_shl_i64(((long long)1 << 62), 2,
                                                64, 1);
  printf("r=%lld\\n", r);
  return 0;
}
"""),
         stdin="", out=b"", rc=110,
         err="left shift loses bits above the source width", env={}, flags=[],
         compile_error=None),
    dict(name="234_shift64_unsigned_overflow_traps",
         source=prog("""
int main(void) {
  volatile long long r = __cplus_checked_shl_i64(
      (long long)((unsigned long long)1 << 63), 1, 64, 0);
  printf("r=%lld\\n", r);
  return 0;
}
"""),
         stdin="", out=b"", rc=110,
         err="left shift loses bits above the source width", env={}, flags=[],
         compile_error=None),
    dict(name="234_shift64_valid_shifts_round_trip",
         source=prog("""
int main(void) {
  long long a = __cplus_checked_shl_i64(((long long)1 << 60), 2, 64, 1);
  unsigned long long b = (unsigned long long)__cplus_checked_shl_i64(1, 63,
                                                                    64, 0);
  long long c = __cplus_checked_shl_i64(1, 30, 32, 1);
  printf("a=%lld b=%llu c=%lld\\n", a, b, c);
  if (a != ((long long)1 << 62)) return 2;
  if (b != ((unsigned long long)1 << 63)) return 3;
  if (c != ((long long)1 << 30)) return 4;
  puts("OK");
  return 0;
}
"""),
         stdin="", out=b"a=4611686018427387904 b=9223372036854775808 "
                       b"c=1073741824\nOK\n",
         rc=0, err="", env={}, flags=[], compile_error=None),
    dict(name="234_shift32_signed_sign_bit_traps",
         source=prog("""
int main(void) {
  volatile long long r = __cplus_checked_shl_i64(((long long)1 << 30), 1,
                                                32, 1);
  printf("r=%lld\\n", r);
  return 0;
}
"""),
         stdin="", out=b"", rc=110,
         err="left shift loses bits above the source width", env={}, flags=[],
         compile_error=None),
    dict(name="234_retired_pointer_never_capacity_probed",
         # Redirect every malloc_size() call in the runtime to an instrumented
         # helper.  A probe of the freed pointer exits 77, which is never a
         # valid C+ fatal code.  CPLUS_NO_RETIRED_CAPACITY_PROBE forces the
         # Linux-equivalent conservative path on this host.
         source=BASE + """
static size_t (*real_malloc_size_fn)(const void *);
static const void *volatile retired_probe;

__attribute__((constructor)) static void init_real_malloc_size(void) {
  real_malloc_size_fn = (size_t (*)(const void *))
      dlsym(RTLD_NEXT, "malloc_size");
}
static size_t cplus_test_malloc_size(const void *p) {
  if (p == retired_probe)
    _Exit(77); /* issue #234: glibc malloc_usable_size(freed) is UB */
  return real_malloc_size_fn ? real_malloc_size_fn(p) : 0u;
}
#define malloc_size cplus_test_malloc_size
""" + RUNTIME_INCLUDE + """
static void fatal_hook(cplus_exit_code code, const char *kind) {
  (void)kind;
  printf("code=%d\\n", (int)code);
  fflush(stdout); /* the fatal path uses _Exit(), which never flushes */
}

int main(void) {
  cplus_fatal_hook = fatal_hook;
  char *p = (char *)cplus_alloc(256u * 1024u);
  cplus_release(p);
  retired_probe = p;
  cplus_register_allocation(p, 8u, __FILE__, __LINE__);
  puts("UNEXPECTED: retired address was re-registered");
  return 2;
}
""",
         stdin="", out=b"code=105\n", rc=105,
         err="retired address cannot be re-registered", env={},
         flags=["-DCPLUS_NO_RETIRED_CAPACITY_PROBE=1"], compile_error=None),
]


def compile_case(name: str, source: str,
                 flags: list[str]) -> tuple[Path | None, bytes]:
    src = OUT / (name + ".c")
    exe = OUT / (name + ".bin")
    src.write_text(source)
    proc = subprocess.run(
        [CC, "-std=gnu17", "-w"] + SDK_ARGS +
        ["-I" + str(ROOT), "-I" + str(HEADER), str(src), "-o", str(exe),
         "-lpthread", "-lm", "-ldl"] + flags,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    return (exe if proc.returncode == 0 else None, proc.stderr)


def main() -> int:
    failures = 0
    for case in CASES:
        name = str(case["name"])
        want_compile_error = case["compile_error"]
        exe, compile_stderr = compile_case(name, str(case["source"]),
                                           list(case["flags"]))  # type: ignore[arg-type]
        if want_compile_error is not None:
            ok = (exe is None and
                  want_compile_error in compile_stderr.decode(errors="replace"))
            detail = "compile_stderr=%r" % compile_stderr[-300:]
        elif exe is None:
            ok = False
            detail = "unexpected compile failure: %r" % compile_stderr[-300:]
        else:
            env = dict(os.environ)
            env.update(case["env"])  # type: ignore[arg-type]
            proc = subprocess.run(
                [str(exe)], input=str(case["stdin"]).encode(),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                env=env, timeout=30)
            err = proc.stderr.decode(errors="replace")
            ok = (proc.returncode == case["rc"] and
                  proc.stdout == case["out"] and
                  str(case["err"]) in err)
            detail = "rc=%s out=%r err=%r" % (proc.returncode,
                                              proc.stdout[:160], err[:220])
        print(("  ok   " if ok else "  FAIL ") + name)
        if not ok:
            failures += 1
            print("        " + detail)
    print("runtime_233_234: %d cases, %d failures" % (len(CASES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
