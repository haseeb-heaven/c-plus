#include "string_lite.hp"
#include "vector_lite.hp"
#include "file_lite.hp"
#include "thread_lite.hp"
static int counter = 0;
int from_a(void) {
  StringLite s = StringLite("abcd");
  IntVecLite v = IntVecLite();
  v.push(3);
  MutexLite m = MutexLite_new();
  { GuardLite g = GuardLite_lock(&m); counter++; (void)&g; }
  FileLite f = FileLite_open("/dev/null");
  return s.len() == 4 && v.len() == 1 && f.h != NULL && counter == 1 ? 0 : 1;
}
