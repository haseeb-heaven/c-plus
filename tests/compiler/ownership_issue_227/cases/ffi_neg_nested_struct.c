/* #227: pointer effect is one record level down (function pointer member). */
struct Inner { void (*cb)(int); };
struct Outer { struct Inner inner; int tag; };
extern void evil_nested(struct Outer o);
int main(void) { struct Outer o = {0}; evil_nested(o); return 0; }
