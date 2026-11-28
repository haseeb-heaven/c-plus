/* Cake _View: reading through a borrow (without moving it) is clean. */
int main(void) {
  int value = 7;
  int *_View view = &value;
  *view = 9;
  return *view;
}
