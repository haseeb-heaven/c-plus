/* Legacy 02 -- fixed-buffer name assembly (strcpy / strcat / printf).
 *
 * The classic "first + space + last" into a caller-sized char array.
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char first[32];
    char last[32];
    char full[80];

    strcpy(first, "Ada");
    strcpy(last, "Lovelace");

    strcpy(full, first);
    strcat(full, " ");
    strcat(full, last);

    printf("name_builder: %s\n", full);
    return 0;
}
