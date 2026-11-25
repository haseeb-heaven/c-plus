/* Legacy 06 -- string table (malloc / strcpy / printf / free).
 *
 * Allocates a small directory of compass points, prints them in reverse order
 * (exercising interior heap pointers passed straight to printf), then frees.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *names[] = {"north", "south", "east", "west"};
    char *table[4];
    int i;

    for (i = 0; i < 4; ++i) {
        table[i] = (char *)malloc(32);
        if (!table[i]) return 2;
        strcpy(table[i], names[i]);
    }

    for (i = 3; i >= 0; --i) {
        printf("string_table: %d %s\n", i, table[i]);
    }

    for (i = 0; i < 4; ++i) free(table[i]);
    return 0;
}
