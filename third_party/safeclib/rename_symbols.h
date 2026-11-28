/* Prefix safeclib exported symbols to avoid host libc/CRT collisions. */
#ifndef CPLUS_SAFECLIB_RENAME_H
#define CPLUS_SAFECLIB_RENAME_H
#define memcpy_s cplus_sc_memcpy_s
#define memmove_s cplus_sc_memmove_s
#define memset_s cplus_sc_memset_s
#define memcmp_s cplus_sc_memcmp_s
#define memzero_s cplus_sc_memzero_s
#define memzero16_s cplus_sc_memzero16_s
#define memzero32_s cplus_sc_memzero32_s
#define strcpy_s cplus_sc_strcpy_s
#define strncpy_s cplus_sc_strncpy_s
#define strcat_s cplus_sc_strcat_s
#define strncat_s cplus_sc_strncat_s
#define strnlen_s cplus_sc_strnlen_s
#define strcmp_s cplus_sc_strcmp_s
#define strcasecmp_s cplus_sc_strcasecmp_s
#define strcmpfld_s cplus_sc_strcmpfld_s
#define strcpyfld_s cplus_sc_strcpyfld_s
#define strcpyfldin_s cplus_sc_strcpyfldin_s
#define strcpyfldout_s cplus_sc_strcpyfldout_s
#define strzero_s cplus_sc_strzero_s
#define set_mem_constraint_handler_s cplus_sc_set_mem_constraint_handler_s
#define set_str_constraint_handler_s cplus_sc_set_str_constraint_handler_s
#define abort_handler_s cplus_sc_abort_handler_s
#define ignore_handler_s cplus_sc_ignore_handler_s
#endif
