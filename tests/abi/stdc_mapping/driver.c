/* Links both TUs (plain and checked) into one program: if macro remapping were
 * symbol interposition, the plain TU's malloc would also be redirected. */
#include <stdio.h>
#include <stdlib.h>

void *tu_plain_alloc(size_t n);
void tu_plain_free(void *p);
void *tu_checked_alloc(size_t n);
void tu_checked_free(void *p);

int main(void) {
    void *a = tu_plain_alloc(16);
    void *b = tu_checked_alloc(16);
    if (!a || !b) return 1;
    tu_plain_free(a);
    tu_checked_free(b);
    puts("tu_isolation:ok");
    return 0;
}
