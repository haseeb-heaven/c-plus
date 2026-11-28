#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stddef.h>

struct DirView { int fd; };

/* BUG 706: using a directory handle returned by a failed opendir() without
 * checking it first -- the error path dereferences a NULL handle. */
int bug706_unchecked_opendir(void) {
    DIR *d = opendir("/no/such/directory");
    struct DirView *v = NULL;

    if (d == NULL)
        return v->fd;
    closedir(d);
    return 0;
}

int main(void) { return 0; }
