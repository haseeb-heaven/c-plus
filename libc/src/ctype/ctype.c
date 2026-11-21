#include "../../log/log.h"
int cplus_isalpha(int c){ cplus_libc_log("isalpha","enter"); return (c>='A'&&c<='Z')||(c>='a'&&c<='z'); }
int cplus_isdigit(int c){ cplus_libc_log("isdigit","enter"); return c>='0'&&c<='9'; }
int cplus_isspace(int c){ cplus_libc_log("isspace","enter"); return c==' '||c=='\f'||c=='\n'||c=='\r'||c=='\t'||c=='\v'; }
