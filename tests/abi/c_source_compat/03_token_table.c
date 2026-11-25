/* Legacy 03 -- heap token table (malloc / strcpy / printf / free).
 *
 * Builds an array of independently allocated strings, prints them, then
 * releases each element and the table itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *words[] = {"alpha", "beta", "gamma", "delta"};
    char **tok;
    int i;

    tok = (char **)malloc(4 * sizeof(char *));
    if (!tok) return 2;

    for (i = 0; i < 4; ++i) {
        tok[i] = (char *)malloc(strlen(words[i]) + 1);
        if (!tok[i]) return 2;
        strcpy(tok[i], words[i]);
        printf("token_table: %d %s\n", i, tok[i]);
    }

    for (i = 0; i < 4; ++i) free(tok[i]);
    free(tok);
    return 0;
}
