#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/exceptions/demo.cp"
#include "cplus/cplus.h"
int main(void) {
  try { short arr[] = {10,30,50,50};
    for (int idx=0; idx<(4); idx++) {
      if (idx==2) { print("Enter value:"); scan(arr[3]); }
      print("Arr is ", arr[idx]);
    }
  } catch (Exception ex) { print("heaven here ;)"); }
  return 0;
}
