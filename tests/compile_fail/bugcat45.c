#define _POSIX_C_SOURCE 200809L
#include <glob.h>
#include <stdio.h>

/* BUG 715: treating a glob() result as if it had more matches than were
 * captured -- reading past the last stored gl_pathv element. */
void bug715_glob_path_oob(void) {
    glob_t g;
    char *paths[2];

    if (glob("*.c", 0, NULL, &g) != 0)
        return;
    paths[0] = g.gl_pathv[0];
    paths[1] = g.gl_pathv[1];
    printf("%s\n", paths[2]);
}

int main(void) { return 0; }
