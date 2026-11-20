/* Bug 45: C23 changed void foo() to mean zero arguments.  C+ targets gnu17
 * but recommends explicit (void) prototypes for forward compatibility. */
#include <stdio.h>
void log_event(void);
void log_event(void) { printf("event\n"); }
int main(void) { log_event(); return 0; }
