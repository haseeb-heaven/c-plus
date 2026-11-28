#include "string_lite.hp"
#include "vector_lite.hp"
#include "file_lite.hp"
#include "thread_lite.hp"
int from_a(void);
int main(void) {
  StringLite s = StringLite("xyz");
  IntVecLite v = IntVecLite();
  v.push(9);
  FileLite f = FileLite_open("/dev/null");
  int rc = from_a();
  return rc == 0 && s.len() == 3 && v.len() == 1 && f.h != NULL ? 0 : 1;
}
