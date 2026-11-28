/* Out-of-bounds case for the -fcplus-disjoint-bounds driver smoke test.
 * The index is a runtime variable so the static checker does not reject it;
 * the injected cplus_shadow_slot_check() must trap with exit code 101. */
int main(void) {
  int a[4] = {0, 1, 2, 3};
  int i = 8;
  return a[i];
}
