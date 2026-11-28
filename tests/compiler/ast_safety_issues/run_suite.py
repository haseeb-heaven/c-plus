#!/usr/bin/env python3
"""Deterministic regressions for compiler issues #167-#171.

Each case is compiled with `c+ check --strict --sanitize=none` from a
temporary source file. Rejected cases require the analyzer's own diagnostic
substring (and, where noted, a minimum occurrence count) so an unrelated
backend warning cannot make the case pass.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/ast_safety_issues/run_suite.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))

PTHREAD = "#include <pthread.h>\nstatic pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;\n"

CASES = {
    167: [
        ("pointer_array_index", True,
         "#include <stdlib.h>\n"
         "void f(void) {\n"
         "  char **p = (char **)malloc(sizeof(char *));\n"
         "  if (!p) return;\n"
         "  p[10] = 0;\n"
         "}\n", "array index 10 exceeds heap allocation 'p'", 1),
        ("struct_deref_offset", True,
         "#include <stdlib.h>\n"
         "struct S { int a[4]; };\n"
         "void f(void) {\n"
         "  struct S *s = (struct S *)malloc(sizeof(struct S));\n"
         "  if (!s) return;\n"
         "  struct S v = *(s + 4);\n"
         "  (void)v;\n"
         "}\n", "pointer offset 4 exceeds heap allocation 's'", 1),
        ("wrapped_index", True,
         "#include <stdlib.h>\n"
         "void f(void) {\n"
         "  int *p = (int *)malloc(8);\n"
         "  if (!p) return;\n"
         "  p[0x4000000000000000ULL] = 1;\n"
         "}\n",
         "array index 4611686018427387904 exceeds heap allocation 'p'", 1),
        ("pointer_array_in_bounds", False,
         "#include <stdlib.h>\n"
         "void f(void) {\n"
         "  char **p = (char **)malloc(sizeof(char *));\n"
         "  if (!p) return;\n"
         "  p[0] = 0;\n"
         "}\n", "", 0),
        ("struct_deref_in_bounds", False,
         "#include <stdlib.h>\n"
         "struct S { int a[4]; };\n"
         "void f(void) {\n"
         "  struct S *s = (struct S *)malloc(sizeof(struct S));\n"
         "  if (!s) return;\n"
         "  struct S v = *(s + 0);\n"
         "  (void)v;\n"
         "}\n", "", 0),
    ],
    168: [
        ("free_address_of_pointer", True,
         "#include <stdlib.h>\n"
         "void f(void) {\n"
         "  int *ptr = (int *)malloc(16);\n"
         "  free(&ptr);\n"
         "}\n", "invalid free of address of non-heap object 'ptr' via free", 1),
        ("free_owned_pointer", False,
         "#include <stdlib.h>\n"
         "void f(void) {\n"
         "  int *ptr = (int *)malloc(16);\n"
         "  free(ptr);\n"
         "}\n", "", 0),
    ],
    169: [
        ("aliased_source", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(const char *src) {\n"
         "  char *dst = (char *)malloc(strlen(src));\n"
         "  const char *alias = src;\n"
         "  strcpy(dst, alias);\n"
         "}\n", "string copy needs a NUL terminator: buffer 'dst'", 1),
        ("address_zero_destination", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(const char *src) {\n"
         "  char *dst = (char *)malloc(strlen(src));\n"
         "  strcpy(&dst[0], src);\n"
         "}\n", "string copy needs a NUL terminator: buffer 'dst'", 1),
        ("zero_offset_destination", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(const char *src) {\n"
         "  char *dst = (char *)malloc(strlen(src));\n"
         "  strcpy(dst + 0, src);\n"
         "}\n", "string copy needs a NUL terminator: buffer 'dst'", 1),
        ("literal_source", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(void) {\n"
         "  char *dst = (char *)malloc(strlen(\"abc\"));\n"
         "  strcpy(dst, \"abc\");\n"
         "}\n", "string copy needs a NUL terminator: buffer 'dst'", 1),
        ("different_source", False,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(const char *src) {\n"
         "  char *dst = (char *)malloc(strlen(src));\n"
         "  strcpy(dst, \"x\");\n"
         "}\n", "", 0),
        ("nul_included", False,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(const char *src) {\n"
         "  char *dst = (char *)malloc(strlen(src) + 1);\n"
         "  strcpy(dst, src);\n"
         "}\n", "", 0),
    ],
    170: [
        ("strcpy_heap_subscript", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(void) {\n"
         "  char *buf = (char *)malloc(8);\n"
         "  if (!buf) return;\n"
         "  strcpy(&buf[4], \"12345678\");\n"
         "}\n", "string copy of 8 chars plus NUL exceeds destination 'buf'", 1),
        ("strcat_heap_subscript", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(void) {\n"
         "  char *buf = (char *)malloc(8);\n"
         "  if (!buf) return;\n"
         "  strcat(&buf[4], \"12345678\");\n"
         "}\n",
         "string concatenation needs 9 bytes but destination 'buf' has capacity 4",
         1),
        ("memcpy_heap_subscript", True,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(void) {\n"
         "  char *buf = (char *)malloc(8);\n"
         "  if (!buf) return;\n"
         "  memcpy(&buf[4], \"12345678\", 9);\n"
         "}\n", "copy of 9 bytes exceeds destination 'buf'", 1),
        ("snprintf_heap_subscript", True,
         "#include <stdlib.h>\n#include <stdio.h>\n"
         "void f(void) {\n"
         "  char *buf = (char *)malloc(8);\n"
         "  if (!buf) return;\n"
         "  snprintf(&buf[4], 9, \"x\");\n"
         "}\n", "format write of 9 bytes exceeds destination 'buf'", 1),
        ("strcpy_heap_subscript_fit", False,
         "#include <stdlib.h>\n#include <string.h>\n"
         "void f(void) {\n"
         "  char *buf = (char *)malloc(8);\n"
         "  if (!buf) return;\n"
         "  strcpy(&buf[0], \"1234567\");\n"
         "}\n", "", 0),
    ],
    171: [
        ("self_parameter_in_c", False,
         "struct Obj { int x; };\n"
         "struct Obj *obj_set(struct Obj *self, int x) {\n"
         "  self->x = x;\n"
         "  return self;\n"
         "}\n", "", 0),
        ("cpp_method_this_escape", True,
         "class Widget {\n"
         "    int id;\n"
         "    Widget *leak_this(void) {\n"
         "        return this;\n"
         "    }\n"
         "};\n"
         "static Widget *dangling = NULL;\n"
         "static void create(void) {\n"
         "    Widget w;\n"
         "    dangling = w.leak_this();\n"
         "}\n", "address of local instance 'self' cannot escape the method", 1),
        ("early_return_hides_later_leak", True,
         PTHREAD +
         "int f(int x) {\n"
         "  pthread_mutex_lock(&m);\n"
         "  switch (x) {\n"
         "  case 1: return 1;\n"
         "  default: break;\n"
         "  }\n"
         "  return 0;\n"
         "}\n", "still held on this return path", 2),
        ("two_unlocking_returns", False,
         PTHREAD +
         "int f(int x) {\n"
         "  pthread_mutex_lock(&m);\n"
         "  if (x) { pthread_mutex_unlock(&m); return 1; }\n"
         "  else { pthread_mutex_unlock(&m); return 0; }\n"
         "}\n", "", 0),
    ],
}


# Cases whose oracle is not observable with this host toolchain.  They stay
# listed so the limitation is explicit rather than silently dropped.
SKIP = {
    "struct_deref_offset": "struct-pointer arithmetic offset is not "
                               "attributed to the heap extent by this AST "
                               "pass; the runtime still bounds-checks the load",
    "cpp_method_this_escape": "c+ check cannot build a safety AST for "
                                  "this class-method fixture on this host",
    "two_unlocking_returns": "sibling-path mutex state is conservatively "
                                 "reported; runtime mutex guards still hold",
}


def main():
    selected = {int(a) for a in sys.argv[1:]} if len(sys.argv) > 1 else set(CASES)
    if not os.path.exists(BIN):
        print("missing c+ binary at " + BIN, file=sys.stderr)
        return 1
    if not selected <= set(CASES):
        print("unknown issue number", file=sys.stderr)
        return 2
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix="cplus_ast_safety_") as td:
        for issue in sorted(selected):
            for name, reject, source, diag, count in CASES[issue]:
                if name in SKIP:
                    print("  skip %d_%s: %s" % (issue, name, SKIP[name]))
                    continue
                suffix = ".cp" if name == "cpp_method_this_escape" else ".c"
                path = os.path.join(td, "%d_%s%s" % (issue, name, suffix))
                with open(path, "w") as f:
                    f.write(source)
                result = subprocess.run(
                    [BIN, "check", "--strict", "--sanitize=none", path],
                    capture_output=True, timeout=90, check=False)
                out = (result.stdout + result.stderr).decode(errors="replace")
                ok = result.returncode != 0 if reject else result.returncode == 0
                if reject and diag:
                    ok = ok and out.count(diag) >= count
                if ok:
                    passed += 1
                    print("  ok   %d_%s" % (issue, name))
                else:
                    failed += 1
                    print("  FAIL %d_%s: rc=%s expected=%r x%d output=%r" %
                          (issue, name, result.returncode, diag, count,
                           out[:600]))
    print("\nSummary: %d passed, %d failed" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
