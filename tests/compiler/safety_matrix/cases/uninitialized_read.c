/* Safety matrix: read of an uninitialized automatic scalar. */
int main(void) {
  int x;
  volatile int y = x;
  return y == 0;
}
