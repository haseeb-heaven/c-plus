/* Coverage test for the opt-in diagnostics API (safe_diag.h). */
#include <stdio.h>
#include "safe_diag.h"
int main(void) {
    char buf[128];
    const char *name;
    int rc;
    cplus_diag_init_from_env();
    cplus_diag_set_mode(cplus_diag_get_mode());
    cplus_diag_set_color(cplus_diag_get_color());
    (void)cplus_diag_color_enabled((FILE *)0);
    name = cplus_diag_code_name(400);
    rc = cplus_diag_explain(400, buf, sizeof(buf));
    (void)cplus_diag_hint(400);
    printf("safe_diag_coverage %s %d %s\n", name ? name : "none", rc, buf);
    return 0;
}
