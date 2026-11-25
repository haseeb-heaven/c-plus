int main(int argc, char **argv) {
  int x;
  if (argc > 1) x = 7; else x = 3;
  return x == 3 || x == 7 ? 0 : 1;
}
