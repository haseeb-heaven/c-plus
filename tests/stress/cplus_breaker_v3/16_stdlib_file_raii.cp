#include "stdlib/file.hp"
int main(void){ file f=file("breaker_v3_file.tmp","w"); if(!f.is_open())return 2; if(!f.write("valid\n"))return 3; print("FILE OK");return 0;}
