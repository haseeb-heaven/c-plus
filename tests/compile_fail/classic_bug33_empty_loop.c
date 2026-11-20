/* Bug 33: empty infinite loop optimized away. */
int wait_forever(void) { while (1) {} return 0; }
int main(void) { wait_forever(); return 0; }
