#!/usr/bin/env python3
"""Deterministic differential compiler fuzzer for C+ (legacy C mode).

This fuzzer *generates* well-formed, side-effect-free-to-the-world C programs
from a fixed seed and compares the observable behaviour of two toolchains:

  * the C+ compiler under test, in ``--legacy-c`` mode
  * a reference host compiler (clang, or gcc if selected/available)

For every generated program both compilers are asked to produce a native
executable.  Compile outcomes, program stdout and process exit status are
compared.  Any divergence is recorded as a mismatch, the exact seed and
original source are saved, the program is minimized with delta debugging
(chunk-wise line deletion, ddmin style) while the mismatch still reproduces,
and the failure is classified as one of:

  parser    - one toolchain rejects the source at lex/parse time, the other accepts
  sema      - one toolchain rejects the source on semantic/type grounds, the other accepts
  codegen   - both compile and exit normally, but stdout / exit status differ
  runtime   - one binary crashes, aborts or times out while the other runs
  optimizer - a codegen/runtime mismatch that only appears at one optimization level

Determinism:
  The program list is a pure function of ``FUZZ_SEED`` and ``FUZZ_CASES``.
  Python's ``random.Random(seed)`` is used (never the module-level RNG, never
  ``hash()``, never the clock).  Re-running with the same seed regenerates
  byte-for-byte identical sources.  A failure stores its *per-case seed*; the
  exact case can be reproduced with ``FUZZ_SEED=<case_seed> FUZZ_CASES=1``.

Environment variables
  CPLUS_BIN           C+ driver (default <repo>/build/c+)
  REF_CC              Reference compiler (default: clang, fallback gcc)
  FUZZ_SEED           Base seed, parsed with int(x, 0) (default 0x5EED1234)
  FUZZ_CASES          Number of programs to generate (default 200)
  FUZZ_WORKERS        Parallel comparison workers (default 8)
  FUZZ_TIMEOUT        Per compile / run timeout in seconds (default 30)
  FUZZ_OPT            Primary optimization level: O0 or O2 (default O2)
  FUZZ_MIN_BUDGET     Max minimizer predicate checks per failure (default 90)
  FUZZ_MAX_MINIMIZED  Max failures to attempt minimizing (default 20)

No files in ``build/`` are read or written except executing the supplied
``CPLUS_BIN``.  All artefacts are written under ``tests/compiler/fuzz/results``.
"""
import concurrent.futures
import json
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parents[3]
HERE = pathlib.Path(__file__).resolve().parent
RESULTS = pathlib.Path(os.environ.get("FUZZ_RESULTS_DIR", str(HERE / "results")))
FAILURES = RESULTS / "failures"

CPLUS_BIN = os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+"))
SEED = int(os.environ.get("FUZZ_SEED", "0x5EED1234"), 0)
CASES = int(os.environ.get("FUZZ_CASES", "200"))
WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
TIMEOUT = float(os.environ.get("FUZZ_TIMEOUT", "30"))
OPT = os.environ.get("FUZZ_OPT", "O2")
MIN_BUDGET = int(os.environ.get("FUZZ_MIN_BUDGET", "60"))
MAX_MINIMIZED = int(os.environ.get("FUZZ_MAX_MINIMIZED", "12"))


def detect_sdk():
    """macOS reference compilers need an explicit -isysroot (see metrics/)."""
    try:
        p = subprocess.run(["xcrun", "--show-sdk-path"], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=10)
        path = p.stdout.decode("utf-8", "replace").strip()
        if p.returncode == 0 and path and pathlib.Path(path).is_dir():
            return path
    except (OSError, subprocess.TimeoutExpired):
        pass
    return None


SDK = os.environ.get("CPLUS_FUZZ_SDK") or detect_sdk()

STATUSES = ("parser", "sema", "codegen", "runtime", "optimizer")


def pick_reference():
    """Choose a reference C compiler deterministically."""
    requested = os.environ.get("REF_CC")
    candidates = []
    if requested:
        candidates.append(requested)
    candidates += ["clang", "gcc", "cc"]
    seen = set()
    for name in candidates:
        path = shutil.which(name)
        if path and path not in seen:
            seen.add(path)
            return name, path
    return None, None


