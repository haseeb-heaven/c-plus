/* LEGACY byte-preservation control: no C+ syntax, no safety instrumentation.
 * The auto-runtime constructor must never be emitted in a legacy build. */
int main(void) { return 0; }
