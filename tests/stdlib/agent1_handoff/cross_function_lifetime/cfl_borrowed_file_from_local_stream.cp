// EXPECT: REJECT
// Cross-function lifetime (P0): make_borrowed() wraps the stream of a local
// owned `file` in a borrowed `file` and returns the wrapper.  The local file's
// drop() fclose()s that FILE* at function exit, so the returned wrapper's
// stream pointer dangles.  The compiler must reject escaping a borrow of a
// function-local resource, even through file_borrow()/native().
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static file make_borrowed(error *err) {
    file local = file_temp(err);               /* owns the local FILE* */
    return file_borrow(local.native());        /* borrowed wrapper escapes */
} /* local.drop() fclose()s the stream here */

int main(void) {
    error err = error();
    file escaped = make_borrowed(&err);
    char buf[4];
    (void)escaped.read(buf, sizeof(buf), &err); /* use of the closed FILE* */
    return 0;
}
