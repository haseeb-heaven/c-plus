/* A positive offset keeps the loop counter expression away from zero. */
int f(void) {
  int r = 0;
  for (int i = 0; i < 3; i++) r += 10 / (i + 1);
  return r;
}
int main(void) { return f(); }
