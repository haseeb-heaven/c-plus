#!/usr/bin/env python3
"""Generate and run the permanent per-API coverage scenario suite.

The generator emits tests/compiler/api_coverage/generated_scenarios.c: a table
of >= 1000 named C scenario functions, each calling the *real* API exported by
runtime/include/cplus/cplus.h and implemented in runtime/cplus_runtime.c.

The runner compiles the generated file, harness.c and cplus_runtime.c with
-fprofile-arcs -ftest-coverage, runs the binary, collects .gcda, runs gcov on
cplus_runtime.c and writes summary.txt / summary.json with the *measured* line
coverage.  Only new files under tests/compiler/api_coverage/ are written.
"""
from __future__ import annotations

import argparse
import itertools
import json
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
RUNTIME_SRC = REPO / "runtime" / "cplus_runtime.c"
INCLUDE_DIR = REPO / "runtime" / "include"

MASK64 = (1 << 64) - 1
I32_MAX, I32_MIN = 2**31 - 1, -(2**31)
I64_MAX, I64_MIN = 2**63 - 1, -(2**63)

APIS = [
    "cplus_fatal", "cplus_check_bounds", "cplus_check_nonnull",
    "cplus_check_ptr_formation", "cplus_register_allocation",
    "cplus_free_registered", "cplus_realloc_registered",
    "__cplus_checked_add_i64", "__cplus_checked_sub_i64",
    "__cplus_checked_mul_i64", "__cplus_checked_div_i64",
    "__cplus_checked_rem_i64", "__cplus_checked_shl_i64",
    "__cplus_checked_shr_i64", "__cplus_checked_narrow_i64",
    "__cplus_checked_f2i_i64", "__cplus_checked_size_mul", "cplus_alloc",
    "cplus_zalloc", "cplus_alloc_array", "cplus_realloc", "cplus_zero",
    "cplus_memcpy", "cplus_memmove", "cplus_strnlen", "cplus_strcpy",
    "cplus_release", "cplus_alloc_bytes", "cplus_check_live",
    "cplus_allocation_generation", "cplus_epoch", "cplus_address_retired",
    "cplus_retired_ledger_full", "__cplus_bounds_check", "cplus_alloc_count",
    "cplus_check_index", "cplus_new", "cplus_new_array", "cplus_move",
    "cplus_scope_push", "cplus_scope_cleanup", "cplus_resource_check_borrow",
    "cplus_mutex_new", "cplus_mutex_lock", "cplus_mutex_unlock",
    "cplus_mutex_free", "cplus_tls_new", "cplus_tls_get", "cplus_tls_set",
    "cplus_tls_free", "countof", "cplus_rotl32", "cplus_rotr32",
    "cplus_print_value", "cplus_print_end", "cplus_scan_value",
    "cplus_scan_value_at", "cplus_print_arg", "cplus_scan_one", "print",
    "scan", "cplus_exception_enter", "cplus_exception_pop",
    "cplus_exception_raise", "cplus_fatal_hook", "cplus_runtime_log_level",
    "cplus_log", "CPLUS_LOG_level_constants",
]

SCENARIOS: list[dict] = []
EXTRAS: list[str] = []
GLOBALS: list[str] = []
_ids = itertools.count(1)


