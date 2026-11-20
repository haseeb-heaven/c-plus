/* NOT DETECTED: no room for the null terminator. */
#include <stdio.h>
#include <string.h>
int main(void) {
    char word[5];
    strncpy(word, "Hello", 5);
    printf("%s\n", word);
    return 0;
}
