#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
/* Desktop-style app: bounded menu selection and form fields. */
static int choose(int menu_max, int sel) {
    error e = error();
    if (menu_max <= 0 || menu_max > 100 || sel < 0 || sel >= menu_max)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "desktop: invalid menu selection"), -1;
    return sel;
}
int main(void) {
    string title = string("Settings");
    int ok = choose(4, 2);
    int bad = choose(4, 9);
    print("DESKTOP_APP", title.size(), ok, bad);
    title.drop();
    return (ok == 2 && bad == -1) ? 0 : 1;
}
