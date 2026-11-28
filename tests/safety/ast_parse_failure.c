/* Regression: a translation unit whose C+ safety AST cannot be built must not
 * be silently accepted in STRICT `check` or `build`. The missing semicolon
 * below is intentional — the backend would still accept this as a C extension
 * in some modes, so the safety AST failure must be a hard error. */
int main(void) { return 0 }