# ---------------------------------------------------------------------------
# Program generator.  Every emitted program is self-contained C with a main()
# that prints deterministic lines.  Statements are emitted one-per-line so
# line-based delta debugging can delete them cleanly.
# ---------------------------------------------------------------------------
class ProgramGen:
    INT_LITS = ["0", "1", "2", "3", "4", "5", "6", "7", "10", "12"]
    INT_VARS = ["vi", "vj", "vii", "vjj", "vk", "vu", "vs", "vc", "vuc"]

    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.seed = seed

    def r(self, seq):
        return self.rng.choice(seq)

    def iexpr(self, depth=0):
        """Small, well-defined integer expression."""
        if depth >= 2 or self.rng.random() < 0.35:
            return self.r(
                self.INT_LITS
                + self.INT_VARS
                + ["sizeof(int)", "sizeof(char)", "E1", "h_add(2,3)", "h_sum3(g_arr)"]
            )
        kind = self.rng.randrange(10)
        a = self.iexpr(depth + 1)
        if kind == 0:
            return "(%s + %s)" % (a, self.iexpr(depth + 1))
        if kind == 1:
            return "(%s - %s)" % (a, self.rng.randint(0, 4))
        if kind == 2:
            return "(%s * %d)" % (a, self.rng.randint(0, 4))
        if kind == 3:
            return "(%s / %d)" % (a, self.rng.randint(1, 5))
        if kind == 4:
            return "(%s %% %d)" % (a, self.rng.randint(1, 5))
        if kind == 5:
            return "(%s << %d)" % (a, self.rng.randint(0, 2))
        if kind == 6:
            return "(%s & %d)" % (a, self.rng.randint(0, 7))
        if kind == 7:
            return "(%s | %d)" % (a, self.rng.randint(0, 7))
        if kind == 8:
            return "(%s ^ %d)" % (a, self.rng.randint(0, 7))
        return "(%s ? %s : %d)" % (a, self.iexpr(depth + 1), self.rng.randint(0, 9))

    def cexpr(self, depth=0):
        """Comparison / logical expression with well-defined operands."""
        a, b = self.iexpr(depth), self.iexpr(depth)
        op = self.r(["==", "!=", "<", "<=", ">", ">=", "&&", "||"])
        return "(%s %s %s)" % (a, op, b)

    def dexpr(self, depth=0):
        """Floating-point expression (small, nonzero divisors)."""
        if depth >= 2 or self.rng.random() < 0.4:
            return self.r(["1.0", "2.0", "3.5", "0.5", "(double)vi", "(double)sizeof(int)"])
        a = self.dexpr(depth + 1)
        op = self.r(["+", "-", "*", "/"])
        b = self.dexpr(depth + 1) if op != "/" else self.r(["2.0", "4.0", "(double)3"])
        return "(%s %s %s)" % (a, op, b)

    def pexpr(self):
        """Bounded in-range pointer expression into a global int array."""
        idx = self.rng.randint(0, 3)
        form = self.rng.randrange(6)
        if form == 0:
            return "g_arr[%d]" % idx
        if form == 1:
            return "*(&g_arr[%d])" % idx
        if form == 2:
            return "pi[%d]" % idx
        if form == 3:
            return "*pi"
        if form == 4:
            return "*(pc + %d)" % (idx * 4)
        return "g_arr[%d] + g_arr[%d]" % (idx, (idx + 1) % 4)

    # --- individual statement families ------------------------------------
    def s_expr(self):
        e = self.iexpr()
        return 'printf("E %%d\\n", (int)(%s));' % e

    def s_fexpr(self):
        return 'printf("F %%.3f\\n", %s);' % self.dexpr()

    def s_compare(self):
        return 'printf("C %%d\\n", %s);' % self.cexpr()

    def s_ptr(self):
        idx = self.rng.randint(0, 3)
        stmts = [
            'pi = &g_arr[%d]; printf("P %%d\\n", *pi);' % idx,
            'printf("P %%d\\n", %s);' % self.pexpr(),
            'printf("P %%d\\n", (int)(pi - g_arr) + %d);' % idx,
            'pc = (char *)&g_arr[%d]; printf("P %%d\\n", (int)*pc);' % idx,
        ]
        return self.r(stmts)

    def s_struct(self):
        return self.r([
            'gs.x = %s; gs.y = %s; printf("S %%d\\n", gs.x + gs.y);'
            % (self.rng.randint(0, 20), self.rng.randint(0, 20)),
            'ls.x = %s; ls.y = %s; printf("S %%d\\n", ls.x - ls.y);'
            % (self.rng.randint(0, 20), self.rng.randint(0, 20)),
            'printf("S %d\\n", gs.x * 2 + ls.y);',
            'printf("S %d\\n", (int)gs.d + gs.x);',
        ])

    def s_union(self):
        return self.r([
            'gu.i = %d; printf("U %%d\\n", gu.i);' % self.rng.randint(0, 255),
            'gu.i = 0x41424344; printf("U %d\\n", (int)gu.c[0]);',
            'lu.i = %d; printf("U %%d\\n", lu.i %% 7);' % self.rng.randint(0, 999),
        ])

    def s_init(self):
        return self.r([
            'int ia[3] = {%s, %s, %s}; printf("I %%d\\n", ia[0]+ia[1]+ia[2]);'
            % (self.rng.randint(0, 5), self.rng.randint(0, 5), self.rng.randint(0, 5)),
            'struct P sp = {%s, %s}; printf("I %%d\\n", sp.x+sp.y);'
            % (self.rng.randint(0, 9), self.rng.randint(0, 9)),
            'struct P sd = {.y=%s, .x=%s}; printf("I %%d\\n", sd.x*10+sd.y);'
            % (self.rng.randint(0, 9), self.rng.randint(0, 9)),
            'char cs[4] = "abc"; printf("I %d\\n", (int)cs[1]);',
            'int sc = %s; printf("I %%d\\n", sc);' % self.rng.randint(0, 99),
        ])

    def s_call(self):
        return self.r([
            'printf("H %%d\\n", h_add(%s, %s));'
            % (self.rng.randint(0, 20), self.rng.randint(0, 20)),
            'printf("H %%d\\n", h_mul(%s, %s));'
            % (self.rng.randint(1, 6), self.rng.randint(1, 6)),
            'printf("H %d\\n", h_sum3(g_arr));',
            'printf("H %%d\\n", fib(%s));' % self.rng.randint(0, 9),
            'printf("H %%d\\n", fp(%s, %s));'
            % (self.rng.randint(0, 15), self.rng.randint(0, 15)),
        ])

    def s_control(self):
        k = self.rng.randrange(6)
        if k == 0:
            cond = self.cexpr()
            return 'if(%s) printf("K %%d\\n", 1); else printf("K %%d\\n", 0);' % cond
        if k == 1:
            n = self.rng.randint(2, 4)
            return ('for(int i0=0;i0<%d;i0++){ if(i0==%d) continue; printf("K %%d\\n", i0); }'
                    % (n, self.rng.randint(0, n - 1)))
        if k == 2:
            n = self.rng.randint(2, 4)
            return ('int w0=0; while(w0<%d){ printf("K %%d\\n", w0); w0++; }' % n)
        if k == 3:
            n = self.rng.randint(2, 4)
            return ('int d0=0; do{ printf("K %%d\\n", d0); d0++; }while(d0<%d);' % n)
        if k == 4:
            c = self.rng.randint(0, 3)
            return ('switch(%d){case 0: printf("K %%d\\n", 10);break; '
                    'case 1: printf("K %%d\\n", 11);break; '
                    'case 2: printf("K %%d\\n", 12);break; '
                    'default: printf("K %%d\\n", 13);}' % c)
        return ('for(int i0=0;i0<3;i0++){ for(int j0=0;j0<2;j0++){ '
                'printf("K %d\\n", i0*10+j0); if(j0==1) break; } }')

    def s_types(self):
        return self.r([
            'printf("T %d\\n", (int)sizeof(short) + (int)sizeof(long));',
            'printf("T %d\\n", (int)(unsigned char)300);',
            'printf("T %d\\n", (int)(signed char)200);',
            'printf("T %d\\n", (int)(long long)123456);',
            'printf("T %%d\\n", (int)(enum E)(%s));' % self.rng.randint(0, 2),
            'printf("T %d\\n", (int)(double)7 / 2);',
        ])

    def generate(self):
        rng = self.rng
        lines = []
        lines.append("/* deterministic fuzz case seed=0x%X */" % (self.seed & 0xFFFFFFFF))
        lines.append("#include <stdio.h>")
        lines.append("enum E { E0=0, E1=1, E2=2 };")
        lines.append("struct P { int x; int y; double d; };")
        lines.append("union U { int i; char c[4]; };")
        lines.append("typedef int (*binop_t)(int,int);")
        lines.append("static int h_add(int a,int b){ return a+b; }")
        lines.append("static int h_mul(int a,int b){ return a*b; }")
        lines.append("static int h_sum3(int *p){ return p[0]+p[1]+p[2]+p[3]; }")
        lines.append("static int fib(int n){ if(n<2) return n; return fib(n-1)+fib(n-2); }")
        lines.append("int g_arr[4] = {1,2,3,4};")
        lines.append("struct P gs = {1,2,3.0};")
        lines.append("union U gu = {0};")
        lines.append("int main(void){")
        lines.append("  int vi=4, vj=2, vii=6, vjj=3, vk=1;")
        lines.append("  unsigned vu=5; short vs=3; char vc=2; unsigned char vuc=7;")
        lines.append("  double vd=2.5; int *pi=g_arr; char *pc=(char*)g_arr;")
        lines.append("  struct P ls = {4,5,6.0}; union U lu = {7};")
        lines.append("  binop_t fp = h_add;")
        builders = [
            self.s_expr, self.s_fexpr, self.s_compare, self.s_ptr,
            self.s_struct, self.s_union, self.s_init, self.s_call,
            self.s_control, self.s_types,
        ]
        weights = [18, 8, 8, 10, 8, 5, 8, 10, 14, 6]
        count = rng.randint(18, 30)
        for _ in range(count):
            # Wrap each statement in its own block so repeated declarations
            # (ia, cs, d0, ...) never collide regardless of selection order.
            stmt = rng.choices(builders, weights=weights, k=1)[0]()
            lines.append("  { %s }" % stmt)
        lines.append("  return 0;")
        lines.append("}")
        return "\n".join(lines) + "\n"


