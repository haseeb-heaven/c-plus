#include "stdlib/file.hp"
int main(void){ File f=File_open("breaker_v3_file.tmp","w"); if(!f.handle)return 2; if(!File_write(&f,"valid\n"))return 3; print("FILE OK");return 0;}
