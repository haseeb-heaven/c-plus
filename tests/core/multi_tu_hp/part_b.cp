#include "shared.hp"
int from_a(void);
int main(void) { Counter c = Counter(8); c.add(2); return from_a() == 5 && c.get() == 10 ? 0 : 1; }
