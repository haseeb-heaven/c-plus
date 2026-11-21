#include <stddef.h>
int main(void){ int a[7]; size_t n=sizeof a/sizeof a[0]; return n==7?0:1; }
