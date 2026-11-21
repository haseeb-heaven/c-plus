#ifndef CPLUS_LIBC_CTYPE_H
#define CPLUS_LIBC_CTYPE_H
#ifdef __cplusplus
extern "C" {
#endif
int isalnum(int c); int isalpha(int c); int isdigit(int c); int isxdigit(int c);
int isspace(int c); int isprint(int c); int isgraph(int c); int iscntrl(int c);
int ispunct(int c); int isupper(int c); int islower(int c); int isblank(int c);
int toupper(int c); int tolower(int c);
#ifdef __cplusplus
}
#endif
#endif
