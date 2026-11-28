#include "cplus/cplus.h"
#include <stdio.h>
#warning CPLUS_REGION_LOCATION_PROBE
int main(void){
  region r(128) {
    printf("LINE=%d\n", __LINE__);
  }
  return 0;
}
