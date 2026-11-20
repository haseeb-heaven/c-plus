/* Bug 44: lying [[unsequenced]] attribute. */
int g = 2;
[[unsequenced]] int compute(int x) { return x * g; }
int main(void) { return compute(5); }
