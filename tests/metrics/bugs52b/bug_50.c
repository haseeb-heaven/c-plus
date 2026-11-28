#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void set_local_env(void){ char env_str; strcpy(env_str,"APP_MODE=prod"); putenv(env_str); }
int main(void){ set_local_env(); printf("%s\n",getenv("APP_MODE")); return 0; }
