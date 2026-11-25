/* POSITIVE definite init: every control-flow join assigns the variable. */
int main(int argc, char **argv) { (void)argv; int c; if (argc > 1) c = 1; else c = 2; return c > 0 ? 0 : 1; }
