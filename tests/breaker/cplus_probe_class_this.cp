#include "cplus/cplus.h"
class Acc {
  int value;
  Acc(int initial) { this->value = initial; }
  void add(int x) { this->value += x; }
  int read(void) { return this->value; }
  int twice(void) { return this->read() * 2; }
};
int main(void) {
  Acc a = Acc(7);
  a.add(5);
  return a.read() == 12 && a.twice() == 24 ? 0 : 1;
}
