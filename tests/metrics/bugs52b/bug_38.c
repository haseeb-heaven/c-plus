#include <stdio.h>
#include <string.h>
const char* cache_key=NULL;
void put_cache(const char*key){ cache_key=key; }
void populate(void){ char temp_key; strcpy(temp_key,"session_123"); put_cache(temp_key); }
int main(void){ populate(); printf("%s\n",cache_key); return 0; }
