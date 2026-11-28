/* In-bounds control for the -fcplus-disjoint-bounds driver smoke test. */
int main(void) {
  int a[4] = {0, 1, 2, 3};
  int i = 2;
  return a[i];
}
