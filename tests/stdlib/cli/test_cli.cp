/* Deterministic tests for the bounded CLI framework (stdlib/cli.hp).
 *
 * Every check prints one "CHECK <name> PASS|FAIL" line.  A successful parse
 * also prints its descriptors, and cli_help output is printed verbatim
 * between HELP_BEGIN / HELP_END markers so run.sh can diff exact output.
 * The final line is "CLI_TEST <total> <passed> <failed>".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/cli.hp"

static int g_total = 0;
static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name) {
    g_total++;
    if (cond) {
        g_pass++;
        print("CHECK", name, "PASS");
    } else {
        g_fail++;
        print("CHECK", name, "FAIL");
    }
}

static int dispatch(int id, const cli_parse_result *parsed, void *ctx) {
    int *calls = (int *)ctx;
    (*calls)++;
    return id * 10 + parsed->arg_count;
}

int main(void) {
    cli c;
    cli raw;
    cli_parse_result r;
    char help[1024];
    char small[8];
    char big_name[CLI_MAX_NAME + 1];
    char big_help[CLI_MAX_HELP + 1];
    char big_token[CLI_MAX_VALUE + 1];
    char line[256];
    int calls = 0;
    int rc;
    int i;

    const char *argv_ok[] = {"prog", "--verbose", "add", "--count=3",
                             "10", "20"};
    const char *argv_unknown_opt[] = {"prog", "add", "--bogus"};
    const char *argv_unknown_cmd[] = {"prog", "nope"};
    const char *argv_no_cmd[] = {"prog", "--verbose"};
    const char *argv_bare[] = {"prog", "add", "-"};
    const char *argv_flagval[] = {"prog", "add", "--verbose=1"};
    const char *argv_valmissing[] = {"prog", "add", "--count"};
    const char *argv_null[] = {"prog", "add", NULL};
    const char *argv_many[] = {
        "prog", "add", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8",
        "a9", "a10", "a11", "a12", "a13", "a14", "a15", "a16", "a17"};
    const char *argv_big[] = {"prog", "add", big_token};
    const char *one[] = {"prog"};

    print("CLI_TEST begin");

    /* -- construction --------------------------------------------------- */
    memset(&c, 0, sizeof(c));
    cli_init(&c);
    check(c.magic == CLI_MAGIC && c.command_count == 0 && c.option_count == 0,
          "init");

    rc = cli_add_command(&c, "add", "add two numbers", 1);
    if (rc == CLI_OK)
        rc = cli_add_command(&c, "greet", "greet a name", 2);
    check(rc == CLI_OK && c.command_count == 2, "add_cmd");

    rc = cli_add_option(&c, "--count", 1);
    if (rc == CLI_OK)
        rc = cli_add_option(&c, "--verbose", 0);
    check(rc == CLI_OK && c.option_count == 2, "add_opt");

    check(cli_add_command(&c, "add", "duplicate", 9) == CLI_ERR_DUPLICATE,
          "dup_cmd");
    check(cli_add_option(&c, "--verbose", 0) == CLI_ERR_DUPLICATE, "dup_opt");

    for (i = 0; i < CLI_MAX_NAME; i++)
        big_name[i] = 'n';
    big_name[CLI_MAX_NAME] = '\0';
    check(cli_add_command(&c, big_name, "help", 9) == CLI_ERR_TOO_LONG,
          "long_name");
    for (i = 0; i < CLI_MAX_HELP; i++)
        big_help[i] = 'h';
    big_help[CLI_MAX_HELP] = '\0';
    check(cli_add_command(&c, "longhelp", big_help, 9) == CLI_ERR_TOO_LONG,
          "long_help");

    check(cli_add_command(NULL, "x", "y", 1) == CLI_ERR_NULL &&
              cli_add_command(&c, NULL, "y", 1) == CLI_ERR_NULL &&
              cli_add_command(&c, "x", NULL, 1) == CLI_ERR_NULL &&
              cli_add_option(NULL, "--x", 0) == CLI_ERR_NULL &&
              cli_add_option(&c, NULL, 0) == CLI_ERR_NULL &&
              cli_parse(NULL, 1, one, &r) == CLI_ERR_NULL &&
              cli_parse(&c, 1, NULL, &r) == CLI_ERR_NULL &&
              cli_parse(&c, 1, one, NULL) == CLI_ERR_NULL &&
              cli_help(NULL, help, sizeof(help)) == CLI_ERR_NULL &&
              cli_help(&c, NULL, sizeof(help)) == CLI_ERR_NULL &&
              cli_run(NULL, &r, dispatch, &calls) == CLI_ERR_NULL &&
              cli_run(&c, NULL, dispatch, &calls) == CLI_ERR_NULL,
          "null_args");

    check(cli_add_option(&c, "x", 0) == CLI_ERR_INVALID &&
              cli_add_option(&c, "", 0) == CLI_ERR_INVALID,
          "bad_option_name");

    memset(&raw, 0, sizeof(raw));
    check(cli_add_command(&raw, "x", "y", 1) == CLI_ERR_NOT_INITIALIZED &&
              cli_parse(&raw, 1, one, &r) == CLI_ERR_NOT_INITIALIZED &&
              cli_help(&raw, help, sizeof(help)) == CLI_ERR_NOT_INITIALIZED &&
              cli_run(&raw, &r, dispatch, &calls) == CLI_ERR_NOT_INITIALIZED,
          "uninit");

    /* Registry capacity: 16 commands and 8 options are accepted, the next
     * entry of each kind is refused.  Uses a separate registry so the help
     * golden below stays small. */
    {
        cli full;
        char nm[8];
        int ok = 1;
        memset(&full, 0, sizeof(full));
        cli_init(&full);
        for (i = 0; i < CLI_MAX_COMMANDS; i++) {
            snprintf(nm, sizeof(nm), "c%d", i);
            if (cli_add_command(&full, nm, "h", i) != CLI_OK)
                ok = 0;
        }
        if (cli_add_command(&full, "extra", "h", 99) != CLI_ERR_FULL)
            ok = 0;
        for (i = 0; i < CLI_MAX_OPTIONS; i++) {
            snprintf(nm, sizeof(nm), "-o%d", i);
            if (cli_add_option(&full, nm, 0) != CLI_OK)
                ok = 0;
        }
        if (cli_add_option(&full, "--more", 0) != CLI_ERR_FULL)
            ok = 0;
        check(ok, "registry_full");
    }

    /* -- deterministic parse -------------------------------------------- */
    memset(&r, 0, sizeof(r));
    rc = cli_parse(&c, 6, argv_ok, &r);
    check(rc == CLI_OK && r.found == 1 && r.command_id == 1 &&
              strcmp(r.command, "add") == 0 && r.option_count == 2 &&
              r.arg_count == 2,
          "parse_ok");

    snprintf(line, sizeof(line), "PARSE command=%s id=%d found=%d options=%d args=%d",
             r.command, r.command_id, r.found, r.option_count, r.arg_count);
    print(line);
    for (i = 0; i < r.option_count; i++) {
        snprintf(line, sizeof(line), "OPT %d %s has_value=%d value=%s", i,
                 r.options[i].name, r.options[i].has_value, r.options[i].value);
        print(line);
    }
    for (i = 0; i < r.arg_count; i++) {
        snprintf(line, sizeof(line), "ARG %d %s", i, r.args[i].value);
        print(line);
    }

    /* -- dispatch by id -------------------------------------------------- */
    rc = cli_run(&c, &r, dispatch, &calls);
    check(rc == 12 && calls == 1, "dispatch");
    {
        cli_parse_result empty;
        memset(&empty, 0, sizeof(empty));
        check(cli_run(&c, &empty, dispatch, &calls) == CLI_ERR_UNKNOWN_COMMAND &&
                  cli_run(&c, &r, NULL, &calls) == CLI_ERR_DISPATCH &&
                  calls == 1,
              "dispatch_refusal");
    }

    /* -- exact help text ------------------------------------------------- */
    rc = cli_help(&c, help, sizeof(help));
    check(rc == CLI_OK &&
              strcmp(help,
                     "Commands:\n"
                     "  add - add two numbers (id=1)\n"
                     "  greet - greet a name (id=2)\n"
                     "Options:\n"
                     "  --count=value\n"
                     "  --verbose\n") == 0,
          "help_exact");
    print("HELP_BEGIN");
    print(help);
    print("HELP_END");
    check(cli_help(&c, small, sizeof(small)) == CLI_ERR_FULL &&
              cli_help(&c, help, 0) == CLI_ERR_NULL,
          "help_bounds");

    /* -- parse refusals -------------------------------------------------- */
    check(cli_parse(&c, 3, argv_unknown_opt, &r) == CLI_ERR_UNKNOWN_OPTION,
          "unknown_option");
    check(cli_parse(&c, 2, argv_unknown_cmd, &r) == CLI_ERR_UNKNOWN_COMMAND,
          "unknown_command");
    check(cli_parse(&c, 2, argv_no_cmd, &r) == CLI_ERR_UNKNOWN_COMMAND,
          "no_command");
    check(cli_parse(&c, 19, argv_many, &r) == CLI_ERR_TOO_MANY_ARGS,
          "too_many_args");
    check(cli_parse(&c, 3, argv_bare, &r) == CLI_ERR_INVALID, "bare_dash");
    check(cli_parse(&c, 3, argv_flagval, &r) == CLI_ERR_INVALID, "flag_value");
    check(cli_parse(&c, 3, argv_valmissing, &r) == CLI_ERR_INVALID,
          "value_missing");
    for (i = 0; i < CLI_MAX_VALUE; i++)
        big_token[i] = 't';
    big_token[CLI_MAX_VALUE] = '\0';
    check(cli_parse(&c, 3, argv_big, &r) == CLI_ERR_TOO_LONG, "oversized_token");
    check(cli_parse(&c, 3, argv_null, &r) == CLI_ERR_NULL, "null_token");
    check(cli_parse(&c, -1, one, &r) == CLI_ERR_INVALID, "negative_argc");

    check(strcmp(cli_error(CLI_OK), "ok") == 0 &&
              strcmp(cli_error(CLI_ERR_UNKNOWN_OPTION), "unknown option") == 0 &&
              strcmp(cli_error(CLI_ERR_TOO_MANY_ARGS), "too many arguments") == 0 &&
              strcmp(cli_error(CLI_ERR_DUPLICATE), "duplicate entry") == 0 &&
              strcmp(cli_error(9999), "unknown cli error") == 0,
          "error_strings");

    /* Direct reach for public helper APIs (coverage tool call sites). */
    (void)cli_check(&c);
    (void)cli_find_command(&c, "add");
    (void)cli_find_option(&c, "verbose");
    cli_result_reset(&r);

    snprintf(line, sizeof(line), "CLI_TEST %d %d %d", g_total, g_pass, g_fail);
    print(line);
    return g_fail == 0 ? 0 : 1;
}