def generate_case(case_seed):
    return ProgramGen(case_seed).generate()


# ---------------------------------------------------------------------------
# Compilation / execution primitives
# ---------------------------------------------------------------------------
def run_cmd(argv, timeout):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
        out = p.stdout.decode("utf-8", "replace")
        err = p.stderr.decode("utf-8", "replace")
        return p.returncode, out, err, False
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, (bytes, bytearray)) else ""
        err = (e.stderr or b"").decode("utf-8", "replace") if isinstance(e.stderr, (bytes, bytearray)) else ""
        return 124, out, err, True


def cplus_opt_args():
    return ["--release"] if OPT == "O2" else ["--debug"]


def ref_opt_args():
    return ["-O2"] if OPT == "O2" else ["-O0"]


def opposite_opt_args(which):
    """Arguments for the *other* optimization level."""
    if which == "cplus":
        return ["--debug"] if OPT == "O2" else ["--release"]
    return ["-O0"] if OPT == "O2" else ["-O2"]


def build_and_run(src_path, exe_path, compiler, is_cplus, opt_args, timeout):
    """Compile one source and, on success, execute it.

    Returns a dict describing compile outcome and (when compiled) run outcome.
    """
    # -w keeps diagnostics out of the comparison; correctness is judged from
    # compile success/failure and from program stdout + exit status only.
    if is_cplus:
        argv = [compiler, "build", "--legacy-c", "-w"] + list(opt_args) + [str(src_path), "-o", str(exe_path)]
    else:
        sdk_args = ["-isysroot", SDK] if SDK else []
        argv = [compiler] + sdk_args + ["-w"] + list(opt_args) + [str(src_path), "-o", str(exe_path)]
    rc, out, err, timed = run_cmd(argv, timeout)
    info = {
        "compile_rc": rc, "compile_timed_out": timed,
        "compile_stdout": out, "compile_stderr": err,
        "compiled": rc == 0 and not timed,
    }
    if not info["compiled"]:
        info.update({"ran": False, "run_rc": None, "run_stdout": "", "run_stderr": "",
                     "run_timed_out": False})
        return info
    rrc, rout, rerr, rtimed = run_cmd([str(exe_path)], timeout)
    info.update({"ran": True, "run_rc": rrc, "run_stdout": rout,
                 "run_stderr": rerr, "run_timed_out": rtimed})
    return info


