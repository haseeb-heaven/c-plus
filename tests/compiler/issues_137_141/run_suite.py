#!/usr/bin/env python3
"""End-to-end regressions for compiler issues #137-#141."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
PASS = 0
FAIL = 0


def run(args):
    return subprocess.run(args, capture_output=True, check=False, timeout=60)


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  ok   {name}")
    else:
        FAIL += 1
        print(f"  FAIL {name}: {detail}")


def source(name, body, headers=""):
    return headers + body.replace("MAIN", "main(void)")


CASES = {
    137: [
        ("strict_rejects_x", True, "int MAIN { return 0; }\n", ["--strict", "-x", "c"], "-x"),
        ("invalid_x_language", True, "int MAIN { return 0; }\n", ["-x", "source.c"], "invalid language"),
        ("missing_x_value", True, "int MAIN { return 0; }\n", ["-x"], "requires a language"),
        ("missing_emit_c_value", True, "int MAIN { return 0; }\n", ["--emit-c"], "requires a value"),
        ("default_accepts_x_c", False, "int MAIN { return 0; }\n", ["-x", "c"], ""),
    ],
    138: [
        ("heap_strcat_overflow", True, r'''#include <stdlib.h>
#include <string.h>
int MAIN {
  char *p = malloc(8);
  if (!p) return 2;
  strcpy(p, "12345");
  strcat(p, "67890");
  free(p);
  return 0;
}
''', [], "string concatenation"),
        ("heap_strcat_exact_fit", False, r'''#include <stdlib.h>
#include <string.h>
int MAIN {
  char *p = malloc(8);
  if (!p) return 2;
  strcpy(p, "12345");
  strcat(p, "67");
  free(p);
  return 0;
}
''', [], ""),
        ("heap_strcat_unknown_contents", True, r'''#include <stdlib.h>
#include <string.h>
int MAIN {
  char *p = malloc(8);
  if (!p) return 2;
  strcat(p, "12345678");
  free(p);
  return 0;
}
''', [], "string concatenation"),
        ("strcpy_pointer_offset_overflow", True, r'''#include <string.h>
int MAIN {
  char buf[8];
  strcpy(buf + 4, "hello");
  return 0;
}
''', [], ""),
        ("strcpy_address_offset_overflow", True, r'''#include <string.h>
int MAIN {
  char buf[8];
  strcpy(&buf[4], "hello");
  return 0;
}
''', [], ""),
        ("strcpy_heap_offset_overflow", True, r'''#include <stdlib.h>
#include <string.h>
int MAIN {
  char *p = malloc(8);
  if (!p) return 2;
  strcpy(p + 4, "hello");
  free(p);
  return 0;
}
''', [], "string copy"),
        ("strcpy_pointer_offset_exact_fit", False, r'''#include <string.h>
int MAIN {
  char buf[8];
  strcpy(buf + 4, "abc");
  return 0;
}
''', [], ""),
    ],
    139: [
        ("disjoint_forward_copy", False, r'''#include <string.h>
int MAIN {
  char buf[64] = {0};
  memcpy(buf, buf + 16, 8);
  return 0;
}
''', [], ""),
        ("disjoint_reverse_copy", False, r'''#include <string.h>
int MAIN {
  char buf[64] = {0};
  memcpy(buf + 8, buf, 4);
  return 0;
}
''', [], ""),
        ("overlapping_copy", True, r'''#include <string.h>
int MAIN {
  char buf[64] = {0};
  memcpy(buf, buf + 2, 8);
  return 0;
}
''', [], "overlap"),
        ("aliased_overlapping_copy", True, r'''#include <string.h>
int MAIN {
  char buf[64] = {0};
  char *q = buf + 2;
  memcpy(buf, q, 8);
  return 0;
}
''', [], "overlap"),
        ("address_aliased_overlapping_copy", True, r'''#include <string.h>
int MAIN {
  char buf[64] = {0};
  memcpy(buf, &buf[2], 8);
  return 0;
}
''', [], "overlap"),
        ("zero_byte_copy", False, r'''#include <string.h>
int MAIN {
  char buf[8] = {0};
  memcpy(buf, buf, 0);
  return 0;
}
''', [], ""),
    ],
    140: [
        ("const_global_mutation", True, r'''static const int g = 1;
int MAIN { *(int *)&g = 2; return 0; }
''', [], "const"),
        ("const_parameter_mutation", True, r'''static int change(const int x) {
  int *p = (int *)&x;
  *p = 2;
  return 0;
}
int MAIN { return change(1); }
''', [], "const"),
        ("const_pointee_mutation", True, r'''static const int g = 1;
int MAIN {
  const int *p = &g;
  int *q = (int *)p;
  *q = 2;
  return 0;
}
''', [], "const"),
        ("const_read", False, r'''static const int g = 1;
int MAIN { const int *p = &g; return *p == 1 ? 0 : 1; }
''', [], ""),
        ("mutable_mutation", False, r'''int MAIN { int x = 1; int *p = &x; *p = 2; return x == 2 ? 0 : 1; }
''', [], ""),
    ],
    141: [
        ("array_address_unaligned", True, r'''#include <stdint.h>
int MAIN { char buf[16] = {0}; (void)(uint32_t *)&buf[1]; return 0; }
''', [], "unaligned"),
        ("alias_unaligned", True, r'''#include <stdint.h>
int MAIN { char buf[16] = {0}; char *q = buf + 1; (void)(uint32_t *)q; return 0; }
''', [], "unaligned"),
        ("chained_cast_unaligned", True, r'''int MAIN { char buf[16] = {0}; (void)(int *)((char *)buf + 1); return 0; }
''', [], "unaligned"),
        ("array_address_aligned", False, r'''#include <stdint.h>
int MAIN { char buf[16] = {0}; (void)(uint32_t *)&buf[4]; return 0; }
''', [], ""),
        ("alias_aligned", False, r'''#include <stdint.h>
int MAIN { char buf[16] = {0}; char *q = buf + 4; (void)(uint32_t *)q; return 0; }
''', [], ""),
    ],
}


def main():
    selected = {int(arg) for arg in sys.argv[1:]} if len(sys.argv) > 1 else set(CASES)
    if not os.path.exists(BIN):
        print(f"missing c+ binary at {BIN}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="cplus_137_141_") as td:
        for issue in sorted(selected):
            if issue not in CASES:
                print(f"unknown issue {issue}", file=sys.stderr)
                return 2
            for name, reject, text, flags, diagnostic in CASES[issue]:
                path = os.path.join(td, f"{issue}_{name}.c")
                with open(path, "w") as f:
                    f.write(source(name, text))
                args = [BIN, "check", "--sanitize=none", path]
                if issue == 137:
                    if name == "missing_x_value":
                        args = [BIN, "-x"]
                    elif name == "missing_emit_c_value":
                        args = [BIN, "--emit-c"]
                    else:
                        args = [BIN, "check", "--sanitize=none", *flags, path]
                result = run(args)
                output = (result.stdout + result.stderr).decode(errors="replace")
                ok = result.returncode != 0 if reject else result.returncode == 0
                if reject and diagnostic:
                    ok = ok and diagnostic in output
                check(f"{issue}_{name}", ok,
                      f"rc={result.returncode} output={output[:400]!r}")
    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
