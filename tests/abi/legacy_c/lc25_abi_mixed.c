/* lc25: mixed ABI command record -- brings together an enum selector, a
 * function-pointer member, a bitfield tag and a flexible payload in a single
 * heap-allocated record passed by pointer through a helper function.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

enum op { OP_NEGATE, OP_DOUBLE };

typedef int (*unary_fn)(int);

struct command {
    unary_fn fn;
    unsigned int tag : 4;
    enum op selector;
    int payload[];
};

static int negate(int x) { return -x; }
static int twice(int x) { return x * 2; }

static int run_command(const struct command *cmd)
{
    int i;
    int acc = 0;
    for (i = 0; i < 3; ++i) {
        acc += cmd->fn(cmd->payload[i]);
    }
    return acc;
}

int main(void)
{
    struct command *cmd = (struct command *)malloc(sizeof(struct command) +
                                                   3 * sizeof(int));
    int ok = (cmd != NULL);

    if (ok) {
        cmd->fn = negate;
        cmd->tag = 5u;
        cmd->selector = OP_NEGATE;
        cmd->payload[0] = 1;
        cmd->payload[1] = 2;
        cmd->payload[2] = 3;
        ok &= (run_command(cmd) == -6);

        cmd->fn = twice;
        cmd->selector = OP_DOUBLE;
        ok &= (run_command(cmd) == 12);
        ok &= (cmd->tag == 5u && cmd->selector == OP_DOUBLE);
        ok &= (offsetof(struct command, payload) >= sizeof(struct command));

        printf("lc25_abi_mixed: %s neg=%d dbl=%d tag=%u sel=%d size=%lu\n",
               ok ? "OK" : "FAIL", -6, 12, cmd->tag,
               (int)cmd->selector, (unsigned long)sizeof(struct command));
        free(cmd);
    } else {
        printf("lc25_abi_mixed: FAIL allocation\n");
    }
    return ok ? 0 : 1;
}
