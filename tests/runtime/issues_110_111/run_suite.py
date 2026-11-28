#!/usr/bin/env python3
"""Regressions for runtime issues #110 and #111.

Each case compiles a tiny C program against the runtime library and asserts
its exact exit status, stdout and stderr markers. A fatal safety failure must
fail closed; a valid realloc must preserve bytes and remain race-free.
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
LIB = ROOT / "build" / "libcplus_runtime.a"
HEADER = ROOT / "runtime" / "include"

PRELUDE = """#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include "cplus/cplus.h"
"""

CASES: list[tuple[str, str, str, bytes, int, str]] = [
    ("realloc_invalid_ptr_fails_before_alloc",
     PRELUDE + """
int main(void) { void *p = cplus_realloc((void *)(uintptr_t)0x1234, 8);
  (void)p; return 0; }
""", "", b"", 104, "not a live C+ allocation"),
    ("realloc_preserves_bytes",
     PRELUDE + """
int main(void) { unsigned char *p = cplus_alloc(8);
  for (int i = 0; i < 8; ++i) p[i] = (unsigned char)(i + 1);
  p = cplus_realloc(p, 16);
  for (int i = 0; i < 8; ++i) if (p[i] != (unsigned char)(i + 1)) return 1;
  if (cplus_alloc_bytes(p) != 16) return 2;
  cplus_release(p); puts("OK"); return 0; }
""", "", b"OK\n", 0, ""),
    ("realloc_release_concurrency",
     PRELUDE + """
static void *worker(void *unused) { (void)unused;
  for (int i = 0; i < 500; ++i) { void *p = cplus_alloc(32);
    p = cplus_realloc(p, 64); cplus_release(p); }
  return NULL; }
int main(void) { pthread_t t[4];
  for (int i = 0; i < 4; ++i) pthread_create(&t[i], NULL, worker, NULL);
  for (int i = 0; i < 4; ++i) pthread_join(t[i], NULL);
  puts("OK"); return 0; }
""", "", b"OK\n", 0, ""),
    ("scan_float_too_small_fails_closed",
     PRELUDE + """
int main(void) { char buf[2]; cplus_scan_value_at(buf, sizeof(buf),
  CPLUS_SCAN_FLOAT, "f", __FILE__, __LINE__); return 0; }
""", "1.5\n", b"", 101, "not enough space"),
    ("scan_double_too_small_fails_closed",
     PRELUDE + """
int main(void) { char buf[2]; cplus_scan_value_at(buf, sizeof(buf),
  CPLUS_SCAN_DOUBLE, "d", __FILE__, __LINE__); return 0; }
""", "1.5\n", b"", 101, "not enough space"),
    ("scan_char_too_small_fails_closed",
     PRELUDE + """
int main(void) { char buf[1]; cplus_scan_value_at(buf, 0,
  CPLUS_SCAN_CHAR, "c", __FILE__, __LINE__); return 0; }
""", "x\n", b"", 101, "not enough space"),
    ("scan_string_too_small_fails_closed",
     PRELUDE + """
int main(void) { char buf[3]; cplus_scan_value_at(buf, sizeof(buf),
  CPLUS_SCAN_STR, "s", __FILE__, __LINE__); return 0; }
""", "abcdef\n", b"", 101, "not enough space"),
    ("scan_string_full_buffer_does_not_desync",
     PRELUDE + """
int main(void) { char buf[4096]; int n = 0;
  cplus_scan_value_at(buf, sizeof(buf), CPLUS_SCAN_STR, "s", __FILE__, __LINE__);
  cplus_scan_value_at(&n, sizeof(n), CPLUS_SCAN_INT, "n", __FILE__, __LINE__);
  if (n != 42) { printf("n=%d\\n", n); return 1; }
  puts("OK"); return 0; }
""", "a" * 4095 + " 42\n", b"OK\n", 0, ""),
]


def compile_case(name: str, source: str) -> Path:
    src = OUT / (name + ".c")
    exe = OUT / (name + ".bin")
    src.write_text(source)
    proc = subprocess.run(
        [CC, "-std=gnu17", "-I" + str(HEADER), str(src), str(LIB),
         "-o", str(exe), "-lpthread", "-lm"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if proc.returncode:
        raise RuntimeError(f"compile failed for {name}: {proc.stderr.decode()[-800:]}")
    return exe


def main() -> int:
    failures = 0
    for name, source, stdin_data, want_out, want_rc, want_err in CASES:
        exe = compile_case(name, source)
        payload = (stdin_data.encode() if isinstance(stdin_data, str)
                   else stdin_data)
        proc = subprocess.run([str(exe)], input=payload,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=60)
        err = proc.stderr.decode(errors="replace")
        ok = (proc.returncode == want_rc and proc.stdout == want_out and
              want_err in err)
        print(("  ok   " if ok else "  FAIL ") + name)
        if not ok:
            failures += 1
            print("        rc=%s out=%r err=%r" %
                  (proc.returncode, proc.stdout[:120], err[:300]))
    print("runtime_110_111: %d cases, %d failures" % (len(CASES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
