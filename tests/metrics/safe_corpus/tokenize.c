#include <string.h>
int main(void) { char s[16]="one two three"; int count=0; char *p=strtok(s," ");
  while (p) { count++; p=strtok(0," "); } return count==3 ? 0 : 1; }
