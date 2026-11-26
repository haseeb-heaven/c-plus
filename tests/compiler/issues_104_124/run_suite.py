#!/usr/bin/env python3
"""End-to-end regression suite for Compiler issues #104-#109 and #121-#124."""
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))

PASS = 0
FAIL = 0
FAILURES = []


def run_cmd(args, env=None, cwd=None):
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        args,
        capture_output=True,
        check=False,
        env=merged_env,
        cwd=cwd,
        timeout=60,
    )


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  ok   {name}")
    else:
        FAIL += 1
        FAILURES.append((name, detail))
        print(f"  FAIL {name}: {detail}")


def build_and_run(td, name, src, extra_flags=None, mode="--strict", sanitize="--sanitize=none"):
    cp = os.path.join(td, f"{name}.cp")
    emitted = os.path.join(td, f"{name}.c")
    exe = os.path.join(td, f"{name}.bin")
    with open(cp, "w") as f:
        f.write(src)
    cmd = [BIN, "build", mode, sanitize, "--emit-c", emitted, cp, "-o", exe]
    if extra_flags:
        cmd.extend(extra_flags)
    b = run_cmd(cmd)
    if b.returncode != 0:
        return b, None, emitted
    r = run_cmd([exe])
    return b, r, emitted


def main():
    if not os.path.exists(BIN):
        print(f"missing c+ binary at {BIN}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="cplus_104_124_") as td:
        # #104: nested try/catch preserves outer cplus_rethrow_exc (including early exits)
        src_104 = r'''#include "cplus/cplus.h"
#include <stdio.h>
static int test_normal_nested(void) {
  int caught_outer = 0;
  try {
    try {
      throw 42;
    } catch (Exception outer) {
      try {
        throw 99;
      } catch (Exception inner) {
        printf("inner=%d\n", inner.code);
      }
      throw;
    }
  } catch (Exception e) {
    caught_outer = e.code;
  }
  return caught_outer;
}
static int test_early_exit_nested(void) {
  int caught_outer = 0;
  try {
    try {
      throw 77;
    } catch (Exception outer) {
      for (int i = 0; i < 1; i++) {
        try {
          throw 88;
        } catch (Exception inner) {
          printf("break_inner=%d\n", inner.code);
          break;
        }
      }
      throw;
    }
  } catch (Exception e) {
    caught_outer = e.code;
  }
  return caught_outer;
}
int main(void) {
  int a = test_normal_nested();
  int b = test_early_exit_nested();
  printf("a=%d b=%d\n", a, b);
  return (a == 42 && b == 77) ? 0 : 1;
}
'''
        b, r, _ = build_and_run(td, "issue_104", src_104)
        out = r.stdout.decode() if r else ""
        check(
            "104_nested_catch_rethrow",
            b.returncode == 0 and r is not None and r.returncode == 0 and
            "inner=99\nbreak_inner=88\na=42 b=77\n" == out,
            f"b={b.returncode} err={b.stderr.decode()[:300]} r={r.returncode if r else None} out={out!r}",
        )

        # #105: lowerThrows handles function calls, method calls, and string-returning calls
        src_105 = r'''#include "cplus/cplus.h"
#include <stdio.h>
class ErrBox {
  int code_;
  ErrBox(int c) { this->code_ = c; }
  int code(void) { return this->code_; }
};
static int g_side = 0;
static int get_code(void) { g_side += 1; return 17; }
static int compute(int a, int b) { return a * 10 + b; }
static const char *get_msg(void) { return "call-msg"; }
int main(void) {
  int c1 = 0, c2 = 0, c3 = 0;
  const char *m = 0;
  try { throw get_code(); } catch (Exception e) { c1 = e.code; }
  try { ErrBox eb = ErrBox(23); throw eb.code(); } catch (Exception e) { c2 = e.code; }
  try { throw compute(3, 5); } catch (Exception e) { c3 = e.code; }
  try { throw get_msg(); } catch (Exception e) { m = e.message; }
  printf("%d,%d,%d,%d,%s\n", g_side, c1, c2, c3, m ? m : "null");
  return (g_side == 1 && c1 == 17 && c2 == 23 && c3 == 35 && m != 0) ? 0 : 1;
}
'''
        b, r, _ = build_and_run(td, "issue_105", src_105)
        out = r.stdout.decode() if r else ""
        check(
            "105_throw_call_expressions",
            b.returncode == 0 and r is not None and r.returncode == 0 and
            out == "1,17,23,35,call-msg\n",
            f"b={b.returncode} err={b.stderr.decode()[:300]} out={out!r}",
        )

        # #106: inner-block shadowing and cross-function non-class shadowing
        src_106 = r'''#include <stdio.h>
class Alpha {
  int v;
  Alpha(int x) { this->v = x; }
  int len(void) { return this->v; }
};
class Beta {
  int v;
  Beta(int x) { this->v = x; }
  int len(void) { return this->v * 10; }
};
static int plain_len(void) { return 99; }
struct PlainObj {
  int (*len)(void);
};
static int f_outer(void) {
  Alpha x = Alpha(5);
  {
    Beta x = Beta(7);
    if (x.len() != 70) return -1;
  }
  return x.len();
}
static int g_nonclass(void) {
  struct PlainObj x = { plain_len };
  return x.len();
}
static int h_inner_nonclass(void) {
  Alpha x = Alpha(4);
  int inner_res = 0;
  {
    struct PlainObj x = { plain_len };
    inner_res = x.len();
  }
  return inner_res + x.len();
}
int main(void) {
  int a = f_outer();
  int b = g_nonclass();
  int c = h_inner_nonclass();
  printf("%d %d %d\n", a, b, c);
  return (a == 5 && b == 99 && c == 103) ? 0 : 1;
}
'''
        b, r, _ = build_and_run(td, "issue_106", src_106)
        out = r.stdout.decode() if r else ""
        check(
            "106_class_shadowing_scoping",
            b.returncode == 0 and r is not None and r.returncode == 0 and
            out == "5 99 103\n",
            f"b={b.returncode} err={b.stderr.decode()[:300]} out={out!r}",
        )

        # #107: scoped array names and struct field arrays must not suppress nonnull checks
        src_107 = r'''#include <stddef.h>
struct Box { int p[4]; int buf[8]; };
static int helper(void) {
  int p[4] = {1, 2, 3, 4};
  return p[0];
}
static void deref_p(int *p) {
  *p = 1;
}
int main(void) {
  struct Box bx = {{0}, {0}};
  int v = helper() + bx.p[0];
  (void)v;
  deref_p(NULL);
  return 0;
}
'''
        b, r, emitted = build_and_run(td, "issue_107", src_107,
                                      extra_flags=["--no-werror"],
                                      mode="--default")
        c_text = ""
        if os.path.exists(emitted):
            with open(emitted) as f:
                c_text = f.read()
        check(
            "107_scoped_nonnull_check",
            b.returncode == 0 and "CPLUS_NONNULL(p)" in c_text and
            r is not None and r.returncode != 0,
            f"b={b.returncode} r={r.returncode if r else None} has_nonnull={'CPLUS_NONNULL(p)' in c_text}",
        )

        # #108: unique O_EXCL temp files (no predictable PID paths left behind)
        hdr = os.path.join(td, "hdr108.hp")
        with open(hdr, "w") as f:
            f.write("static int add1(int x) { return x + 1; }\n")
        src_108 = f'#include "{hdr}"\nint main(void) {{ return add1(0) - 1; }}\n'
        cp_108 = os.path.join(td, "test_108.cp")
        with open(cp_108, "w") as f:
            f.write(src_108)
        b_108 = run_cmd([BIN, "build", "--strict", "--sanitize=none", "-v",
                         cp_108, "-o", os.path.join(td, "test_108.bin")])
        v_out = b_108.stdout.decode() + b_108.stderr.decode()
        leftover = glob.glob(os.path.join(td, ".cplus-tmp-*"))
        pid_pat = re.search(rf"cplus-hp-{os.getpid()}|cplus-tmp-{os.getpid()}", v_out)
        check(
            "108_unique_temp_files_cleanup",
            b_108.returncode == 0 and not leftover and pid_pat is None,
            f"rc={b_108.returncode} leftover={leftover}",
        )

        # #109: strict-mode -D leading whitespace bypass & -Ofast rejection
        src_109 = "int main(void) { return 0; }\n"
        cp_109 = os.path.join(td, "test_109.cp")
        with open(cp_109, "w") as f:
            f.write(src_109)
        for tag, flag in [
            ("leading_space_CPLUS_STRICT", "-D CPLUS_STRICT=0"),
            ("leading_space_CPLUS", "-D CPLUS=0"),
            ("leading_space_bounds_macro", "-D __cplus_bounds_check(i,l,n)=(i)"),
            ("Ofast", "-Ofast"),
        ]:
            r_bad = run_cmd([BIN, "check", "--strict", cp_109, "--", flag])
            check(f"109_reject_{tag}", r_bad.returncode != 0,
                  f"unexpectedly succeeded with {flag}")
        r_good_o2 = run_cmd([BIN, "check", "--strict", cp_109, "--", "-O2", "-DNORMAL_OK=1"])
        check("109_allow_O2_and_normal_macro", r_good_o2.returncode == 0,
              r_good_o2.stderr.decode()[:200])

        # #121: VLA cleanup on break/continue/goto and bare `return;` in void fn
        src_121 = r'''#include <stdio.h>
#include <stddef.h>
static void void_ret_vla(int n, int early) {
  int a[n];
  a[0] = 5;
  if (early)
    return;
  else
    a[0] = 6;
}
static int loop_vla(int n) {
  int sum = 0;
  for (int i = 0; i < 6; i++) {
    int buf[n];
    buf[0] = i + 1;
    if (i == 1) continue;
    if (i == 4) break;
    sum += buf[0];
  }
  return sum;
}
int main(void) {
  void_ret_vla(3, 1);
  void_ret_vla(3, 0);
  int s = loop_vla(4);
  printf("%d\n", s);
  return s == 8 ? 0 : 1;
}
'''
        b, r, emitted = build_and_run(td, "issue_121", src_121)
        c_text = ""
        if os.path.exists(emitted):
            with open(emitted) as f:
                c_text = f.read()
        out = r.stdout.decode() if r else ""
        check(
            "121_vla_exits_and_void_return",
            b.returncode == 0 and r is not None and r.returncode == 0 and
            out == "8\n" and "});;" not in c_text and
            c_text.count("cplus_release((void *)") >= 5,
            f"b={b.returncode} err={b.stderr.decode()[:300]} out={out!r}",
        )

        src_121_goto = r'''#include <stdio.h>
static int goto_vla(int n) {
  int res = 0;
  {
    int tmp[n];
    tmp[0] = 11;
    res = tmp[0];
    goto out;
  }
out:
  return res;
}
int main(void) {
  int g = goto_vla(4);
  printf("%d\n", g);
  return g == 11 ? 0 : 1;
}
'''
        b_g, r_g, em_g = build_and_run(td, "issue_121_goto", src_121_goto,
                                       extra_flags=["--no-werror"])
        c_g = ""
        if os.path.exists(em_g):
            with open(em_g) as f:
                c_g = f.read()
        out_g = r_g.stdout.decode() if r_g else ""
        check(
            "121_vla_goto_cleanup",
            b_g.returncode == 0 and r_g is not None and r_g.returncode == 0 and
            out_g == "11\n" and "cplus_release((void *)tmp)" in c_g,
            f"b={b_g.returncode} err={b_g.stderr.decode()[:300]} out={out_g!r}",
        )

        # #122: compound VLA extents (`2 * n`, `BASE + n`, `sizeof(int) * n`, `fn() + 1`)
        src_122 = r'''#include <stdint.h>
#include <stdio.h>
#define BASE 4
static int get_n(void) { return 3; }
static int test_vlas(int n) {
  int a[2 * n];
  int b[BASE + n];
  uint8_t c[sizeof(int) * n];
  int d[get_n() + 1];
  int b_idx = 4 + n - 1;
  a[2 * n - 1] = 10;
  b[b_idx] = 20;
  c[sizeof(int) * n - 1] = 30;
  d[3] = 40;
  return a[2 * n - 1] + b[b_idx] + c[sizeof(int) * n - 1] + d[3];
}
int main(void) {
  int v = test_vlas(2);
  printf("%d\n", v);
  return v == 100 ? 0 : 1;
}
'''
        b, r, emitted = build_and_run(td, "issue_122", src_122)
        c_text = ""
        if os.path.exists(emitted):
            with open(emitted) as f:
                c_text = f.read()
        out = r.stdout.decode() if r else ""
        check(
            "122_compound_vla_extents_lowered",
            b.returncode == 0 and r is not None and r.returncode == 0 and
            out == "100\n" and c_text.count("cplus_alloc_array(") >= 4,
            f"b={b.returncode} err={b.stderr.decode()[:300]} allocs={c_text.count('cplus_alloc_array(')}",
        )

        # #123: reserved exception/scan helper names rejected in --strict
        for sym in ["cplus_exception_raise", "cplus_exception_enter",
                    "cplus_exception_pop", "cplus_scan_value_at"]:
            src_sym = f"void {sym}(void) {{}}\nint main(void) {{ return 0; }}\n"
            p_sym = os.path.join(td, f"res_{sym}.cp")
            with open(p_sym, "w") as f:
                f.write(src_sym)
            r_sym = run_cmd([BIN, "check", "--strict", p_sym])
            check(f"123_reject_def_{sym}", r_sym.returncode != 0,
                  f"allowed definition of {sym}")
        src_rethrow = "int cplus_rethrow_exc = 0;\nint main(void) { return 0; }\n"
        p_rt = os.path.join(td, "res_rethrow.cp")
        with open(p_rt, "w") as f:
            f.write(src_rethrow)
        r_rt = run_cmd([BIN, "check", "--strict", p_rt])
        check("123_reject_def_cplus_rethrow_exc", r_rt.returncode != 0,
              "allowed definition of cplus_rethrow_exc")
        for macro in ["cplus_exception_raise(...)=((void)0)", "cplus_rethrow_exc=0"]:
            r_m = run_cmd([BIN, "check", "--strict", cp_109, "--", f"-D{macro}"])
            check(f"123_reject_macro_{macro.split('(')[0].split('=')[0]}",
                  r_m.returncode != 0, f"allowed -D{macro}")

        # #124: unbraced conditional defer rejected; braced conditional & block defer work
        src_124_bad = r'''static void f(void) {}
int main(void) {
  int c = 0;
  if (c) defer f();
  return 0;
}
'''
        p_124_bad = os.path.join(td, "bad_124.cp")
        with open(p_124_bad, "w") as f:
            f.write(src_124_bad)
        r_124_bad = run_cmd([BIN, "check", "--strict", p_124_bad])
        check(
            "124_reject_unbraced_if_defer",
            r_124_bad.returncode != 0 and
            "unbraced" in (r_124_bad.stdout.decode() + r_124_bad.stderr.decode()),
            r_124_bad.stderr.decode()[:200],
        )

        src_124_good = r'''#include <stdio.h>
static int g_log = 0;
static void add_log(int v) { g_log = g_log * 10 + v; }
static void run_case(int c) {
  if (c) {
    defer add_log(1);
  }
  defer {
    add_log(2);
    add_log(3);
  }
  add_log(4);
}
int main(void) {
  run_case(0);
  int r0 = g_log;
  g_log = 0;
  run_case(1);
  int r1 = g_log;
  printf("%d %d\n", r0, r1);
  return (r0 == 423 && r1 == 1423) ? 0 : 1;
}
'''
        b, r, _ = build_and_run(td, "issue_124_good", src_124_good)
        out = r.stdout.decode() if r else ""
        check(
            "124_block_defer_and_braced_if_defer",
            b.returncode == 0 and r is not None and r.returncode == 0 and
            out == "423 1423\n",
            f"b={b.returncode} err={b.stderr.decode()[:300]} out={out!r}",
        )

    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
