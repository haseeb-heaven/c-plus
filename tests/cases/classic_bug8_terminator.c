/* Bug 8: missing null terminator.  C+ does not auto-detect this; reserve one
 * extra byte and terminate explicitly. */
#include <stdio.h>
#include <string.h>
int main(void) {
    char word[6];
    strncpy(word, "Hello", 5);
    word[5] = '\0';
    printf("%s\n", word);
    return 0;
}
