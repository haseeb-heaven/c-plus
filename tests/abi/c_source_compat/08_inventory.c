/* Legacy 08 -- inventory description (memset / strcpy / strcat / printf).
 *
 * A fixed-size stack struct is zeroed and a human-readable description is
 * assembled in place with successive strcat() calls.
 */
#include <stdio.h>
#include <string.h>

typedef struct {
    char sku[12];
    char desc[64];
} item_t;

int main(void)
{
    item_t it;

    memset(&it, 0, sizeof(it));
    strcpy(it.sku, "X-100");
    strcpy(it.desc, "widget");
    strcat(it.desc, " - blue");
    strcat(it.desc, " (box of 12)");

    printf("inventory: sku=%s desc=%s\n", it.sku, it.desc);
    return 0;
}
