int main(int argc, char **argv) {
  int x;
  if (argc > 1) x = 7;
  return x;  // NOSONAR intentional safety regression probe
}
