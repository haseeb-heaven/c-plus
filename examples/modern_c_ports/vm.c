/* Independent modern-C port of the vm sample workload: a tiny stack machine,
 * its disassembler, and two scripted programs. C11; the instruction images are
 * transient scoped-arena allocations. No inspiration-language source was copied. */
#include "common.h"

static unsigned char arena_bytes[1 << 12];
static size_t arena_top;
static void *scoped_take(size_t n) {
    void *p = arena_bytes + arena_top;
    arena_top = (arena_top + n + 15u) & ~(size_t)15u;
    return p;
}

enum { OP_PUSH, OP_STORE, OP_LOAD, OP_JZ, OP_JUMP, OP_SUB, OP_MUL, OP_PRINT, OP_HALT };
typedef struct { int op; int arg; } Ins;
static const char *mnemonic[] = {
    "push", "store", "load", "jz", "jump", "sub", "mul", "print", "halt"
};
static int has_arg(int op) {
    return op == OP_PUSH || op == OP_STORE || op == OP_LOAD ||
           op == OP_JZ || op == OP_JUMP;
}

static void disassemble(const char *title, const Ins *p, int n) {
    printf("%s:\n", title);
    for (int i = 0; i < n; ++i) {
        if (has_arg(p[i].op))
            printf("%3d  %s %d\n", i, mnemonic[p[i].op], p[i].arg);
        else
            printf("%3d  %s\n", i, mnemonic[p[i].op]);
    }
}

static int run(const Ins *p, int n, long *out, int *nout) {
    long stack[64], regs[8];
    int sp = 0, pc = 0, count = 0;
    memset(regs, 0, sizeof(regs));
    *nout = 0;
    while (pc >= 0 && pc < n) {
        Ins in = p[pc++];
        ++count;
        switch (in.op) {
            case OP_PUSH: stack[sp++] = in.arg; break;
            case OP_STORE: regs[in.arg] = stack[--sp]; break;
            case OP_LOAD: stack[sp++] = regs[in.arg]; break;
            case OP_JZ: if (stack[--sp] == 0) pc = in.arg; break;
            case OP_JUMP: pc = in.arg; break;
            case OP_SUB: {
                long b = stack[--sp], a = stack[--sp];
                stack[sp++] = a - b;
                break;
            }
            case OP_MUL: {
                long b = stack[--sp], a = stack[--sp];
                stack[sp++] = a * b;
                break;
            }
            case OP_PRINT: out[(*nout)++] = stack[--sp]; break;
            case OP_HALT: return count;
        }
    }
    return count;
}

static void execute(const char *title, const Ins *prog, int n) {
    long out[8];
    int nout, steps;
    disassemble(title, prog, n);
    steps = run(prog, n, out, &nout);
    for (int i = 0; i < nout; ++i) printf("  out: %ld\n", out[i]);
    printf("ran %d instructions\n", steps);
}

int main(void) {
    static const Ins countdown[] = {
        {OP_PUSH, 3}, {OP_STORE, 0}, {OP_LOAD, 0}, {OP_JZ, 11},
        {OP_LOAD, 0}, {OP_PRINT, 0}, {OP_LOAD, 0}, {OP_PUSH, 1},
        {OP_SUB, 0}, {OP_STORE, 0}, {OP_JUMP, 2}, {OP_HALT, 0}
    };
    static const Ins factorial[] = {
        {OP_PUSH, 1}, {OP_STORE, 1}, {OP_PUSH, 10}, {OP_STORE, 0},
        {OP_LOAD, 0}, {OP_JZ, 15}, {OP_LOAD, 1}, {OP_LOAD, 0},
        {OP_MUL, 0}, {OP_STORE, 1}, {OP_LOAD, 0}, {OP_PUSH, 1},
        {OP_SUB, 0}, {OP_STORE, 0}, {OP_JUMP, 4}, {OP_LOAD, 1},
        {OP_PRINT, 0}, {OP_HALT, 0}
    };
    Ins *cd, *fc;

    (void)fails; (void)report; (void)cmp_int; /* shared common.h helpers */
    cd = scoped_take(sizeof(countdown));
    memcpy(cd, countdown, sizeof(countdown));
    fc = scoped_take(sizeof(factorial));
    memcpy(fc, factorial, sizeof(factorial));

    execute("countdown", cd, (int)(sizeof(countdown) / sizeof(*countdown)));
    execute("factorial", fc, (int)(sizeof(factorial) / sizeof(*factorial)));
    printf("an instruction is 9 bytes: a tag and the largest operand\n");
    return 0;
}