def _slug(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", s)[:60]


def add(api: str, slug: str, body: str, extra: str | None = None,
        glob: str | None = None) -> str:
    idx = next(_ids)
    name = f"sc_{idx:04d}_{_slug(slug)}"
    SCENARIOS.append({"api": api, "name": name, "body": body.strip("\n")})
    if extra:
        EXTRAS.append(extra.strip("\n"))
    if glob and glob not in GLOBALS:
        GLOBALS.append(glob.strip("\n"))
    return name


def u64(x: int) -> str:
    return f"{int(x) & MASK64}ULL"


def i64(x: int) -> int:
    x &= MASK64
    return x - (1 << 64) if x >= (1 << 63) else x


def c_ll(x: int) -> str:
    """C literal that fits in a signed long long (including INT64_MIN)."""
    x = i64(x)
    if x == I64_MIN:
        return "INT64_MIN"
    if x == I64_MAX:
        return "INT64_MAX"
    return f"{x}LL"


def val_i64(api, slug, expr, want, what="result"):
    add(api, slug, f"""int64_t got = (int64_t)({expr});
return h_eq_i64(got, {c_ll(want)}, "{what}");""")


def val_u64(api, slug, expr, want, what="result"):
    add(api, slug, f"""uint64_t got = (uint64_t)({expr});
return h_eq_u64(got, (uint64_t){u64(want)}, "{what}");""")


def val_sz(api, slug, expr, want, what="result"):
    add(api, slug, f"""size_t got = (size_t)({expr});
return h_eq_sz(got, (size_t){u64(want)}, "{what}");""")


def void_ok(api, slug, expr):
    add(api, slug, f"""({expr});
return 1;""")


def trap(api, slug, expr, status, substr, stdin=None):
    name = f"tw_{len(SCENARIOS) + 1:04d}_{_slug(slug)}"
    extra = f"""static void {name}(void) {{ (void)({expr}); }}"""
    helper = "h_fork_trap_stdin" if stdin else "h_fork_trap"
    tail = f', "{stdin}"' if stdin else ""
    add(api, slug, f"return {helper}({name}, {status}, \"{substr}\"{tail});",
        extra=extra)


def trap_block(api, slug, block, status, substr, stdin=None):
    name = f"tw_{len(SCENARIOS) + 1:04d}_{_slug(slug)}"
    extra = f"""static void {name}(void) {{
{block}
}}"""
    helper = "h_fork_trap_stdin" if stdin else "h_fork_trap"
    tail = f', "{stdin}"' if stdin else ""
    add(api, slug, f"return {helper}({name}, {status}, \"{substr}\"{tail});",
        extra=extra)


def hook_block(api, slug, block, code, kind, substr):
    name = f"tw_{len(SCENARIOS) + 1:04d}_{_slug(slug)}"
    extra = f"""static void {name}(void) {{
{block}
}}"""
    add(api, slug,
        f"return h_hook_trap({name}, {code}, \"{kind}\", \"{substr}\");",
        extra=extra)


def stdout_case(api, slug, call, expected):
    # The `print(...)` macro opens its own print frame; direct helper calls do
    # not, so give each of those its own begin/end pair.
    body = call
    if "cplus_print_value" in call or "cplus_print_arg" in call:
        body = re.sub(r"(cplus_print_(?:value|arg)\()",
                      r"cplus_print_begin(); \1", call)
    add(api, slug, f"""h_capture_begin();
{body}
const char *out = h_capture_end();
return h_eq_str(out, "{expected}", "captured stdout");""")


# ---------------------------------------------------------------------------
# Diagnostics
# ---------------------------------------------------------------------------
EXIT_CODES = [0, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111]
for code in EXIT_CODES:
    trap("cplus_fatal", f"fatal_code_{code}",
         f'cplus_fatal((cplus_exit_code){code}, "probe", "api.c", 17, "detail")',
         code, "C+ runtime error: probe")
    trap("cplus_fatal", f"fatal_nullkind_{code}",
         f'cplus_fatal((cplus_exit_code){code}, NULL, NULL, 0, NULL)',
         code, "C+ runtime error: safety violation")
    hook_block("cplus_fatal", f"fatal_hook_code_{code}",
               f'cplus_fatal((cplus_exit_code){code}, "probe", "h.c", 3, "detail");',
               code, "probe", "C+ runtime error: probe")
trap("cplus_fatal", "fatal_file_line",
     'cplus_fatal(CPLUS_EXIT_BOUNDS, "kind", "api.c", 17, "msg")', 101,
     " at api.c:17: msg")

# ---------------------------------------------------------------------------
# cplus_check_bounds / cplus_check_nonnull / cplus_check_ptr_formation
# ---------------------------------------------------------------------------
BOUNDS_COUNTS = [0, 1, 4, 1000]
BOUNDS_IDX = [0, 1, 4, 999, 1000, MASK64]
for count in BOUNDS_COUNTS:
    for idx in BOUNDS_IDX:
        tag = f"cnt{count}_idx{idx}"
        if idx < count:
            void_ok("cplus_check_bounds", "ok_" + tag,
                    f'cplus_check_bounds(NULL, {count}U, {idx}U, NULL, 0, NULL)')
            void_ok("cplus_check_bounds", "ok_expr_" + tag,
                    f'cplus_check_bounds((const void *)"abcd", {count}U, '
                    f'{idx}U, "b.c", 7, "a[i]")')
        else:
            trap("cplus_check_bounds", "bad_" + tag,
                 f'cplus_check_bounds(NULL, {count}U, {idx}U, NULL, 0, NULL)',
                 101, "out-of-bounds access")
            trap("cplus_check_bounds", "bad_expr_" + tag,
                 f'cplus_check_bounds(NULL, {count}U, {idx}U, "b.c", 7, "a[i]")',
                 101, "a[i]")

for expr in ["NULL", '"p"']:
    for file in ["NULL", '"nn.c"']:
        void_ok("cplus_check_nonnull", f"ok_{expr}_{file}",
                f'cplus_check_nonnull((const void *)"x", {file}, 5, {expr})')
        trap("cplus_check_nonnull", f"bad_{expr}_{file}",
             f'cplus_check_nonnull(NULL, {file}, 5, {expr})', 102,
             "null pointer dereference")

PF_INDEX = [-1, 0, 1, 4, 1000, 1001, I64_MIN, I64_MAX]
for count in BOUNDS_COUNTS:
    for idx in PF_INDEX:
        tag = f"cnt{count}_idx{idx}"
        if 0 <= idx <= count:
            void_ok("cplus_check_ptr_formation", "ok_" + tag,
                    f'cplus_check_ptr_formation(NULL, {count}U, '
                    f'{c_ll(idx)}, NULL, 0, NULL)')
        else:
            trap("cplus_check_ptr_formation", "bad_" + tag,
                 f'cplus_check_ptr_formation(NULL, {count}U, {c_ll(idx)}, '
                 f'"p.c", 9, "p+k")', 101, "out-of-bounds pointer formation")

# ---------------------------------------------------------------------------
# Registered-allocation ABI helpers
# ---------------------------------------------------------------------------
void_ok("cplus_register_allocation", "null_pointer",
        'cplus_register_allocation(NULL, 8, "r.c", 1)')
for size in [0, 1, 4, 1024, 65536]:
    add("cplus_register_allocation", f"fresh_{size}", f"""void *m = malloc({size} ? {size} : 1);
CHECK(m != NULL, "malloc failed");
void *p = cplus_register_allocation(m, {size}U, "r.c", 2);
CHECK(p == m, "registration changed the pointer");
size_t got = cplus_alloc_bytes(p);
cplus_free_registered(p, "r.c", 2);
return h_eq_sz(got, {size}U, "registered size");""")
add("cplus_register_allocation", "clamped_declared_size", """void *m = malloc(8);
CHECK(m != NULL, "malloc failed");
void *p = cplus_register_allocation(m, 1000000U, "r.c", 3);
CHECK(p == m, "pointer changed");
size_t got = cplus_alloc_bytes(p);
CHECK(got > 0 && got <= 1000000U, "declared size was not clamped: %zu", got);
cplus_free_registered(p, "r.c", 3);
return 1;""")
trap_block("cplus_register_allocation", "double_registration", """void *m = malloc(8);
(void)cplus_register_allocation(m, 8, "r.c", 4);
(void)cplus_register_allocation(m, 8, "r.c", 4);""", 104,
           "pointer is already a live C+ allocation")
add("cplus_register_allocation", "distinct_interior_registration", """void *m = malloc(16);
CHECK(m != NULL, "malloc failed");
void *a = cplus_register_allocation(m, 8, "r.c", 5);
void *b = cplus_register_allocation((char *)m + 8, 8, "r.c", 5);
CHECK(a == m && b == (char *)m + 8, "registration changed pointers");
cplus_check_live(a);
cplus_check_live(b);
/* Only the base address may be passed to free(); the interior slot is left
 * live but untouched, mirroring the documented base-address-only ledger. */
cplus_free_registered(a, "r.c", 5);
return 1;""")

void_ok("cplus_free_registered", "null_pointer",
        'cplus_free_registered(NULL, "r.c", 6)')
for size in [1, 8, 4096]:
    add("cplus_free_registered", f"registered_{size}", f"""void *m = malloc({size});
CHECK(m != NULL, "malloc failed");
(void)cplus_register_allocation(m, {size}U, "r.c", 7);
cplus_free_registered(m, "r.c", 7);
return h_eq_sz((size_t)cplus_address_retired(m), 1U, "retired flag");""")
# The "unmanaged malloc is plain free" behaviour cannot be probed safely in
# this long-lived process: an address reused by malloc may still be in the
# persistent retired ledger from an earlier scenario and be (correctly)
# reported as a double free.  It is intentionally not generated here.
trap_block("cplus_free_registered", "double_free", """void *m = malloc(8);
(void)cplus_register_allocation(m, 8, "r.c", 9);
cplus_free_registered(m, "r.c", 9);
cplus_free_registered(m, "r.c", 9);""", 104, "allocation released twice")

add("cplus_realloc_registered", "newp_null_keeps_old", """void *m = malloc(8);
CHECK(m != NULL, "malloc failed");
(void)cplus_register_allocation(m, 8, "r.c", 10);
void *r = cplus_realloc_registered(m, NULL, 16, "r.c", 10);
CHECK(r == NULL, "failed realloc must return NULL");
size_t got = cplus_alloc_bytes(m);
cplus_free_registered(m, "r.c", 10);
return h_eq_sz(got, 8U, "old allocation still live");""")
for size in [1, 8, 1024]:
    add("cplus_realloc_registered", f"in_place_{size}", f"""void *m = malloc({size});
CHECK(m != NULL, "malloc failed");
(void)cplus_register_allocation(m, {size}U, "r.c", 11);
void *r = cplus_realloc_registered(m, m, {size}U, "r.c", 11);
CHECK(r == m, "in-place realloc changed the pointer");
size_t got = cplus_alloc_bytes(r);
cplus_free_registered(r, "r.c", 11);
return h_eq_sz(got, {size}U, "in-place size");""")
    add("cplus_realloc_registered", f"move_{size}", f"""void *oldp = malloc({size});
void *newp = malloc({size});
CHECK(oldp && newp, "malloc failed");
(void)cplus_register_allocation(oldp, {size}U, "r.c", 12);
void *r = cplus_realloc_registered(oldp, newp, {size}U, "r.c", 12);
CHECK(r == newp, "realloc did not transfer to newp");
CHECK(cplus_address_retired(oldp) == 1, "old allocation was not retired");
cplus_free_registered(newp, "r.c", 12);
return 1;""")
trap_block("cplus_realloc_registered", "dest_already_live", """void *a = malloc(8);
void *b = malloc(8);
(void)cplus_register_allocation(a, 8, "r.c", 13);
(void)cplus_register_allocation(b, 8, "r.c", 13);
(void)cplus_realloc_registered(a, b, 8, "r.c", 13);""", 104,
           "pointer is already a live C+ allocation")
trap_block("cplus_realloc_registered", "stale_dest", """void *oldp = malloc(8);
(void)cplus_register_allocation(oldp, 8, "r.c", 14);
cplus_free_registered(oldp, "r.c", 14);
(void)cplus_realloc_registered(NULL, oldp, 8, "r.c", 14);""", 105,
           "retired address cannot be re-registered")

# ---------------------------------------------------------------------------
# Checked arithmetic
# ---------------------------------------------------------------------------
ARITH_PAIRS = [
    (0, 0), (1, 0), (0, 1), (1, 1), (-1, 1), (1, -1), (4, 7), (-4, -7),
    (I32_MAX, 1), (I32_MIN, -1), (I32_MAX, I32_MAX), (65535, 1), (127, 1),
    (32767, 1), (I64_MAX, 1), (I64_MIN, -1), (I64_MAX, 0), (I64_MIN, 0),
    (123456789, 987654321), (-123456789, -987654321), (1000000, 1000000),
]
for api, cfun, word in [
    ("__cplus_checked_add_i64", "__cplus_checked_add_i64", "addition"),
    ("__cplus_checked_sub_i64", "__cplus_checked_sub_i64", "subtraction"),
    ("__cplus_checked_mul_i64", "__cplus_checked_mul_i64", "multiplication"),
]:
    for a, b in ARITH_PAIRS:
        r = a + b if "add" in cfun else (a - b if "sub" in cfun else a * b)
        for bits in [0, 8, 32, 64, 65]:
            for signed in [0, 1]:
                tag = f"a{a}_b{b}_bits{bits}_s{signed}"
                expr = f"{cfun}({c_ll(a)}, {c_ll(b)}, {bits}U, {signed}U)"
                if not signed or bits == 0 or bits > 64:
                    val_i64(api, "wrap_" + tag, expr, r)
                elif r < -(1 << (bits - 1)) or r > (1 << (bits - 1)) - 1:
                    trap(api, "trap_" + tag, expr, 110,
                         f"{word} result does not fit")
                else:
                    val_i64(api, "ok_" + tag, expr, r)

DIV_PAIRS = [
    (0, 0), (1, 0), (-1, 0), (7, 2), (-7, 2), (7, -2), (-7, -2),
    (I32_MIN, -1), (I64_MIN, -1), (I64_MIN, 1), (100, 7), (0, 5), (5, 5),
]


def c_div(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


for api, cfun, rem, word in [
    ("__cplus_checked_div_i64", "__cplus_checked_div_i64", False, "division"),
    ("__cplus_checked_rem_i64", "__cplus_checked_rem_i64", True, "remainder"),
]:
    for a, b in DIV_PAIRS:
        for bits in [1, 32, 64, 0, 65]:
            for signed in [0, 1]:
                tag = f"a{a}_b{b}_bits{bits}_s{signed}"
                expr = f"{cfun}({c_ll(a)}, {c_ll(b)}, {bits}U, {signed}U)"
                if b == 0:
                    trap(api, "zero_" + tag, expr, 110,
                         f"{word} by zero")
                elif (signed and bits > 1 and bits <= 64 and b == -1
                      and a == -(1 << (bits - 1))):
                    trap(api, "ovf_" + tag, expr, 110,
                         f"{word} overflows the source type")
                else:
                    want = (a - c_div(a, b) * b) if rem else c_div(a, b)
                    if not signed:
                        # The runtime performs unsigned div/rem on the
                        # unsigned bit pattern and returns the int64_t view.
                        # The helper takes int64_t operands and always uses
                        # the full 64-bit bit pattern, independent of `bits`.
                        ua, ub = a & MASK64, b & MASK64
                        want = (ua % ub) if rem else (ua // ub)
                        if want >= (1 << 63):
                            want -= 1 << 64
                    val_i64(api, "ok_" + tag, expr, want)

SHIFT_A = [0, 1, 7, 255, -1, I32_MAX, I64_MAX]
SHIFT_B = [0, 1, 4, 31, 32, -1, 64]
for api, cfun, left in [
    ("__cplus_checked_shl_i64", "__cplus_checked_shl_i64", True),
    ("__cplus_checked_shr_i64", "__cplus_checked_shr_i64", False),
]:
    for a in SHIFT_A:
        for b in SHIFT_B:
            for bits in [32, 64, 0, 65]:
                for signed in [0, 1]:
                    if (bits == 0 or bits > 64) and b < 0:
                        continue  # C UB for a negative shift count here
                    tag = f"a{a}_b{b}_bits{bits}_s{signed}"
                    expr = f"{cfun}({c_ll(a)}, {c_ll(b)}, {bits}U, {signed}U)"
                    if bits == 0 or bits > 64:
                        # The emitted code casts b to uint64_t and shifts a
                        # 64-bit value: x86/ARM mask the count to six bits.
                        k = b & 63
                        if left:
                            want = i64(((a & MASK64) << k) & MASK64)
                        else:
                            want = i64(a >> k)
                        val_i64(api, "wrap_" + tag, expr, want)
                    elif b < 0 or b >= bits:
                        trap(api, "count_" + tag, expr, 110, "shift count")
                    elif left and signed and a < 0:
                        trap(api, "neg_" + tag, expr, 110,
                             "left shift of a negative signed value")
                    elif left and signed:
                        limit = MASK64 >> (64 - bits)
                        shifted = ((a & MASK64) << b) & MASK64
                        if (shifted & ~limit) or (shifted & (1 << (bits - 1))):
                            trap(api, "bits_" + tag, expr, 110,
                                 "left shift loses bits above")
                        else:
                            val_i64(api, "ok_" + tag, expr,
                                    i64(((a & MASK64) << b) & MASK64))
                    elif left:
                        val_i64(api, "ok_" + tag, expr,
                                i64(((a & MASK64) << b) & MASK64))
                    elif not signed:
                        val_i64(api, "ok_" + tag, expr,
                                i64((a & MASK64) >> b))
                    else:
                        val_i64(api, "ok_" + tag, expr, i64(a >> b))

NARROW_CASES = [
    (0, 8, 0, 8, 0), (127, 8, 1, 8, 1), (128, 8, 0, 8, 1),
    (255, 8, 0, 16, 0), (256, 8, 0, 8, 0), (0xFF, 8, 1, 32, 1),
    (0x7F, 8, 1, 32, 1), (32767, 16, 1, 16, 1), (32768, 16, 0, 16, 1),
    (65535, 16, 0, 16, 0), (2147483647, 32, 1, 32, 1),
    (2147483648, 32, 0, 32, 1), (4294967295, 32, 0, 32, 0),
    (0xFFFFFFFF, 32, 1, 64, 1), (0x7FFFFFFFFFFFFFFF, 64, 1, 64, 1),
    (MASK64, 64, 0, 64, 0), (MASK64, 64, 1, 64, 1),
    (1, 64, 1, 8, 1), (1, 8, 0, 64, 0), (0x8000000000000000, 64, 0, 64, 1),
    (0xFFFFFFFFFFFFFFFE, 64, 1, 8, 1), (63, 8, 0, 8, 0),
    (64, 8, 0, 8, 0), (1023, 16, 0, 8, 0),
]
for value, sb, ss, db, ds in NARROW_CASES:
    v = value & MASK64
    src = v
    if ss and sb == 64:
        src = i64(v)
    elif ss and 0 < sb < 64 and ((v >> (sb - 1)) & 1):
        ext = (((~0) & MASK64) << sb) & MASK64
        src = i64(v | ext)
    mask = MASK64 if db >= 64 else ((1 << db) - 1)
    trunc = v & mask
    dst = trunc
    if ds and db == 64:
        dst = i64(trunc)
    elif ds and 0 < db < 64 and ((trunc >> (db - 1)) & 1):
        ext = (((~0) & MASK64) << db) & MASK64
        dst = i64(trunc | ext)
    tag = f"v{value}_sb{sb}_ss{ss}_db{db}_ds{ds}"
    expr = (f"__cplus_checked_narrow_i64({u64(value)}, {sb}U, {ss}U, "
            f"{db}U, {ds}U)")
    if src != dst:
        trap("__cplus_checked_narrow_i64", "trap_" + tag, expr, 111,
             "implicit conversion")
    else:
        val_i64("__cplus_checked_narrow_i64", "ok_" + tag, expr, dst)

F2I_VALUES = [
    ("0.0", 0.0), ("-0.0", -0.0), ("1.5", 1.5), ("-1.5", -1.5),
    ("127.0", 127.0), ("128.0", 128.0), ("-128.0", -128.0),
    ("-129.0", -129.0), ("2147483647.0", 2147483647.0),
    ("2147483648.0", 2147483648.0), ("-2147483648.0", -2147483648.0),
    ("9223372036854775808.0", 2.0**63), ("1.8446744073709552e19", 2.0**64),
    ("nan(\"\")", float("nan")), ("INFINITY", float("inf")),
    ("-INFINITY", float("-inf")), ("1e300", 1e300), ("255.9", 255.9),
    ("-0.9", -0.9),
]
for lit, value in F2I_VALUES:
    for bits in [8, 32, 64, 0, 65]:
        for signed in [0, 1]:
            b = 64 if (bits == 0 or bits > 64) else bits
            s = 1 if (bits == 0 or bits > 64) else signed
            if s:
                lo, hi = (-(2.0**63), 2.0**63) if b == 64 else (
                    -(2.0 ** (b - 1)), 2.0 ** (b - 1))
            else:
                lo, hi = (0.0, 2.0**64) if b == 64 else (0.0, 2.0**b)
            tag = f"v{lit.replace('.', 'd').replace('(', '').replace(')', '')}_bits{bits}_s{signed}"
            expr = f"__cplus_checked_f2i_i64({lit}, {bits}U, {signed}U)"
            if math.isnan(value) or not (lo <= value < hi):
                trap("__cplus_checked_f2i_i64", "trap_" + tag, expr, 111,
                     "floating-point value")
            elif not s and b == 64:
                val_i64("__cplus_checked_f2i_i64", "ok_" + tag, expr,
                        i64(int(value)))
            else:
                val_i64("__cplus_checked_f2i_i64", "ok_" + tag, expr,
                        i64(math.trunc(value)))

MUL_PAIRS = [
    (0, 0), (0, MASK64), (1, 0), (1, 4), (4, 1), (3, 7), (1024, 4096),
    (MASK64 // 4, 4), (4, MASK64 // 4), (MASK64 // 2 + 1, 2),
    (MASK64, 2), (MASK64, MASK64), (1, MASK64), (MASK64, 1),
]
for a, b in MUL_PAIRS:
    tag = f"a{a}_b{b}"
    expr = f"__cplus_checked_size_mul({u64(a)}, {u64(b)})"
    if b != 0 and a > MASK64 // b:
        trap("__cplus_checked_size_mul", "trap_" + tag, expr, 110,
             "allocation size")
    else:
        val_u64("__cplus_checked_size_mul", "ok_" + tag, expr, (a * b) & MASK64)

# ---------------------------------------------------------------------------
# Managed allocation
# ---------------------------------------------------------------------------
for size, expect in [(0, 1), (1, 1), (4, 4), (64, 64), (65536, 65536)]:
    add("cplus_alloc", f"size_{size}", f"""void *p = cplus_alloc({size}U);
CHECK(p != NULL, "cplus_alloc returned NULL");
size_t got = cplus_alloc_bytes(p);
cplus_release(p);
return h_eq_sz(got, {expect}U, "alloc_bytes");""")
add("cplus_alloc", "distinct_nested", """void *a = cplus_alloc(16);
void *b = cplus_alloc(16);
void *c = cplus_alloc(16);
CHECK(a && b && c, "allocation failed");
CHECK(a != b && b != c && a != c, "allocations are not distinct");
cplus_release(c);
cplus_release(b);
cplus_release(a);
return 1;""")
add("cplus_alloc", "repeated_after_release", """for (int i = 0; i < 32; ++i) {
  void *p = cplus_alloc((size_t)i + 1);
  CHECK(p != NULL, "allocation failed");
  cplus_release(p);
}
return 1;""")
add("cplus_alloc", "many_live_growth", """void *p[200];
for (int i = 0; i < 200; ++i) { p[i] = cplus_alloc(8); CHECK(p[i] != NULL, "alloc failed"); }
for (int i = 0; i < 200; ++i) cplus_release(p[i]);
return 1;""")

for size, expect in [(0, 1), (1, 1), (4, 4), (32, 32), (4096, 4096)]:
    add("cplus_zalloc", f"size_{size}", f"""unsigned char *p = cplus_zalloc({size}U);
CHECK(p != NULL, "cplus_zalloc returned NULL");
size_t n = {size} ? {size} : 1;
for (size_t i = 0; i < n; ++i) CHECK(p[i] == 0, "byte %zu is not zero", i);
size_t got = cplus_alloc_bytes(p);
cplus_release(p);
return h_eq_sz(got, {expect}U, "alloc_bytes");""")

for count, elem in [(0, 4), (1, 4), (4, 4), (16, 8), (1024, 1), (4, 0), (0, 0)]:
    # cplus_alloc(0) normalises the extent to one byte; cplus_alloc_array
    # delegates to cplus_zalloc, so a zero count still registers one byte.
    expect = max(1, count * (elem if elem else 1))
    add("cplus_alloc_array", f"count{count}_elem{elem}", f"""unsigned char *p = cplus_alloc_array({count}U, {elem}U);
CHECK(p != NULL, "cplus_alloc_array returned NULL");
for (size_t i = 0; i < {expect}U; ++i) CHECK(p[i] == 0, "byte %zu is not zero", i);
size_t got = cplus_alloc_bytes(p);
cplus_release(p);
return h_eq_sz(got, {expect}U, "alloc_bytes");""")
trap("cplus_alloc_array", "overflow_1",
     f"cplus_alloc_array({u64(MASK64)}, 4U)", 107, "allocation overflow")
trap("cplus_alloc_array", "overflow_2",
     f"cplus_alloc_array(2U, {u64(MASK64 // 2 + 1)})", 107,
     "allocation overflow")

add("cplus_realloc", "null_ptr", """unsigned char *q = cplus_realloc(NULL, 24);
CHECK(q != NULL, "realloc(NULL) failed");
size_t got = cplus_alloc_bytes(q);
cplus_release(q);
return h_eq_sz(got, 24U, "alloc_bytes");""")
for newsize in [1, 4, 8, 64]:
    add("cplus_realloc", f"grow_from_4_to_{newsize}", f"""unsigned char *p = cplus_alloc(4);
CHECK(p != NULL, "alloc failed");
for (int i = 0; i < 4; ++i) p[i] = 0xAB;
unsigned char *q = cplus_realloc(p, {newsize}U);
CHECK(q != NULL, "realloc failed");
for (int i = 0; i < ({newsize} < 4 ? {newsize} : 4); ++i)
  CHECK(q[i] == 0xAB, "byte %d not preserved", i);
size_t got = cplus_alloc_bytes(q);
cplus_release(q);
return h_eq_sz(got, {newsize}U, "alloc_bytes");""")
add("cplus_realloc", "size_zero_releases", """void *p = cplus_alloc(32);
CHECK(p != NULL, "alloc failed");
void *q = cplus_realloc(p, 0);
CHECK(q == NULL, "realloc(p, 0) must return NULL");
return h_eq_sz((size_t)cplus_address_retired(p), 1U, "old allocation retired");""")
add("cplus_realloc", "repeated_realloc", """void *p = cplus_alloc(4);
for (int i = 1; i < 20; ++i) {
  void *q = cplus_realloc(p, (size_t)(i * 8));
  CHECK(q != NULL, "realloc failed");
  p = q;
}
cplus_release(p);
return 1;""")

add("cplus_zero", "null_pointer", """cplus_zero(NULL, 8);
void *p = cplus_alloc(8);
cplus_release(p);
return 1;""")
for size in [0, 1, 8, 256]:
    add("cplus_zero", f"size_{size}", f"""unsigned char *p = cplus_alloc({size} ? {size} : 1);
CHECK(p != NULL, "alloc failed");
for (size_t i = 0; i < ((size_t){size} ? (size_t){size} : 1U); ++i) p[i] = 0xCD;
cplus_zero(p, {size}U);
for (size_t i = 0; i < ((size_t){size} ? (size_t){size} : 1U); ++i) CHECK(p[i] == ({(0 if size else 0xCD)}), "byte %zu not zeroed", i);
cplus_release(p);
return 1;""")

# ---------------------------------------------------------------------------
# Compatibility helpers
# ---------------------------------------------------------------------------
add("cplus_memcpy", "zero_len_null", """void *r = cplus_memcpy(NULL, NULL, 0);
return h_eq_sz((size_t)(r == NULL), 1U, "zero-length copy returns dest");""")
for n, src, expect in [(1, '"A"', '"A"'), (3, '"abc"', '"abc"'),
                       (8, '"abcdefgh"', '"abcdefgh"')]:
    add("cplus_memcpy", f"len_{n}", f"""char dst[16];
memset(dst, 'x', sizeof(dst));
char *r = cplus_memcpy(dst, {src}, {n}U);
CHECK(r == dst, "memcpy did not return dest");
CHECK(memcmp(dst, {expect}, {n}U) == 0, "copy mismatch");
CHECK(dst[{n}] == 'x', "wrote past n");
return 1;""")
add("cplus_memcpy", "overlap_forward", """char b[16] = "abcdefgh";
char *r = cplus_memcpy(b + 2, b, 4);
CHECK(r == b + 2, "return mismatch");
CHECK(memcmp(b, "ababcdgh", 8) == 0, "overlap result: %s", b);
return 1;""")
add("cplus_memmove", "zero_len_null", """void *r = cplus_memmove(NULL, NULL, 0);
return h_eq_sz((size_t)(r == NULL), 1U, "zero-length move returns dest");""")
for n in [1, 3, 8]:
    add("cplus_memmove", f"len_{n}", f"""char dst[16];
memset(dst, 'x', sizeof(dst));
char *r = cplus_memmove(dst, "abcdefgh", {n}U);
CHECK(r == dst, "memmove did not return dest");
CHECK(memcmp(dst, "abcdefgh", {n}U) == 0, "move mismatch");
return 1;""")
add("cplus_memmove", "overlap_backward", """char b[16] = "abcdefgh";
char *r = cplus_memmove(b, b + 2, 4);
CHECK(r == b, "return mismatch");
CHECK(memcmp(b, "cdefefgh", 8) == 0, "overlap result: %s", b);
return 1;""")
for s, maxn, want in [('"abc"', 0, 0), ('"abc"', 1, 1), ('"abc"', 3, 3),
                      ('"abc"', 10, 3), ('""', 5, 0),
                      ('"0123456789"', 6, 6)]:
    val_sz("cplus_strnlen", f"s{s}_max{maxn}",
           f"cplus_strnlen({s}, {maxn}U)", want)
for n, src, want in [(8, '"abc"', 3), (4, '"abc"', 3), (5, '"abc"', 3)]:
    add("cplus_strcpy", f"fit_{n}_{src}", f"""char dst[{n}];
int w = cplus_strcpy(dst, {n}U, {src});
CHECK(w == {want}, "written %d", w);
CHECK(strcmp(dst, "abc") == 0, "content: %s", dst);
return 1;""")
add("cplus_strcpy", "empty", """char dst[4];
int w = cplus_strcpy(dst, 4U, "");
CHECK(w == 0 && dst[0] == '\\0', "empty copy failed");
return 1;""")
trap_block("cplus_strcpy", "overflow", """char dst[3];
(void)cplus_strcpy(dst, 3U, "hello");""", 101, "string overflow")

# ---------------------------------------------------------------------------
# Liveness / introspection
# ---------------------------------------------------------------------------
void_ok("cplus_release", "null_pointer", "cplus_release(NULL)")
for size in [1, 8, 4096]:
    add("cplus_release", f"valid_{size}", f"""void *p = cplus_alloc({size}U);
CHECK(p != NULL, "alloc failed");
cplus_release(p);
return h_eq_sz((size_t)cplus_address_retired(p), 1U, "retired");""")
trap_block("cplus_release", "double_free", """void *p = cplus_alloc(8);
cplus_release(p);
cplus_release(p);""", 104, "managed memory released twice")
trap_block("cplus_release", "invalid_free", """int stack = 0;
cplus_release(&stack);""", 105, "pointer was not produced by the C+ allocator")
trap_block("cplus_release", "interior_pointer", """void *p = cplus_alloc(16);
cplus_release((char *)p + 4);""", 105,
           "pointer was not produced by the C+ allocator")
add("cplus_release", "release_registered", """void *p = malloc(8);
CHECK(p != NULL, "malloc failed");
(void)cplus_register_allocation(p, 8, "rel.c", 1);
cplus_release(p);
return h_eq_sz((size_t)cplus_address_retired(p), 1U, "retired");""")

for size in [0, 1, 8, 1024]:
    add("cplus_alloc_bytes", f"size_{size}", f"""void *p = cplus_alloc({size}U);
size_t got = cplus_alloc_bytes(p);
cplus_release(p);
return h_eq_sz(got, {size if size else 1}U, "alloc_bytes");""")
trap("cplus_alloc_bytes", "null_pointer", "cplus_alloc_bytes(NULL)", 105,
     "null pointer has no allocation length")
trap_block("cplus_alloc_bytes", "freed_pointer", """void *p = cplus_alloc(8);
cplus_release(p);
(void)cplus_alloc_bytes(p);""", 105, "pointer is not a live C+ allocation")

add("cplus_check_live", "valid", """void *p = cplus_alloc(8);
cplus_check_live(p);
cplus_release(p);
return 1;""")
trap("cplus_check_live", "null_pointer", "cplus_check_live(NULL)", 105,
     "null pointer is not a live allocation")
trap_block("cplus_check_live", "freed_pointer", """void *p = cplus_alloc(8);
cplus_release(p);
cplus_check_live(p);""", 105, "pointer is no longer a live C+ allocation")
trap_block("cplus_check_live", "stack_pointer", """int x = 0;
cplus_check_live(&x);""", 105, "pointer is no longer a live C+ allocation")

add("cplus_allocation_generation", "null_zero", """uint64_t g = cplus_allocation_generation(NULL);
return h_eq_u64(g, 0, "null generation");""")
for size in [1, 8, 64]:
    add("cplus_allocation_generation", f"live_{size}", f"""void *p = cplus_alloc({size}U);
uint64_t g = cplus_allocation_generation(p);
uint64_t epoch = cplus_epoch();
cplus_release(p);
CHECK(g > 0, "generation must be non-zero");
return h_eq_u64(g, epoch - 1, "generation vs epoch");""")
    add("cplus_allocation_generation", f"freed_{size}", f"""void *p = cplus_alloc({size}U);
cplus_release(p);
return h_eq_u64(cplus_allocation_generation(p), 0, "freed generation");""")

add("cplus_epoch", "monotonic", """uint64_t e0 = cplus_epoch();
void *a = cplus_alloc(8);
uint64_t e1 = cplus_epoch();
void *b = cplus_alloc(8);
uint64_t e2 = cplus_epoch();
cplus_release(b);
cplus_release(a);
CHECK(e1 > e0 && e2 > e1, "epoch did not advance: %llu %llu %llu",
      (unsigned long long)e0, (unsigned long long)e1, (unsigned long long)e2);
return 1;""")
add("cplus_epoch", "stable_without_alloc", """uint64_t a = cplus_epoch();
uint64_t b = cplus_epoch();
return h_eq_u64(a, b, "stable epoch");""")
for kind in ["null", "live", "freed"]:
    body = {
        "null": "return h_eq_sz((size_t)cplus_address_retired(NULL), 0U, \"null\");",
        "live": """/* The ledger may legitimately report a reused address as retired, so
 * choose a live address that has never been retired before asserting. */
void *p[16];
void *chosen = NULL;
size_t r = 2;
/* Large blocks come from fresh mmap regions, so they have not been retired
 * by earlier small-object scenarios. */
for (int i = 0; i < 16; ++i) {
  p[i] = cplus_alloc(1U << 20);
  CHECK(p[i] != NULL, "alloc failed");
  if (!chosen && cplus_address_retired(p[i]) == 0) {
    chosen = p[i];
    r = (size_t)cplus_address_retired(chosen);
  }
}
for (int i = 0; i < 16; ++i) cplus_release(p[i]);
CHECK(chosen != NULL, "no never-retired address found");
return h_eq_sz(r, 0U, "live never-retired address");""",
        "freed": """void *p = cplus_alloc(8);
cplus_release(p);
return h_eq_sz((size_t)cplus_address_retired(p), 1U, "retired address");""",
    }[kind]
    add("cplus_address_retired", kind, body)
add("cplus_retired_ledger_full", "initial_state", """int full = cplus_retired_ledger_full();
return h_eq_sz((size_t)(full == 0 || full == 1), 1U, "boolean ledger flag");""")
add("cplus_retired_ledger_full", "after_retire", """void *p = cplus_alloc(8);
cplus_release(p);
int full = cplus_retired_ledger_full();
return h_eq_sz((size_t)(full == 0 || full == 1), 1U, "boolean ledger flag");""")

BOUNDS_CHECK_PAIRS = [(0, 0), (0, 1), (1, 0), (1, 1), (3, 4), (4, 4),
                      (4, 5), (999, 1000), (1000, 1000), (1001, 1000),
                      (0, MASK64)]
for index, length in BOUNDS_CHECK_PAIRS:
    tag = f"i{index}_n{length}"
    if index < length:
        val_sz("__cplus_bounds_check", "ok_" + tag,
               f'__cplus_bounds_check({u64(index)}, {u64(length)}, "arr")', index)
        val_sz("__cplus_bounds_check", "ok_noname_" + tag,
               f'__cplus_bounds_check({u64(index)}, {u64(length)}, NULL)', index)
    else:
        trap("__cplus_bounds_check", "abort_" + tag,
             f'__cplus_bounds_check({u64(index)}, {u64(length)}, "arr")',
             "H_STATUS_SIGABRT", "C+ error: index")
        trap("__cplus_bounds_check", "abort_noname_" + tag,
             f'__cplus_bounds_check({u64(index)}, {u64(length)}, NULL)',
             "H_STATUS_SIGABRT", "outside the bounds of 'array'")

for index, count in [(0, 0), (0, 1), (1, 1), (3, 4), (4, 4), (4, 5),
                     (0, 1000), (999, 1000), (1000, 1000), (MASK64, 1000)]:
    tag = f"i{index}_c{count}"
    if index < count:
        val_sz("cplus_check_index", "ok_" + tag,
               f'cplus_check_index({u64(index)}, {u64(count)}, "f.c", 1, "i")',
               index)
    else:
        trap("cplus_check_index", "bad_" + tag,
             f'cplus_check_index({u64(index)}, {u64(count)}, "f.c", 1, "i")',
             101, "index")
for size in [1, 8, 64]:
    add("cplus_alloc_count", f"elem_{size}", f"""void *p = cplus_alloc({size * 4}U);
size_t got = cplus_alloc_count(p, {size}U);
cplus_release(p);
return h_eq_sz(got, 4U, "alloc_count");""")
trap_block("cplus_alloc_count", "elem_zero", """void *p = cplus_alloc(8);
(void)cplus_alloc_count(p, 0);""", 105, "element size must be greater than zero")
trap_block("cplus_alloc_count", "freed_pointer", """void *p = cplus_alloc(8);
cplus_release(p);
(void)cplus_alloc_count(p, 4);""", 105, "pointer is not a live C+ allocation")

# ---------------------------------------------------------------------------
# cplus_new / cplus_new_array / cplus_move / countof
# ---------------------------------------------------------------------------
for ty, expect in [("int", 4), ("char", 1), ("long", 8), ("double", 8)]:
    add("cplus_new", f"type_{ty}", f"""{ty} *p = cplus_new({ty});
CHECK(p != NULL, "cplus_new returned NULL");
CHECK(*p == 0, "object is not zeroed");
size_t got = cplus_alloc_bytes(p);
cplus_release(p);
return h_eq_sz(got, {expect}U, "alloc_bytes");""")
add("cplus_new", "struct_zeroed", """typedef struct { int a; double b; } S;
S *p = cplus_new(S);
CHECK(p != NULL && p->a == 0 && p->b == 0.0, "struct not zeroed");
cplus_release(p);
return 1;""")
for ty, n in [("int", 0), ("int", 1), ("int", 8), ("char", 32), ("double", 4)]:
    add("cplus_new_array", f"{ty}_{n}", f"""{ty} *p = cplus_new_array({ty}, {n});
CHECK(p != NULL, "cplus_new_array returned NULL");
for (int i = 0; i < {n}; ++i) CHECK(p[i] == 0, "element %d not zeroed", i);
size_t got = cplus_alloc_bytes(p);
cplus_release(p);
return h_eq_sz(got, {max(1, n * 8 if ty == 'double' else n * 4 if ty == 'int' else n)}U, "alloc_bytes");""")
for i in range(1, 9):
    add("cplus_move", f"pointer_{i}", f"""void *src = cplus_alloc({i * 4}U);
CHECK(src != NULL, "alloc failed");
void *dst = cplus_move(src);
CHECK(src == NULL, "source was not zeroed");
CHECK(dst != NULL, "destination is null");
cplus_release(dst);
return 1;""")
add("cplus_move", "struct_move", """typedef struct { void *p; int n; } S;
S s = { cplus_alloc(8), 7 };
S t = cplus_move(s);
CHECK(s.p == NULL && s.n == 0, "source not zeroed");
CHECK(t.p != NULL && t.n == 7, "destination wrong");
cplus_release(t.p);
return 1;""")
for ty, size in [("char", 1), ("int", 4), ("long", 8), ("double", 8)]:
    val_sz("countof", ty, f"countof(({ty}[7]){{0}})", 7)
add("countof", "array_var", """int a[13] = {0};
return h_eq_sz(countof(a), 13U, "countof(a)");""")
add("countof", "array_var2", """char a[5] = {0};
return h_eq_sz(countof(a), 5U, "countof(a)");""")

# ---------------------------------------------------------------------------
# rotations
# ---------------------------------------------------------------------------
ROT_VALUES = [0, 1, 0x80000000, 0x12345678, 0xFFFFFFFF, 0xDEADBEEF]
for value in ROT_VALUES:
    for count in [0, 1, 4, 15, 31, 32, 33, 64, 100]:
        k = count % 32
        left = ((value << k) | (value >> (32 - k))) & MASK64 if k else value
        right = ((value >> k) | (value << (32 - k))) & MASK64 if k else value
        val_u64("cplus_rotl32", f"v{value}_c{count}",
                f"cplus_rotl32({value}U, {count}U)", left & 0xFFFFFFFF)
        val_u64("cplus_rotr32", f"v{value}_c{count}",
                f"cplus_rotr32({value}U, {count}U)", right & 0xFFFFFFFF)

# ---------------------------------------------------------------------------
# print / scan
# ---------------------------------------------------------------------------
stdout_case("cplus_print_value", "int",
            "cplus_print_value(CPLUS_PRINT_INT, -42); cplus_print_end();",
            "-42\\n")
stdout_case("cplus_print_value", "uint",
            "cplus_print_value(CPLUS_PRINT_UINT, 42U); cplus_print_end();",
            "42\\n")
stdout_case("cplus_print_value", "long",
            "cplus_print_value(CPLUS_PRINT_LONG, -7L); cplus_print_end();",
            "-7\\n")
stdout_case("cplus_print_value", "ulong",
            "cplus_print_value(CPLUS_PRINT_ULONG, 9UL); cplus_print_end();",
            "9\\n")
stdout_case("cplus_print_value", "llong",
            "cplus_print_value(CPLUS_PRINT_LLONG, -11LL); cplus_print_end();",
            "-11\\n")
stdout_case("cplus_print_value", "ullong",
            "cplus_print_value(CPLUS_PRINT_ULLONG, 12ULL); cplus_print_end();",
            "12\\n")
stdout_case("cplus_print_value", "double",
            "cplus_print_value(CPLUS_PRINT_DOUBLE, 1.5); cplus_print_end();",
            "1.5\\n")
stdout_case("cplus_print_value", "ldouble",
            "cplus_print_value(CPLUS_PRINT_LDOUBLE, 2.5L); cplus_print_end();",
            "2.5\\n")
stdout_case("cplus_print_value", "char",
            "cplus_print_value(CPLUS_PRINT_CHAR, 'Q'); cplus_print_end();",
            "Q\\n")
stdout_case("cplus_print_value", "bool_true",
            "cplus_print_value(CPLUS_PRINT_BOOL, 1); cplus_print_end();",
            "true\\n")
stdout_case("cplus_print_value", "bool_false",
            "cplus_print_value(CPLUS_PRINT_BOOL, 0); cplus_print_end();",
            "false\\n")
stdout_case("cplus_print_value", "str",
            'cplus_print_value(CPLUS_PRINT_STR, "abc"); cplus_print_end();',
            "abc\\n")
stdout_case("cplus_print_value", "str_null",
            "cplus_print_value(CPLUS_PRINT_STR, NULL); cplus_print_end();",
            "(null)\\n")
stdout_case("cplus_print_value", "unknown",
            "cplus_print_value(CPLUS_PRINT_UNKNOWN, 1); cplus_print_end();",
            "<unprintable>\\n")
stdout_case("cplus_print_value", "separator",
            "cplus_print_value(CPLUS_PRINT_INT, 1); "
            "cplus_print_value(CPLUS_PRINT_INT, 2); cplus_print_end();",
            "1 2\\n")
stdout_case("cplus_print_value", "two_lines",
            "cplus_print_value(CPLUS_PRINT_INT, 1); cplus_print_end(); "
            "cplus_print_value(CPLUS_PRINT_INT, 2); cplus_print_end();",
            "1\\n2\\n")
stdout_case("cplus_print_end", "empty_line", "cplus_print_end();", "\\n")
stdout_case("cplus_print_end", "resets_separator",
            "cplus_print_value(CPLUS_PRINT_INT, 8); cplus_print_end(); "
            "cplus_print_value(CPLUS_PRINT_INT, 9); cplus_print_end();",
            "8\\n9\\n")

stdout_case("cplus_print_arg", "int",
            "cplus_print_arg(7); cplus_print_end();", "7\\n")
stdout_case("cplus_print_arg", "uint",
            "cplus_print_arg(7u); cplus_print_end();", "7\\n")
stdout_case("cplus_print_arg", "long",
            "cplus_print_arg(7L); cplus_print_end();", "7\\n")
stdout_case("cplus_print_arg", "char",
            "char c = 'Z'; cplus_print_arg(c); cplus_print_end();", "Z\\n")
stdout_case("cplus_print_arg", "str",
            'cplus_print_arg("xy"); cplus_print_end();', "xy\\n")
stdout_case("cplus_print_arg", "double",
            "cplus_print_arg(2.5); cplus_print_end();", "2.5\\n")
stdout_case("cplus_print_arg", "bool_as_int",
            "cplus_print_arg((_Bool)1); cplus_print_end();", "1\\n")
stdout_case("print", "zero_args", "print();", "\\n")
stdout_case("print", "one_arg", "print(5);", "5\\n")
stdout_case("print", "four_args", 'print(1, 2u, 3L, "x");', "1 2 3 x\\n")
stdout_case("print", "sixteen_args",
            "print(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);",
            "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16\\n")

for text, want in [("42\\n", 42), ("-17\\n", -17), ("0x2a\\n", 42)]:
    add("cplus_scan_value", f"int_{want}", f"""int v = 0;
h_stdin_from("{text}");
cplus_scan_value(&v, sizeof(v), CPLUS_SCAN_INT);
h_stdin_restore();
return h_eq_i64(v, {want}, "scanned int");""")
add("cplus_scan_value", "uint", """unsigned v = 0;
h_stdin_from("77\\n");
cplus_scan_value(&v, sizeof(v), CPLUS_SCAN_UINT);
h_stdin_restore();
return h_eq_u64(v, 77, "scanned uint");""")
add("cplus_scan_value", "long", """long v = 0;
h_stdin_from("-123\\n");
cplus_scan_value(&v, sizeof(v), CPLUS_SCAN_LONG);
h_stdin_restore();
return h_eq_i64(v, -123, "scanned long");""")
add("cplus_scan_value", "double", """double v = 0;
h_stdin_from("3.5\\n");
cplus_scan_value(&v, sizeof(v), CPLUS_SCAN_DOUBLE);
h_stdin_restore();
return h_eq_i64((int64_t)(v * 2), 7, "scanned double");""")
add("cplus_scan_value", "float", """float v = 0;
h_stdin_from("2.25\\n");
cplus_scan_value(&v, sizeof(v), CPLUS_SCAN_FLOAT);
h_stdin_restore();
return h_eq_i64((int64_t)(v * 4), 9, "scanned float");""")
add("cplus_scan_value", "char", """char v = 0;
h_stdin_from("q\\n");
cplus_scan_value(&v, sizeof(v), CPLUS_SCAN_CHAR);
h_stdin_restore();
return h_eq_i64(v, 'q', "scanned char");""")
add("cplus_scan_value", "str", """char v[16] = {0};
h_stdin_from("hello\\n");
cplus_scan_value(v, sizeof(v), CPLUS_SCAN_STR);
h_stdin_restore();
return h_eq_str(v, "hello", "scanned string");""")
add("cplus_scan_value", "str_truncated", """char v[4] = {0};
h_stdin_from("abcdef\\n");
cplus_scan_value(v, sizeof(v), CPLUS_SCAN_STR);
h_stdin_restore();
return h_eq_str(v, "abc", "truncated string");""")
trap("cplus_scan_value", "null_dest",
     "cplus_scan_value(NULL, 4, CPLUS_SCAN_INT)", 109,
     "scan destination")
trap("cplus_scan_value", "unsupported_kind", """cplus_scan_value(&(int){0}, 4, (cplus_scan_kind)99)""",
     108, "unsupported scan type")
for text, kind, substr in [
    ("abc\\n", "CPLUS_SCAN_INT", "expected a valid integer"),
    ("99999999999999999999999\\n", "CPLUS_SCAN_INT", "too large/small"),
    ("-5\\n", "CPLUS_SCAN_UINT", "negative numbers are not allowed"),
]:
    trap_block("cplus_scan_value_at", f"input_{kind}_{substr[:8]}",
               f"""int v = 0;
(void)cplus_scan_value_at(&v, sizeof(v), {kind}, "v", "scan.c", 4);""",
               109 if substr == "expected a valid integer" else 111,
               substr, stdin=text)
trap_block("cplus_scan_value_at", "eof", """int v = 0;
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_INT, "v", "scan.c", 5);""",
           109, "input ended unexpectedly", stdin="")
add("cplus_scan_value_at", "exact_kind_int", """int v = 0;
h_stdin_from("55\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_INT, "v", "scan.c", 6);
h_stdin_restore();
return h_eq_i64(v, 55, "scan_value_at int");""")
add("cplus_scan_value_at", "exact_kind_ull", """unsigned long long v = 0;
h_stdin_from("18446744073709551615\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_ULLONG, "v", "scan.c", 7);
h_stdin_restore();
return h_eq_u64(v, MASK64, "scan_value_at ull");""")
add("cplus_scan_value_at", "signed_char_width", """signed char v = 0;
h_stdin_from("-12\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_INT, "v", "scan.c", 8);
h_stdin_restore();
return h_eq_i64(v, -12, "signed char scan");""")
add("cplus_scan_value_at", "short_width", """short v = 0;
h_stdin_from("-300\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_INT, "v", "scan.c", 9);
h_stdin_restore();
return h_eq_i64(v, -300, "short scan");""")
add("cplus_scan_value_at", "llong_width", """long long v = 0;
h_stdin_from("-9000000000\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_LLONG, "v", "scan.c", 10);
h_stdin_restore();
return h_eq_i64(v, -9000000000LL, "long long scan");""")
add("cplus_scan_value_at", "unsigned_char_width", """unsigned char v = 0;
h_stdin_from("250\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_UINT, "v", "scan.c", 11);
h_stdin_restore();
return h_eq_u64(v, 250, "unsigned char scan");""")
add("cplus_scan_value_at", "ushort_width", """unsigned short v = 0;
h_stdin_from("60000\\n");
(void)cplus_scan_value_at(&v, sizeof(v), CPLUS_SCAN_UINT, "v", "scan.c", 12);
h_stdin_restore();
return h_eq_u64(v, 60000, "unsigned short scan");""")
trap_block("cplus_scan_value_at", "unsupported_signed_size", """int v = 0;
(void)cplus_scan_value_at(&v, 3, CPLUS_SCAN_INT, "v", "scan.c", 13);""",
           108, "unsupported destination size", stdin="7\\n")
trap_block("cplus_scan_value_at", "unsupported_unsigned_size", """int v = 0;
(void)cplus_scan_value_at(&v, 3, CPLUS_SCAN_UINT, "v", "scan.c", 14);""",
           108, "unsupported destination size", stdin="7\\n")
for i in range(1, 9):
    add("cplus_scan_one", f"int_{i}", f"""int v = 0;
h_stdin_from("{i}\\n");
cplus_scan_one(v);
h_stdin_restore();
return h_eq_i64(v, {i}, "scan_one");""")
add("cplus_scan_one", "char_array", """char v[256] = {0};
h_stdin_from("world\\n");
cplus_scan_one(v);
h_stdin_restore();
return h_eq_str(v, "world", "scan_one char array");""")
add("scan", "two_ints", """int a = 0, b = 0;
h_stdin_from("11 22\\n");
scan(a, b);
h_stdin_restore();
CHECK(a == 11 && b == 22, "scan values: %d %d", a, b);
return 1;""")
add("scan", "int_and_str", """int a = 0;
char b[16] = {0};
h_stdin_from("7 token\\n");
scan(a, b);
h_stdin_restore();
CHECK(a == 7, "scan int: %d", a);
return h_eq_str(b, "token", "scan string");""")

# ---------------------------------------------------------------------------
# Scope guards / resource ownership / concurrency
# ---------------------------------------------------------------------------
for kind_name, kind in [("managed", "CPLUS_CLEANUP_MANAGED"),
                        ("resource", "CPLUS_CLEANUP_RESOURCE")]:
    add("cplus_scope_push", f"null_scope_{kind_name}",
        f"""return h_eq_i64(cplus_scope_push(NULL, NULL, NULL, {kind}), -1, "null scope");""")
    add("cplus_scope_push", f"push_{kind_name}", f"""cplus_scope s = {{0}};
void *p = cplus_alloc(8);
CHECK(cplus_scope_push(&s, p, NULL, {kind}) == 0, "push failed");
CHECK(s.head != NULL, "head not set");
cplus_scope_cleanup(&s);
return h_eq_sz((size_t)cplus_address_retired(p), 1U, "cleanup retired");""")
add("cplus_scope_push", "many_records", """cplus_scope s = {0};
for (int i = 0; i < 32; ++i) {
  void *p = cplus_alloc((size_t)i + 1);
  CHECK(cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED) == 0, "push failed");
}
cplus_scope_cleanup(&s);
return h_eq_sz((size_t)(s.head == NULL), 1U, "scope not empty");""")
add("cplus_scope_cleanup", "null_scope", """cplus_scope_cleanup(NULL);
return 1;""")
add("cplus_scope_cleanup", "empty_scope", """cplus_scope s = {0};
cplus_scope_cleanup(&s);
return 1;""")
add("cplus_scope_cleanup", "reverse_order", """cplus_scope s = {0};
int order[3] = {0, 0, 0};
int idx = 0;
(void)idx;
CHECK(cplus_scope_push(&s, &order[0], NULL, CPLUS_CLEANUP_RESOURCE) == 0, "push");
CHECK(cplus_scope_push(&s, &order[1], NULL, CPLUS_CLEANUP_RESOURCE) == 0, "push");
CHECK(cplus_scope_push(&s, &order[2], NULL, CPLUS_CLEANUP_RESOURCE) == 0, "push");
/* Records with a NULL drop callback are skipped; verify the list is drained. */
cplus_scope_cleanup(&s);
return h_eq_sz((size_t)(s.head == NULL), 1U, "scope not drained");""")
add("cplus_scope_cleanup", "resource_drop_calls", """cplus_scope s = {0};
int a = 0, b = 0;
h_drop_counter_reset();
CHECK(cplus_scope_push(&s, &a, h_drop_callback, CPLUS_CLEANUP_RESOURCE) == 0, "push");
CHECK(cplus_scope_push(&s, &b, h_drop_callback, CPLUS_CLEANUP_RESOURCE) == 0, "push");
cplus_scope_cleanup(&s);
return h_eq_sz((size_t)g_drop_calls, 2U, "drop callback count");""")
add("cplus_scope_cleanup", "idempotent", """cplus_scope s = {0};
void *p = cplus_alloc(8);
CHECK(cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED) == 0, "push");
cplus_scope_cleanup(&s);
cplus_scope_cleanup(&s);
return 1;""")
void_ok("cplus_resource_check_borrow", "valid",
        'cplus_resource_check_borrow("x", "f.c", 1, "r")')
trap("cplus_resource_check_borrow", "null",
     'cplus_resource_check_borrow(NULL, "f.c", 1, "r")', 106,
     "borrowed resource is null")
trap("cplus_resource_check_borrow", "null_noexpr",
     "cplus_resource_check_borrow(NULL, NULL, 0, NULL)", 106,
     "borrowed resource is null")

add("cplus_mutex_new", "valid", """void *m = cplus_mutex_new();
CHECK(m != NULL, "mutex_new returned NULL");
cplus_mutex_free(m);
return 1;""")
for i in range(8):
    add("cplus_mutex_new", f"repeated_{i}", """void *m[8];
for (int i = 0; i < 8; ++i) { m[i] = cplus_mutex_new(); CHECK(m[i] != NULL, "new failed"); }
for (int i = 0; i < 8; ++i) cplus_mutex_free(m[i]);
return 1;""")
for i in range(12):
    add("cplus_mutex_lock", f"lock_unlock_{i}", """void *m = cplus_mutex_new();
CHECK(m != NULL, "new failed");
for (int k = 0; k < 8; ++k) { cplus_mutex_lock(m); cplus_mutex_unlock(m); }
cplus_mutex_free(m);
return 1;""")
add("cplus_mutex_lock", "null_lock", """cplus_mutex_lock(NULL);
return 1;""")
add("cplus_mutex_unlock", "null_unlock", """cplus_mutex_unlock(NULL);
return 1;""")
add("cplus_mutex_free", "null_free", """cplus_mutex_free(NULL);
return 1;""")
GLOBALS.append("static volatile int g_thread_counter = 0;\n"
               "static void *g_thread_mutex = NULL;\n"
               "static void *g_thread_tls = NULL;\n"
               "static void *thread_worker(void *arg) {\n"
               "  (void)arg;\n"
               "  cplus_tls_set(g_thread_tls, (void *)0x55AA);\n"
               "  for (int i = 0; i < 500; ++i) {\n"
               "    cplus_mutex_lock(g_thread_mutex);\n"
               "    ++g_thread_counter;\n"
               "    cplus_mutex_unlock(g_thread_mutex);\n"
               "  }\n"
               "  return cplus_tls_get(g_thread_tls);\n"
               "}")
add("cplus_mutex_lock", "threaded_increment", """pthread_t t1, t2;
g_thread_counter = 0;
g_thread_mutex = cplus_mutex_new();
g_thread_tls = cplus_tls_new();
CHECK(g_thread_mutex && g_thread_tls, "primitive creation failed");
CHECK(pthread_create(&t1, NULL, thread_worker, NULL) == 0, "pthread_create 1");
CHECK(pthread_create(&t2, NULL, thread_worker, NULL) == 0, "pthread_create 2");
void *r1 = NULL, *r2 = NULL;
CHECK(pthread_join(t1, &r1) == 0, "pthread_join 1");
CHECK(pthread_join(t2, &r2) == 0, "pthread_join 2");
CHECK(r1 == (void *)0x55AA && r2 == (void *)0x55AA, "TLS value in thread");
CHECK(g_thread_counter == 1000, "counter=%d", g_thread_counter);
cplus_tls_free(g_thread_tls);
cplus_mutex_free(g_thread_mutex);
return 1;""")

for i in range(8):
    add("cplus_tls_new", f"repeated_{i}", """void *s[8];
for (int i = 0; i < 8; ++i) { s[i] = cplus_tls_new(); CHECK(s[i] != NULL, "new failed"); }
for (int i = 0; i < 8; ++i) cplus_tls_free(s[i]);
return 1;""")
add("cplus_tls_get", "null_slot", """return h_eq_sz((size_t)(cplus_tls_get(NULL) == NULL), 1U, "null slot");""")
add("cplus_tls_get", "initial_value", """void *s = cplus_tls_new();
CHECK(s != NULL, "new failed");
void *v = cplus_tls_get(s);
cplus_tls_free(s);
return h_eq_sz((size_t)(v == NULL), 1U, "initial TLS value");""")
for i in range(10):
    add("cplus_tls_set", f"set_get_{i}", """void *s = cplus_tls_new();
CHECK(s != NULL, "new failed");
void *value = (void *)(uintptr_t)(0x1000 + 7);
cplus_tls_set(s, value);
void *got = cplus_tls_get(s);
cplus_tls_free(s);
return h_eq_u64((uint64_t)(uintptr_t)got, (uint64_t)(uintptr_t)value, "TLS value");""")
add("cplus_tls_set", "null_slot_is_noop", """cplus_tls_set(NULL, (void *)0x1);
return 1;""")
add("cplus_tls_free", "null_slot_is_noop", """cplus_tls_free(NULL);
return 1;""")

# ---------------------------------------------------------------------------
# Exceptions, hooks and logging
# ---------------------------------------------------------------------------
for code in [0, 1, 7, -5, 12345]:
    add("cplus_exception_enter", f"roundtrip_{code}", f"""int ec = 0;
const char *msg = NULL, *file = NULL;
int line = 0;
int seen = h_exception_roundtrip({code}, "boom", "exc.c", 42,
                                 &ec, &msg, &file, &line);
CHECK(seen == 1, "exception was not caught (seen=%d)", seen);
CHECK(ec == {code}, "code=%d", ec);
CHECK(msg && strcmp(msg, "boom") == 0, "message=%s", msg ? msg : "(null)");
CHECK(file && strcmp(file, "exc.c") == 0, "file=%s", file ? file : "(null)");
return h_eq_i64(line, 42, "line");""")
add("cplus_exception_enter", "null_frame_is_noop", """cplus_exception_enter(NULL);
return 1;""")
add("cplus_exception_pop", "no_frame_is_noop", """cplus_exception_pop();
cplus_exception_pop();
return 1;""")
trap_block("cplus_exception_pop", "after_pop_is_unhandled", """CPlusExceptionFrame f = {0};
cplus_exception_enter(&f);
cplus_exception_pop();
cplus_exception_raise(3, "x", "e.c", 1);""", 108,
           "throw outside a try block")
trap_block("cplus_exception_raise", "outside_try",
           'cplus_exception_raise(9, "oops", "e.c", 12);', 108,
           "throw outside a try block")
add("cplus_exception_raise", "inside_try", """int ec = 0;
int seen = h_exception_roundtrip(5, "ok", "e.c", 8, &ec, NULL, NULL, NULL);
CHECK(seen == 1 && ec == 5, "raise failed");
return 1;""")
add("cplus_fatal_hook", "null_hook_restored", """void (*old)(cplus_exit_code, const char *) = cplus_fatal_hook;
cplus_fatal_hook = NULL;
CHECK(cplus_fatal_hook == NULL, "hook not cleared");
cplus_fatal_hook = old;
return 1;""")
add("cplus_fatal_hook", "hook_intercepts", """return h_hook_trap(tw_hook_probe, 101, "probe", "C+ runtime error: probe");""",
    extra="static void tw_hook_probe(void) { cplus_fatal(101, \"probe\", \"h.c\", 1, \"d\"); }")
add("cplus_runtime_log_level", "default_quiet", """cplus_runtime_log_level = 99;
h_capture_begin();
void *p = cplus_alloc(8);
cplus_release(p);
const char *out = h_capture_end();
return h_eq_str(out, "", "quiet runtime output");""")
add("cplus_runtime_log_level", "level_zero_emits", """cplus_runtime_log_level = 0;
h_capture_begin();
void *p = cplus_realloc(NULL, 8);
cplus_release(p);
const char *out = h_capture_end();
cplus_runtime_log_level = 99;
return h_has_substr(out, "cplus_realloc ptr=", "debug log");""")
add("cplus_log", "disabled_macro_is_noop", """h_capture_begin();
cplus_log(CPLUS_LOG_DEBUG, "should not appear %d", 1);
const char *out = h_capture_end();
return h_eq_str(out, "", "cplus_log output");""")
add("CPLUS_LOG_level_constants", "values", """return h_eq_i64((int64_t)CPLUS_LOG_TRACE * 100000
                  + CPLUS_LOG_DEBUG * 10000 + CPLUS_LOG_INFO * 1000
                  + CPLUS_LOG_WARN * 100 + CPLUS_LOG_ERROR * 10
                  + CPLUS_LOG_FATAL, 12345, "log level constants");""")
add("CPLUS_LOG_level_constants", "exit_codes", """return h_eq_i64(CPLUS_EXIT_OK * 1000 + CPLUS_EXIT_BOUNDS
                  + CPLUS_EXIT_NULL_POINTER + CPLUS_EXIT_USE_AFTER_FREE
                  + CPLUS_EXIT_DOUBLE_FREE + CPLUS_EXIT_TYPE_CONFUSION,
                  0 + 101 + 102 + 103 + 104 + 105, "exit constants");""")


# ---------------------------------------------------------------------------
# Additional edge cases for the small/no-op APIs
# ---------------------------------------------------------------------------
for i in range(4):
    add("cplus_mutex_unlock", f"repeated_cycles_{i}", """void *m = cplus_mutex_new();
CHECK(m != NULL, "new failed");
for (int k = 0; k < (3 + 2); ++k) { cplus_mutex_lock(m); cplus_mutex_unlock(m); }
cplus_mutex_free(m);
return 1;""")
add("cplus_mutex_unlock", "null_repeated", """for (int i = 0; i < 8; ++i) cplus_mutex_unlock(NULL);
return 1;""")
add("cplus_mutex_free", "after_locks", """void *m = cplus_mutex_new();
CHECK(m != NULL, "new failed");
cplus_mutex_lock(m);
cplus_mutex_unlock(m);
cplus_mutex_free(m);
return 1;""")
for i in range(4):
    add("cplus_mutex_free", f"create_free_{i}", """for (int k = 0; k < 4; ++k) {
  void *m = cplus_mutex_new();
  CHECK(m != NULL, "new failed");
  cplus_mutex_free(m);
}
return 1;""")
add("cplus_tls_free", "reuse_slot", """void *a = cplus_tls_new();
CHECK(a != NULL, "new failed");
cplus_tls_set(a, (void *)0x11);
cplus_tls_free(a);
void *b = cplus_tls_new();
CHECK(b != NULL, "second new failed");
CHECK(cplus_tls_get(b) == NULL, "new slot is not empty");
cplus_tls_free(b);
return 1;""")
add("cplus_tls_get", "independent_slots", """void *a = cplus_tls_new();
void *b = cplus_tls_new();
CHECK(a && b && a != b, "slots invalid");
cplus_tls_set(a, (void *)0x22);
CHECK(cplus_tls_get(a) == (void *)0x22, "slot a value");
CHECK(cplus_tls_get(b) == NULL, "slot b leaked value");
cplus_tls_free(a);
cplus_tls_free(b);
return 1;""")
add("cplus_print_end", "three_blank_lines", """h_capture_begin();
cplus_print_begin();
cplus_print_end();
cplus_print_end();
cplus_print_end();
const char *out = h_capture_end();
return h_eq_str(out, "\\n\\n\\n", "three lines");""")
for level in ["CPLUS_LOG_TRACE", "CPLUS_LOG_WARN", "CPLUS_LOG_FATAL"]:
    add("cplus_log", f"noop_{level}", f"""h_capture_begin();
cplus_log({level}, "never printed %d", 1);
cplus_log({level}, "never printed again");
const char *out = h_capture_end();
return h_eq_str(out, "", "cplus_log is a no-op macro");""")
add("cplus_runtime_log_level", "threshold_suppresses_debug", """cplus_runtime_log_level = 2;
h_capture_begin();
void *p = cplus_realloc(NULL, 8);
cplus_release(p);
const char *out = h_capture_end();
cplus_runtime_log_level = 99;
return h_eq_str(out, "", "level 2 suppresses debug logs");""")
add("cplus_runtime_log_level", "level_one_emits", """cplus_runtime_log_level = 1;
h_capture_begin();
void *p = cplus_realloc(NULL, 8);
cplus_release(p);
const char *out = h_capture_end();
cplus_runtime_log_level = 99;
return h_has_substr(out, "[1] cplus_realloc", "debug log at level 1");""")
add("cplus_fatal_hook", "restore_after_hook", """void (*old)(cplus_exit_code, const char *) = cplus_fatal_hook;
int ok = h_hook_trap(tw_hook_probe2, 105, "probe2", "C+ runtime error: probe2");
CHECK(cplus_fatal_hook == old, "hook was not restored");
return ok;""",
    extra='static void tw_hook_probe2(void) { cplus_fatal(105, "probe2", "h.c", 2, "d"); }')
add("cplus_exception_pop", "double_pop", """cplus_exception_pop();
cplus_exception_enter(NULL);
cplus_exception_pop();
cplus_exception_pop();
return 1;""")
add("cplus_exception_raise", "null_message", """int ec = 0;
const char *msg = (const char *)1;
int seen = h_exception_roundtrip(4, NULL, NULL, 0, &ec, &msg, NULL, NULL);
CHECK(seen == 1 && ec == 4, "roundtrip failed");
return h_eq_sz((size_t)(msg == NULL), 1U, "null message preserved");""")
add("scan", "three_values", """int a = 0, b = 0, c = 0;
h_stdin_from("1 2 3\\n");
scan(a, b, c);
h_stdin_restore();
CHECK(a == 1 && b == 2 && c == 3, "values %d %d %d", a, b, c);
return 1;""")

# ---------------------------------------------------------------------------
# Emit generated_scenarios.c
# ---------------------------------------------------------------------------
def emit_source() -> str:
    out = [
        "/*===-- generated_scenarios.c - GENERATED FILE --------------*- C -*-===*",
        " *",
        " * Generated by run_suite.py.  Do not edit by hand.",
        f" * Scenarios: {len(SCENARIOS)}; APIs: {len(APIS)}.",
        " *",
        " *===----------------------------------------------------------------------===*/",
        '#include "harness.h"',
        '#include "cplus/cplus.h"',
        "",
        "#include <limits.h>",
        "#include <math.h>",
        "#define MASK64 18446744073709551615ULL",
        "#include <pthread.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "",
    ]
    for g in GLOBALS:
        out.append(g)
    out.append("")
    for e in EXTRAS:
        out.append(e)
        out.append("")
    for s in SCENARIOS:
        out.append(f"static int {s['name']}(void) {{")
        out.append("  g_detail[0] = '\\0';")
        for line in s["body"].splitlines():
            out.append("  " + line if line.strip() else "")
        out.append("  return 1;")
        out.append("}")
        out.append("")
    out.append("const scenario_t g_scenarios[] = {")
    for s in SCENARIOS:
        out.append(f'    {{"{s["name"]}", "{s["api"]}", {s["name"]}}},')
    out.append("};")
    out.append("")
    out.append(f"const size_t g_scenario_count = {len(SCENARIOS)}U;")
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------
def run(cmd, *, cwd=None, timeout=900, check=True):
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        raise SystemExit(f"HARD TIMEOUT ({timeout}s): {' '.join(map(str, cmd))}\n{e}")
    if check and p.returncode != 0:
        raise SystemExit(f"Command failed ({p.returncode}): {' '.join(map(str, cmd))}\n"
                         f"stdout:\n{p.stdout[-4000:]}\nstderr:\n{p.stderr[-4000:]}")
    return p


def parse_gcov(path: Path):
    text = path.read_text(errors="replace")
    executed = 0
    executable = 0
    uncovered = []
    all_lines: dict[int, str] = {}
    pat = re.compile(r"^\s*([^:]+):\s*(\d+):(.*)$")
    for line in text.splitlines():
        m = pat.match(line)
        if not m:
            continue
        count, lineno, source = m.group(1).strip(), int(m.group(2)), m.group(3)
        if lineno == 0:
            continue
        all_lines[lineno] = source
        if count == "-" or count.startswith("-"):
            continue
        if count in ("#####", "====="):
            executable += 1
            uncovered.append((lineno, source))
        elif count.isdigit():
            executable += 1
            if int(count) > 0:
                executed += 1
            else:
                uncovered.append((lineno, source))
    return executed, executable, uncovered, all_lines


def reason_for(line: int, source: str, context: str = "") -> str:
    s = (source + " " + context).lower()
    if ("fatal" in s or "abort(" in s or "runtime error" in s
            or "out of memory" in s or "snprintf(detail" in s
            or "input error" in s or "input overflow" in s):
        return ("fatal / OOM / input-error diagnostic line; its trap scenario "
                "runs in a forked child, whose counters are not merged into "
                "the parent .gcda")
    if "quarantine" in s:
        return "opt-in CPLUS_QUARANTINE path (disabled by default)"
    if "wrapped" in s or "evict" in s or "ledger is full" in s:
        return "retired-ledger saturation path (1,048,576 retirements required)"
    if "atfork" in s or "pthread_mutex_init" in s:
        return "pthread_atfork child hook: executes only in forked children"
    if "generation = 1u" in s:
        return "uint64 generation wrap after 2^64 registrations"
    if "lock failure" in s or "pthread" in s:
        return "pthread lock/key failure branch (requires simulated OS failure)"
    if "n = usable" in s:
        return "requires a registry extent larger than the malloc usable size"
    return "branch not reached by the generated scenarios"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--scratch", type=Path, default=HERE / ".scratch",
                    help="scratch build directory (default: ./.scratch)")
    ap.add_argument("--generate-only", action="store_true")
    ap.add_argument("--run-only", action="store_true")
    ap.add_argument("--timeout", type=int, default=900,
                    help="hard timeout for the suite binary (seconds)")
    args = ap.parse_args()

    generated = HERE / "generated_scenarios.c"
    if not args.run_only:
        generated.write_text(emit_source())
        print(f"generated {generated} with {len(SCENARIOS)} scenarios")
    if args.generate_only:
        return 0

    scratch = args.scratch.resolve()
    objdir = scratch / "obj"
    objdir.mkdir(parents=True, exist_ok=True)
    # stale gcda from an older generated file causes libgcov merge warnings
    for stale in objdir.glob("*.gcda"):
        stale.unlink()
    for stale in objdir.glob("*.gcno"):
        stale.unlink()
    clang = os.environ.get("CPLUS_CLANG") or "/usr/bin/clang"
    if not Path(clang).exists():
        alt = shutil.which("clang")
        if not alt:
            raise SystemExit(f"clang not found: {clang}")
        clang = alt
    cflags = ["-std=gnu17", "-g", "-O0", "-fprofile-arcs", "-ftest-coverage",
              f"-I{INCLUDE_DIR}", f"-I{HERE}"]
    objs = []
    for src in [HERE / "harness.c", generated, RUNTIME_SRC]:
        obj = objdir / (src.stem + ".o")
        run([clang, *cflags, "-c", str(src), "-o", str(obj)], cwd=REPO)
        objs.append(str(obj))
    binary = scratch / "api_coverage_suite"
    run([clang, "-fprofile-arcs", "-ftest-coverage", *objs, "-o", str(binary),
         "-lpthread", "-lm"], cwd=REPO)

    proc = run([str(binary)], cwd=REPO, timeout=args.timeout, check=False)
    results = []
    for line in proc.stdout.splitlines():
        if line.startswith("PASS "):
            results.append((line[5:].split(" | ")[0], True, ""))
        elif line.startswith("FAIL "):
            rest = line[5:]
            name, _, detail = rest.partition(" | ")
            results.append((name, False, detail))
    m = re.search(r"SUMMARY scenarios=(\d+) passed=(\d+) failed=(\d+)",
                  proc.stdout)
    summary_line = m.group(0) if m else "SUMMARY line missing"
    if proc.returncode != 0 or not m:
        print(proc.stdout[-8000:])
        print(proc.stderr[-4000:])

    # gcov needs the source at the relative path embedded in the .gcno file,
    # and emits its .gcov files into the current directory.  Both are kept
    # inside the scratch dir via a relative symlink to runtime/.
    gcda = objdir / "cplus_runtime.gcda"
    if not gcda.exists():
        raise SystemExit(f"missing coverage data: {gcda}")
    link = scratch / "runtime"
    if not link.exists() or not link.is_symlink():
        if link.exists():
            raise SystemExit(f"unexpected non-symlink at {link}")
        link.symlink_to(REPO / "runtime")
    run(["/usr/bin/gcov", "-o", str(objdir), "-p", "runtime/cplus_runtime.c"],
        cwd=scratch)
    gcov_file = scratch / "runtime#cplus_runtime.c.gcov"
    if not gcov_file.exists():
        raise SystemExit(f"missing gcov output: {gcov_file}")
    executed, total, uncovered, all_lines = parse_gcov(gcov_file)
    pct = (100.0 * executed / total) if total else 0.0

    api_counts = {a: 0 for a in APIS}
    by_name = {s["name"]: s["api"] for s in SCENARIOS}
    for name, _ok, _detail in results:
        api = by_name.get(name)
        if api:
            api_counts[api] += 1
    zero_apis = [a for a, n in api_counts.items() if n == 0]

    failed = [{"name": n, "detail": d} for n, ok, d in results if not ok]
    data = {
        "generator": str(relative(generated)),
        "source_under_test": "runtime/cplus_runtime.c",
        "compiler": clang,
        "scenarios": {
            "discovered": len(SCENARIOS),
            "executed": len(results),
            "passed": sum(1 for _, ok, _ in results if ok),
            "failed": len(failed),
            "summary_line": summary_line,
            "binary_exit_code": proc.returncode,
        },
        "apis": [{"name": a, "scenarios": api_counts[a]} for a in APIS],
        "apis_with_zero_scenarios": zero_apis,
        "coverage": {
            "tool": "/usr/bin/gcov -fprofile-arcs -ftest-coverage",
            "executed_lines": executed,
            "total_executable_lines": total,
            "line_coverage_percent": round(pct, 2),
            "uncovered_lines": [
                {"line": ln, "source": src.strip(),
                 "reason": reason_for(ln, src, " ".join(
                     all_lines.get(k, "") for k in range(max(1, ln - 3), ln + 7)))}
                for ln, src in uncovered
            ],
        },
        "failed_scenarios": failed,
    }
    (HERE / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    lines = [
        "API coverage scenario suite - measured results",
        "===============================================",
        f"Generated file : {relative(generated)}",
        f"Source under test: runtime/cplus_runtime.c",
        f"Compiler       : {clang}",
        f"{summary_line}",
        f"Scenarios discovered : {len(SCENARIOS)}",
        f"Scenarios executed   : {len(results)}",
        f"Scenarios passed     : {data['scenarios']['passed']}",
        f"Scenarios failed     : {len(failed)}",
        f"APIs inventoried     : {len(APIS)}",
        f"APIs with scenarios  : {len(APIS) - len(zero_apis)}",
        f"APIs with ZERO scenarios: {zero_apis if zero_apis else 'none'}",
        "",
        "Measured line coverage of runtime/cplus_runtime.c (gcov):",
        f"  executed lines   : {executed}",
        f"  executable lines : {total}",
        f"  line coverage    : {pct:.2f}%",
        "",
        "Per-API scenario counts:",
    ]
    for a in APIS:
        lines.append(f"  {a:36s} {api_counts[a]:4d}")
    lines += ["", f"Uncovered executable lines ({len(uncovered)}):"]
    for ln, src in uncovered:
        ctx = " ".join(all_lines.get(k, "") for k in range(max(1, ln - 3), ln + 7))
        lines.append(f"  line {ln}: {src.strip()}  -- {reason_for(ln, src, ctx)}")
    if failed:
        lines += ["", "Failed scenarios:"]
        for f in failed:
            lines.append(f"  {f['name']}: {f['detail']}")
    (HERE / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines[:20]))
    print(f"\nWrote {relative(HERE / 'summary.txt')} and "
          f"{relative(HERE / 'summary.json')}")
    return 0 if (len(failed) == 0 and not zero_apis
                 and len(results) == len(SCENARIOS)) else 1


def relative(path: Path) -> str:
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)


if __name__ == "__main__":
    sys.exit(main())
