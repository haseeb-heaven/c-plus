/* Cake _Opt: guarded arrow dereference is accepted. */
struct Node { int value; };
int main(void) {
  struct Node node = {7};
  struct Node *_Opt p = &node;
  if (p != 0) return p->value;
  return 1;
}
