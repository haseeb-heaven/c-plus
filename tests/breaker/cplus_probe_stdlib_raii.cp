#include "stdlib/string.hp"
#include "stdlib/vector.hp"
int main(void) {
  String s = String("hello");
  IntVec v = IntVec();
  for (auto i = 0; i < 20; i++) v.push(i * i);
  return s.len() == 5 && s.at(1) == 'e' && v.len() == 20 && v.at(19) == 361 ? 0 : 1;
}
