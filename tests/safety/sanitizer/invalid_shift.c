int main(void) {
  volatile int amount = 32;
  int value = 1 << amount;
  return value;
}
