#include <stdint.h>
void write_64(uint64_t*out){ *out=0x1122334455667788ULL; }
int main(void){ uint32_t small=0; write_64((uint64_t*)&small); return (int)small; }
