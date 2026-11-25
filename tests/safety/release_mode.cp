#include "cplus/cplus.h"
class Box { int v; Box(int x) { this->v = x; } int get(void) { return this->v; } };
int main(void) { Box b = Box(41); return b.get() + 1 == 42 ? 0 : 1; }
