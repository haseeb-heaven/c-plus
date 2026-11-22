#!/usr/bin/env python3
"""Generate 40 NEW safety scenarios (p0new_*) that are distinct from the
bugs51-97, fmt25, runtime_crashes and fnptr_bounds corpora.

Ten bug families, four scenarios each (two reject + two safe counterpart):

  trunc   integer truncation then loop
  s2u     signed-to-unsigned bound bypass
  arena   arena offset wrap
  pkt     packet length underflow
  ring    ring cursor wrap
  jump    jump table OOB
  file    FILE* alias UAF
  field   nested struct field release
  realloc realloc failure overwrite
  out     output-parameter assumptions

Each generated source carries // scenario:, // EXPECT: and // DIAG: markers
so it can be checked in isolation with `c+ check --strict`.
"""
from __future__ import annotations

import argparse
import pathlib

ROOT = pathlib.Path(__file__).parents[2]
DEFAULT_OUT = ROOT / "tests" / "safety" / "p0new"

STD = (
    "#include <stddef.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
)


def scenario(name: str, category: str, expect: str, diag: str, code: str,
             includes: str = STD) -> dict:
    return dict(name=name, category=category, expect=expect, diag=diag,
                includes=includes, code=code)


SCENARIOS = [
    # ------------------------------------------------------------------
    # 1. integer truncation then loop
    # ------------------------------------------------------------------
    scenario("p0new_trunc_loop_wide", "trunc", "reject",
             "constant array index",
             "void p0new_trunc_loop_wide(void){\n"
             "    unsigned int count = (unsigned int)0x100000005ULL;\n"
             "    char slots[4];\n"
             "    for (unsigned i = 0; i < count; ++i) slots[i] = 0;\n"
             "}\n"),
    scenario("p0new_trunc_loop_byte", "trunc", "reject",
             "constant array index",
             "void p0new_trunc_loop_byte(void){\n"
             "    uint8_t cap = (uint8_t)300u;\n"
             "    char table[4];\n"
             "    for (int k = 0; k < cap; ++k) table[k] = 0;\n"
             "}\n"),
    scenario("p0new_trunc_loop_wide_ok", "trunc", "accept", "",
             "void p0new_trunc_loop_wide_ok(void){\n"
             "    unsigned int count = (unsigned int)0x100000005ULL;\n"
             "    char slots[8];\n"
             "    for (unsigned i = 0; i < count; ++i) slots[i] = 0;\n"
             "}\n"),
    scenario("p0new_trunc_loop_byte_ok", "trunc", "accept", "",
             "void p0new_trunc_loop_byte_ok(void){\n"
             "    uint8_t cap = (uint8_t)300u;\n"
             "    char table[64];\n"
             "    for (int k = 0; k < cap; ++k) table[k] = 0;\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 2. signed-to-unsigned bound bypass
    # ------------------------------------------------------------------
    scenario("p0new_s2u_guard_bypass", "s2u", "reject",
             "negative array index",
             "void p0new_s2u_guard_bypass(void){\n"
             "    int delta = -1;\n"
             "    char lane[8];\n"
             "    if ((unsigned long)delta < sizeof lane) lane[delta] = 0;\n"
             "}\n"),
    scenario("p0new_s2u_capacity_trick", "s2u", "reject",
             "negative array index",
             "void p0new_s2u_capacity_trick(void){\n"
             "    int back = -2;\n"
             "    unsigned limit = 16u;\n"
             "    char window[16];\n"
             "    if ((unsigned)back < limit) window[back] = 7;\n"
             "}\n"),
    scenario("p0new_s2u_guard_bypass_ok", "s2u", "accept", "",
             "void p0new_s2u_guard_bypass_ok(void){\n"
             "    int delta = 1;\n"
             "    char lane[8];\n"
             "    if ((unsigned long)delta < sizeof lane) lane[delta] = 0;\n"
             "}\n"),
    scenario("p0new_s2u_capacity_trick_ok", "s2u", "accept", "",
             "void p0new_s2u_capacity_trick_ok(void){\n"
             "    int back = 2;\n"
             "    unsigned limit = 16u;\n"
             "    char window[16];\n"
             "    if ((unsigned)back < limit) window[back] = 7;\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 3. arena offset wrap
    # ------------------------------------------------------------------
    scenario("p0new_arena_cursor_wrap", "arena", "reject",
             "integer overflow in addition",
             "void p0new_arena_cursor_wrap(void){\n"
             "    unsigned int off = 0xFFFFFFF0u;\n"
             "    unsigned int end = off + 32u;\n"
             "    char arena[8];\n"
             "    arena[end] = 0;\n"
             "}\n"),
    scenario("p0new_arena_span_wrap", "arena", "reject",
             "integer overflow in multiplication",
             "void p0new_arena_span_wrap(void){\n"
             "    unsigned int stride = 0x40000000u;\n"
             "    unsigned int span = stride * 4u;\n"
             "    char pool[16];\n"
             "    pool[span] = 1;\n"
             "}\n"),
    scenario("p0new_arena_cursor_wrap_ok", "arena", "accept", "",
             "void p0new_arena_cursor_wrap_ok(void){\n"
             "    unsigned short off = 100;\n"
             "    unsigned short end = off + 8;\n"
             "    char arena[128];\n"
             "    arena[end] = 0;\n"
             "}\n"),
    scenario("p0new_arena_span_wrap_ok", "arena", "accept", "",
             "void p0new_arena_span_wrap_ok(void){\n"
             "    unsigned int stride = 1024u;\n"
             "    unsigned int span = stride * 4u;\n"
             "    char pool[8192];\n"
             "    pool[span] = 1;\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 4. packet length underflow
    # ------------------------------------------------------------------
    scenario("p0new_pkt_payload_underflow", "pkt", "reject",
             "negative array index",
             "void p0new_pkt_payload_underflow(void){\n"
             "    uint16_t total = 4;\n"
             "    uint16_t payload = total - 8;\n"
             "    char frame[16];\n"
             "    frame[payload] = 0;\n"
             "}\n"),
    scenario("p0new_pkt_copy_underflow", "pkt", "reject",
             "copy of",
             "void p0new_pkt_copy_underflow(void){\n"
             "    unsigned int wire = 3u;\n"
             "    unsigned int body = wire - 10u;\n"
             "    char src[8] = {0};\n"
             "    char dst[8];\n"
             "    memcpy(dst, src, body);\n"
             "}\n",
             includes=STD),
    scenario("p0new_pkt_payload_underflow_ok", "pkt", "accept", "",
             "void p0new_pkt_payload_underflow_ok(void){\n"
             "    uint16_t total = 32;\n"
             "    uint16_t payload = total - 8;\n"
             "    char frame[32];\n"
             "    frame[payload] = 0;\n"
             "}\n"),
    scenario("p0new_pkt_copy_underflow_ok", "pkt", "accept", "",
             "void p0new_pkt_copy_underflow_ok(void){\n"
             "    unsigned int wire = 64u;\n"
             "    unsigned int body = wire - 10u;\n"
             "    char src[64] = {0};\n"
             "    char dst[64];\n"
             "    memcpy(dst, src, body);\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 5. ring cursor wrap
    # ------------------------------------------------------------------
    scenario("p0new_ring_cursor_wrap", "ring", "reject",
             "constant array index",
             "void p0new_ring_cursor_wrap(void){\n"
             "    unsigned char head = 250;\n"
             "    unsigned char tail = head + 10;\n"
             "    char ring[4];\n"
             "    ring[tail] = 0;\n"
             "}\n"),
    scenario("p0new_ring_signed_wrap", "ring", "reject",
             "constant array index",
             "void p0new_ring_signed_wrap(void){\n"
             "    int cursor = 65535;\n"
             "    signed char idx = (signed char)cursor;\n"
             "    char slots[8];\n"
             "    slots[idx] = 0;\n"
             "}\n"),
    scenario("p0new_ring_cursor_wrap_ok", "ring", "accept", "",
             "void p0new_ring_cursor_wrap_ok(void){\n"
             "    unsigned char head = 10;\n"
             "    unsigned char tail = head + 3;\n"
             "    char ring[16];\n"
             "    ring[tail] = 0;\n"
             "}\n"),
    scenario("p0new_ring_signed_wrap_ok", "ring", "accept", "",
             "void p0new_ring_signed_wrap_ok(void){\n"
             "    int cursor = 5;\n"
             "    signed char idx = (signed char)cursor;\n"
             "    char slots[8];\n"
             "    slots[idx] = 0;\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 6. jump table OOB
    # ------------------------------------------------------------------
    scenario("p0new_jump_flat_table", "jump", "reject",
             "constant array index",
             "typedef int (*p0new_jump_fn)(int);\n"
             "static int step_up(int x){return x+1;}\n"
             "static int step_down(int x){return x-1;}\n"
             "int p0new_jump_flat_table(void){\n"
             "    p0new_jump_fn table[2] = {step_up, step_down};\n"
             "    return table[4](3);\n"
             "}\n"),
    scenario("p0new_jump_enum_table", "jump", "reject",
             "constant array index",
             "typedef void (*p0new_cb)(void);\n"
             "static void action_a(void){}\n"
             "static void action_b(void){}\n"
             "enum { P0NEW_A, P0NEW_B, P0NEW_N };\n"
             "void p0new_jump_enum_table(void){\n"
             "    p0new_cb handlers[P0NEW_N] = {action_a, action_b};\n"
             "    unsigned sel = 9u;\n"
             "    handlers[sel]();\n"
             "}\n"),
    scenario("p0new_jump_flat_table_ok", "jump", "accept", "",
             "typedef int (*p0new_jump_fn)(int);\n"
             "static int step_up(int x){return x+1;}\n"
             "static int step_down(int x){return x-1;}\n"
             "int p0new_jump_flat_table_ok(void){\n"
             "    p0new_jump_fn table[2] = {step_up, step_down};\n"
             "    return table[1](3);\n"
             "}\n"),
    scenario("p0new_jump_enum_table_ok", "jump", "accept", "",
             "typedef void (*p0new_cb)(void);\n"
             "static void action_a(void){}\n"
             "static void action_b(void){}\n"
             "enum { P0NEW_A, P0NEW_B, P0NEW_N };\n"
             "void p0new_jump_enum_table_ok(void){\n"
             "    p0new_cb handlers[P0NEW_N] = {action_a, action_b};\n"
             "    for (int i = 0; i < P0NEW_N; ++i) handlers[i]();\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 7. FILE* alias UAF
    # ------------------------------------------------------------------
    scenario("p0new_file_alias_write_uaf", "file", "reject",
             "use of closed FILE*",
             "struct p0new_io { int tag; FILE *stream; };\n"
             "void p0new_file_alias_write_uaf(void){\n"
             "    struct p0new_io meta = { .tag = 7, .stream = 0 };\n"
             "    FILE *primary = fopen(\"p0new.log\", \"a+\");\n"
             "    if (!primary) return;\n"
             "    meta.stream = primary;\n"
             "    FILE *mirror = primary;\n"
             "    fclose(primary);\n"
             "    fputc('x', mirror);\n"
             "    (void)meta.tag;\n"
             "}\n"),
    scenario("p0new_file_alias_flush_uaf", "file", "reject",
             "use of closed FILE*",
             "void p0new_file_alias_flush_uaf(void){\n"
             "    FILE *primary = fopen(\"p0new.txt\", \"a\");\n"
             "    if (!primary) return;\n"
             "    FILE *secondary = primary;\n"
             "    fclose(primary);\n"
             "    fflush(secondary);\n"
             "}\n"),
    scenario("p0new_file_field_alias_ok", "file", "accept", "",
             "struct p0new_io { FILE *stream; };\n"
             "void p0new_file_field_alias_ok(void){\n"
             "    struct p0new_io h;\n"
             "    h.stream = fopen(\"p0new.log\", \"w\");\n"
             "    if (!h.stream) return;\n"
             "    FILE *mirror = h.stream;\n"
             "    fputs(\"ok\\n\", mirror);\n"
             "    fclose(h.stream);\n"
             "}\n"),
    scenario("p0new_file_reopen_ok", "file", "accept", "",
             "void p0new_file_reopen_ok(void){\n"
             "    FILE *f = fopen(\"p0new.txt\", \"w\");\n"
             "    if (!f) return;\n"
             "    fclose(f);\n"
             "    f = fopen(\"p0new.txt\", \"r\");\n"
             "    if (!f) return;\n"
             "    fclose(f);\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 8. nested struct field release
    # ------------------------------------------------------------------
    scenario("p0new_field_bundle_uaf", "field", "reject",
             "released pointer",
             "struct p0new_node { int *value; };\n"
             "struct p0new_bundle { struct p0new_node slots[2]; };\n"
             "void p0new_field_bundle_uaf(void){\n"
             "    struct p0new_bundle b;\n"
             "    b.slots[0].value = malloc(sizeof(int));\n"
             "    if (!b.slots[0].value) return;\n"
             "    free(b.slots[0].value);\n"
             "    b.slots[0].value[0] = 9;\n"
             "}\n"),
    scenario("p0new_field_deep_uaf", "field", "reject",
             "released pointer",
             "struct p0new_in { char *buf; };\n"
             "struct p0new_mid { struct p0new_in in; };\n"
             "struct p0new_out { struct p0new_mid m; };\n"
             "void p0new_field_deep_uaf(void){\n"
             "    struct p0new_out o;\n"
             "    o.m.in.buf = malloc(16);\n"
             "    if (!o.m.in.buf) return;\n"
             "    free(o.m.in.buf);\n"
             "    o.m.in.buf[0] = 'z';\n"
             "}\n"),
    scenario("p0new_field_deep_ok", "field", "accept", "",
             "struct p0new_in { int *v; };\n"
             "struct p0new_holder { struct p0new_in i; };\n"
             "void p0new_field_deep_ok(void){\n"
             "    struct p0new_holder h;\n"
             "    h.i.v = malloc(sizeof(int));\n"
             "    if (!h.i.v) return;\n"
             "    h.i.v[0] = 1;\n"
             "    free(h.i.v);\n"
             "}\n"),
    scenario("p0new_field_bundle_ok", "field", "accept", "",
             "struct p0new_node { int *value; };\n"
             "void p0new_field_bundle_ok(void){\n"
             "    struct p0new_node n[2];\n"
             "    n[0].value = malloc(sizeof(int));\n"
             "    n[1].value = malloc(sizeof(int));\n"
             "    if (!n[0].value || !n[1].value) return;\n"
             "    n[0].value[0] = 1;\n"
             "    n[1].value[0] = 2;\n"
             "    free(n[0].value);\n"
             "    free(n[1].value);\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 9. realloc failure overwrite
    # ------------------------------------------------------------------
    scenario("p0new_realloc_lost_result", "realloc", "reject",
             "raw realloc return value is ignored",
             "void p0new_realloc_lost_result(void){\n"
             "    int *block = malloc(32);\n"
             "    if (!block) return;\n"
             "    realloc(block, 256);\n"
             "    block[0] = 7;\n"
             "    free(block);\n"
             "}\n"),
    scenario("p0new_realloc_old_after_grow", "realloc", "reject",
             "realloc result and original pointer",
             "void p0new_realloc_old_after_grow(void){\n"
             "    int *buf = malloc(16);\n"
             "    if (!buf) return;\n"
             "    int *grown = realloc(buf, 64);\n"
             "    if (grown) buf[2] = 3;\n"
             "    free(buf);\n"
             "}\n"),
    scenario("p0new_realloc_checked_ok", "realloc", "accept", "",
             "void p0new_realloc_checked_ok(void){\n"
             "    int *p = malloc(16);\n"
             "    if (!p) return;\n"
             "    int *r = realloc(p, 64);\n"
             "    if (!r) { free(p); return; }\n"
             "    p = r;\n"
             "    p[2] = 3;\n"
             "    free(p);\n"
             "}\n"),
    scenario("p0new_realloc_null_seed_ok", "realloc", "accept", "",
             "void p0new_realloc_null_seed_ok(void){\n"
             "    int *p = realloc(0, sizeof(long) * 8);\n"
             "    if (!p) return;\n"
             "    p[0] = 1;\n"
             "    free(p);\n"
             "}\n"),

    # ------------------------------------------------------------------
    # 10. output-parameter assumptions
    # ------------------------------------------------------------------
    scenario("p0new_out_uninit_ptr", "out", "reject",
             "uninitialized",
             "static int p0new_acquire_fail(int **out){ (void)out; return -1; }\n"
             "void p0new_out_uninit_ptr(void){\n"
             "    int *slot;\n"
             "    p0new_acquire_fail(&slot);\n"
             "    (void)*slot;\n"
             "}\n"),
    scenario("p0new_out_uninit_len", "out", "reject",
             "uninitialized",
             "static void p0new_fill_no_len(int *dst, int *len){ *dst = 3; }\n"
             "void p0new_out_uninit_len(void){\n"
             "    int buf[4];\n"
             "    int n;\n"
             "    p0new_fill_no_len(buf, &n);\n"
             "    int taken = buf[n];\n"
             "    (void)taken;\n"
             "}\n"),
    scenario("p0new_out_checked_ptr_ok", "out", "accept", "",
             "static int p0new_acquire_ok(int **out){\n"
             "    *out = malloc(sizeof(int));\n"
             "    return *out ? 0 : -1;\n"
             "}\n"
             "void p0new_out_checked_ptr_ok(void){\n"
             "    int *p = 0;\n"
             "    if (p0new_acquire_ok(&p) == 0 && p) *p = 1;\n"
             "    free(p);\n"
             "}\n"),
    scenario("p0new_out_len_set_ok", "out", "accept", "",
             "static void p0new_fill_len(int *dst, int *len){ *dst = 3; *len = 1; }\n"
             "void p0new_out_len_set_ok(void){\n"
             "    int buf[4] = {0};\n"
             "    int n = 0;\n"
             "    p0new_fill_len(buf, &n);\n"
             "    buf[n] = 1;\n"
             "}\n"),
]


def render(s: dict) -> str:
    lines = [
        f"// scenario: {s['name']}",
        f"// EXPECT: {s['expect']}",
        f"// DIAG: {s['diag']}",
        s["includes"].rstrip("\n"),
        s["code"].rstrip("\n"),
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(DEFAULT_OUT),
                    help="directory to write scenario .c files")
    ap.add_argument("--list", action="store_true",
                    help="just list scenario names and expectations")
    args = ap.parse_args()

    if args.list:
        for s in SCENARIOS:
            print(f"{s['category']:7} {s['expect']:6} {s['name']}")
        return

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    names = set()
    for s in SCENARIOS:
        assert s["name"] not in names, f"duplicate name {s['name']}"
        names.add(s["name"])
        (out / f"{s['name']}.c").write_text(render(s))
    print(f"generated {len(SCENARIOS)} scenarios into {out}")


if __name__ == "__main__":
    main()
