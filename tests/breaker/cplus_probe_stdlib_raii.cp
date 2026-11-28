#include "stdlib/string.hp"
#include "stdlib/vector.hp"
int main(void) {
  string s = string("hello");
  vector v = vector();
  for (auto i = 0; i < 20; i++) v.push_back(i * i);
  return s.size() == 5 && s.at(1) == 'e' && v.size() == 20 && v.at(19) == 361 ? 0 : 1;
}
