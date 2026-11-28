/* cli_calc.cp -- deterministic calculator CLI built on stdlib/cli.hp.
 *
 * This demo exercises the full cli.hp surface: command registration, option
 * registration, argv parsing, status-based refusals, id-based dispatch and
 * deterministic help rendering.  It has no clock, no prompts, no environment
 * reads and no file I/O, so every invocation has one exact expected output.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/cli_app/cli_calc.cp --default --sanitize=none \
 *       -I. -o apps/cli_app/out/cli_calc -- build/libc/libcplus_hardened.a
 *
 * Usage:
 *   cli_calc add A B          sum two bounded integers
 *   cli_calc mul A B          multiply two bounded integers
 *   cli_calc help             print the generated help text
 *   cli_calc --verbose add A B        optional flag
 *   cli_calc --tag=NAME mul A B       optional value option
 *
 * All refusals are recoverable: cli_parse returns a stable cli_status code
 * and cli_error() renders it.  Nothing crashes on hostile argv.  cli.hp
 * treats any leading '-' token as an option, so operands are non-negative
 * decimal integers; products are range-checked in long before printing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/cli.hp"

#define CALC_MIN (-1000000)
#define CALC_MAX 1000000

/* Context handed to cli_run; the help command needs the registry itself. */
typedef struct calc_ctx {
    const cli *app;
    char help[2048];
} calc_ctx;

/* strict-decimal, bounded integer parse of one parsed positional argument */
static int calc_int_arg(const cli_parse_result *p, size_t index, int *out) {
    const char *text;
    char *end = (char *)0;
    long v;
    if (p == (cli_parse_result *)0 || out == (int *)0)
        return 0;
    if (p->arg_count <= (int)index)
        return 0;
    text = p->args[index].value;
    if (text == (const char *)0 || text[0] == '\0')
        return 0;
    errno = 0;
    end = (char *)text;          /* strtol writes it; keep a non-null seed */
    v = strtol(text, &end, 10);
    if (end == (char *)0 || end == text || *end != '\0' || errno == ERANGE)
        return 0;
    if (v < (long)CALC_MIN || v > (long)CALC_MAX)
        return 0;
    *out = (int)v;
    return 1;
}

static const cli_option *calc_find_option(const cli_parse_result *p,
                                          const char *name) {
    int i;
    if (p == (cli_parse_result *)0 || name == (const char *)0)
        return (const cli_option *)0;
    for (i = 0; i < p->option_count; i++) {
        if (strcmp(p->options[i].name, name) == 0)
            return &p->options[i];
    }
    return (const cli_option *)0;
}

/* Dispatch callback: ids are the integers supplied to cli_add_command. */
static int calc_dispatch(int id, const cli_parse_result *p, void *ctx) {
    calc_ctx *c = (calc_ctx *)ctx;
    const cli_option *opt;

    if (id == 1 || id == 2) {
        int a = 0, b = 0, result = 0;
        long wide = 0;
        if (p->arg_count != 2) {
            print("CLI args", p->arg_count);
            return 1;
        }
        if (!calc_int_arg(p, 0, &a) || !calc_int_arg(p, 1, &b)) {
            print("CLI bad-int");
            return 1;
        }
        /* Widen before multiplying: a 32-bit product can overflow even when
         * both operands pass the per-operand bound, so refuse it cleanly. */
        wide = (id == 1) ? ((long)a + (long)b) : ((long)a * (long)b);
        if (wide < (long)INT_MIN || wide > (long)INT_MAX) {
            print("CLI overflow");
            return 1;
        }
        result = (int)wide;
        if (id == 1)
            print("CLI add", a, b, result);
        else
            print("CLI mul", a, b, result);
        if (calc_find_option(p, "--verbose") != (const cli_option *)0)
            print("CLI verbose", 1);
        opt = calc_find_option(p, "--tag");
        if (opt != (const cli_option *)0)
            print("CLI tag", opt->value);
        return 0;
    }

    if (id == 3) {
        int st = cli_help(c->app, c->help, sizeof(c->help));
        if (st != CLI_OK) {
            print("CLI help-fail", st);
            return 4;
        }
        fputs(c->help, stdout);
        return 0;
    }

    print("CLI bad-id", id);
    return 4;
}

int main(int argc, char **argv) {
    cli app = {0};
    cli_parse_result parsed = {0};
    calc_ctx ctx = {0};
    int st;

    cli_init(&app);
    if (cli_add_command(&app, "add", "add two integers", 1) != CLI_OK ||
        cli_add_command(&app, "mul", "multiply two integers", 2) != CLI_OK ||
        cli_add_command(&app, "help", "print help text", 3) != CLI_OK ||
        cli_add_option(&app, "--verbose", 0) != CLI_OK ||
        cli_add_option(&app, "--tag", 1) != CLI_OK) {
        print("CLI reg-fail");
        return 3;
    }

    if (argc < 2) {
        print("CLI no-command");
        return 2;
    }

    /* cli_parse only reads argv; cast away main's char** -> const char** mismatch. */
    st = cli_parse(&app, argc, (const char **)argv, &parsed);
    if (st != CLI_OK) {
        print("CLI refuse", cli_error(st), st);
        return 2;
    }

    ctx.app = &app;
    return cli_run(&app, &parsed, calc_dispatch, &ctx);
}