PARSER_RE = re.compile(
    r"syntax error|expected ['\"a-zA-Z]|unexpected token|parse error|"
    r"stray |missing terminating|expected declaration|expected expression", re.I)
SEMA_RE = re.compile(
    r"error:|undeclared|incompatible|invalid type|unknown type|"
    r"conflicting types|too few|too many|redefinition", re.I)


def classify(ci, ri):
    """Classify a C+ vs reference divergence."""
    if ci["compiled"] != ri["compiled"]:
        failed = ci if not ci["compiled"] else ri
        text = (failed.get("compile_stderr", "") + "\n" + failed.get("compile_stdout", ""))
        if failed.get("compile_timed_out"):
            return "runtime"
        err_lines = [ln for ln in text.splitlines() if "error" in ln.lower()]
        focus = "\n".join(err_lines) if err_lines else text
        if PARSER_RE.search(focus):
            return "parser"
        return "sema"
    if ci["compiled"] and ri["compiled"]:
        c_crash = ci["run_timed_out"] or ci["run_rc"] not in (0,) or ci["run_rc"] is None
        r_crash = ri["run_timed_out"] or ri["run_rc"] not in (0,) or ri["run_rc"] is None
        if c_crash != r_crash:
            return "runtime"
        if ci["run_stdout"] != ri["run_stdout"] or ci["run_rc"] != ri["run_rc"]:
            return "codegen"
    return None


