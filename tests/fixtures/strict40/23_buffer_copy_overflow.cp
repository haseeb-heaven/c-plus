#include <string.h>
int main(void) { char destination[4]; const char source[] = "hello"; strcpy(destination, source); return destination[0]; }
