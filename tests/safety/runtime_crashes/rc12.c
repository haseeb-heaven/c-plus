#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
int main(void){char*bio=malloc(24); char*next=malloc(24); if(!bio||!next)return 1; char input[64]; printf("bio: "); if(fgets(input,sizeof input,stdin)){size_t len=strlen(input); if(len&&input[len-1]=='\n')input[--len]='\0'; if(len<=24) memcpy(bio,input,len+1);} free(next); free(bio); return 0;}