def diverged(ci, ri):
    return classify(ci, ri) is not None


FILE_BASENAME_RE = re.compile(r"[^A-Za-z0-9_.-]+")


def relpath(path):
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def compare_source(tmpdir, tag, src, reference, reference_name, opt_args_cplus,
                   opt_args_ref, timeout, classification_only=False):
    """Write *src* to a file, build+run with both toolchains, return comparison."""
    safe = FILE_BASENAME_RE.sub("_", tag)[:60]
    src_path = pathlib.Path(tmpdir) / (safe + ".c")
    src_path.write_text(src)
    cp_exe = pathlib.Path(tmpdir) / (safe + ".cp.exe")
    rf_exe = pathlib.Path(tmpdir) / (safe + ".rf.exe")
    for exe in (cp_exe, rf_exe):
        if exe.exists():
            try:
                exe.unlink()
            except OSError:
                pass
    ci = build_and_run(src_path, cp_exe, CPLUS_BIN, True, opt_args_cplus, timeout)
    ri = build_and_run(src_path, rf_exe, reference, False, opt_args_ref, timeout)
    cls = classify(ci, ri)
    return {
        "cplus": ci, "reference": ri, "classification": cls,
        "diverged": cls is not None, "src_path": str(src_path),
    }


# ---------------------------------------------------------------------------
# Delta-debugging minimizer: delete line chunks while the mismatch persists.
# ---------------------------------------------------------------------------
def minimize_source(original, target_class, make_predicate, budget):
    """Return (minimized_source, checks_used, converged)."""
    checks = [0]

    def test(lines):
        if checks[0] >= budget:
            return False
        checks[0] += 1
        return make_predicate("".join(lines))

    lines = original.splitlines(keepends=True)
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    n = len(lines)
    granularity = 2
    while n >= 2 and checks[0] < budget:
        chunk = max(1, n // granularity)
        start = 0
        reduced = False
        while start < n and checks[0] < budget:
            candidate = lines[:start] + lines[start + chunk:]
            if test(candidate):
                lines = candidate
                n = len(lines)
                granularity = max(2, granularity - 1)
                reduced = True
                break
            start += chunk
        if not reduced:
            if granularity >= n:
                break
            granularity = min(n, granularity * 2)
    # Final pass: try removing each single remaining line.
    i = 0
    while i < len(lines) and checks[0] < budget:
        candidate = lines[:i] + lines[i + 1:]
        if test(candidate):
            lines = candidate
        else:
            i += 1
    return "".join(lines), checks[0], checks[0] < budget


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------
def main():
    global OPT
    results_dir = RESULTS
    failures_dir = FAILURES
    results_dir.mkdir(parents=True, exist_ok=True)
    failures_dir.mkdir(parents=True, exist_ok=True)
    # Clear artefacts from a previous run so the results directory always
    # describes exactly this seed / this run.
    for stale in failures_dir.glob("case_seed_*"):
        if stale.is_file():
            stale.unlink()

    if not pathlib.Path(CPLUS_BIN).is_file():
        print("FATAL: C+ binary not found: %s (set CPLUS_BIN)" % CPLUS_BIN, file=sys.stderr)
        return 2
    ref_name, reference = pick_reference()
    if reference is None:
        print("FATAL: no reference compiler (clang/gcc/cc) available", file=sys.stderr)
        return 2
    if OPT not in ("O0", "O2"):
        print("FATAL: FUZZ_OPT must be O0 or O2", file=sys.stderr)
        return 2

    started = time.time()
    opt_cplus = cplus_opt_args()
    opt_ref = ref_opt_args()

    cases = []
    for i in range(CASES):
        case_seed = SEED + i
        cases.append((i, case_seed, generate_case(case_seed)))

    tmp_root = pathlib.Path(tempfile.mkdtemp(prefix="cp_fuzz_"))
    print("C+ fuzzer: %d cases, seed=0x%X, opt=%s" % (CASES, SEED & 0xFFFFFFFF, OPT))
    print("  compiler under test: %s" % CPLUS_BIN)
    print("  reference: %s (%s)%s" % (reference, ref_name,
                                     " [isysroot %s]" % SDK if SDK else ""))
    print("  workdir: %s" % tmp_root)

    compared = 0
    compile_ok = 0
    compile_div = 0
    run_div = 0
    mismatches = []

    def work(case):
        i, case_seed, src = case
        cdir = tmp_root / ("case_%05d" % i)
        cdir.mkdir()
        res = compare_source(cdir, "seed_%08X" % (case_seed & 0xFFFFFFFF), src,
                             reference, ref_name, opt_cplus, opt_ref, TIMEOUT)
        res.update({"index": i, "case_seed": case_seed, "source": src})
        return res

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for res in pool.map(work, cases):
            compared += 1
            ci, ri = res["cplus"], res["reference"]
            if ci["compiled"] and ri["compiled"]:
                compile_ok += 1
                if res["classification"] in ("codegen", "runtime"):
                    run_div += 1
            else:
                compile_div += 1
            if res["diverged"]:
                mismatches.append(res)
                cls = res["classification"]
                print("  MISMATCH case=%d seed=0x%X class=%s" %
                      (res["index"], res["case_seed"] & 0xFFFFFFFF, cls))

    # --- minimize + persist each mismatch ---
    failure_records = []
    for idx, res in enumerate(mismatches):
        case_seed = res["case_seed"]
        target = res["classification"]
        tag = "case_seed_%08X" % (case_seed & 0xFFFFFFFF)
        orig_path = failures_dir / (tag + ".c")
        min_path = failures_dir / (tag + ".min.c")
        meta_path = failures_dir / (tag + ".json")
        orig_path.write_text(res["source"])

        minimized = res["source"]
        checks_used = 0
        converged = True
        min_result = None
        if idx < MAX_MINIMIZED:
            mdir = tmp_root / ("min_%05d" % res["index"])
            mdir.mkdir()

            def predicate(src_text, _mdir=mdir, _tag=tag):
                out = compare_source(_mdir, _tag, src_text, reference, ref_name,
                                     opt_cplus, opt_ref, TIMEOUT)
                # Require the *same* mismatch class to survive minimization.
                return out["classification"] == target

            minimized, checks_used, converged = minimize_source(
                res["source"], target, predicate, MIN_BUDGET)
            min_path.write_text(minimized)
            min_result = compare_source(mdir, tag + "_final", minimized, reference,
                                        ref_name, opt_cplus, opt_ref, TIMEOUT)

        # Optimizer refinement: re-test at the opposite optimization level.
        refined_class = target
        if target in ("codegen", "runtime"):
            other_c, other_r = opposite_opt_args("cplus"), opposite_opt_args("reference")
            odir = tmp_root / ("opt_%05d" % res["index"])
            odir.mkdir()
            other = compare_source(odir, tag + "_otheropt", res["source"], reference,
                                   ref_name, other_c, other_r, TIMEOUT)
            if not other["diverged"]:
                refined_class = "optimizer"

        def trim(info):
            return {
                "compile_rc": info["compile_rc"],
                "compiled": info["compiled"],
                "compile_timed_out": info["compile_timed_out"],
                "compile_stderr_tail": info["compile_stderr"][-1500:],
                "run_rc": info["run_rc"],
                "run_stdout": info["run_stdout"][-4000:],
                "run_stderr_tail": info["run_stderr"][-1000:],
                "run_timed_out": info["run_timed_out"],
            }

        record = {
            "index": res["index"],
            "seed": SEED,
            "case_seed": case_seed,
            "case_seed_hex": "0x%X" % (case_seed & 0xFFFFFFFF),
            "reproduce": "FUZZ_SEED=%d FUZZ_CASES=1 python3 tests/compiler/fuzz/fuzz.py" % case_seed,
            "classification": refined_class,
            "classification_raw": target,
            "original_source_file": relpath(orig_path),
            "minimized_source_file": relpath(min_path) if idx < MAX_MINIMIZED else None,
            "minimizer_checks": checks_used,
            "minimizer_converged": converged,
            "original_lines": len(res["source"].splitlines()),
            "minimized_lines": len(minimized.splitlines()),
            "primary_result": {
                "cplus": trim(res["cplus"]),
                "reference": trim(res["reference"]),
                "opt": OPT,
            },
        }
        if min_result is not None:
            record["minimized_result"] = {
                "cplus": trim(min_result["cplus"]),
                "reference": trim(min_result["reference"]),
                "classification": min_result["classification"],
            }
        meta_path.write_text(json.dumps(record, indent=2))
        failure_records.append(record)

    elapsed = time.time() - started
    by_class = Counter(r["classification"] for r in failure_records)

    summary = {
        "tool": "deterministic C+ legacy differential fuzzer",
        "seed": SEED,
        "seed_hex": "0x%X" % (SEED & 0xFFFFFFFF),
        "cases_requested": CASES,
        "cases_compared": compared,
        "opt": OPT,
        "cplus_bin": CPLUS_BIN,
        "reference_compiler": reference,
        "reference_name": ref_name,
        "reference_isysroot": SDK,
        "workers": WORKERS,
        "timeout_seconds": TIMEOUT,
        "seed_policy": "case_seed = base_seed + index; reproducible with FUZZ_SEED=<case_seed> FUZZ_CASES=1",
        "elapsed_seconds": round(elapsed, 2),
        "counts": {
            "both_compiled": compile_ok,
            "compile_outcome_divergences": compile_div,
            "run_divergences": run_div,
            "total_mismatches": len(failure_records),
            "by_classification": {k: by_class.get(k, 0) for k in STATUSES},
        },
        "failures": failure_records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(summary, indent=2))

    lines = []
    lines.append("C+ deterministic differential fuzzer - summary")
    lines.append("=" * 48)
    lines.append("base seed         : %d (0x%X)" % (SEED, SEED & 0xFFFFFFFF))
    lines.append("cases compared    : %d / %d" % (compared, CASES))
    lines.append("optimization      : %s" % OPT)
    lines.append("C+ binary         : %s" % CPLUS_BIN)
    lines.append("reference compiler: %s (%s)%s" % (
        reference, ref_name, " [isysroot %s]" % SDK if SDK else ""))
    lines.append("workers / timeout : %d / %ss" % (WORKERS, TIMEOUT))
    lines.append("elapsed           : %.2fs" % elapsed)
    lines.append("")
    lines.append("Counts")
    lines.append("-" * 48)
    lines.append("both compiled cleanly      : %d" % compile_ok)
    lines.append("compile-outcome divergences: %d" % compile_div)
    lines.append("runtime-output divergences : %d" % run_div)
    lines.append("total mismatches           : %d" % len(failure_records))
    for cls in STATUSES:
        lines.append("  %-10s: %d" % (cls, by_class.get(cls, 0)))
    lines.append("")
    lines.append("Mismatches (seed = per-case seed, reproducible)")
    lines.append("-" * 48)
    if not failure_records:
        lines.append("none")
    for rec in failure_records:
        lines.append("seed=%d (0x%s) case=%d class=%s lines=%d->%d min_checks=%d" % (
            rec["case_seed"], rec["case_seed_hex"].replace("0x", ""), rec["index"],
            rec["classification"], rec["original_lines"], rec["minimized_lines"],
            rec["minimizer_checks"]))
        lines.append("  source   : %s" % rec["original_source_file"])
        lines.append("  minimized: %s" % rec["minimized_source_file"])
        lines.append("  repro    : %s" % rec["reproduce"])
    lines.append("")
    lines.append("Seed list (all generated per-case seeds):")
    lines.append("  " + ", ".join(str(s) for s in (SEED + i for i in range(CASES))))
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")

    print("")
    print("Summary:")
    print("  cases compared: %d" % compared)
    print("  both compiled : %d" % compile_ok)
    print("  mismatches    : %d" % len(failure_records))
    for cls in STATUSES:
        print("    %-10s %d" % (cls, by_class.get(cls, 0)))
    print("  wrote %s" % (RESULTS / "summary.txt"))

    try:
        shutil.rmtree(tmp_root)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
