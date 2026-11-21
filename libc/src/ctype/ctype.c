#include "../../log/log.h"
#include <ctype.h>

int cplus_isalpha(int c){ cplus_libc_log("isalpha","enter"); return isalpha((unsigned char)c) != 0; }
int cplus_isdigit(int c){ cplus_libc_log("isdigit","enter"); return isdigit((unsigned char)c) != 0; }
int cplus_isspace(int c){ cplus_libc_log("isspace","enter"); return isspace((unsigned char)c) != 0; }
int cplus_isalnum(int c){ cplus_libc_log("isalnum","enter"); return isalnum((unsigned char)c) != 0; }
int cplus_isupper(int c){ cplus_libc_log("isupper","enter"); return isupper((unsigned char)c) != 0; }
int cplus_islower(int c){ cplus_libc_log("islower","enter"); return islower((unsigned char)c) != 0; }
int cplus_isxdigit(int c){ cplus_libc_log("isxdigit","enter"); return isxdigit((unsigned char)c) != 0; }
int cplus_isprint(int c){ cplus_libc_log("isprint","enter"); return isprint((unsigned char)c) != 0; }
int cplus_isgraph(int c){ cplus_libc_log("isgraph","enter"); return isgraph((unsigned char)c) != 0; }
int cplus_ispunct(int c){ cplus_libc_log("ispunct","enter"); return ispunct((unsigned char)c) != 0; }
int cplus_iscntrl(int c){ cplus_libc_log("iscntrl","enter"); return iscntrl((unsigned char)c) != 0; }
int cplus_toupper(int c){ cplus_libc_log("toupper","enter"); return toupper((unsigned char)c); }
int cplus_tolower(int c){ cplus_libc_log("tolower","enter"); return tolower((unsigned char)c); }
