#include "shared.hp"
int from_a(void) { Counter c = Counter(4); c.add(1); return c.get(); }
