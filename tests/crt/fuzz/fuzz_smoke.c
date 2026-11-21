/* Compile-time smoke for fuzz targets when libFuzzer unavailable. */
#include <stdint.h>
#include <stddef.h>
extern int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int main(void){ uint8_t d[4]={1,2,3,4}; return LLVMFuzzerTestOneInput(d,4)==0?0:1; }
