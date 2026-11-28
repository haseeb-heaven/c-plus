/* Cake _Opt: arrow dereference also needs an active non-null refinement. */
struct Node { int value; };
int main(void) {
  struct Node node = {7};
  struct Node *_Opt p = &node;
  return p->value;
}
